# PhotonEngine – Architecture

## Overview

PhotonEngine follows a **modular, layered architecture** where each subsystem is isolated into its own static library. Modules communicate through well-defined interfaces, making it easy to swap implementations (e.g., replacing the BVH with a GPU-accelerated structure) without affecting the rest of the engine.

---

## Module Dependency Graph

```
                    ┌──────────┐
                    │  engine   │  ← orchestrates rendering
                    └────┬─────┘
           ┌─────────┬──┴──┬──────────┬──────────┐
           ▼         ▼     ▼          ▼          ▼
      integrators  camera lights  materials   samplers
           │         │     │          │          │
           └─────────┴──┬──┴──────────┘          │
                        ▼                        │
                    geometry  ◄──────────────────┘
                        │
                        ▼
                      core
```

---

## Modules

### `core`
Foundation layer providing math primitives (`Vec3`, `Mat4`, `Ray`, `AABB`), spectrum representation, random number generators, and common utilities. Every other module depends on `core`.

### `geometry`
Defines geometric primitives (`Sphere`, `Triangle`, `Mesh`) and acceleration structures (`BVH`). Responsible for ray–scene intersection queries.

### `materials`
Implements physically-based shading models:
- **Lambertian** – ideal diffuse reflection
- **Disney Principled BRDF** – metallic/roughness workflow
- **Glass** – specular transmission with Fresnel
- **Metal** – conductor reflection with microfacet GGX

Each material implements a common `Material` interface with `evaluate()`, `sample()`, and `pdf()` methods.

### `lights`
Light source abstractions including point lights, directional lights, area lights (mesh emitters), and HDR environment maps with importance-sampled lookup.

### `camera`
Camera models that generate primary rays:
- **Pinhole** – standard perspective projection
- **ThinLens** – depth-of-field with configurable aperture and focus distance
- **Orthographic** – parallel projection for technical rendering

### `integrators`
Light transport algorithms that compute pixel radiance:
- **PathTracer** – unbiased Monte Carlo path tracing with Russian roulette
- **DirectLighting** – single-bounce direct illumination (fast preview)
- **AmbientOcclusion** – geometry-only visibility estimator

### `samplers`
Quasi-random and stratified sampling strategies for variance reduction:
- **Stratified** – jittered grid samples
- **Halton** – low-discrepancy sequence
- **Sobol** – scrambled Sobol' quasi-random sequence

### `io`
File I/O for images and scenes:
- **Image** – PNG read/write (via stb), EXR read/write (via tinyexr)
- **Scene** – OBJ loading (via tinyobjloader), glTF loading (planned)

### `engine`
Top-level orchestration layer:
- **RenderScheduler** – divides the image into tiles and dispatches work across threads
- **ToneMapper** – converts HDR radiance to LDR output (ACES, Reinhard, filmic)
- **Denoiser** – optional OIDN integration for noise-free output at low sample counts

### `gpu` *(optional)*
GPU compute backend for accelerated ray tracing. Planned targets:
- NVIDIA OptiX (RTX hardware)
- Vulkan Ray Tracing

---

## Data Flow

```
Scene File (JSON/OBJ)
        │
        ▼
    io::SceneLoader  →  Scene { geometries, materials, lights, camera }
        │
        ▼
    engine::RenderScheduler
        │
        ├── for each tile:
        │       sampler  →  generate samples
        │       camera   →  generate primary ray
        │       integrator  →  trace ray, evaluate materials/lights
        │       accumulate radiance
        │
        ▼
    engine::ToneMapper  →  LDR image
        │
        ▼
    io::ImageWriter  →  output.png / output.exr
```

---

## Build Targets

| Target            | Type       | Description                        |
|-------------------|------------|------------------------------------|
| `photon_core`     | STATIC_LIB | Math, ray, spectrum, RNG           |
| `photon_geometry` | STATIC_LIB | Shapes, BVH                        |
| `photon_materials`| STATIC_LIB | BRDF models                        |
| `photon_lights`   | STATIC_LIB | Light sources                      |
| `photon_camera`   | STATIC_LIB | Camera models                      |
| `photon_integrators` | STATIC_LIB | Path tracer, direct lighting    |
| `photon_samplers` | STATIC_LIB | Random samplers                    |
| `photon_io`       | STATIC_LIB | Image & scene I/O                  |
| `photon_engine`   | STATIC_LIB | Scheduler, tone mapping, denoise   |
| `photon_gpu`      | STATIC_LIB | GPU backend *(optional)*           |
| `photon_render`   | EXECUTABLE | CLI renderer                       |
