#!/usr/bin/env python3
"""tests/data/golden/ altındaki altın görüntüleri Mitsuba 3 ile üretir (F2.8).

  python3 bench/reference/make_golden.py [--spp 65536]

Her referans sahnesinin küçük çözünürlüklü kopyası (en uzun kenar 64–96 piksel) için:
  <sahne>.photon        PhotonEngine projesi (HDRI, depodaki assets/ dosyasını göreli yolla gösterir)
  <sahne>.usda          aynı sahne OpenUSD olarak (içe aktarıcının render eşitliği testi)
  <sahne>_mitsuba.exr   Mitsuba 3 (llvm_ad_rgb, path, kutu filtre) çok yüksek örnekli render
tests/test_golden_reference.cpp bu dosyaları okur. Sahne tanımı değişirse bu betik yeniden çalıştırılır.
"""
from __future__ import annotations

import argparse
import dataclasses
import os
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refscenes as rs  # noqa: E402
from compare import render_mitsuba  # noqa: E402

GOLDEN_RES = {"cornell_diffuse": (64, 64), "cornell_specular": (64, 64), "hdri_materials": (96, 48)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spp", type=int, default=65536)
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--out", default=os.path.join(rs.REPO, "tests", "data", "golden"))
    ap.add_argument("--scenes-only", action="store_true", help="yalnız .photon ve .usda (Mitsuba render'ı yok)")
    args = ap.parse_args()
    if args.scenes_only:
        for sc in rs.scenes():
            if sc.name in GOLDEN_RES:
                sc = dataclasses.replace(sc, res=GOLDEN_RES[sc.name])
                env = os.path.join(rs.REPO, sc.env["hdr"]) if sc.env else None
                rs.to_photon(sc, args.out, env)
                rs.to_usd(sc, args.out, env)
        return

    import mitsuba as mi
    mi.set_variant("llvm_ad_rgb")
    os.makedirs(args.out, exist_ok=True)
    tmp = os.path.join(args.out, "_tmp")
    os.makedirs(tmp, exist_ok=True)
    for sc in rs.scenes():
        if sc.name not in GOLDEN_RES:
            continue
        sc = dataclasses.replace(sc, res=GOLDEN_RES[sc.name])
        env_p = env_m = None
        if sc.env:
            env_p = os.path.join(rs.REPO, sc.env["hdr"])  # Photon: depodaki .hdr (stb ile aynı çözüm)
            env_m = os.path.join(tmp, sc.name + "_env_mitsuba.exr")
            rs.write_exr(env_m, rs.resample_for_mitsuba(rs.load_hdr_stb(env_p), 4))
        rs.to_photon(sc, args.out, env_p)
        rs.to_usd(sc, args.out, env_p)
        ref, a, b = render_mitsuba(rs.to_mitsuba(sc, tmp, env_m, spp=args.chunk), args.spp, args.chunk)
        rs.write_exr(os.path.join(args.out, sc.name + "_mitsuba.exr"), ref)
        print(f"{sc.name}: {sc.res[0]}x{sc.res[1]} @ {args.spp} spp, ortalama {ref.reshape(-1, 3).mean(0)}")
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)


if __name__ == "__main__":
    main()
