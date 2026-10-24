#!/usr/bin/env python3
"""İki ya da daha fazla EXR'nin bir referansa göre hatası (görüntü ortalaması, relMSE).

  python3 scripts/compare_exr.py referans.exr a.exr b.exr ...

relMSE = ortalama((x − ref)² / (ref² + 0.01)): parlak bölgelerin baskın olmaması için
göreli hata (Rousselle vd. 2011). Eşit örnek sayısında daha küçük = daha az gürültü.
"""
import sys

import numpy as np
import OpenEXR


def load(path):
    with OpenEXR.File(path) as f:
        ch = f.channels()
        if "RGB" in ch:
            a = np.asarray(ch["RGB"].pixels, dtype=np.float64)
        else:
            a = np.stack([np.asarray(ch[c].pixels, dtype=np.float64) for c in ("R", "G", "B")], axis=-1)
    return a[..., :3]


ref = load(sys.argv[1])
print(f"{'dosya':40s} {'ortalama':>10s} {'relMSE':>10s} {'RMSE':>10s}")
print(f"{sys.argv[1]:40s} {ref.mean():10.4f} {'—':>10s} {'—':>10s}")
for p in sys.argv[2:]:
    x = load(p)
    rel = np.mean((x - ref) ** 2 / (ref ** 2 + 0.01))
    rmse = np.sqrt(np.mean((x - ref) ** 2))
    print(f"{p:40s} {x.mean():10.4f} {rel:10.5f} {rmse:10.5f}")
