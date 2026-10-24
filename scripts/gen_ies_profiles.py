#!/usr/bin/env python3
"""assets/ies/ altına örnek IES LM-63-2002 profilleri üretir (prosedürel, CC0).

  python3 scripts/gen_ies_profiles.py

Gerçek üretici dosyaları yerine geçmez; motorun IES yolunu denemek ve tipik
armatür karakterlerini göstermek içindir.
"""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "ies")


def write(name, title, vert, horiz, fn):
    lines = ["IESNA:LM-63-2002", f"[TEST] PhotonEngine prosedürel profil", f"[LUMCAT] {title}",
             "[MANUFAC] PhotonEngine (CC0)", "TILT=NONE",
             f"1 1000 1 {len(vert)} {len(horiz)} 1 2 0 0 0", "1 1 20"]
    lines.append(" ".join(f"{v:g}" for v in vert))
    lines.append(" ".join(f"{h:g}" for h in horiz))
    for h in horiz:
        lines.append(" ".join(f"{max(0.0, fn(v, h)):.2f}" for v in vert))
    with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("yazıldı:", name)


vert = [float(v) for v in range(0, 181, 2)]
# Dar huzmeli spot: ~24° huzme, yumuşak kenar ve zayıf dış hale.
write("dar_spot.ies", "Dar spot 24°", vert, [0.0],
      lambda v, h: 1000 * math.exp(-(v / 10.0) ** 2) + 40 * math.exp(-(v / 35.0) ** 2) if v < 90 else 0)
# Geniş gömme downlight: "yarasa kanadı" (batwing) — 35° civarında tepe, aşağıda hafif çukur.
write("downlight_yarasa_kanadi.ies", "Downlight batwing", vert, [0.0],
      lambda v, h: (600 + 500 * math.sin(math.radians(min(v, 70)) * 2.2) ** 2) * max(0.0, math.cos(math.radians(v))) ** 0.6
      if v < 88 else 0)
# Duvar yıkayıcı: yatay açıya göre asimetrik (bir yana fazla), duvarda tipik "yelpaze" izi.
horiz = [float(h) for h in range(0, 361, 10)]
write("duvar_yelpaze.ies", "Wall washer scallop", vert, horiz,
      lambda v, h: (800 * math.exp(-((v - 30) / 18.0) ** 2) + 150 * math.exp(-(v / 50.0) ** 2))
      * (0.35 + 0.65 * max(0.0, math.cos(math.radians(h))) ** 1.5) if v < 100 else 0)
