# PhotonEngine assets

Offline-friendly content for the desktop app. Paths are resolved relative to this folder (`assetsRoot`).

## Layout

| Path | Purpose |
|------|---------|
| `materials/*.json` | Disney PBR material presets |
| `environments/*.json` | Procedural / HDR environment entries |
| `studios/*.json` | Camera + light + env packs |
| `models/` | Built-in sample meshes for the test environment |

## Sample models (`models/`)

All meshes below are **procedurally generated in-repo** (no third-party downloads) by `scripts/gen_sample_models.py`. Treat as public domain / CC0. Units are meters, Y is up.

| File | Format | Notes |
|------|--------|-------|
| `cube.obj` | OBJ | Product cube, 1×1×1 m, sits on y = 0 |
| `sphere.obj` | OBJ | UV sphere, r = 0.5 m, centred at the origin |
| `product_stand.obj` | OBJ | Round pedestal, r = 0.6 m, top at y = 0.45 m |
| `sample_box.gltf` | glTF 2.0 | Soft-white PBR box with embedded buffer |
| `floor.obj` | OBJ | Ground plane, 12×12 m at y = 0 |

## FBX

FBX import is **not shipped**. Drag-drop may list `.fbx` as a known extension, but there is no Assimp/FBX loader. Use OBJ or glTF. Adding Assimp is deferred to keep the dependency surface small.

## Opening the test environment

In the app:

1. **Dosya → Ornek Sahne** — clears the graph, loads stand + sphere + floor, Product Studio lights, chrome / white plastic materials
2. **Kutuphane → Ornekler** — drag a sample model into the viewport
3. **Dosya → Cornell Box** — classic secondary test scene

First launch prefers the sample product scene when `models/` is present.
