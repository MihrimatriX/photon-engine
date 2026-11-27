#!/usr/bin/env python3
"""Verim kıyası: PhotonEngine ve Blender Cycles (CPU) aynı sahnede, aynı iş parçacığı sayısıyla.

  python3 bench/reference/efficiency.py --refdir bench/reference/out --blender-python /yol/python3.11
         [--spp-lo 16 --spp-hi 128 --ref-spp 2048 --threads 4]

Ölçü (her motor kendi yakınsamış görüntüsüne göre, böylece model farkları gürültüye karışmaz):
  * örnek başına süre  t_s = (T(spp_hi) − T(spp_lo)) / (spp_hi − spp_lo)   (sabit kurulum maliyeti düşer)
  * gürültü            relMSE(spp_hi)  (kendi ref_spp görüntüsüne göre)
  * verim              1 / (relMSE · spp_hi · t_s)   — büyük = aynı sürede daha temiz görüntü
Photon'un kendi referansı compare.py'nin yazdığı photon.exr'dir (refdir/<sahne>/photon.exr).
Kurulum maliyeti (sahne yükleme, BVH, çekirdek derleme) ayrıca raporlanır.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refscenes as rs  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def relmse(x, ref):
    return float(np.mean((x - ref) ** 2 / (ref ** 2 + 0.01)))


def run_photon(exe, proj, out, res, spp, threads):
    st = out + ".json"
    subprocess.run([exe, proj, "--out", out, "--res", f"{res[0]}x{res[1]}", "--spp", str(spp), "--no-denoise",
                    "--filter", "box", "--bounces", "64", "--threads", str(threads), "--stats", st],
                   check=True, stdout=subprocess.DEVNULL)
    with open(st) as f:
        return rs.read_exr(out), json.load(f)["renderSeconds"]


def run_cycles(py, scene, out, spp, threads, env):
    st = out + ".json"
    cmd = [py, os.path.join(HERE, "cycles_render.py"), scene, out, "--spp", str(spp), "--threads", str(threads),
           "--stats", st]
    if env:
        cmd += ["--env", env]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(st) as f:
        return rs.read_exr(out), json.load(f)["renderSeconds"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--photon", default=os.path.join(rs.REPO, "build", "fetch-release", "photon_render"))
    ap.add_argument("--blender-python", required=True)
    ap.add_argument("--refdir", default=os.path.join(HERE, "out"))
    ap.add_argument("--scenes", default="")
    ap.add_argument("--spp-lo", type=int, default=16)
    ap.add_argument("--spp-hi", type=int, default=128)
    ap.add_argument("--ref-spp", type=int, default=2048, help="Cycles'ın kendi referansı")
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    want = set(filter(None, args.scenes.split(",")))
    results = {}
    for sc in rs.scenes():
        if want and sc.name not in want:
            continue
        d = os.path.join(args.refdir, sc.name)
        proj = os.path.join(d, sc.name + ".photon")
        env = os.path.join(d, "env_photon.exr") if sc.env else ""
        p_ref = rs.read_exr(os.path.join(d, "photon.exr"))
        e = os.path.join(d, "eff")
        os.makedirs(e, exist_ok=True)

        print(f"== {sc.name}", flush=True)
        _, p_lo = run_photon(args.photon, proj, os.path.join(e, "p_lo.exr"), sc.res, args.spp_lo, args.threads)
        p_img, p_hi = run_photon(args.photon, proj, os.path.join(e, "p_hi.exr"), sc.res, args.spp_hi, args.threads)

        c_ref_path = os.path.join(e, "c_ref.exr")
        if not os.path.exists(c_ref_path):
            run_cycles(args.blender_python, sc.name, c_ref_path, args.ref_spp, args.threads, env)
        c_ref = rs.read_exr(c_ref_path)
        _, c_lo = run_cycles(args.blender_python, sc.name, os.path.join(e, "c_lo.exr"), args.spp_lo, args.threads, env)
        c_img, c_hi = run_cycles(args.blender_python, sc.name, os.path.join(e, "c_hi.exr"), args.spp_hi, args.threads, env)

        n = args.spp_hi - args.spp_lo
        p_ts, c_ts = (p_hi - p_lo) / n, (c_hi - c_lo) / n
        p_err, c_err = relmse(p_img, p_ref), relmse(c_img, c_ref)
        p_eff = 1.0 / (p_err * args.spp_hi * p_ts)
        c_eff = 1.0 / (c_err * args.spp_hi * c_ts)
        r = {
            "res": list(sc.res), "threads": args.threads, "spp": args.spp_hi,
            "photon": {"sec_per_spp": p_ts, "relmse": p_err, "setup_sec": p_lo - args.spp_lo * p_ts,
                       "msamples_per_sec": sc.res[0] * sc.res[1] / p_ts / 1e6},
            "cycles": {"sec_per_spp": c_ts, "relmse": c_err, "setup_sec": c_lo - args.spp_lo * c_ts,
                       "msamples_per_sec": sc.res[0] * sc.res[1] / c_ts / 1e6},
            "speed_ratio_photon_over_cycles": c_ts / p_ts,
            "noise_ratio_cycles_over_photon_equal_spp": c_err / p_err,
            "efficiency_ratio_photon_over_cycles": p_eff / c_eff,
            "cycles_mean_vs_photon_mean": (c_ref.reshape(-1, 3).mean(0) / p_ref.reshape(-1, 3).mean(0)).tolist(),
        }
        results[sc.name] = r
        print(json.dumps(r, indent=1), flush=True)

    path = os.path.join(args.refdir, "efficiency.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = json.load(f)
    old.update(results)
    with open(path, "w") as f:
        json.dump(old, f, indent=1)


if __name__ == "__main__":
    main()
