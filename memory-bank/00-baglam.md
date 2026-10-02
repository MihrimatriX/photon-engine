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
- **Sıradaki adımlar:**
  - `F0.1` baz commit. Onay gerekiyor: 13 değişmiş ve 6 izlenmeyen dosya var.
  - `F0.2` vcpkg seçimi: VS'in kendi vcpkg'si mi, yoksa `C:\vcpkg`'ye yeniden kurulum mu.
  - `F0.4`: tek doğruluk kaynağı ne olacak, bu klasör mü yoksa `docs/ROADMAP.md` mi.
- **Açık karar:** `memory-bank/` git'e commit edilecek mi? Öneri: evet. Yalnız geliştirici notu; ürünle dağıtılmaz.

## Mimari özet (bugünkü hal)

- **Modül zinciri:**
  - `photon_core` ← `geometry` ← `materials`/`lights` ← `integrators` ← `engine` (Scene, Renderer, denoiser) ← `scene` (SceneGraph, MaterialLibrary, project_io) ← `preview`/`ui` ← `app`.
  - Döngü var: `path_tracer.cpp` dosyası `engine/scene.h`'i içeriyor ama o modüle bağlanmıyor.
- **İki sahne temsili:**
  - Düzenlenebilir `SceneGraph`.
  - Düz render `Scene`: dünya uzayına bake edilmiş kopyalar ve tek global BVH.
  - Tek köprü `SceneGraph::compile`. Ama köprü bir anlık görüntü üretmiyor: malzemeler, HDRI ve dokular ham işaretçiyle ödünç alınıyor.
- **Thread'ler:**
  - UI thread'i.
  - Önizleme kontrol thread'i.
  - Global havuz (`hardware_concurrency` işçi).
  - Tam render ya da turntable thread'i.
- **Kilit:** tek kilit `imageMutex` var ve bir pass boyunca tutuluyor; bu yüzden UI pass hızında (~4 fps) çalışıyor. Ayrıntılı veri akışı `02-bulgular.md`'nin sonunda.
- **Sağlam olanlar (üzerine inşa edilir):**
  - Duff 2017 ONB, kofaktör ters matris, ters-transpoz normal dönüşümü, PCG32.
  - Örnekleme warp'ları ve pdf'leri, kesin dielektrik Fresnel.
  - Ortam ışığı CDF'si ve sinθ Jakobiyeni.
  - MIS iskeleti, binned SAH BVH, thin lens.
  - `ScriptedSampler` test düzeneği (`tests/test_area_light.cpp:20-43`).

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
