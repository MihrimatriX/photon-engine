# Üçüncü taraf bağımlılıklar

Bu dosyada satırı olmayan bağımlılık eklenmez. GPL lisanslı bağımlılık yasaktır; LGPL yalnız dinamik bağlanırsa kabul edilir.

vcpkg paketleri `vcpkg.json` (builtin-baseline `eb2d3a3279fd019cb7733072d86900d0ad2a1aef`) sürümlerini izler. FetchContent sabitlemeleri `CMakeLists.txt`'deki `GIT_TAG` / `URL` değerleridir. glfw, imgui, imguizmo ve nlohmann-json için vcpkg bulunamazsa FetchContent yedekleri derlenir; `fetch-*` preset'leri her zaman yedekleri kullanır.

| Ad | Sürüm / sabitleme | Lisans | Bağlantı biçimi |
| --- | --- | --- | --- |
| stb (stb_image, stb_image_write) | `2c980bb5…` | MIT ya da Unlicense | FetchContent, başlık (`photon_core`) |
| tinyobjloader | `v2.0.0rc13` | MIT (earcut: ISC) | FetchContent, başlık (`photon_io`) |
| tinyexr | `v1.0.8` | BSD-3-Clause (miniz derlenmez; stb zlib kullanılır) | FetchContent, başlık (`photon_core`) |
| cgltf | `v1.13` | MIT | FetchContent, başlık (`photon_io`) |
| nlohmann/json | vcpkg 3.12.0 (yedek: 3.11.3) | MIT | başlık (`photon_scene`, uygulama) |
| googletest | `v1.14.0` | BSD-3-Clause | statik, yalnız testler |
| glfw3 | vcpkg 3.5.1 (yedek: 3.4) | Zlib | dinamik (`glfw3.dll`) |
| Dear ImGui (docking) | vcpkg 1.92.9 | MIT | statik |
| ImGuizmo | vcpkg 1.10 (yedek: 1.91.3) | MIT | statik |
| Intel Embree | vcpkg 4.4.x (`geometry-triangle`, `geometry-user`; TBB'siz) | Apache-2.0 | dinamik (`embree4.dll`) |
| Intel Open Image Denoise | 2.5.1, resmi Windows paketi (`scripts/fetch_deps.ps1` → `third_party/oidn`) | Apache-2.0 (oneTBB: Apache-2.0) | dinamik (`OpenImageDenoise*.dll`, `tbb12.dll`) |

## Varlıklar (assets/)

| Varlık | Kaynak | Lisans |
| --- | --- | --- |
| Inter (Regular, Medium, SemiBold) | rsms/inter v4.1 | SIL Open Font License 1.1 (`assets/fonts/Inter-LICENSE.txt`) |
| Lucide ikon fontu | lucide-static 1.52.0 | ISC (`assets/fonts/Lucide-LICENSE.txt`) |
| HDRI'lar: studio_small_09, brown_photostudio_02, photo_studio_01, kloppenheim_06, sunflowers_puresky (1k) | Poly Haven | CC0 |
| Dokular: wood_table_001, leather_red_03, fabric_pattern_07, metal_plate, rubber_tiles (1k; renk, normal GL, pürüzlülük) | Poly Haven | CC0 |
| Örnek modeller (`assets/models`) | `scripts/gen_sample_models.py` ile depoda üretildi | CC0 |

Kaldırılan: glm (2026-10-02, hiçbir dosya kullanmıyordu); GL raster önizleme modülü (`src/preview`, 2026-10-06, yerini çözünürlük merdivenli path-traced viewport aldı).
