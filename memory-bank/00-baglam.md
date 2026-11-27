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
- **Hafif süreç:** her görev için önce kırmızı sonra yeşil bir test. Görüntü değişiyorsa bench sayısı ya da önce/sonra ekran görüntüsü eklenir. ADR ve Linux CI yalnız ürünleşme kararıyla gelir. Windows CI + otomatik sürüm 2026-10-30 kullanıcı isteğiyle geldi (sürüm notları `docs/releases/`).
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
  - **Cihaz/mod katmanı (F9.7, RTXPT incelemesinden sonra):** `RenderDevice` (CPU; GPU'lar CUDA sürücüsü dinamik yüklenerek listelenir, arka uç yok), `RenderSession` (Interactive / Final: parti boyu, hedef/süre/yakınsama, AOV/geçiş, denoise). Viewport, son render, turntable ve CLI tek yoldan; CLI `--device`, `--list-devices`; Render penceresinde Cihaz; `--ui finalrender` duman testi. RTXPT doğrudan alınmadı: NVIDIA RTX SDKs lisansı, Windows/D3D12, sürücü 595.71+ (GTX 1080'de çalışmaz). Testler → 165.
  - **Doğrulama:** Embree + OIDN derlemesi, ASan + UBSan + LSan temiz; Embree ile kendi BVH aynı görüntü (relMSE 3e-5).
  - **3ds Max / Maya / Blender:** `integrations/` (canlı bağlantı betikleri, ortak istemci + `sync_client.py`), `app/live_link.*` (127.0.0.1, 128-bit jeton, JSON satırı, yerinde güncelleme), `scene/dcc_convert.*` + `core/platform/process.*` (kabuksuz süreç, zaman aşımı), FBX kamera/ışık/kamera animasyonu (`ImportExtras`), `scene/camera_animation.*` + zaman çizelgesi, `.photonpack` (`core/io/zip`, `scene/mesh_binary`, `scene/package`).
  - **Kamera ve ışık araçları:** `placeLightForHighlight` (document), Kelvin (`temperatureColor`, `LightDesc.temperatureK`, proje `"temperature"`), lens kaydırma (`setShift`; GPU izdüşümünde de), iki noktalı perspektif, kompozisyon kılavuzları ve çıktı kadrajı.
  - **Gerçek zamanlı mod (Faz 8):** `scene/realtime_scene.*` (ortam pişirme, HDRI güneşi ayıklama, SH9, ışık listesi, gölge izdüşümü, ton LUT'u) + `app/raster_view.*` (`ViewMode::Realtime`, PBR shader, 2×2 PCSS gölge atlası, cam atlası, arka plan, cam iki geçiş). Ayrıntı `docs/architecture.md` §2c. Testler → 203.
- **2026-10-30 (oturum 8 devamı, sürüm 0.3.0):**
  - **Windows CI ve otomatik sürüm:** `.github/workflows/windows.yml` (windows-2025, MSVC, vcpkg'siz `fetch-release` + `fetch_deps.ps1`; her push'ta derle, test, `package.ps1`, paketlenmiş CLI ile duman testi, zip + SHA-256 artifact; `v*` etiketinde ayrı iş `gh release create`, notlar `docs/releases/<etiket>.md`, etiket = CMake sürümü denetimi). `package.ps1` hem vcpkg hem FetchContent derlemesiyle çalışır; lisansı bulunamayan bileşende durur; `integrations\` pakete girer. MSVC `/bigobj`.
  - Windows'a özel kod yolları MinGW-w64 ile sözdizimi denetiminden geçirildi (`-fsyntax-only -Wvla -Wpedantic`, 128 dosya temiz); MSVC'nin kendisi CI'da.
  - GitHub Pages (`docs/index.html`, main'den yayınlanıyor): 0.3 bölümü, gerçek zamanlı görünüm, İndir düğmeleri (`releases/latest`), güncel yol haritası sayıları.
- **2026-11-06 (oturum 9, yerel Windows, kesikler + veri güvenliği):**
  - Bu makinede ilk temiz kurulum: `fetch_deps.ps1` + `cmake --preset fetch-release` (vcpkg gerekmiyor, ~7,5 dk). Tüm testler yeşil, photon kodunda 0 MSVC uyarısı; bu yüzden `/WX` her yerde varsayılan açık (F0.9). tinyexr C4702'si `image_io.cpp`'de susturuldu. CI 2026-11-02'de zaten yeşildi, "Windows'ta derlenmedi" notu eskimişti.
  - **Alfa kesikleri:** `Material::opacity`, Disney'de çarpan + `opacityMap`. Mevcut ince duvarlı cam mekanizması kullanıldı: gölge ışını `shadowTransmittance` ile (kesik varsa yavaş yol), kamera/sekme ışını `passesCutout` ile stokastik geçiş; Embree filtresi gerekmedi, iki kesişim arka ucu aynı kodla çalışıyor. Maske: dosyanın alfası, yoksa gri (doğrusal). Proje/preset/paket kalıcılığı. glTF `alphaMode`/`alphaCutoff`, baseColor alfası. FBX: `opacity` + klasik `TransparentColor`/`TransparencyFactor` (eskiden malzemeyi cama çeviriyordu). Gerçek zamanlı: alpha hashing, gölgede 0.5 eşiği. Arayüz: Temel > Opaklık, Dokular > Opaklık.
  - **Veri güvenliği:** otomatik kayıt (`tickAutosave`, 120 sn, anlık görüntü + arka plan yazımı), çökme kurtarma sorusu, kaydedilmemiş işte çıkış/yeni/aç onayı (eskiden pencere kapanınca iş sessizce gidiyordu). `scripts/smoke_recovery.ps1` gerçek uygulamayla uçtan uca dener (GPU ister, CI'da yok).
  - Temizlik: kullanılmayan `quaternion.h` silindi (F1.26). F6.9, F7.1 kutuları gerçeğe getirildi.
  - **Dikkat:** GitHub'da etiket ve sürüm yok; sitenin İndir düğmeleri `releases/latest` adresine gidiyor. `v0.3.0` etiketi atılırsa CI paketi üretip yayımlar.
  - Bu makinede `gh` ve Python yok; GitHub sorguları `Invoke-RestMethod` ile.
- **2026-11-26 (oturum 10, bulut, v0.4.0 + v0.5.0 Stüdyo Prodüksiyon Sprinti, dal `feat/v0.5.0`):**
  - **M1 — Temeller, OpenPBR & Kompakt Mipmap:** Birleşik OpenPBR / glTF 2.0 (`ior`, `specularTransmission`, `coatIor`, `coatTint`, `sheenColor`), 8-bit kompakt mipmap dokular (~%83 bellek tasarrufu) ve Ray Cone LOD (`TextureMipMap`, RTG §20).
  - **M2 — BLAS/TLAS Instancing, Görünürlük, Işık/Gölge Bağlama & Motion Blur:** Embree 4 + CPU `SceneInstance` BLAS/TLAS instancing, nesne başına ışın görünürlük maskesi (`VisCamera|Diffuse|Glossy|Transmission|Shadow`), `LightLinkSet` ışık ve gölge bağlama, kamera + nesne doğrusal hareket bulanıklığı (`TransformTrack`, enstantane açısı).
  - **M3 — Prodüksiyon Geçişleri & Path Guiding:** Psyop Cryptomatte 1.2.0 (`CryptoObject`/`CryptoMaterial` + MurmurHash3-32 manifest), ışık grupları (`LG_*` EXR katmanları), Müller 2017 SD-Tree Path Guiding (`GuidingField`).
  - **M4 — GPU Engine v2:** Vulkan SDK derleme bağımlılığı olmadan dinamik yükleyicili (`vulkan_abi.h`) Vulkan 1.1 Compute SAH BVH2 + 4 aşamalı Wavefront + `VK_KHR_ray_query` Donanım RT shader'ları (önceden derlenmiş SPIR-V başlıkları repoda), eşzamanlı `HybridDevice` (GPU+CPU) ve artımlı `updateScene(Materials|Lights|Transforms)` + FNV-1a BLAS önbelleği.
  - **M5 — Gerçek Zamanlı OpenGL 3.3 Viewport v2:** Heitz 2016 64×64 LTC dikdörtgen alan ışıkları (`ltc_lut.*`), Spot IES 1D dokulu atlas, Jimenez 2016 GTAO + çift yanlı bulanıklaştırma, 5 seviyeli Hi-Z SSR, 5 seviyeli Jimenez/Karis HDR Bloom ve CPU ile birebir GPU Imager + 64³ ToneLut kompoziti.
  - **M6 — Stüdyo UX & v0.5.0:** `Ctrl+K` Türkçe bulanık aramalı Komut Paleti (40+ eylem), canlı viewport performans HUD'u (`Vulkan RT/WF/Compute` / `Hibrit` / `CPU`, M örnek/sn, VRAM, bayraklar), Özellikler ve Render panellerinde OpenPBR, ışın görünürlüğü, ışık/gölge bağlama, ışık grubu, Motion Blur, Path Guiding ve GPU modu denetimleri. Sürüm `0.5.0`.
- **2026-11-27 (oturum 11, bulut, yol haritası Adım 1 "kalite kanıtı", dal `claude/project-thread-zlqmmf`):**
  - Kullanıcı hedefi: KeyShot/Octane/V-Ray'i geçen, en yeni standartları destekleyen, kullanımı kolay motor; ticarileşme sonra. 4 adımlı plan onaylandı: 1) kalite kanıtı → 2) standartlar (OpenUSD, MaterialX/OpenPBR, OCIO/ACES 2, glTF, OpenVDB, Hydra) → 3) donanım yükseltmesi sonrası GPU (Vulkan RT ana yol, ReSTIR DI, GPU OIDN, OptiX isteğe bağlı) → 4) kullanım kolaylığı + CAD (STEP/IGES, 3DM).
  - `bench/reference/`: Mitsuba 3 ve Cycles ile aynı sahne karşılaştırması, `REPORT.md`. Photon yansız (Cornell difüz/kostik oran 0.9999/1.0000, gürültü tabanında); pürüzlü metal Mitsuba'dan %12 parlak (Kulla–Conty), Cycles çoklu saçılmalı GGX ile %0.3. Cycles'a göre eşit sürede 1.2–2.2× daha temiz.
  - Hızlandırma: Sobol örnekleyici bit bit aynı hızlı sürüm (profilde %28 → %11), Lambert, satır içi `getPixel`, yoğunluğu 0 ortam sahneye girmez → 4 iş parçacığında +%17–37.
  - F2.8 altın görüntüler Mitsuba referanslı. Linux için `scripts/fetch_deps.sh` (Embree + OIDN). Görev listesi M1–M6'ya göre işaretlendi; yeni maddeler F2.16–F2.18.
- **2026-11-27 (oturum 12, bulut, yol haritası Adım 2a "OpenUSD", aynı dal, ikinci PR):**
  - OpenUSD 26.11.1 kaynaktan, monolitik `usd_ms` (Python/Hydra/araçlar kapalı); `scripts/build_usd.ps1` (CI, önbellekli) ve `build_usd.sh`. USD, motorun bağlandığı TBB'nin başlıklarıyla derlenmeli (ABI): oneTBB 2023.1.0 `third_party/tbb`'de sabit.
  - `src/io/usd_loader.cpp`: sahne birleştirme, birim/eksen, mesh/şekil/örnekleme, UsdPreviewSurface + MaterialX OpenPBR/Standard Surface/glTF PBR, UsdLux, DomeLight (USD latlong'u Photon'un aynası), kameralar. Uygulama kubbe ortamını ışık tercihiyle birlikte alır.
  - Malzeme modeli: yalıtkan speküler ağırlığı (F7.14). Referans sahneler `.usda` olarak da üretiliyor (`refscenes.py to_usd`), aynı Mitsuba eşiklerini tutuyor.
- **Sıradaki adımlar:**
  - Adım 2 devamı: tam disk/küre/silindir + tek yüzlü ışıklar (F2.17), MaterialX kütüphanesi (usdMtlx), OCIO/ACES 2, OpenVDB, Hydra delegesi (F7.15).
  - Gerçek 3ds Max / Maya / Blender ile canlı bağlantı ve doğrudan açma denemesi (bu ortamda yok); Windows'ta `CreateProcessW` yolu.
  - Gerçek zamanlı mod: TAA (zamansal kenar yumuşatma) ve GPU Katı modunda doku/ışık desteği.
  - OIDN ile yeni sahneler (SSS kanal gürültüsü) denenmeli; yüksek performanslı RTX/RDNA kartlarda Vulkan RT benchmark ölçümü ve `bench/BASELINE.md` güncellemesi.
  - Pullar ve kutu eşleme dünya uzayında (nesne hareket edince kayar) → nesne uzayı.
  - Tutamaç sürüklenirken Esc ile iptal (şimdilik Ctrl+Z).
  - Kurulum paketi (Inno Setup, makinede yok), kod imzalama, log + çökme dökümü.
  - `v0.5.0` etiketi ile GitHub Release yayımlama.
  - TODO(human) öğrenme maddeleri önceki oturumda kullanıcı isteğiyle tamamlandı; istenirse ilgili fonksiyonlar egzersiz olarak yeniden boşaltılabilir.

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
