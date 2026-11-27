#!/usr/bin/env python3
"""PhotonEngine'i Mitsuba 3 referansıyla karşılaştırır (yansızlık / doğruluk testi).

  python3 bench/reference/compare.py [--photon build/fetch-release/photon_render]
         [--scenes cornell_diffuse,hdri_materials] [--photon-spp 4096] [--ref-spp 8192]
         [--out bench/reference/out]

Gerekenler: pip install mitsuba numpy OpenEXR pillow

Her sahne için iki motor da yüksek örnekle (gürültü ihmal edilecek kadar) render edilir, sonra:
  * kanal başına ortalama oranı (Photon / Mitsuba): toplam enerji farkı,
  * blok ortalamalı göreli fark (16×16 piksel): gürültüyü bastırıp yerel sapmayı gösterir,
  * relMSE (Rousselle 2011) ve aynı ölçünün Mitsuba'nın kendi iki yarısı arasındaki değeri
    (gürültü tabanı). Photon–Mitsuba relMSE'si gürültü tabanına yakınsa fark gürültüdür.
Sonuçlar <out>/results.json'a, görseller <out>/<sahne>/ altına yazılır.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refscenes as rs  # noqa: E402


def tonemap_png(rgb, path, exposure=0.0):
    from PIL import Image
    x = np.clip(rgb * (2.0 ** exposure), 0, None)
    x = x / (1.0 + x)  # Reinhard: yalnız göz kontrolü için
    x = np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)
    Image.fromarray((np.clip(x, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path)


def diff_png(a, ref, path, block=8, scale=0.10):
    """Blok ortalamalı göreli fark: kırmızı = Photon daha parlak, mavi = daha koyu; ±scale tam doygun."""
    from PIL import Image
    la, lr = a.mean(-1), ref.mean(-1)
    H, W = la.shape
    h, w = H // block, W // block
    ba = la[:h * block, :w * block].reshape(h, block, w, block).mean((1, 3))
    br = lr[:h * block, :w * block].reshape(h, block, w, block).mean((1, 3))
    d = (ba - br) / np.maximum(br, 1e-3)
    d = np.where(br < 1e-3, 0.0, d)
    t = np.clip(d / scale, -1, 1)
    img = np.ones((h, w, 3))
    img[..., 1] = 1 - np.abs(t)
    img[..., 0] = np.where(t < 0, 1 - np.abs(t), 1)
    img[..., 2] = np.where(t > 0, 1 - np.abs(t), 1)
    img = np.repeat(np.repeat(img, block, 0), block, 1)
    Image.fromarray((img * 255).astype(np.uint8)).save(path)


def relmse(x, ref):
    return float(np.mean((x - ref) ** 2 / (ref ** 2 + 0.01)))


def block_rel(a, ref, block=16):
    la, lr = a.mean(-1), ref.mean(-1)
    H, W = la.shape
    h, w = H // block, W // block
    ba = la[:h * block, :w * block].reshape(h, block, w, block).mean((1, 3))
    br = lr[:h * block, :w * block].reshape(h, block, w, block).mean((1, 3))
    m = br > 0.02 * max(br.max(), 1e-6)  # neredeyse siyah bloklar oranı anlamsızlaştırır
    d = (ba[m] - br[m]) / br[m]
    return d


def render_mitsuba(scene_dict, spp, chunk, seed0=0):
    import mitsuba as mi
    sc = mi.load_dict(scene_dict)
    acc, n, k = None, 0, 0
    halves = [None, None]
    halves_n = [0, 0]
    while n < spp:
        s = min(chunk, spp - n)
        img = np.array(mi.render(sc, spp=s, seed=seed0 + k), dtype=np.float64)
        acc = img * s if acc is None else acc + img * s
        h = k % 2
        halves[h] = img * s if halves[h] is None else halves[h] + img * s
        halves_n[h] += s
        n += s
        k += 1
    full = acc / n
    a = halves[0] / halves_n[0] if halves_n[0] else full
    b = halves[1] / halves_n[1] if halves_n[1] else full
    return full, a, b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--photon", default=os.path.join(rs.REPO, "build", "fetch-release", "photon_render"))
    ap.add_argument("--scenes", default="")
    ap.add_argument("--photon-spp", type=int, default=4096)
    ap.add_argument("--ref-spp", type=int, default=8192)
    ap.add_argument("--chunk", type=int, default=256, help="Mitsuba'da bir render çağrısındaki örnek")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--variant", default="llvm_ad_rgb")
    ap.add_argument("--out", default=os.path.join(rs.REPO, "bench", "reference", "out"))
    args = ap.parse_args()

    import mitsuba as mi
    mi.set_variant(args.variant)

    os.makedirs(args.out, exist_ok=True)
    want = set(filter(None, args.scenes.split(",")))
    results = {}
    for sc in rs.scenes():
        if want and sc.name not in want:
            continue
        d = os.path.join(args.out, sc.name)
        os.makedirs(d, exist_ok=True)
        env_p = env_m = None
        if sc.env and "hdr" in sc.env:
            hdr = rs.load_hdr_stb(os.path.join(rs.REPO, sc.env["hdr"]))
            env_p = os.path.join(d, "env_photon.exr")
            env_m = os.path.join(d, "env_mitsuba.exr")
            rs.write_exr(env_p, hdr)
            rs.write_exr(env_m, rs.resample_for_mitsuba(hdr, 4))
        proj = rs.to_photon(sc, d, env_p)
        W, H = sc.res

        print(f"== {sc.name}: Photon {args.photon_spp} spp", flush=True)
        p_exr = os.path.join(d, "photon.exr")
        cmd = [args.photon, proj, "--out", p_exr, "--res", f"{W}x{H}", "--spp", str(args.photon_spp),
               "--no-denoise", "--filter", "box", "--bounces", "64", "--stats", os.path.join(d, "photon_stats.json")]
        if args.threads:
            cmd += ["--threads", str(args.threads)]
        t0 = time.time()
        subprocess.run(cmd, check=True)
        t_photon = time.time() - t0
        photon = rs.read_exr(p_exr)

        print(f"== {sc.name}: Mitsuba {args.ref_spp} spp ({args.variant})", flush=True)
        md = rs.to_mitsuba(sc, d, env_m, spp=args.chunk)
        t0 = time.time()
        ref, ra, rb = render_mitsuba(md, args.ref_spp, args.chunk)
        t_mi = time.time() - t0
        rs.write_exr(os.path.join(d, "mitsuba.exr"), ref)

        ratio = (photon.reshape(-1, 3).mean(0) / np.maximum(ref.reshape(-1, 3).mean(0), 1e-9)).tolist()
        br = block_rel(photon, ref)
        noise_br = block_rel(ra, rb)
        r = {
            "title": sc.title, "notes": sc.notes, "res": [W, H],
            "photon_spp": args.photon_spp, "ref_spp": args.ref_spp,
            "photon_seconds": t_photon, "mitsuba_seconds": t_mi,
            "mean_ratio_rgb": ratio,
            "relmse_photon_vs_ref": relmse(photon, ref),
            # Yarılar ref_spp/2 örnekli: tam referansla Photon arasındaki gürültünün kabaca 2 katı
            "relmse_ref_half_vs_half": relmse(ra, rb),
            "block16_rel_diff": {"median_abs": float(np.median(np.abs(br))), "p95_abs": float(np.percentile(np.abs(br), 95)),
                                 "mean": float(br.mean()), "max_abs": float(np.abs(br).max())},
            "block16_noise_floor": {"median_abs": float(np.median(np.abs(noise_br))),
                                    "p95_abs": float(np.percentile(np.abs(noise_br), 95))},
        }
        results[sc.name] = r
        tonemap_png(photon, os.path.join(d, "photon.png"))
        tonemap_png(ref, os.path.join(d, "mitsuba.png"))
        diff_png(photon, ref, os.path.join(d, "diff.png"))
        print(json.dumps(r, indent=1, ensure_ascii=False), flush=True)

    path = os.path.join(args.out, "results.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = json.load(f)
    old.update(results)
    with open(path, "w") as f:
        json.dump(old, f, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()
