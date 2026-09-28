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

All meshes below are **procedurally generated in-repo** (no third-party downloads). Treat as public domain / CC0.

| File | Format | Notes |
|------|--------|-------|
| `cube.obj` | OBJ | Unit-ish product cube (1×1×1, sits on Y=0) |
| `sphere.obj` | OBJ | UV sphere (r≈0.5) |
| `product_stand.obj` | OBJ | Pedestal for product viz |
| `sample_box.gltf` | glTF 2.0 | Soft-white PBR box with embedded buffer |
| `floor.obj` | OBJ | Large ground plane |

## FBX

FBX import is **not shipped**. Drag-drop may list `.fbx` as a known extension, but there is no Assimp/FBX loader. Use OBJ or glTF. Adding Assimp is deferred to keep the dependency surface small.

## Opening the test environment

In the app:

1. **Dosya → Ornek Sahne** — clears the graph, loads stand + sphere + floor, Product Studio lights, chrome / white plastic materials
2. **Kutuphane → Ornekler** — drag a sample model into the viewport
3. **Dosya → Cornell Box** — classic secondary test scene

First launch prefers the sample product scene when `models/` is present.
