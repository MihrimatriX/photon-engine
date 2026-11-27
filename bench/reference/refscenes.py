"""Referans karşılaştırma sahneleri: tek tanımdan hem PhotonEngine (.photon) hem Mitsuba 3 sahnesi.

Amaç, iki motorun da *aynı* fiziksel modeli tam olarak ifade edebildiği parçalarda (Lambert,
ideal ayna, pürüzsüz/pürüzlü cam, GGX iletken, alan ışığı, HDRI) PhotonEngine'in yansız olup
olmadığını ölçmek. Malzeme eşlemesi bu yüzden bilerek dar tutulur:

  diffuse  → Photon "lambert"                 ↔ Mitsuba diffuse
  emitter  → Photon "lambert" + emission      ↔ Mitsuba area (siyah difüz yüzey)
  mirror   → Photon "mirror"                  ↔ Mitsuba conductor (material = none)
  glass    → Photon "glass" (pürüzlülük r)    ↔ Mitsuba dielectric / roughdielectric (α = r²)
  metal    → Photon "disney" metallic = 1,    ↔ Mitsuba roughconductor (material = none, α = r²)
             baseColor = 1 (F82 tonu beyazda F ≡ 1)

Bilinen ve kasıtlı model farkları (rapor bunları ayrıca açıklar):
  * GGX gölgeleme: Photon yüksekliğe bağlı Smith G2 kullanır, Mitsuba ayrık G1·G1. Fark yüksek
    pürüzlülükte ve sıyırma açılarında birkaç yüzde.
  * Çoklu saçılma: Photon'un Disney metali Kulla–Conty enerji telafisi yapar (beyaz metal fırında
    ≈ 1 yansıtır), Mitsuba'nın tek saçılmalı GGX'i pürüzlülükle enerji kaybeder.
  * Işık yayan yüzeyler: Photon'da Lambert ışıması (mesh yayıcı) çift yüzlü, Mitsuba'da tek yüzlü.
    Sahneler, ışık kaynaklarının arka yüzü boşluğa bakacak biçimde kuruldu. Gerçek ışıklar
    (Scene.lights: tek yüzlü dikdörtgen, disk, küre, tüp) iki motorda da aynı tek yüzlü modeldir.
  * HDRI texel kuralı: Mitsuba texel'leri ızgara noktası sayar, Photon piksel merkezi. Mitsuba'ya
    verilen ortam haritası bu yüzden resample_for_mitsuba ile 4× yoğun ızgarada yeniden örneklenir.

Koordinatlar: Y yukarı, birim metre. Kamera dikey FOV (derece) ile verilir.
"""
from __future__ import annotations

import json
import math
import os
from dataclasses import dataclass, field

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))


# ─────────────────────────────── sahne tanımı ───────────────────────────────

@dataclass
class Obj:
    shape: str                 # "sphere" | "quad"
    mat: dict
    center: tuple = (0, 0, 0)  # sphere
    radius: float = 1.0        # sphere
    corners: tuple = ()        # quad: 4 köşe, saat yönünün tersi (normal sağ el kuralıyla)
    name: str = ""


@dataclass
class Light:
    """Motorun şekilli ışığı; Mitsuba'da aynı şekil + area emitter (siyah difüz yüzey).

    rect/disk: center merkez, aim ışıdığı yön (normal), width × height (disk: çap); tek yüzlü.
    tube: center merkez, aim eksen yönü, width boy, height çap (açık silindir, uçlar ışımaz).
    sphere: center merkez, radius yarıçap (Photon'da yarıçaplı nokta ışık).
    """
    kind: str
    center: tuple
    radiance: tuple
    aim: tuple = (0, -1, 0)
    width: float = 1.0
    height: float = 1.0
    radius: float = 0.0
    name: str = ""


@dataclass
class Scene:
    name: str
    title: str
    eye: tuple
    target: tuple
    fov: float
    objects: list
    env: dict | None = None    # None → siyah; {"hdr": yol} → HDRI
    res: tuple = (320, 240)
    lights: list = field(default_factory=list)
    notes: str = ""
    extra: dict = field(default_factory=dict)


def diffuse(c):
    return {"type": "diffuse", "albedo": _c3(c)}


def emitter(L):
    return {"type": "emitter", "radiance": _c3(L)}


def mirror(c=1.0):
    return {"type": "mirror", "color": _c3(c)}


def glass(ior=1.5, roughness=0.0):
    return {"type": "glass", "ior": ior, "roughness": roughness}


def metal(roughness):
    return {"type": "metal", "roughness": roughness}


def _c3(c):
    return [float(c)] * 3 if np.isscalar(c) else [float(x) for x in c]


def quad(p0, p1, p2, p3, mat, name=""):
    return Obj("quad", mat, corners=(p0, p1, p2, p3), name=name)


def sphere(c, r, mat, name=""):
    return Obj("sphere", mat, center=c, radius=r, name=name)


# ─────────────────────────────── sahneler ───────────────────────────────

def _cornell(spheres, title, name, notes):
    W = (0.73, 0.73, 0.73)
    R = (0.63, 0.065, 0.05)
    G = (0.14, 0.45, 0.091)
    # Kutu [-1,1]×[0,2]×[-1,1], kamera +z'den bakar, ön yüz açık.
    # Işık tavan düzleminde, deliği tam doldurur. Photon'da Lambert ışıması çift yüzlüdür; ışık
    # tavanın biraz altında olsaydı arka yüzü deliğin kenarındaki tavanı sıyırarak aydınlatır ve
    # sahneye ~%0.2 fazladan ışık katardı (Mitsuba'da ışık tek yüzlü). Düzlemdeyken arka yüz boşluğa bakar.
    a, h = 0.25, 2.0  # ışık deliğinin yarı genişliği, ışığın yüksekliği
    objs = [
        quad((-1, 0, 1), (1, 0, 1), (1, 0, -1), (-1, 0, -1), diffuse(W), "zemin"),
        quad((-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), diffuse(W), "arka"),
        quad((-1, 0, 1), (-1, 0, -1), (-1, 2, -1), (-1, 2, 1), diffuse(R), "sol"),
        quad((1, 0, -1), (1, 0, 1), (1, 2, 1), (1, 2, -1), diffuse(G), "sağ"),
        # Tavan, ortasında ışık için kare delikle dört parça (aşağı bakar).
        quad((-1, 2, -1), (1, 2, -1), (1, 2, -a), (-1, 2, -a), diffuse(W), "tavan1"),
        quad((-1, 2, a), (1, 2, a), (1, 2, 1), (-1, 2, 1), diffuse(W), "tavan2"),
        quad((-1, 2, -a), (-a, 2, -a), (-a, 2, a), (-1, 2, a), diffuse(W), "tavan3"),
        quad((a, 2, -a), (1, 2, -a), (1, 2, a), (a, 2, a), diffuse(W), "tavan4"),
        quad((-a, h, -a), (a, h, -a), (a, h, a), (-a, h, a), emitter((17.0, 12.0, 4.0)), "ışık"),
    ]
    objs += spheres
    return Scene(name, title, eye=(0, 1, 3.9), target=(0, 1, 0), fov=39.3, objects=objs,
                 res=(256, 256), notes=notes)


def scenes():
    out = []
    out.append(_cornell(
        [sphere((-0.45, 0.4, -0.3), 0.4, diffuse(0.73)), sphere((0.45, 0.4, 0.3), 0.4, diffuse(0.73))],
        "Cornell kutusu, difüz", "cornell_diffuse",
        "Çok sekmeli difüz global aydınlatma, alan ışığı NEE + MIS, renk taşması."))
    out.append(_cornell(
        [sphere((-0.45, 0.4, -0.3), 0.4, mirror(1.0)), sphere((0.45, 0.4, 0.3), 0.4, glass(1.5, 0.0))],
        "Cornell kutusu, ayna + cam (kostik)", "cornell_specular",
        "Delta loblar, cam kırılması ve camın altındaki kostik (yalnız BSDF örneklemesiyle bulunur)."))

    floor = 4.0
    objs = [quad((-floor, 0, floor), (floor, 0, floor), (floor, 0, -floor), (-floor, 0, -floor), diffuse(0.5), "zemin")]
    xs = [-2.5, -1.5, -0.5, 0.5, 1.5, 2.5]
    mats = [diffuse((0.8, 0.8, 0.8)), metal(0.15), metal(0.5), mirror(1.0), glass(1.5, 0.0), glass(1.5, 0.3)]
    names = ["difüz", "metal r=0.15", "metal r=0.5", "ayna", "cam", "buzlu cam r=0.3"]
    for x, m, n in zip(xs, mats, names):
        objs.append(sphere((x, 0.45, 0.0), 0.45, m, n))
    out.append(Scene("hdri_materials", "HDRI stüdyo, malzeme sırası", eye=(0, 2.2, 6.2), target=(0, 0.45, 0),
                     fov=32.0, objects=objs, env={"hdr": "assets/environments/studio_small_09_1k.hdr"},
                     res=(384, 192),
                     notes="HDRI önem örneklemesi ve eşleme, GGX iletken (Kulla–Conty farkı beklenir), "
                           "pürüzsüz ve pürüzlü cam, zemine düşen yumuşak gölgeler."))

    # Şekilli ışıklar: tavanın hemen altında aşağı bakan tek yüzlü dikdörtgen (arka yüzü ışısaydı
    # üstündeki tavanı yakardı), duvardan sahneye eğik bakan disk, kameranın gördüğü küre ve tüp.
    W = 0.6
    room = [
        quad((-3, 0, 3), (3, 0, 3), (3, 0, -2), (-3, 0, -2), diffuse(W), "zemin"),
        quad((-3, 0, -2), (3, 0, -2), (3, 3, -2), (-3, 3, -2), diffuse(W), "arka duvar"),
        quad((-3, 2.6, -2), (3, 2.6, -2), (3, 2.6, 3), (-3, 2.6, 3), diffuse(W), "tavan"),
        sphere((-0.7, 0.45, -0.2), 0.45, diffuse((0.75, 0.75, 0.75)), "küre"),
        sphere((0.75, 0.3, 0.5), 0.3, diffuse((0.7, 0.45, 0.3)), "küçük küre"),
    ]
    lights = [
        Light("rect", (0.6, 2.35, -0.5), (6.0, 6.0, 6.0), aim=(0, -1, 0), width=1.2, height=0.7, name="tavan paneli"),
        Light("disk", (-1.9, 1.5, 0.3), (9.0, 7.0, 5.0), aim=(1, -0.35, -0.15), width=0.7, height=0.7,
              name="disk"),
        Light("sphere", (1.5, 0.85, -0.3), (14.0, 11.0, 8.0), radius=0.14, name="ampul"),
        Light("tube", (-0.2, 1.85, -1.6), (10.0, 12.0, 16.0), aim=(1, 0.08, 0.1), width=1.8, height=0.08,
              name="tüp"),
    ]
    out.append(Scene("shape_lights", "Şekilli ışıklar: tek yüzlü panel, disk, küre, tüp", eye=(0, 1.15, 4.4),
                     target=(0, 0.95, 0), fov=40.0, objects=room, lights=lights, res=(288, 192),
                     notes="Disk/küre/tüp ışıklarının NEE örneklemesi ve BSDF isabeti MIS'i, tek yüzlü ışıma, "
                           "kameranın gördüğü analitik ışık yüzeyleri."))
    return out


def area_frame(aim):
    """document.cpp buildLights ile aynı taban: n ışıdığı yön, u = up × n (genişlik), v = n × u."""
    n = np.array(aim, dtype=np.float64)
    n /= np.linalg.norm(n)
    up = np.array([1.0, 0.0, 0.0]) if abs(n[1]) > 0.95 else np.array([0.0, 1.0, 0.0])
    u = np.cross(up, n)
    u /= np.linalg.norm(u)
    v = np.cross(n, u)
    v /= np.linalg.norm(v)
    return n, u, v


def _unit(a):
    a = np.array(a, dtype=np.float64)
    return a / np.linalg.norm(a)


def _axis_frame(axis):
    """Sağ elli (X = eksen, Y, Z) tabanı: X × Y = Z."""
    x = _unit(axis)
    helper = np.array([0.0, 1.0, 0.0]) if abs(x[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    z = np.cross(x, helper)
    z /= np.linalg.norm(z)
    y = np.cross(z, x)
    return x, y, z


# ─────────────────────────────── geometri ───────────────────────────────

def quad_mesh(corners):
    p = np.array(corners, dtype=np.float64)
    n = np.cross(p[1] - p[0], p[3] - p[0])
    n /= np.linalg.norm(n)
    normals = np.tile(n, (4, 1))
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float64)
    idx = np.array([0, 1, 2, 0, 2, 3], dtype=np.int64)
    return p, normals, uv, idx


def write_ply(path, p, n, uv, idx):
    with open(path, "w") as f:
        f.write("ply\nformat ascii 1.0\n")
        f.write(f"element vertex {len(p)}\n")
        f.write("property float x\nproperty float y\nproperty float z\n")
        f.write("property float nx\nproperty float ny\nproperty float nz\n")
        f.write("property float u\nproperty float v\n")
        f.write(f"element face {len(idx) // 3}\nproperty list uchar int vertex_indices\nend_header\n")
        for i in range(len(p)):
            f.write(" ".join(f"{x:.9g}" for x in (*p[i], *n[i], *uv[i])) + "\n")
        for t in range(len(idx) // 3):
            f.write(f"3 {idx[3*t]} {idx[3*t+1]} {idx[3*t+2]}\n")


# ─────────────────────────────── HDRI ───────────────────────────────

def load_hdr_stb(path):
    """Radiance RGBE'yi stb_image ile aynı kuralla çözer: değer = m · 2^(e − 136).

    PhotonEngine HDR'yi stb_image ile okur; Mitsuba'nın kendi çözücüsü (+0.5 yuvarlama gibi)
    küçük farklar verebileceği için iki motora da bu çözümden yazılmış aynı float EXR verilir.
    """
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    while True:  # başlık satırları, boş satıra kadar
        end = data.index(b"\n", pos)
        line = data[pos:end]
        pos = end + 1
        if line.strip() == b"":
            break
    end = data.index(b"\n", pos)
    res = data[pos:end].split()
    pos = end + 1
    assert res[0] == b"-Y" and res[2] == b"+X", res
    H, W = int(res[1]), int(res[3])
    out = np.zeros((H, W, 4), dtype=np.uint8)
    buf = memoryview(data)
    for y in range(H):
        if buf[pos] == 2 and buf[pos + 1] == 2 and (buf[pos + 2] << 8 | buf[pos + 3]) == W:
            pos += 4
            for c in range(4):
                x = 0
                while x < W:
                    cnt = buf[pos]
                    pos += 1
                    if cnt > 128:
                        cnt -= 128
                        out[y, x:x + cnt, c] = buf[pos]
                        pos += 1
                    else:
                        out[y, x:x + cnt, c] = np.frombuffer(buf[pos:pos + cnt], dtype=np.uint8)
                        pos += cnt
                    x += cnt
        else:  # düz (RLE'siz) satır
            out[y] = np.frombuffer(buf[pos:pos + 4 * W], dtype=np.uint8).reshape(W, 4)
            pos += 4 * W
    e = out[..., 3].astype(np.int32)
    scale = np.where(e == 0, 0.0, np.ldexp(1.0, e - 136))
    rgb = out[..., :3].astype(np.float64) * scale[..., None]
    return rgb.astype(np.float32)


def write_exr(path, rgb):
    import OpenEXR
    rgb = np.ascontiguousarray(rgb.astype(np.float32))
    header = {"compression": OpenEXR.ZIP_COMPRESSION, "type": OpenEXR.scanlineimage}
    with OpenEXR.File(header, {"RGB": rgb}) as f:
        f.write(path)


def read_exr(path):
    import OpenEXR
    with OpenEXR.File(path) as f:
        ch = f.channels()
        if "RGB" in ch:
            a = np.asarray(ch["RGB"].pixels, dtype=np.float64)
        elif "RGBA" in ch:
            a = np.asarray(ch["RGBA"].pixels, dtype=np.float64)
        else:
            a = np.stack([np.asarray(ch[c].pixels, dtype=np.float64) for c in ("R", "G", "B")], axis=-1)
    return a[..., :3]


# ─────────────────────────────── dışa aktarıcılar ───────────────────────────────

def _identity_xf(t=(0, 0, 0)):
    return [1, 0, 0, t[0], 0, 1, 0, t[1], 0, 0, 1, t[2], 0, 0, 0, 1]


def _photon_material(m):
    t = m["type"]
    if t == "diffuse":
        return {"type": "lambert", "albedo": m["albedo"], "emission": [0, 0, 0]}
    if t == "emitter":
        return {"type": "lambert", "albedo": [0, 0, 0], "emission": m["radiance"]}
    if t == "mirror":
        return {"type": "mirror", "color": m["color"]}
    if t == "glass":
        return {"type": "glass", "ior": m["ior"], "tint": [1, 1, 1], "roughness": m["roughness"]}
    if t == "metal":
        return {"type": "disney", "baseColor": [1, 1, 1], "metallic": 1.0, "roughness": m["roughness"],
                "specular": 0.5, "clearCoat": 0.0, "clearCoatRoughness": 0.03, "anisotropy": 0.0,
                "sheen": 0.0, "diffuseTransmission": 0.0, "emission": [0, 0, 0]}
    raise ValueError(t)


def _photon_light(l: Light) -> dict:
    """Photon proje ışığı. Renk en büyük kanala bölünür, şiddet = o kanal (radyans = renk × şiddet).
    Küre: yarıçaplı nokta ışık, şiddet I = L · π r² (motor radyansı I / (π r²) olarak kurar)."""
    lum = max(l.radiance)
    c = np.array(l.center, dtype=np.float64)
    base = {"name": l.name or l.kind, "enabled": True, "color": [x / lum for x in l.radiance],
            "position": c.tolist(), "visibleToCamera": True, "azimuth": 45.0, "elevation": 45.0}
    if l.kind == "sphere":
        return {**base, "type": "point", "intensity": lum * math.pi * l.radius ** 2, "target": c.tolist(),
                "width": 1.0, "height": 1.0, "radius": l.radius}
    shape = {"rect": "rectangle", "disk": "disk", "tube": "tube"}[l.kind]
    return {**base, "type": "area", "shape": shape, "twoSided": False, "intensity": lum,
            "target": (c + _unit(l.aim)).tolist(), "width": l.width, "height": l.height}


def to_photon(scene: Scene, outdir: str, env_exr: str | None) -> str:
    mats, nodes = [], []
    for o in scene.objects:
        mats.append(_photon_material(o.mat))
        mi = len(mats) - 1
        if o.shape == "sphere":
            nodes.append({"type": "sphere", "name": o.name or "sphere", "visible": True, "material": mi,
                          "radius": o.radius, "xf": _identity_xf(o.center)})
        else:
            p, n, uv, idx = quad_mesh(o.corners)
            nodes.append({"type": "mesh", "name": o.name or "quad", "visible": True, "material": mi,
                          "xf": _identity_xf(), "geometry": {"p": p.ravel().tolist(), "n": n.ravel().tolist(),
                                                             "uv": uv.ravel().tolist(), "i": idx.tolist()}})
    e = np.array(scene.eye, dtype=np.float64)
    t = np.array(scene.target, dtype=np.float64)
    d = e - t
    r = float(np.linalg.norm(d))
    cam = {"target": t.tolist(), "radius": r, "theta": math.acos(d[1] / r), "phi": math.atan2(d[2], d[0]),
           "fov": scene.fov, "aperture": 0.0, "orthographic": False}
    if env_exr:
        env = {"hdr": os.path.relpath(env_exr, outdir), "rotation": 0.0, "intensity": 1.0,
               "background": {"mode": "environment", "color": [0, 0, 0]},
               "ground": {"enabled": False, "color": [0, 0, 0], "roughness": 1.0, "shadowCatcher": False}}
    else:
        env = {"hdr": "", "zenith": [0, 0, 0], "horizon": [0, 0, 0], "rotation": 0.0, "intensity": 0.0,
               "background": {"mode": "color", "color": [0, 0, 0]},
               "ground": {"enabled": False, "color": [0, 0, 0], "roughness": 1.0, "shadowCatcher": False}}
    proj = {"format": "photon-project", "version": 2, "camera": cam, "environment": env,
            "lights": [_photon_light(l) for l in scene.lights],
            "materials": mats, "nodes": nodes,
            "render": {"denoise": False, "exposure": 0.0, "maxBounces": 64, "toneMap": 0, "pixelFilter": "box",
                       "filterWidth": 1.0}}
    path = os.path.join(outdir, scene.name + ".photon")
    with open(path, "w") as f:
        json.dump(proj, f, indent=1, ensure_ascii=False)
    return path


def _usd_material(path, m):
    """OpenUSD malzemesi. Difüz ve ışık yayan yüzeyler MaterialX OpenPBR (specular_weight = 0 → saf
    Lambert; UsdPreviewSurface'in speküler lobu kapatılamaz), ayna/cam/metal UsdPreviewSurface."""
    t = m["type"]

    def f3(c):
        return "(" + ", ".join(f"{x:.9g}" for x in c) + ")"

    if t in ("diffuse", "emitter"):
        lines = ['uniform token info:id = "ND_open_pbr_surface_surfaceshader"', "float inputs:specular_weight = 0"]
        if t == "diffuse":
            lines += [f"color3f inputs:base_color = {f3(m['albedo'])}", "float inputs:base_weight = 1"]
        else:
            L = m["radiance"]
            lum = max(L)
            lines += ["float inputs:base_weight = 0", f"float inputs:emission_luminance = {lum:.9g}",
                      f"color3f inputs:emission_color = {f3([x / lum for x in L])}"]
        out, conn = "token outputs:out", "outputs:mtlx:surface"
    else:
        lines = ['uniform token info:id = "UsdPreviewSurface"']
        if t == "mirror":
            lines += [f"color3f inputs:diffuseColor = {f3(m['color'])}", "float inputs:metallic = 1",
                      "float inputs:roughness = 0"]
        elif t == "glass":
            lines += ["color3f inputs:diffuseColor = (1, 1, 1)", "float inputs:opacity = 0",
                      f"float inputs:ior = {m['ior']:.9g}", f"float inputs:roughness = {m['roughness']:.9g}"]
        elif t == "metal":
            lines += ["color3f inputs:diffuseColor = (1, 1, 1)", "float inputs:metallic = 1",
                      f"float inputs:roughness = {m['roughness']:.9g}"]
        else:
            raise ValueError(t)
        out, conn = "token outputs:surface", "outputs:surface"
    body = "\n".join("                " + x for x in lines + [out])
    port = out.split()[-1]
    return (f'        def Material "{path.split("/")[-1]}"\n        {{\n'
            f"            token {conn}.connect = <{path}/Surface.{port}>\n"
            f'            def Shader "Surface"\n            {{\n{body}\n'
            f"            }}\n        }}\n")


def _usd_light(k: int, l: Light) -> str:
    """UsdLux ışığı (yerelde −Z'ye ışır; tüpün ekseni X). Dikdörtgen/disk dönüşümü X = v, Y = u,
    Z = −n (sağ elli): USD genişliği (X) motorun yüksekliğidir, içe aktarıcı kenarları geri çevirir."""
    def f3(v):
        return "(" + ", ".join(f"{float(x):.9g}" for x in v) + ")"

    lum = max(l.radiance)
    color = f3([x / lum for x in l.radiance])
    c = np.array(l.center, dtype=np.float64)
    head = [f"float inputs:intensity = {lum:.9g}", f"color3f inputs:color = {color}"]
    if l.kind == "sphere":
        kind, attrs = "SphereLight", [f"float inputs:radius = {l.radius:.9g}"]
        xf = [f"double3 xformOp:translate = {f3(c)}", 'uniform token[] xformOpOrder = ["xformOp:translate"]']
    else:
        if l.kind == "tube":
            x, y, z = _axis_frame(l.aim)
            kind = "CylinderLight"
            attrs = [f"float inputs:length = {l.width:.9g}", f"float inputs:radius = {0.5 * l.height:.9g}"]
        else:
            n, u, v = area_frame(l.aim)
            x, y, z = v, u, -n
            if l.kind == "rect":
                kind = "RectLight"
                attrs = [f"float inputs:width = {l.height:.9g}", f"float inputs:height = {l.width:.9g}"]
            else:
                kind, attrs = "DiskLight", [f"float inputs:radius = {0.5 * l.width:.9g}"]
        xf = [f"matrix4d xformOp:transform = {_usd_matrix([[*x, 0], [*y, 0], [*z, 0], [*c, 1]])}",
              'uniform token[] xformOpOrder = ["xformOp:transform"]']
    body = "".join(f"        {a}\n" for a in head + attrs + xf)
    doc = (l.name or l.kind).replace('"', "'")
    return f'    def {kind} "L{k}" (\n        doc = "{doc}"\n    )\n    {{\n{body}    }}\n'


def _usd_matrix(rows):
    return "(" + ", ".join("(" + ", ".join(f"{x:.12g}" for x in r) + ")" for r in rows) + ")"


def to_usd(scene: Scene, outdir: str, env_hdr: str | None) -> str:
    """Sahneyi OpenUSD (.usda) olarak yazar: metre, Y yukarı. Dörtgenler 4 köşeli çokgen mesh (yükleyici
    üçgenler), küreler UsdGeomSphere, kamera UsdGeomCamera, HDRI DomeLight.

    Photon'un .photon'daki ortam eşlemesiyle birebir aynı ortam için kubbeye Ry(90°)·diag(−1, 1, 1)
    verilir: USD kubbesi OpenEXR eşdikdörtgen kuralını (Photon'un aynası) izler, aynalı dönüşüm bunu
    geri çevirir. Böylece aynı .hdr dosyası çevrilmeden ve dönüşsüz kullanılır.
    """
    def f3(v):
        return "(" + ", ".join(f"{float(x):.9g}" for x in v) + ")"

    mats, prims = [], []
    for k, o in enumerate(scene.objects):
        mpath = f"/World/Looks/M{k}"
        mats.append(_usd_material(mpath, o.mat))
        name = f"O{k}"
        doc = (o.name or o.shape).replace('"', "'")
        if o.shape == "sphere":
            prims.append(f'    def Sphere "{name}" (\n        prepend apiSchemas = ["MaterialBindingAPI"]\n'
                         f'        doc = "{doc}"\n    )\n    {{\n'
                         f"        double radius = {o.radius:.9g}\n"
                         f"        double3 xformOp:translate = {f3(o.center)}\n"
                         f'        uniform token[] xformOpOrder = ["xformOp:translate"]\n'
                         f"        rel material:binding = <{mpath}>\n    }}\n")
        else:
            p, n, uv, _ = quad_mesh(o.corners)
            st = [(u, 1.0 - v) for u, v in uv]  # USD st: t yukarı
            prims.append(f'    def Mesh "{name}" (\n        prepend apiSchemas = ["MaterialBindingAPI"]\n'
                         f'        doc = "{doc}"\n    )\n    {{\n'
                         f"        int[] faceVertexCounts = [4]\n"
                         f"        int[] faceVertexIndices = [0, 1, 2, 3]\n"
                         f"        point3f[] points = [{', '.join(f3(x) for x in p)}]\n"
                         f"        normal3f[] normals = [{', '.join(f3(x) for x in n)}] (\n"
                         f'            interpolation = "vertex"\n        )\n'
                         f"        texCoord2f[] primvars:st = [{', '.join(f3(x) for x in st)}] (\n"
                         f'            interpolation = "vertex"\n        )\n'
                         f'        uniform token subdivisionScheme = "none"\n'
                         f"        rel material:binding = <{mpath}>\n    }}\n")

    # Kamera: −Z'ye bakar, +Y yukarı. Açıklık oranı render oranına eşit (pencere uydurma belirsizliği yok).
    e = np.array(scene.eye, dtype=np.float64)
    t = np.array(scene.target, dtype=np.float64)
    f = (t - e) / np.linalg.norm(t - e)
    r = np.cross(f, [0.0, 1.0, 0.0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    W, H = scene.res
    va = 24.0
    focal = 0.5 * va / math.tan(math.radians(scene.fov) * 0.5)
    cam = (f'    def Camera "Camera"\n    {{\n'
           f"        float focalLength = {focal:.9g}\n"
           f"        float verticalAperture = {va:.9g}\n"
           f"        float horizontalAperture = {va * W / H:.9g}\n"
           f"        float2 clippingRange = (0.01, 1000)\n"
           f"        matrix4d xformOp:transform = {_usd_matrix([[*r, 0], [*u, 0], [*(-f), 0], [*e, 1]])}\n"
           f'        uniform token[] xformOpOrder = ["xformOp:transform"]\n    }}\n')
    lights = "".join(_usd_light(k, l) for k, l in enumerate(scene.lights))
    dome = ""
    if env_hdr:
        rel = os.path.relpath(env_hdr, outdir).replace(os.sep, "/")
        dome = (f'    def DomeLight "Ortam"\n    {{\n'
                f"        asset inputs:texture:file = @{rel}@\n"
                f'        token inputs:texture:format = "latlong"\n'
                f"        float inputs:intensity = 1\n"
                f"        float xformOp:rotateY = 90\n"
                f"        float3 xformOp:scale = (-1, 1, 1)\n"
                f'        uniform token[] xformOpOrder = ["xformOp:rotateY", "xformOp:scale"]\n    }}\n')
    text = ("#usda 1.0\n(\n"
            f'    doc = "PhotonEngine referans sahnesi {scene.name} (bench/reference/refscenes.py, to_usd)"\n'
            '    defaultPrim = "World"\n    metersPerUnit = 1\n    upAxis = "Y"\n)\n\n'
            'def Xform "World"\n{\n    def Scope "Looks"\n    {\n' + "".join(mats) + "    }\n\n" +
            "".join(prims) + lights + cam + dome + "}\n")
    path = os.path.join(outdir, scene.name + ".usda")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return path


def _mitsuba_bsdf(m):
    t = m["type"]
    if t == "diffuse":
        return {"type": "diffuse", "reflectance": {"type": "rgb", "value": m["albedo"]}}
    if t == "emitter":
        return {"type": "diffuse", "reflectance": {"type": "rgb", "value": [0, 0, 0]}}
    if t == "mirror":
        return {"type": "conductor", "material": "none", "specular_reflectance": {"type": "rgb", "value": m["color"]}}
    if t == "glass":
        if m["roughness"] <= 1e-3:
            return {"type": "dielectric", "int_ior": m["ior"], "ext_ior": 1.0}
        return {"type": "roughdielectric", "distribution": "ggx", "alpha": m["roughness"] ** 2,
                "int_ior": m["ior"], "ext_ior": 1.0}
    if t == "metal":
        return {"type": "roughconductor", "distribution": "ggx", "alpha": m["roughness"] ** 2, "material": "none"}
    raise ValueError(t)


def to_mitsuba(scene: Scene, outdir: str, env_exr_mitsuba: str | None, spp: int, max_depth: int = 65) -> dict:
    W, H = scene.res
    d = {"type": "scene",
         "integrator": {"type": "path", "max_depth": max_depth, "rr_depth": 5},
         "sensor": {"type": "perspective", "fov": scene.fov, "fov_axis": "y",
                    "to_world": _mi_look_at(scene.eye, scene.target),
                    "sampler": {"type": "independent", "sample_count": spp},
                    "film": {"type": "hdrfilm", "width": W, "height": H, "pixel_format": "rgb",
                             "rfilter": {"type": "box"}}}}
    for k, o in enumerate(scene.objects):
        if o.shape == "sphere":
            s = {"type": "sphere", "center": list(o.center), "radius": o.radius}
        else:
            ply = os.path.join(outdir, f"{scene.name}_{k}.ply")
            write_ply(ply, *quad_mesh(o.corners))
            s = {"type": "ply", "filename": ply, "face_normals": False}
        s["bsdf"] = _mitsuba_bsdf(o.mat)
        if o.mat["type"] == "emitter":
            s["emitter"] = {"type": "area", "radiance": {"type": "rgb", "value": o.mat["radiance"]}}
        d[f"obj{k}"] = s
    for k, l in enumerate(scene.lights):
        d[f"light{k}"] = _mitsuba_light(l)
    if env_exr_mitsuba:
        d["env"] = {"type": "envmap", "filename": env_exr_mitsuba, "scale": 1.0}
    return d


def _mitsuba_light(l: Light) -> dict:
    """Şekil + tek yüzlü area emitter (normal yönüne ışır), yüzey siyah difüz. Mitsuba'nın silindiri
    de uçları açık bir tüptür."""
    import mitsuba as mi
    black = {"type": "diffuse", "reflectance": {"type": "rgb", "value": [0, 0, 0]}}
    em = {"type": "area", "radiance": {"type": "rgb", "value": list(l.radiance)}}
    c = np.array(l.center, dtype=np.float64)
    if l.kind == "sphere":
        return {"type": "sphere", "center": c.tolist(), "radius": l.radius, "bsdf": black, "emitter": em}
    if l.kind == "tube":
        a = _unit(l.aim) * (0.5 * l.width)
        return {"type": "cylinder", "p0": (c - a).tolist(), "p1": (c + a).tolist(), "radius": 0.5 * l.height,
                "bsdf": black, "emitter": em}
    n, u, v = area_frame(l.aim)
    m = np.eye(4)
    m[:3, 0] = u * (0.5 * l.width)
    m[:3, 1] = v * (0.5 * l.height)
    m[:3, 2] = n
    m[:3, 3] = c
    shape = "rectangle" if l.kind == "rect" else "disk"
    return {"type": shape, "to_world": mi.ScalarTransform4f(m.tolist()), "bsdf": black, "emitter": em}


def _mi_look_at(eye, target):
    import mitsuba as mi
    return mi.ScalarTransform4f().look_at(origin=list(eye), target=list(target), up=[0, 1, 0])


def resample_for_mitsuba(img, factor=4):
    """Photon'un piksel-merkezli çift doğrusal ortam fonksiyonunu Mitsuba'nın köşe (texel = ızgara
    noktası) kuralına örnekler.

    Mitsuba 3 envmap'i texel'leri ızgara noktası sayar: u = i / W (sarma için ek sütun), v = j / (H − 1).
    Photon (ve çoğu motor) piksel merkezi kullanır: u = (i + ½) / W, v = (j + ½) / H. Aynı görüntü iki
    kuralda dikeyde H / (H − 1) kadar gerilir; 512 satırlık bir HDRI'da ortamın toplam gücü Mitsuba'da
    ≈ %0.2 fazla çıkar. Bu fonksiyon Photon'un sürekli fonksiyonunu @p factor kat yoğun ızgarada
    Mitsuba'nın ızgara noktalarında değerlendirir; kalan fark ~1 / (factor · H) mertebesindedir.
    """
    H, W, _ = img.shape
    Hm, Wm = H * factor, W * factor
    i = np.arange(Wm)
    j = np.arange(Hm)
    u_m = i / Wm
    v = j / (Hm - 1)
    u_p = (0.75 - u_m) % 1.0
    x = u_p * W - 0.5
    y = v * H - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[None, :, None]
    fy = (y - y0)[:, None, None]
    xa, xb = x0 % W, (x0 + 1) % W
    ya, yb = np.clip(y0, 0, H - 1), np.clip(y0 + 1, 0, H - 1)
    img = img.astype(np.float64)
    c0 = img[ya][:, xa] * (1 - fx) + img[ya][:, xb] * fx
    c1 = img[yb][:, xa] * (1 - fx) + img[yb][:, xb] * fx
    return (c0 * (1 - fy) + c1 * fy).astype(np.float32)
