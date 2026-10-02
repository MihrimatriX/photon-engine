# PhotonEngine

**A physically based CPU path tracer with a desktop editor for product visualization.**

PhotonEngine is a C++20 renderer and a learning project: the goal is a fast, physically correct, easy-to-use engine, built while learning the whole computer-graphics pipeline. The work plan is in [`plan.md`](plan.md); the task list and the audit it is based on are in [`memory-bank/`](memory-bank/).

---

## What exists today

- **Path tracer** with next-event estimation and MIS (power heuristic), Russian roulette, environment and area-light importance sampling.
- **Materials:** Lambertian, mirror, smooth and rough glass (GGX), and a simplified Disney principled BRDF (Burley diffuse, GGX specular, clearcoat, anisotropy, sheen, thin diffuse transmission).
- **Lights:** rectangular area lights, emissive meshes, point, directional, and an equirectangular HDR environment with a luminance CDF.
- **Geometry:** triangle meshes and spheres in a single-level binned-SAH BVH.
- **Cameras:** pinhole, thin lens (depth of field), orthographic.
- **Sampling:** stratified (default) and independent samplers.
- **Import:** OBJ (tinyobjloader) and glTF 2.0 (cgltf), with base-color, normal, roughness and metalness maps.
- **Image I/O:** PNG, JPG, HDR, EXR in; PNG and EXR out.
- **Denoising:** Intel Open Image Denoise when it is found at configure time. Without it, the "denoise" switch is only a 3×3 blur.
- **Desktop editor:** GLFW + ImGui with docking, drag and drop, a material library, an OpenGL raster preview and progressive path-traced viewport.

Known gaps and bugs are listed with file and line in [`memory-bank/02-bulgular.md`](memory-bank/02-bulgular.md). Nothing has been benchmarked yet.

---

## Build

Requirements: CMake ≥ 3.21, Ninja, and a C++20 compiler (MSVC 19.4x+, GCC 12+, Clang 15+).

### Windows (MSVC + vcpkg)

`VCPKG_ROOT` must point at a vcpkg checkout. glfw3 and imgui come from vcpkg (`vcpkg.json`).

```powershell
. .\scripts\devshell.ps1          # VS developer shell at the repo root
cmake --preset release
cmake --build --preset release
ctest --preset release
```

`dev` (Debug) and `asan` (AddressSanitizer) presets work the same way.

### Without vcpkg (Linux or Windows)

Every dependency has a pinned FetchContent fallback, so no package manager is needed:

```bash
cmake --preset fetch-release
cmake --build --preset fetch-release
ctest --preset fetch-release
```

`fetch-asan` builds the same tree with AddressSanitizer and UBSan (GCC/Clang). On Linux, GLFW needs the X11 and Wayland development packages (`libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev libwayland-dev libxkbcommon-dev`).

### CMake options

| Option | Default | Description |
|---|---|---|
| `PHOTON_BUILD_APP` | `ON` | Build the `photon_app` desktop editor |
| `PHOTON_BUILD_CLI` | `OFF` (`ON` in `fetch-*`) | Build the `photon_render` command-line renderer |
| `PHOTON_BUILD_TESTS` | `ON` | Build the GoogleTest suite |
| `PHOTON_ENABLE_SIMD` | `ON` | Compile with AVX2/FMA |
| `PHOTON_ENABLE_ASAN` | `OFF` | Instrument `photon_*` targets with AddressSanitizer |
| `PHOTON_WARNINGS_AS_ERRORS` | `ON` (GCC/Clang), `OFF` (MSVC) | Warnings in `photon_*` targets fail the build |
| `PHOTON_ENABLE_OIDN` | `OFF` | OIDN is linked automatically when `find_package(OpenImageDenoise)` succeeds; `ON` only adds a warning when it does not |

OIDN is not in vcpkg. The plan (Phase 3) is to take the official prebuilt package into `third_party/oidn/`.

Every third-party dependency and its license is listed in [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).

---

## Usage

```bash
./build/fetch-release/src/app/photon_app      # or build\release\src\app\photon_app.exe
```

1. Drag an OBJ or glTF model into the viewport, or use **Dosya → Ornek Sahne**.
2. Drag materials from the library (left) onto parts.
3. Pick an environment or a studio preset.
4. **Render → Tam Cozunurluk** renders at full resolution; **Dosya → Disa Aktar** saves PNG or EXR.

`photon_render` currently renders a fixed Cornell box; command-line arguments come in Phase 2.

---

## Layout

```
src/
├── core/          math, color, image + I/O, tone mapping, RNG, sampling warps, thread pool
├── geometry/      sphere, triangle, mesh, BVH
├── materials/     Lambertian, mirror, dielectric, Disney
├── lights/        point, directional, area, mesh, environment
├── camera/        perspective, thin lens, orthographic
├── samplers/      independent, stratified
├── integrators/   path tracer
├── io/            OBJ and glTF loaders
├── engine/        render Scene, Renderer, denoiser
├── scene/         editable SceneGraph, material library, project files, undo
├── preview/       OpenGL raster preview, picking, viewport texture
├── ui/            theme, orbit camera, file dialog
├── app/           photon_app
└── main.cpp       photon_render
```

See [`docs/architecture.md`](docs/architecture.md) for the module graph.
