#!/usr/bin/env python3
"""Aynı referans sahnesini Blender Cycles (CPU) ile render eder: verim (gürültü × süre) kıyası için.

  python3.11 bench/reference/cycles_render.py <sahne> <çıktı.exr> --spp N [--res WxH] [--threads 4]
         [--env env_photon.exr]

Gerekenler (Python 3.11): pip install bpy numpy OpenEXR

Eşleme (bkz. refscenes.py): diffuse → Diffuse BSDF, emitter → Emission, mirror → Glossy (r = 0),
glass → Glass BSDF (GGX), metal → Glossy BSDF "Multiscatter GGX" (Photon'un Kulla–Conty telafisine
en yakın Cycles lobu). Önyargılı kısayollar kapatılır: clamp 0, filter glossy 0, light threshold 0,
kostikler açık, tüm sekme sınırları 64, kutu piksel filtresi.

Koordinatlar: refscenes Y yukarı → Blender Z yukarı, (x, y, z) ↦ (x, −z, y) (det = +1).
HDRI: Cycles eşdikdörtgen u_c = 1 − u_p (Photon'a göre yatay ayna); ortam görüntüsü buna göre
ters çevrilip verilir.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import refscenes as rs  # noqa: E402

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402


def bl(v):
    x, y, z = v
    return Vector((x, -z, y))


def make_material(m, name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    t = m["type"]
    if t == "diffuse":
        s = nt.nodes.new("ShaderNodeBsdfDiffuse")
        s.inputs["Color"].default_value = (*m["albedo"], 1)
        s.inputs["Roughness"].default_value = 0.0
    elif t == "emitter":
        s = nt.nodes.new("ShaderNodeEmission")
        s.inputs["Color"].default_value = (*m["radiance"], 1)
        s.inputs["Strength"].default_value = 1.0
    elif t == "mirror":
        s = nt.nodes.new("ShaderNodeBsdfGlossy")
        s.distribution = "GGX"  # pürüzlülük 0 = ideal ayna (Blender 4+ "Sharp" seçeneğini kaldırdı)
        s.inputs["Color"].default_value = (*m["color"], 1)
        s.inputs["Roughness"].default_value = 0.0
    elif t == "glass":
        s = nt.nodes.new("ShaderNodeBsdfGlass")
        s.distribution = "GGX"
        s.inputs["IOR"].default_value = m["ior"]
        s.inputs["Roughness"].default_value = m["roughness"]
        s.inputs["Color"].default_value = (1, 1, 1, 1)
    elif t == "metal":
        s = nt.nodes.new("ShaderNodeBsdfGlossy")
        s.distribution = "MULTI_GGX"
        s.inputs["Color"].default_value = (1, 1, 1, 1)
        s.inputs["Roughness"].default_value = m["roughness"]
    else:
        raise ValueError(t)
    nt.links.new(s.outputs[0], out.inputs["Surface"])
    return mat


def add_quad(corners, mat, name):
    p, n, uv, idx = rs.quad_mesh(corners)
    me = bpy.data.meshes.new(name)
    verts = [tuple(bl(v)) for v in p]
    me.from_pydata(verts, [], [tuple(idx[0:3]), tuple(idx[3:6])])
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.data.materials.append(mat)
    bpy.context.scene.collection.objects.link(ob)


def add_sphere(c, r, mat, name):
    # Cycles'ta analitik küre yok: yüksek çözünürlüklü UV küre, yumuşak gölgelendirme.
    bpy.ops.mesh.primitive_uv_sphere_add(segments=256, ring_count=128, radius=r, location=tuple(bl(c)))
    ob = bpy.context.active_object
    ob.name = name
    bpy.ops.object.shade_smooth()
    ob.data.materials.append(mat)


def look_at(eye, target):
    e, t = bl(eye), bl(target)
    d = (t - e).normalized()
    up = Vector((0, 0, 1))
    right = d.cross(up).normalized()
    cam_up = right.cross(d)
    # Blender kamerası −Z'ye bakar, +Y yukarı.
    rot = Matrix((right, cam_up, -d)).transposed()
    m = rot.to_4x4()
    m.translation = e
    return m


def main():
    argv = sys.argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("scene")
    ap.add_argument("out")
    ap.add_argument("--spp", type=int, default=64)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--env", default="")
    ap.add_argument("--stats", default="")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args(argv)

    sc = next(s for s in rs.scenes() if s.name == a.scene)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    cy = scene.cycles
    cy.device = "CPU"
    cy.samples = a.spp
    cy.use_adaptive_sampling = False
    cy.use_denoising = False
    cy.seed = a.seed
    cy.max_bounces = cy.diffuse_bounces = cy.glossy_bounces = cy.transmission_bounces = 64
    cy.volume_bounces = 0
    cy.transparent_max_bounces = 64
    cy.sample_clamp_direct = 0.0
    cy.sample_clamp_indirect = 0.0
    cy.blur_glossy = 0.0
    cy.caustics_reflective = True
    cy.caustics_refractive = True
    cy.light_sampling_threshold = 0.0
    cy.pixel_filter_type = "BOX"
    cy.filter_width = 1.0
    scene.render.threads_mode = "FIXED" if a.threads else "AUTO"
    if a.threads:
        scene.render.threads = a.threads
    W, H = sc.res
    scene.render.resolution_x, scene.render.resolution_y = W, H
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0

    for k, o in enumerate(sc.objects):
        mat = make_material(o.mat, f"m{k}")
        if o.shape == "sphere":
            add_sphere(o.center, o.radius, mat, f"o{k}")
        else:
            add_quad(o.corners, mat, f"o{k}")

    cam_data = bpy.data.cameras.new("cam")
    cam_data.sensor_fit = "VERTICAL"
    cam_data.angle_y = math.radians(sc.fov)
    cam = bpy.data.objects.new("cam", cam_data)
    cam.matrix_world = look_at(sc.eye, sc.target)
    scene.collection.objects.link(cam)
    scene.camera = cam

    world = bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    wn = world.node_tree
    bg = wn.nodes["Background"]
    if a.env:
        img = rs.read_exr(a.env)[:, ::-1, :]  # Cycles eşlemesi Photon'a göre yatay ayna
        tmp = os.path.splitext(a.out)[0] + "_env_cycles.exr"
        rs.write_exr(tmp, img)
        tex = wn.nodes.new("ShaderNodeTexEnvironment")
        tex.image = bpy.data.images.load(tmp)
        tex.image.colorspace_settings.name = "Linear Rec.709" if "Linear Rec.709" in [
            e.identifier for e in tex.image.colorspace_settings.bl_rna.properties["name"].enum_items] else "Linear"
        tex.interpolation = "Linear"
        wn.links.new(tex.outputs["Color"], bg.inputs["Color"])
        bg.inputs["Strength"].default_value = 1.0
    else:
        bg.inputs["Color"].default_value = (0, 0, 0, 1)
        bg.inputs["Strength"].default_value = 0.0

    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_depth = "32"
    scene.render.image_settings.exr_codec = "ZIP"
    scene.render.filepath = a.out
    t0 = time.time()
    bpy.ops.render.render(write_still=True)
    dt = time.time() - t0
    print(f"Cycles: {W}x{H} @ {a.spp} spp, {dt:.2f} sn")
    if a.stats:
        with open(a.stats, "w") as f:
            json.dump({"renderSeconds": dt, "spp": a.spp, "width": W, "height": H,
                       "blender": bpy.app.version_string}, f, indent=1)


if __name__ == "__main__":
    main()
