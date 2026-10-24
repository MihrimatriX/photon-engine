#!/usr/bin/env python3
"""bench/scenes/ altına malzeme vitrin sahneleri üretir (görsel regresyon ve tanıtım).

  python3 scripts/gen_material_scenes.py

Her sahne: yan yana küreler (assets/models/sphere.obj, yarıçap 0.5), stüdyo HDRI,
üç alan ışığı (ana, dolgu, kontur). Kürelerin malzemeleri aşağıdaki listelerden gelir;
anahtarlar proje dosyasının malzeme şemasıyla aynıdır (src/scene/project_io.cpp).
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "bench", "scenes")


def disney(base, metallic=0.0, roughness=0.5, specular=0.5, **extra):
    m = {"type": "disney", "baseColor": list(base), "metallic": metallic, "roughness": roughness,
         "specular": specular, "clearCoat": 0.0, "clearCoatRoughness": 0.03, "anisotropy": 0.0,
         "sheen": 0.0, "diffuseTransmission": 0.0, "emission": [0, 0, 0]}
    m.update(extra)
    return m


def glass(ior=1.5, roughness=0.0, tint=(1, 1, 1), **extra):
    m = {"type": "glass", "ior": ior, "roughness": roughness, "tint": list(tint)}
    m.update(extra)
    return m


def preset(file, **overrides):
    """assets/materials/<file> preset'ini proje malzemesine çevir (anahtarlar ortak)."""
    with open(os.path.join(ROOT, "assets", "materials", file), encoding="utf-8") as f:
        p = json.load(f)
    if p.get("type") == "glass":
        m = glass(p.get("ior", 1.5), p.get("roughness", 0.0), p.get("baseColor", (1, 1, 1)))
    else:
        m = disney(p.get("baseColor", (0.8, 0.8, 0.8)), p.get("metallic", 0.0), p.get("roughness", 0.5),
                   p.get("specular", 0.5))
        for k in ("clearCoat", "clearCoatRoughness", "anisotropy", "sheen", "diffuseTransmission"):
            if k in p:
                m[k] = p[k]
    for k in ("absorption", "scattering", "abbe", "subsurface", "edgeTint", "thinFilm", "flakes"):
        if k in p:
            m[k] = p[k]
    m.update(overrides)
    return m


def area(name, az, el, dist, size, intensity, target, color=(1, 1, 1)):
    a, e = math.radians(az), math.radians(el)
    d = (math.cos(e) * math.cos(a), math.sin(e), math.cos(e) * math.sin(a))
    pos = [target[i] + d[i] * dist for i in range(3)]
    return {"type": "area", "name": name, "enabled": True, "color": list(color), "intensity": intensity,
            "position": pos, "target": list(target), "width": size, "height": size,
            "azimuth": az, "elevation": el}


def scene(materials, names, spacing=1.25, hdr="studio_small_09_1k.hdr", env_intensity=0.8,
          theta=1.28, extra_lights=None, ground=0.42, camera_extra=None):
    n = len(materials)
    width = (n - 1) * spacing
    nodes = []
    for i, name in enumerate(names):
        x = -width / 2 + i * spacing
        xf = [1, 0, 0, x, 0, 1, 0, 0.5, 0, 0, 1, 0, 0, 0, 0, 1]
        nodes.append({"name": name, "type": "group", "visible": True, "source": "../../assets/models/sphere.obj",
                      "xf": xf, "children": [{"name": "Parça 1", "type": "mesh", "visible": True, "sourceIndex": 0,
                                              "material": i, "xf": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}]})
    target = [0.0, 0.5, 0.0]
    radius = max(3.2, (width / 2 + 0.45) / 0.62)  # ~2.5:1 kadrajda sıra tam sığar
    lights = [area("Ana ışık", 120, 42, radius * 0.9, radius * 0.45, 7.0, target, (1.0, 0.97, 0.92)),
              area("Dolgu", 45, 25, radius * 1.1, radius * 0.6, 1.5, target),
              area("Kontur", 260, 35, radius * 0.8, radius * 0.3, 9.0, target)]
    if extra_lights:
        lights += extra_lights
    cam = {"target": target, "radius": radius, "theta": theta, "phi": math.pi / 2, "fov": 30.0,
           "orthographic": False, "aperture": 0.0}
    if camera_extra:
        cam.update(camera_extra)
    return {
        "format": "photon-project", "version": 2,
        "camera": cam,
        "render": {"denoise": True, "exposure": 0.0, "maxBounces": 12, "toneMap": 5},
        "environment": {"background": {"color": [0.18, 0.18, 0.18], "mode": "environment"},
                        "ground": {"color": [ground] * 3, "enabled": True, "roughness": 0.55},
                        "hdr": "../../assets/environments/" + hdr, "intensity": env_intensity, "rotation": 0.0,
                        "horizon": [0.55, 0.55, 0.58], "zenith": [0.75, 0.8, 0.95]},
        "lights": lights,
        "materials": materials,
        "nodes": nodes,
    }


def write(name, data):
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    print("yazıldı:", os.path.relpath(path, ROOT))


# 1) Hacim ve ışık geçirgenliği: SSS, renkli/bulanık cam, dispersiyon.
volume = [
    ("Mum", disney((0.92, 0.82, 0.62), roughness=0.38,
                   subsurface={"weight": 1.0, "radius": [1.0, 0.55, 0.25], "scale": 0.12, "anisotropy": 0.0})),
    ("Yeşim", disney((0.32, 0.72, 0.42), roughness=0.12, clearCoat=0.4, clearCoatRoughness=0.05,
                     subsurface={"weight": 1.0, "radius": [0.45, 1.0, 0.6], "scale": 0.25, "anisotropy": 0.2})),
    ("Deri", disney((0.86, 0.55, 0.45), roughness=0.45,
                    subsurface={"weight": 1.0, "radius": [1.0, 0.2, 0.1], "scale": 0.08, "anisotropy": 0.0})),
    ("Kırmızı şarap", glass(1.34, absorption={"color": [0.55, 0.04, 0.09], "distance": 0.25})),
    ("Opal cam", glass(1.45, absorption={"color": [0.95, 0.85, 0.75], "distance": 1.0},
                       scattering={"density": 6.0, "color": [0.96, 0.97, 1.0], "anisotropy": 0.3})),
    ("Elmas", glass(2.42, abbe=12.0)),
]
write("materials_volume.photon", scene([m for _, m in volume], [n for n, _ in volume]))

# 2) Metaller ve boyalar: ölçülmüş metaller (F82 kenar tonu), ince film, pullu boyalar.
metals = [
    ("Krom", preset("20_chrome.json")),
    ("Altın", preset("23_gold.json", roughness=0.25)),
    ("Bakır", preset("24_copper.json", roughness=0.45)),
    ("Isıl renkli titanyum", preset("29_titanium_heat_tint.json")),
    ("Gümüş metalik boya", preset("44_silver_metallic_paint.json",
                                  flakes={"density": 0.7, "size": 0.006, "roughness": 0.3, "color": [0.92, 0.92, 0.93]})),
    ("Sedef", preset("46_pearlescent.json")),
    ("Şeker kırmızı", preset("47_candy_red.json",
                             flakes={"density": 0.6, "size": 0.006, "roughness": 0.25, "color": [1.0, 0.2, 0.15]})),
]
write("materials_metal.photon", scene([m for _, m in metals], [n for n, _ in metals], spacing=1.15))


def node(name, model, xf, material):
    return {"name": name, "type": "group", "visible": True, "source": "../../assets/models/" + model, "xf": xf,
            "children": [{"name": "Parça 1", "type": "mesh", "visible": True, "sourceIndex": 0, "material": material,
                          "xf": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}]}


def trs(tx, ty, tz, sx=1.0, sy=None, sz=None):
    sy = sx if sy is None else sy
    sz = sx if sz is None else sz
    return [sx, 0, 0, tx, 0, sy, 0, ty, 0, 0, sz, tz, 0, 0, 0, 1]


# 3) Fiziksel gök + gölge yakalayıcı: beyaz zemin üstünde ürün (e-ticaret çekimi), altın saat.
sky_scene = scene([preset("47_candy_red.json"), preset("20_chrome.json"), preset("63_jade.json")],
                  ["Kırmızı küre", "Krom küre", "Yeşim küre"], spacing=1.2)
sky_scene["lights"] = []
sky_scene["environment"].update({
    "kind": "sky", "intensity": 1.0,
    "sky": {"elevation": 14.0, "azimuth": 130.0, "turbidity": 3.0, "ozone": 1.0, "groundAlbedo": 0.3,
            "sunSize": 1.2, "sunStrength": 3.0, "fromLocation": False},
    "background": {"mode": "color", "color": [0.92, 0.92, 0.92]},
    "ground": {"enabled": True, "color": [0.8, 0.8, 0.8], "roughness": 0.6, "shadowCatcher": True}})
write("sky_shadow_catcher.photon", sky_scene)

# 4) Spot ve IES: karanlık oda, arka duvar, üç IES profili.
wall_mat = disney((0.75, 0.74, 0.72), roughness=0.8)
ies_scene = scene([wall_mat, preset("23_gold.json"), preset("62_wax.json")], ["Duvar", "Altın küre", "Mum küre"])
ies_scene["nodes"] = [node("Duvar", "cube.obj", trs(0, 0, -1.6, 7.0, 3.2, 0.1), 0),
                      node("Altın küre", "sphere.obj", trs(-0.8, 0.5, 0.0), 1),
                      node("Mum küre", "sphere.obj", trs(0.8, 0.5, 0.0), 2)]
ies_scene["environment"].update({"intensity": 0.02, "ground": {"enabled": True, "color": [0.3, 0.3, 0.3], "roughness": 0.5}})
spots = []
for i, (ies, x) in enumerate([("dar_spot.ies", -2.2), ("downlight_yarasa_kanadi.ies", 0.0), ("duvar_yelpaze.ies", 2.2)]):
    spots.append({"type": "spot", "name": "Spot " + str(i + 1), "enabled": True, "color": [1.0, 0.86, 0.7],
                  "intensity": 14.0, "position": [x, 3.0, -1.3], "target": [x, 0.0, -1.52],
                  "width": 1, "height": 1, "azimuth": 0, "elevation": 0, "coneAngle": 180.0, "coneBlend": 0.0,
                  "ies": "../../assets/ies/" + ies})
ies_scene["lights"] = spots
ies_scene["camera"].update({"radius": 6.5, "theta": 1.35})
write("spot_ies.photon", ies_scene)

# 5) Alan derinliği + çokgen bokeh + görüntüleyici (bloom ve yıldız parlaması).
neon = [disney(c, roughness=0.4, emission=[v * 12.0 for v in c]) for c in
        [(1.0, 0.25, 0.1), (0.2, 0.6, 1.0), (1.0, 0.85, 0.4), (0.8, 0.2, 1.0), (0.3, 1.0, 0.5)]]
bokeh = scene([preset("20_chrome.json"), preset("23_gold.json")] + neon,
              ["Krom", "Altın"] + ["Işık " + str(i) for i in range(len(neon))])
bokeh["nodes"] = [node("Krom", "sphere.obj", trs(-0.35, 0.5, 1.2), 0), node("Altın", "sphere.obj", trs(0.55, 0.35, 1.9, 0.7), 1)]
import random
random.seed(7)
for i in range(18):
    m = 2 + (i % len(neon))
    bokeh["nodes"].append(node("Işık " + str(i), "sphere.obj",
                               trs(random.uniform(-5, 5), random.uniform(0.3, 3.5), random.uniform(-9, -6), 0.12), m))
bokeh["lights"] = bokeh["lights"][:1]
bokeh["lights"][0]["intensity"] = 4.0
bokeh["environment"].update({"intensity": 0.15, "ground": {"enabled": True, "color": [0.15, 0.15, 0.16], "roughness": 0.25}})
bokeh["camera"] = {"target": [0.0, 0.5, 1.4], "radius": 4.2, "theta": 1.45, "phi": math.pi / 2, "fov": 32.0,
                   "orthographic": False, "aperture": 0.09, "focusExplicit": True, "focusDistance": 4.0,
                   "apertureBlades": 6, "bladeRotation": 15.0}
bokeh["render"]["imager"] = {"bloom": 0.35, "bloomRadius": 0.03, "bloomThreshold": 1.5, "glare": 0.3,
                             "glareBlades": 6, "glareAngle": 15.0, "glareLength": 0.12, "vignette": 0.35,
                             "whiteBalance": 6500.0, "contrast": 1.1, "saturation": 1.05}
write("bokeh_imager.photon", bokeh)
