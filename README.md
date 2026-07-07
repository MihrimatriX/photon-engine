# 🔥 PhotonEngine

**Physically-based render engine with CPU/GPU path tracing, inspired by KeyShot.**

PhotonEngine is a modern C++20 rendering engine built for photorealistic image synthesis. It implements physically-based light transport algorithms with a focus on material accuracy, performance, and extensibility.

---

## ✨ Features

- **Path Tracing** with global illumination (unbiased Monte Carlo)
- **Disney Principled BRDF** for versatile material authoring
- **HDR Environment Lighting** with importance sampling
- **BVH Acceleration** structure for fast ray–scene intersection
- **Multi-threaded Tile-based Rendering** with work-stealing scheduler
- **Denoising** via Intel Open Image Denoise (OIDN) integration
- **Multiple Camera Models** – pinhole, thin-lens (DoF), orthographic
- **Scene Import** – OBJ and glTF file formats
- **Desktop Editor** – GLFW + ImGui with drag-and-drop workflow
- **Web Version** *(planned)* – WebAssembly + WebGPU target

---

## 🛠️ Build Instructions

### Prerequisites

- **CMake** ≥ 3.20
- **C++20** compatible compiler (MSVC 2022, GCC 12+, Clang 15+)
- **vcpkg** *(optional, for glm/glfw3/imgui)*

### Build

```bash
# Clone the repository
git clone https://github.com/your-org/PhotonEngine.git
cd PhotonEngine

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release -j

# Run tests (optional)
cd build && ctest --output-on-failure
```

### CMake Options

| Option                | Default | Description                          |
|-----------------------|---------|--------------------------------------|
| `PHOTON_BUILD_APP`    | `ON`    | Build `photon_app` desktop editor    |
| `PHOTON_BUILD_CLI`    | `OFF`   | Build `photon_render` CLI *(dev only)* |
| `PHOTON_BUILD_TESTS`  | `ON`    | Build unit tests (Google Test)       |
| `PHOTON_BUILD_GPU`    | `OFF`   | Build GPU backend (OptiX/Vulkan)     |
| `PHOTON_ENABLE_SIMD`  | `ON`    | Enable AVX2/FMA SIMD instructions    |
| `PHOTON_ENABLE_OIDN`  | `OFF`   | Enable Intel Open Image Denoise      |

---

## 🚀 Usage

Launch the desktop editor:

```bash
./build/photon_app
# Windows: build\photon_app.exe
```

**Workflow:**

1. Drag an OBJ or glTF model into the viewport
2. Assign materials from the library (left panel)
3. Adjust camera and render settings in the inspector / Render panel
4. Use **Render → Tam Cozunurluk** for full-resolution export
5. Export PNG or EXR via **Dosya → Disa Aktar**

> **Note:** The CLI renderer (`photon_render`) is optional and disabled by default. Enable with `-DPHOTON_BUILD_CLI=ON` for development only.

---

## 🏗️ Architecture

PhotonEngine is organized into focused, decoupled modules:

```
src/
├── core/          Math primitives, ray, spectrum, RNG
├── geometry/      Shapes, meshes, BVH acceleration
├── materials/     BRDF models (Lambert, Disney, Glass, Metal)
├── lights/        Point, area, directional, HDR environment
├── camera/        Pinhole, thin-lens, orthographic cameras
├── integrators/   Path tracer, direct lighting, ambient occlusion
├── samplers/      Stratified, Halton, Sobol quasi-random samplers
├── io/            Image I/O (PNG, EXR), scene loaders (OBJ, glTF)
├── engine/        Render scheduler, tile manager, tone mapping
├── app/           photon_app entry point and UI application
├── ui/            ImGui theme, orbit camera, file dialogs
├── scene/         Scene graph, material library, project I/O
├── preview/       OpenGL viewport preview
├── gpu/           GPU compute backend (optional)
└── main.cpp       CLI entry point (dev only, PHOTON_BUILD_CLI)
```

See [`docs/architecture.md`](docs/architecture.md) for a detailed breakdown.

---

<p align="center">
  Built with ❤️ and physics.
</p>
