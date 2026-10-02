# Third-party dependencies

No new dependency without a row in this file. GPL-licensed dependencies are forbidden. LGPL-licensed dependencies are allowed only when linked dynamically.

Versions are what this tree uses today. vcpkg packages follow `vcpkg.json` builtin-baseline `eb2d3a3279fd019cb7733072d86900d0ad2a1aef`. FetchContent pins are the `GIT_TAG` values in `CMakeLists.txt`. glfw and imgui come from vcpkg when `find_package` succeeds; otherwise the pinned FetchContent fallbacks (glfw tag `3.4`, imgui docking hash below) are built. The `fetch-*` CMake presets always use the fallbacks. Header-only FetchContent dependencies are fetched with `SOURCE_SUBDIR photon-headers-only`, so none of their own CMake targets (samples, tests, libraries) are configured.

| Name | Version / pin | License | Link |
| --- | --- | --- | --- |
| stb | `2c980bb59875b0d32144a71867fbdebb2f77cd20` (branch `master`, 2026-10-01). Headers in that commit: stb_image 2.30, stb_image_write 1.16. | MIT or Unlicense (public domain); either, from the header | FetchContent, header (compiled into `photon_core`) |
| tinyobjloader | `v2.0.0rc13` | MIT. Bundled `mapbox/earcut.hpp` in the same LICENSE is ISC. | FetchContent, header (compiled into `photon_io`) |
| tinyexr | `v1.0.8` | 3-clause BSD (`tinyexr.h` / README). Its bundled miniz (MIT) is not compiled: `TINYEXR_USE_MINIZ 0`, `TINYEXR_USE_STB_ZLIB 1` use stb's zlib instead. | FetchContent, header (compiled into `photon_core`) |
| cgltf | `v1.13` | MIT | FetchContent, header (compiled into `photon_io`) |
| googletest | `v1.14.0` | BSD-3-Clause | FetchContent, static (`gtest` / `gtest_main`; tests only) |
| glfw3 | 3.5.1 | Zlib (vcpkg port license field) | vcpkg, dynamic (`glfw3.dll`) |
| imgui | 1.92.9, features `docking-experimental`, `glfw-binding`, `opengl3-binding`. Fallback FetchContent pin `64944b4520b30772de8dbf0b37d0311746477a32` (branch `docking`, 2026-10-01). | MIT (vcpkg port license field) | vcpkg, static (`imgui.lib`) |

Removed: glm (2026-10-02). No source file included it.

Tools that ship nothing: `scripts/gen_sample_models.py` writes the sample meshes in `assets/models/` (CC0, generated in-repo).
