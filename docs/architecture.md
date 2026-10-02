# PhotonEngine – Architecture

Each subsystem is a static library under `src/<module>/`. Every target exports `src/` as its include root, so includes read `#include "core/math/vec.h"`.

This page describes the tree as it is. Planned changes (a two-level BVH, a `Film` type, a `RenderDevice` interface, CUDA) are in [`plan.md`](../plan.md) and [`memory-bank/01-gorevler.md`](../memory-bank/01-gorevler.md).

---

## Link graph (from the CMake files)

```
photon_app ──► photon_ui ──► photon_preview ──► photon_scene ──► photon_engine
     │                                                │                │
     └──────────────► photon_scene, photon_engine     └─► photon_io    ├─► photon_integrators ─┐
                                                                       ├─► photon_io           │
photon_render (CLI) ──► photon_engine                                  ├─► photon_camera       │
photon_tests ──► photon_engine, photon_scene, gtest_main               ├─► photon_samplers     │
                                                                       ├─► photon_lights       │
                                                                       ├─► photon_materials    │
                                                                       ├─► photon_geometry     │
                                                                       └─► photon_core         │
                                                                                               │
photon_integrators ──► materials, lights, camera, samplers, geometry, core ◄───────────────────┘
photon_io          ──► materials, geometry, core
photon_materials   ──► geometry, core
photon_lights      ──► geometry, core
photon_camera, photon_samplers, photon_geometry ──► photon_core
```

Known wart: `integrators/path_tracer.cpp` includes `engine/scene.h` but `photon_integrators` does not link `photon_engine`. It only links because both are static and the final executable pulls in both. Fixing this (moving the render `Scene` below the integrator) is task F2.3.

Every `photon_*` target also links `photon_build_flags` privately: warnings (`/W4` or `-Wall -Wextra -Wpedantic`), optional warnings-as-errors, `/utf-8`, and the float model (`/fp:fast`, or `-ffast-math -fno-finite-math-only`). Third-party code does not get these flags.

---

## Modules

| Module | Contents |
|---|---|
| `core` | `Vec2f/Vec3f/Vec4f`, `Mat4f`, `Transform`, `Ray`, `AABB`, `Frame` (Duff 2017 ONB), `Quaternion`; `Color3f`; `Image` (float RGB + per-pixel sample counts), PNG/EXR/HDR I/O via stb and tinyexr, tone mapping; PCG32 `RNG`; sampling warps; `ThreadPool` and `parallelFor2D`; an unused arena allocator. |
| `geometry` | `Shape` interface, `Sphere`, `Triangle` (Möller–Trumbore), `TriangleMesh`, single-level binned-SAH `BVH` with 32-byte nodes. |
| `materials` | `Material` interface (`sample`, `eval`, `pdf`, `emitted`), `Lambertian`, `Mirror`, `Dielectric` (smooth and GGX rough), `DisneyMaterial` with texture maps. |
| `lights` | `Light` interface, `PointLight`, `DirectionalLight`, `AreaLight` (two-sided rectangle), `MeshLight`, `EnvironmentLight` (equirectangular, luminance·sinθ CDF). |
| `camera` | `PerspectiveCamera`, `ThinLensCamera`, `OrthographicCamera`. |
| `samplers` | `IndependentSampler`, `StratifiedSampler` (default for rendering). |
| `integrators` | `PathTracer`: NEE + BSDF sampling with the power heuristic, Russian roulette, optional (non-physical) contact AO. |
| `io` | `ObjLoader`, `GltfLoader`, with size and index limits on untrusted input. |
| `engine` | Render `Scene` (shapes, lights, environment, BVH), `Renderer` (tile-major `render`, pass-major `renderSamplePass`/`renderProgressive`), `denoiser` (OIDN or a 3×3 blur). |
| `scene` | Editable `SceneGraph` of `SceneNode`s, compiled into a render `Scene`; `MaterialLibrary` (JSON presets); project files; `UndoStack`; Cornell box builder. |
| `preview` | `GLPreview` (OpenGL GGX/IBL raster preview), `PickingPass`, `ViewportTexture`. |
| `ui` | ImGui theme, `OrbitCamera`, Windows file dialog, drag-and-drop payload ids. |
| `app` | `Application`: window, panels, preview render thread, full render and turntable jobs. |

---

## Data flow (desktop app)

```
SceneGraph (UI thread)
   │  SceneGraph::compile — bakes meshes to world space, builds the BVH
   ▼
render Scene  ──►  Renderer::renderSamplePass (preview thread + ThreadPool tiles)
                       per pixel: sampler → camera ray → PathTracer::Li → Image::addSample
   ▼
Image (sum + count) ──► ViewportTexture::upload (average, tone map, sRGB) ──► ImGui viewport
                    └─► saveImagePNG / saveImageEXR
```

The threading model, its single `imageMutex`, and the races it allows are described in the "UI → render veri akışı" section of [`memory-bank/02-bulgular.md`](../memory-bank/02-bulgular.md).

---

## Build targets

| Target | Type | Notes |
|---|---|---|
| `photon_core` … `photon_ui` | static library | one per `src/` folder |
| `photon_build_flags` | interface | warnings and float model for `photon_*` |
| `photon_app` | executable | desktop editor, `PHOTON_BUILD_APP` |
| `photon_render` | executable | CLI, `PHOTON_BUILD_CLI` |
| `photon_tests` | executable | GoogleTest suite, registered with CTest |
