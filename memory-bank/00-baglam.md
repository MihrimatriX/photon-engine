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
  - Faz 0 bitti ve push edildi. F0.2/F0.3 Windows'ta doğrulanacak; MSVC `/WX` varsayılanı kapalı (F0.9).
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
- **2026-10-06 (oturum 4, paketleme):**
  - `scripts/package.ps1` → `dist/PhotonEngine-0.1.0-win64.zip` (74 MB, taşınabilir; VC++ CRT uygulama yanında, lisans metinleri `lisanslar/`). Kullanıcı kılavuzu `packaging/KULLANIM.txt`.
  - Paket temiz klasörde denendi: CLI proje/model renderı ve uygulama (`--ui material|object|render|about`) çalışıyor.
  - Düzeltmeler: CLI model modu artık uygulamayla aynı "Ürün Stüdyosu" preset'ini + HDRI'ı kullanıyor ve modeli zemine oturtuyor (önce aşırı pozlanıyordu); `findAssetsRoot/executableDir` `core/platform/path.cpp`'ye taşındı; içe aktarılan model seçili kalıyor (applyStudio seçimi siliyordu); Hakkında'da sürüm (`PHOTON_VERSION`).
  - Not: `--screenshot` çekimleri sırasında pencere odak alır; kullanıcı o an tıklar/yazarsa görüntü bozulur (yanlış alarm olarak hata sanıldı).
- **2026-10-06 (oturum 5, sahne ve kontroller):**
  - `SceneGraph`: `reparent` / `move` / `group` / `ungroup` / `topmost` / `detachFromSource` (dünya konumu korunur, döngü reddedilir); `tests/test_scene_edit.cpp` (7 test).
  - **Veri kaybı düzeltildi:** içe aktarılmış modelin parçası aynı grupta çoğaltılınca kopya projede geometrisiz kaydediliyor, açılışta kayboluyordu. Kopyalanan/taşınan parçaların kaynak bağı koparılır, geometri gömülür.
  - Çoklu seçim (`selNodes` + birincil `selUid`), Ctrl/Shift+tık, Ctrl+A, ↑/↓; sahne panelinde sürükle-bırak, arama (Türkçe harf katlama), F2/çift tık ad (başka satıra tıklayınca kaydeder), ışık sağ tık menüsü, gizle/izole/hepsini göster (H/I/Alt+H), grupla/çöz (Ctrl+G/Ctrl+Shift+G).
  - Tutamaç: pivot seçimin ortası, çoklu nesne, ışık taşıma, adımlı hareket (mıknatıs / Ctrl), yerel/dünya eksen; geri al adımı ilk değişiklikte (önceden ilk kare değişmezse kaçıyordu).
  - Malzeme: "Bağımsız yap" (paylaşılan malzemeyi ayır). Nesne sekmesi çoklu seçimde toplu işlemler.
  - `--ui multi|group|filter|lightgizmo` ekran görüntüsü durumları.
- **2026-10-06 (oturum 6, şekiller, görüntü modları, kompakt arayüz):**
  - `scene/primitives.*`: küp, küre, silindir, koni, düzlem, simit (1 birimlik kutu, taban y=0, dışa bakan sarma; `test_primitives`). Kütüphane → Model'de sürüklenebilir kutucuklar, Ekle menüsü, sahne panelindeki "+". Boyut sahnedeki nesnelerin ortanca boyu (toplam boyuna bağlıyken her şekil bir öncekinden büyük geliyordu).
  - Görüntü modları (`ViewMode`, Z): Render, Kil (`buildRenderScene(..., clay)` → tek mat gri malzeme, yalnız viewport), Katı / Tel kafes / Normaller (`app/raster_view.*`: OpenGL 3.3, 3.x işlevleri glfwGetProcAddress ile, 4× MSAA FBO, hidden-line tel kafes, zemin ızgarası). GPU modlarında ışın izleyici duraklar. "Tel kafes bindir" Render/Kil üstüne.
  - Kompakt tema: 14 px yazı, dar dolgular; malzeme ızgarası 4 sütun; araç çubuğu kutusu düğmelerin gerçek boyutundan (ImDrawList kanalları).
- **2026-10-24 (oturum 7, bulut, "KeyShot/Octane seviyesi" geçişi, dal `ccr-7dde8115-rf4vin`, sürüm 0.2.0):**
  - Linux: vcpkg'siz FetchContent yolu artık `photon_app`'i de derliyor (ImGuizmo commit sabitlemesi + `src/` düzeni, imgui_stdlib, `<cstdint>`); Xvfb altında `--screenshot` ile arayüz doğrulandı.
  - **Hacimler:** `Medium`, ortam yığını, Beer–Lambert, spektral MIS serbest uçuş, HG faz. Cam: soğurma rengi/mesafesi, bulanıklık.
  - **SSS:** Disney `subsurface` (Blender 4 anlamı), Chiang 2016 ters çevirmesi, rastgele yürüyüş, beyaz Lambert çıkış köşesi.
  - **Dispersiyon:** `core/color/spectral.*` (CIE → sRGB, beyaza normalize, önem örneklemeli λ), Cauchy/Abbe.
  - **Metaller:** F82 kenar tonu, Turquin çoklu saçılma telafisi (E tablosu), ölçülmüş n,k'den preset'ler. **İnce film** (Airy, Gulbrandsen tabanı), **pullar** (Voronoi).
  - **Işıklar:** küresel dikdörtgen örnekleme, noktaya özel ışık seçimi (`LightPicker`), O(1) yayıcı pdf, kameraya görünmez alan ışığı (yeni ışıklarda varsayılan), spot + IES (`assets/ies`, prosedürel).
  - **Ortam:** Nishita fiziksel gök + örneklenen güneş konisi, NOAA konum/saat; **gölge yakalayıcı** zemin.
  - **Görüntü:** görüntüleyici (bloom, glare, beyaz dengesi, kontrast, doygunluk, vinyet, renk sapması), çokgen bokeh, gürültü eşikli uyarlamalı örnekleme, dolaylı parlama sınırı; CLI `--noise/--clamp/--time`.
  - **Yuvarlatılmış kenarlar** (integratörde bevel normali) ve **ince duvarlı cam** (+ film; sabun köpüğü).
  - Arayüz: malzeme panelinde tüm yeni bölümler (cam paneli artık preset üzerinden kuruluyor: önceden IOR değişince gelişmiş alanlar kayboluyordu), Ortam'da HDRI/fiziksel gök, Işıklar'da spot/IES, Kamera'da diyafram kanatları, Görüntü'de görüntüleyici. Kütüphanede 5 fiziksel gök hazırı ve ~20 yeni malzeme.
  - Testler 56 → 133 (fırın testleri: saçan ortam, dispersiyonlu cam, ince duvar, beyaz pürüzlü metal; küresel dikdörtgen kareleme; güneş konumu; gölge yakalayıcı; görüntüleyici; uyarlamalı hata).
  - Kod incelemesi düzeltmeleri: keskin/ince duvarlı camın ağırlığı |cos θi|'ye bölünüyor (integratörün kosinüsü iki kez sayılıyordu); düz renk arka planda geçiş (pass-through) ıskası arka planı biriktiriyor; ışın başlangıç ofseti **geometrik** normal boyunca (yumuşak normalli + saçan camda fırın 0.88 → 1.0); Abbe ≥ 10, spektral IOR ≥ 1.0001; `visibleToCamera` eksik proje dosyalarında görünür.
  - Ölçüm: `bench/BASELINE.md` "Linux" bölümü; `scripts/compare_exr.py`; sahneler `scripts/gen_material_scenes.py`.
- **2026-10-29 (oturum 8, bulut, "dünyanın en iyi render uygulaması" + 3B uygulama entegrasyonu, dal `ccr-f83d8156-rqn1yj`):**
  - Eksik analizi: `docs/EKSIK_ANALIZI.md` (öncelikli liste, alternatifler, bu oturumun ölçümleri).
  - **Malzeme:** enerji korunan Principled katmanlama (Kulla–Conty; beyaz plastik fırında 1.58 → 1.00), EON difüz (`diffuseRoughness`), Charlie sheen (`sheenRoughness`), ORM kanal düzeltmesi; `test_bsdf_verification` (chi-kare + fırın).
  - **Görüntü:** piksel filtresi (filtre önem örneklemesi; eski projeler kutu).
  - **Sağlamlık:** pError'lı ışın başlangıcı, tMin = 0, gölge terminatörü (yalnız gölge ışınlarında); ölçek testi.
  - **Işık:** ışık BVH'si + üçgen başına yayıcı (LED şeritte 2.9×); HDRI MIS telafisi (güneşli gökte 6.8×).
  - **Geçişler:** lob × doğrudan/dolaylı + emisyon/ortam + veri geçişleri, çok katmanlı EXR (CLI `--passes`, Render penceresi).
  - **Entegrasyon:** FBX (ufbx v0.23.1) ve Alembic 1.8.12 (+ Imath 3.2.3, CMake ≥ 3.29) içe aktarma; hiyerarşi/ad/instancing, birim/eksen, malzeme eşleme, gömülü doku önbelleği; içe aktarma penceresi, imlecin altına bırakma, son dosyalar.
  - Testler 133 → 157.
- **Sıradaki adımlar:**
  - `docs/EKSIK_ANALIZI.md` öncelik 1: GPU (CUDA), path guiding (Open PGL), alfa kesikleri, doku belleği + mipmap, BLAS/TLAS, otomatik kayıt.
  - Windows/MSVC'de oturum 7 ve 8'in değişiklikleri derlenmedi (yalnız GCC 13). `/W4` uyarıları ve `std::complex` kullanımı kontrol edilmeli; OIDN ile yeni sahneler (SSS kanal gürültüsü) denenmeli.
  - Kostikler: cam arkası ve delta ışık + parlak yüzey yolları hâlâ gürültülü (parlama sınırı geçici çözüm). Path guiding (Open PGL) ya da MNEE.
  - Render geçişleri (AOV'ler, ışık grupları, çok katmanlı EXR, Cryptomatte); ışık bağlama; yansımada görünürlük bayrağı.
  - GPU (Faz 9).
  - Pullar ve kutu eşleme dünya uzayında (nesne hareket edince kayar).
  - GPU Katı modunda dokular ve ışıklar yok (yalnız taban rengi).
  - Tutamaç sürüklenirken Esc ile iptal (şimdilik Ctrl+Z).
  - Kurulum paketi (Inno Setup, makinede yok), kod imzalama, log + çökme dökümü, otomatik kaydetme (eksik analizi "Faz 1").
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
