# PhotonEngine memory bank — bağlam

Bu klasör projenin çalışma hafızası. Yeni bir oturumda sırasıyla okunur:
1. Bu dosya: hedef, kararlar, makine, kaldığımız yer.
2. `01-gorevler.md`: bütün fazların görev listesi. Bitenler `[x]` ile işaretlenir.
3. `02-bulgular.md`: 2026-10-02 taramasının doğrulanmış bulguları. Görevler buradaki kimliklere atıf yapar, ör. `[engine-1]`.

Her çalışma oturumunun sonunda iki şey güncellenir: aşağıdaki "Kaldığımız yer" bölümü ve görev kutuları.

## Hedef

1. **Ürün:** kullanımı kolay, son derece hızlı ve fiziksel olarak gerçekçi bir render motoru, şık bir arayüzle.
2. **Öğrenme:** bu proje üzerinden bilgisayar grafiği ve CGI pipeline'ının tamamını öğrenmek.

## Kararlar (2026-10-02)

- **Seviye:** grafikte yeni, matematik tarafı sağlam.
- **Yaklaşım:** her parçada en kaliteli sonucu veren yol seçilir. Kritik parçaların önce yalın, kendi sürümü yazılır (öğrenme ve test referansı için); üretimde en iyi kütüphane kullanılır (Embree 4, OIDN 2).
- **Sıra:** önce doğruluk ve kararlılık, sonra ölçüm, sonra hız. Yanlış görüntüyü hızlandırmak anlamsız.
- **Ticari hedef belirsiz, kapı açık:**
  - GPL bağımlılık yok, LGPL yalnız dinamik bağlanır.
  - Her yeni bağımlılık `docs/THIRD_PARTY.md`'ye yazılır.
  - Ticari maddeler `01-gorevler.md`'deki "Ürünleşme (karar bekliyor)" ekinde durur.
- **GPU:** GTX 1080 üzerinde CUDA 12.x ve TinyBVH CWBVH. Çekirdekler `PHOTON_HD` ile CPU ile paylaşılır. OptiX yalnız RTX kart gelince. Vulkan/DXR ikinci bir shading kod tabanı demek; yalnız seçmeli öğrenme egzersizi.
- **Hafif süreç:** her görev için önce kırmızı sonra yeşil bir test. Görüntü değişiyorsa bench sayısı ya da önce/sonra ekran görüntüsü eklenir. ADR, CHANGELOG ve Linux CI yalnız ürünleşme kararıyla gelir.
- **Hız rakamları tahmin.** Plandaki tüm "N× hızlanma" değerleri ölçülmedi. Faz 2'deki gerçek Mrays/s ölçümünden sonra Faz 3–7'nin sırası yeniden değerlendirilir.

## Öğrenme yöntemi (her görev için)

1. İlgili bölümü oku (bkz. "Kaynaklar").
2. Motorda kodu bul ve neden yanlış ya da eksik olduğunu gör (`02-bulgular.md`).
3. Kırmızı testi yaz.
4. Kilit 2–10 satırı **sen** yazarsın. Görevlerde **[SEN]** ile işaretli; kodda `TODO(human)` olarak bırakılır. Gerisini Claude yazar.
5. Test yeşile döner, ölçülür, bir paragraf not alınır.

## Makine ve araçlar

- **Donanım:** i7-6700K (4 çekirdek, 8 iş parçacığı, AVX2, AVX-512 yok), 32 GB RAM.
- **GPU:** GTX 1080 (Pascal sm_61, 8 GB), sürücü 582.66. R580 Pascal'ın son dalı; güvenlik güncellemeleri 2028-10'a kadar.
  - CUDA 13 sm_61 desteğini kaldırdı; **CUDA 12.x** kullanılacak.
  - OptiX 9.1 Turing ve R590+ istiyor; bu kartta çalışmaz.
  - Vulkan'da `VK_KHR_ray_tracing_pipeline` var, `ray_query` yok.
  - OIDN Pascal'da GPU modunda çalışmaz, yalnız CPU'da.
- **Derleyici:** MSVC (VS 18 Community). `cmake` yalnız VS geliştirici kabuğunda bulunur: `. .\scripts\devshell.ps1`.
- **vcpkg:**
  - `VCPKG_ROOT=C:\vcpkg` (kullanıcı ortam değişkeni) ama **bu klasör yok**.
  - VS 18'in kendi vcpkg'si var: `C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg`, sürüm 2026-07-27, `usegitregistry: true`. Bu sayede `builtin-baseline` git kaydından çözülebilir.
  - `build-vcpkg/` ve `build/release/` önbellekleri `C:/vcpkg` toolchain'ine bağlı; yeniden yapılandırmada kırılır.
- **vcpkg port durumu:**
  - OIDN yok (`ports/openimagedenoise` ve `ports/oidn` 404); resmi derlenmiş paketten alınacak.
  - Embree 4.4.1 var (`embree`). Varsayılan özelliklerinde `tasking-tbb` açık.
  - Diğerleri (nlohmann-json, tracy, imguizmo, nativefiledialog-extended) uygulama sırasında doğrulanacak.

## Kaldığımız yer

- **2026-10-02:**
  - 22 ajanlı tarama bitti ve plan onaylandı. Bu memory bank yazıldı.
  - **Henüz hiçbir kod değişikliği yapılmadı.**
- **2026-10-02 (oturum 2, bulut):**
  - Faz 0 bitti ve push edildi (`4a97845`). F0.2/F0.3 Windows'ta doğrulanacak; MSVC `/WX` varsayılanı kapalı (F0.9).
  - Faz 1b **yarım (WIP commit, derlenmiyor)**. Yapılanlar: `core/math/float_bits.h`, `core/color/transfer.{h,cpp}` (sRGB; `srgbEncode` TODO(human), şimdilik gamma 2.2), `core/image/film.{h,cpp}` (toplam+sayı, NaN/negatif reddi), `core/platform/path.h` (UTF-8 yollar), `Image` artık yalnız düz RGB, `image_io` (ortalama + dither'lı PNG, half+ZIP EXR, `TextureEncoding` sRGB/Linear, LDR/HDR biçim kontrolü, STBI UTF-8), tone mapping (EV varsayılanı 0, sRGB encode), denoiser ve renderer `Film`'e geçti (y=0 üstte, `renderProgressive` iptal edilebilir ve `Image` döner), OBJ'de V çevrildi, Disney veri haritaları doğrusal, viewport_texture sadeleşti.
  - **Kalan (derlemeyi tamamlamak için):** `application.{h,cpp}`: `accumImage` → `Film accumFilm` + `Image displayImage` + `displayDirty`; render thread `accumFilm.resize`; UI upload'ta kilit altında `resolve()`, denoise ve upload kilit dışında; export `displayImage`'ı yazar; tam render `renderProgressive` dönüş değerini kaydeder; ton eşleme/pozlama `markDirty` yerine `displayDirty`; CPU dokusu UV (0,0)-(1,1), GL önizleme (0,1)-(1,0); `gpuTexture` silinir. `tests/test_product.cpp` Denoise testleri `Image`/`Film`'e göre güncellenir. Ardından yeni testler: `test_export_roundtrip`, `test_texture_colorspace`; `tests/learning/test_srgb_oetf.cpp` ayrı `photon_learning_tests` hedefinde, ctest etiketi `todo_human`.
  - Sonra Faz 1c (cam: `refractVec` gövdesi TODO(human) olarak kalır, kırmızı `test_snell` learning hedefine), 1d ve 1a.
- **2026-10-06 (oturum 3, "KeyShot'un yerini alacak" büyük geçiş, dal `keyshot-overhaul`):**
  - Windows/MSVC derlemesi ilk kez yeşil: VS 18'in kendi vcpkg'si (`VCPKG_ROOT` oturumda ona çevrilir), testler geçiyor.
  - **Fizik (paralel ajan, birleştirildi):** cam kırılması ters değil, eta² ölçekleme, PBRT-v4 pürüzlü cam + VNDF, kararlı GGX, F0 = 0.08·specular, enerji korunan clearcoat, iletimde |cos| NEE, son köşe, MIS'te ışık seçim olasılığı, max-bileşen Rus ruleti, NaN koruması, Owen-Sobol örnekleyici (üretimde), kesin sRGB OETF, küçük üçgen eşiği, küre teğeti. `refractVec` TODO(human)'ı ve `srgbEncode` TODO(human)'ı kapandı.
  - **Motor:** iptal edilebilir karolar, kayıp-uyanma yarışı kapandı, Scene malzemeleri sahiplenir, BVH tek sefer, ortam ışığı `shared_ptr<const Image>` + döndürme/parlaklık, arka plan (ortam/renk/şeffaf), AOV'ler örnek başına (ek ışın yok, `PathTracer::Li(..., PrimaryHit*)`), kalıcı OIDN cihazı (2.5.1, `third_party/oidn`), AgX + Khronos PBR Nötr, PNG alfa/JPEG çıktısı, `Material::isDelta/evalPdf`, Embree 4 (`engine/embree_accel.*`, kendi BVH yedek), yalın üçgen testli BVH gezinmesi.
  - **Belge katmanı (scene/):** JSON malzeme kütüphanesi (42 preset, dokulu olanlar dahil, kutu eşleme + tekrar), `LightDesc`/`EnvironmentDesc`, kameraya göreli stüdyo preset'leri (6), model içe aktarma, proje v2 (her şey kaydedilir, göreli yollar, atomik yazım), anlık-görüntü geri al, `buildRenderScene`.
  - **Uygulama yeniden yazıldı (src/app, 13 dosya):** UI'ı bekletmeyen `RenderController` (çözünürlük merdiveni + etkileşimde OIDN), motorla render edilen küçük resimler (disk önbelleği), ImGuizmo, yön küpü, imlece doğru zoom, çift tık pivot, seçim/üzerine gelme kutuları, ışık şekilleri, kayıtlı kameralar, malzeme kopyala/yapıştır, asenkron içe aktarma/HDRI, son render penceresi (önizleme, kalan süre, durdur ve kaydet), turntable, `--screenshot` modu. Inter + Lucide, Türkçe arayüz, WIN32 alt sistemi, kullanıcı klasörü `%APPDATA%/PhotonEngine`.
  - **CLI:** `photon_render` proje/model render eder, `--stats` yazar. `bench/scenes/showcase.photon`.
  - **Yorumlar:** her kaynak dosyada Türkçe başlık; özel mantığa açıklamalar. `docs/file-reference.md` başlıklardan üretilir (`scripts/gen_file_reference.sh`).
  - **Ölçüm (i7-6700K, 960×540, 8 sekme, Embree öncesi):** 2.3 M örnek/sn; tek thread profil: kesişim %44, malzeme değerlendirme %27.
- **Sıradaki adımlar:**
  - Embree sonrası ölçüm ve `bench/BASELINE.md` (F2.13).
  - F2.6/F2.7 furnace + chi-kare testleri; F2.8 golden görüntüler.
  - F3.8 malzeme ID tablosu / geometri önbelleği: büyük modelde ışık-ortam düzenlemesi tüm sahneyi yeniden bake ediyor. Faz 6 TLAS/BLAS (Embree instancing) ile birlikte.
  - Kutu eşleme dünya uzayında (nesne taşınınca doku kayar) → nesne uzayı.
  - TODO(human) öğrenme maddeleri bu oturumda kullanıcı yokken Claude tarafından yazıldı (kullanıcı isteği: "tüm işleri sen yap"). İstenirse ilgili fonksiyonlar egzersiz olarak yeniden boşaltılabilir.

## Kaynaklar (CG/CGI pipeline)

| Aşama | Kaynak | |
|---|---|---|
| ışın izleme, örnekleme, BSDF, ışık taşıma, hızlandırma, GPU | [PBRT 4. baskı](https://pbr-book.org/4ed/contents) | ücretsiz |
| ışın izleme, Monte Carlo | [Ray Tracing in One Weekend serisi](https://raytracing.github.io/) | ücretsiz |
| hızlandırma yapıları | [How to Build a BVH (Jacco Bikker, 9 bölüm) + TinyBVH](https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/) | ücretsiz |
| matematik, rasterizasyon, geometri, animasyon | [CMU 15-462/662 (Keenan Crane)](http://15462.courses.cs.cmu.edu/) | ücretsiz |
| matematik, rasterizasyon, ışın izleme | [Scratchapixel](https://www.scratchapixel.com/) | ücretsiz |
| rasterizasyon, gerçek zamanlı | [LearnOpenGL](https://learnopengl.com/) | ücretsiz |
| ışık taşıma, MIS | [Veach tezi (1997)](https://graphics.stanford.edu/papers/veach_thesis/) | ücretsiz |
| BSDF, malzemeler | [SIGGRAPH Physically Based Shading notları](https://blog.selfshadow.com/publications/) | ücretsiz |
| GGX örnekleme | [Heitz 2018, VNDF](https://jcgt.org/published/0007/04/01/) | ücretsiz |
| QMC örnekleme | [Burley 2020, hash tabanlı Owen karıştırma](https://jcgt.org/published/0009/04/01/) | ücretsiz |
| sağlam ofset, ışın konisi, terminatör | [Ray Tracing Gems I ve II](https://www.realtimerendering.com/raytracinggems/) | ücretsiz |
| gerçek zamanlı | [Real-Time Rendering, 4. baskı](https://www.realtimerendering.com/) | ücretli |
| temel kitap | [Fundamentals of Computer Graphics, 5. baskı](https://www.routledge.com/Fundamentals-of-Computer-Graphics/Marschner-Shirley/p/book/9780367505035) | ücretli |
| GPU, shader, ışın izleme dersleri | [Cem Yüksel dersleri](https://www.youtube.com/@cmyuksel) | ücretsiz |
| render dersi | [TU Wien Rendering (Károly Zsolnai-Fehér)](https://users.cg.tuwien.ac.at/zsolnai/gfx/rendering-course/) | ücretsiz |
| renk bilimi | [Cinematic Color + Blender AgX belgeleri](https://cinematiccolor.org/) | ücretsiz |
| renk bilimi | [Khronos PBR Neutral](https://github.com/KhronosGroup/ToneMapping) | ücretsiz |
| gürültü giderme | [OIDN belgeleri](https://www.openimagedenoise.org/documentation.html) | ücretsiz |
| malzeme, look-dev | [OpenPBR Surface](https://academysoftwarefoundation.github.io/OpenPBR/) | ücretsiz |
| referans render | [Mitsuba 3](https://mitsuba.readthedocs.io/) | ücretsiz |
| GPU wavefront | [Laine, Karras, Aila 2013](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus) | ücretsiz |
| GPU BVH | [Ylitie, Karras, Laine 2017, CWBVH](https://research.nvidia.com/publication/2017-07_efficient-incoherent-ray-traversal-gpus-through-compressed-wide-bvhs) | ücretsiz |
| CUDA | [Accelerated Ray Tracing in One Weekend in CUDA](https://developer.nvidia.com/blog/accelerated-ray-tracing-cuda/) | ücretsiz |
| Vulkan (seçmeli) | [Vulkan Tutorial + nvpro RT tutorial](https://vulkan-tutorial.com/) | ücretsiz |
| geometri işleme | [Discrete Differential Geometry (Keenan Crane)](https://www.cs.cmu.edu/~kmcrane/Projects/DDG/) | ücretsiz |
| UV, parametrizasyon | [libigl tutorial](https://libigl.github.io/tutorial/) (+ xatlas) | ücretsiz |
| üretim pipeline'ı, USD | [OpenUSD belgeleri](https://openusd.org/release/index.html) | ücretsiz |
| compositing | [Natron kılavuzu](https://natron.readthedocs.io/) | ücretsiz |
| animasyon | [The Orange Duck (Daniel Holden)](https://theorangeduck.com/) | ücretsiz |
| stüdyo pipeline'ı | [VFX Reference Platform](https://vfxplatform.com/) | ücretsiz |

Kaynak bağlantılarının çoğu 2026-10-02'de yeniden doğrulanmadı; ilk kullanımda kontrol et.
