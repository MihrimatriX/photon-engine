# Yapılacaklar

Nasıl okunur:
- `- [ ]` açık görev, `- [x]` bitti.
- Köşeli parantezdeki kimlikler, ör. `[engine-1]`, `02-bulgular.md`'deki ayrıntılı bulguya gider (dosya:satır ve düzeltme önerisi orada).
- **[SEN]** işaretli parçayı sen yazarsın; kodda `TODO(human)` olarak bırakılır.
- Efor tam zaman varsayımıyla verildi. Faz 0–9 toplamı ~27–36 hafta.
- Bir faz, "Kanıt" satırındaki her şey sağlanınca biter.

---

## Faz 0 — Zemin (2–3 gün)

- [ ] **F0.1** Baz commit: 13 değişmiş ve 6 izlenmeyen dosya. **Kullanıcı onayı gerekli.** Commit dışında kalacaklara karar ver: `photon_ui.ini.bak`, `Testing/`, `app_stdout.txt`, `app_stderr.txt`. `[build-1]`
- [ ] **F0.2** vcpkg'yi düzelt. `VCPKG_ROOT=C:\vcpkg` gösteriyor ama klasör yok.
  - A: VS 18'in kendi vcpkg'si (`C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg`). `VCPKG_ROOT`'u ona çevir.
  - B: `git clone https://github.com/microsoft/vcpkg C:\vcpkg` ve ardından `bootstrap-vcpkg.bat`.
  - Hangisi seçilirse seçilsin `build-vcpkg/` ve `build/release/` temizden yapılandırılır.
- [ ] **F0.3** `. .\scripts\devshell.ps1`, ardından `cmake --preset release`, `cmake --build --preset release` ve `ctest --preset release`. Hepsi yeşil olmalı. `[build-3]`
- [ ] **F0.4** Tek doğruluk kaynağını seç. Öneri: bu klasör çalışma listesi olur; `docs/ROADMAP.md` fazları özetleyen kısa bir belgeye iner, ticari maddeler eke taşınır, durum bölümü gerçeğe göre yazılır. `[build-15]` `[build-16]`
- [ ] **F0.5** README, `docs/architecture.md` ve `docs/file-reference.md`'deki yanlış iddiaları düzelt: "unbiased", "work-stealing", Halton/Sobol, DirectLighting/AO integratörleri, `src/gpu`, `vcpkg install openimagedenoise`, "Metal" malzemesi. `[build-4]` `[build-5]`
- [ ] **F0.6** `.gitignore`'a `!assets/models/*.obj`, `Testing/` ve `photon_ui.ini` ekle. `[build-14]` `[build-m1]` `[io-7]`
- [ ] **F0.7** `PHOTON_BUILD_GPU=ON` var olmayan `src/gpu`'yu gösteriyor. Şimdilik ya kaldır ya da dizin varsa ekle. `[build-6]`
- [ ] **F0.8** `memory-bank/` git'e commit edilecek mi, karar ver.
- [ ] **F0.9** Uyarılar: motor kütüphanelerinde `/W4 /WX`; üçüncü taraf kodu hariç tutulur. Bilinen iki uyarı çalışma ağacında zaten düzeltildi. `[build-11]`
- [ ] **F0.10** FetchContent kullanılmayan üçüncü taraf hedeflerini de derliyor; `THIRD_PARTY.md`'deki miniz satırı yanlış. `[build-13]`
- **Öğrenme:** *Ray Tracing in One Weekend* 1. kitap (2–3 akşam) ve Scratchapixel'de kamera ışınları üretimi.

## Faz 1 — Çökme yok, görüntü doğru (2–3 hafta)

### 1a Sahiplik ve thread güvenliği

- [ ] **F1.1** Render `Scene` malzemeleri sahiplenir: derleme sırasında `vector<shared_ptr<const Material>>` doldurulur. Ham `Material*` artık sahibinden uzun yaşamaz. `[io-2]` `[app-2]` `[engine-7]` `[io-m4]`
- [ ] **F1.2** `EnvironmentLight` kendi `shared_ptr<const Image>`'ını tutar. Yeni HDRI kenarda kurulur ve kilit altında takas edilir; `m_state.envMap` artık yerinde değiştirilmez. `[lighting-1]` `[app-1]` `[ui-5]` `[engine-m1]` `[color-m2]` `[io-m3]`
- [ ] **F1.3** Doku slotu:
  - değişiklik copy-on-write olur,
  - diskten okuma UI thread'inden çıkar,
  - bekleyen doku bırakma, sonra seçilen mesh'e sessizce uygulanmaz.
  
  `[app-5]` `[engine-m2]` `[app-14]`
- [ ] **F1.4** Malzeme parametre düzenlemesi kilitsiz yerinde yazmak yerine kopya ve takasla yapılır. Faz 3'teki malzeme ID tablosuyla birleşir. `[engine-m2]`
- [ ] **F1.5** Hiç tekrar kullanılmayan `uint64` düğüm kimliği. Kullanan yerler: undo/redo, import-undo (bugün isimle eşliyor), proje override'ları, picking. Seçim tek yerde tutulur. `[app-3]` `[io-15]`
- [ ] **F1.6** glTF'de malzemesiz primitive için `nullptr` döner, OBJ yolundaki gibi. Bugün yığındaki bir nesneye sahip olmayan `shared_ptr` dönüyor. `[io-1]`
- [ ] **F1.7** Şu yazımlar tam render ve turntable'ın arka plan derlemesiyle yarışıyor: `m_state.lights` (push_back/clear), SceneGraph, RenderSettings. Hepsi kilit ya da anlık görüntü arkasına alınır. `[app-12]`
- [ ] **F1.8** Önizleme thread lambda'sına try/catch eklenir; karo istisnasında diğer karolar güvenle beklenir. `[core-5]`
- [ ] **F1.9** ThreadPool'daki kayıp uyanma yarışı: `m_stop` ve `m_activeTasks` kilit altında yazılır. `[core-4]`

### 1b Görüntü doğruluğu

- [ ] **F1.10** `Image` ikiye ayrılır: `Film` (toplam + sayı, `resolve()`) ve doku. PNG, EXR, turntable ve Export ortalamayı yazar. `[engine-1]` `[color-1]`
- [ ] **F1.11** Tek raster kuralı: y=0 üstte. UV çevirmeleri kalkar; dışa aktarılan görüntü viewport ile aynı yönde olur. `[engine-2]` `[color-m1]`
- [ ] **F1.12** Gamma 2.2 yerine sRGB OETF (`spectrum.h`'deki `linearToSRGB`). 8-bit'e yuvarlama ve dither eklenir. `[color-5]` `[color-8]` **[SEN]** parçalı sRGB OETF.
- [ ] **F1.13** Veri dokuları doğrusal okunur:
  - `loadMap`'e `srgb` bayrağı; yalnız albedo ve emisyon sRGB'den çözülür.
  - OBJ'de V koordinatı çevrilir.
  - LDR/HDR yükleyicilerin yanlış biçim kabulü düzeltilir.
  
  `[materials-1]` `[color-2]` `[io-m2]` `[color-3]` `[color-15]`
- [ ] **F1.14** Pozlama ya da ton eşleme değişimi birikimi sıfırlamaz; yalnız ekran yeniden yüklenir. `[engine-9]` `[color-4]` `[app-10]` `[ui-4]`
- [ ] **F1.15** Export viewport'ta görüneni yazar, denoise durumu dahil. `[color-9]` `[app-10]`
- [ ] **F1.16** UTF-8 yollar (Türkçe dosya adları): `STBI_WINDOWS_UTF8`, `u8path`, W API'li dosya diyaloğu (ya da nativefiledialog-extended), `MessageBoxW`. `[color-11]` `[color-m4]` `[io-17]` `[app-13]`
- [ ] **F1.17** NaN/Inf/negatif örnekler tek yerde reddedilir. Bit-testli `finiteFloat` core'a taşınır. `[core-6]` `[color-6]` `[lighting-10]` `[build-9]`

### 1c Cam

- [ ] **F1.18** `refractVec` konvansiyonu ve belgesi düzeltilir. `test_snell` el hesabıyla doğrular, `refractVec`'i çağırmaz. `[core-1]` `[materials-m1]` **[SEN]** Snell'in vektör formu (~6 satır).
- [ ] **F1.19** Pürüzlü cam:
  - iç yansıma ve mikrofaset TIR,
  - Walter 2007 yarım vektörü ve Jakobiyen (eta ile),
  - pdf.
  
  `[materials-2]` `[materials-3]`
- [ ] **F1.20** Radyans ölçekleme ters: cama girerken 1/η² olmalı. `[materials-4]`
- [ ] **F1.21** Camda giriş/çıkış kararı geometrik normalden, kırılma çerçevesi gölgelendirme normalinden. `[materials-8]` `[geometry-m2]`
- [ ] **F1.22** Negatif determinantlı (aynalı) dönüşümlerde sarma yönü ve normal tutarlılığı. `[geometry-m1]`

### 1d Küçük ama görünür

- [ ] **F1.23** AO çarpanı güzellik geçişinden çıkar, varsayılanı 0. `[lighting-4]` `[engine-13]` `[build-2]`
- [ ] **F1.24** Möller–Trumbore'un mutlak 1e-6 determinant eşiği kalkar. `[geometry-1]`
- [ ] **F1.25** Küre tangent işareti düzelir; küreler dünya ölçeğini ve dönüşünü dikkate alır. `[geometry-9]` `[io-18]`
- [ ] **F1.26** Quaternion: slerp ve Y-up `fromEuler` düzeltilir. Kullanılmıyorsa ölü kodla birlikte silinir. `[core-10]` `[core-14]`
- [ ] **F1.41** Dejenere dönüşümler sessizce başarısız oluyor: ölçek alt sınırı ve `inverse()` başarı bayrağı; perspektif yorumu düzeltilir. `[core-15]`
- [ ] **F1.42** Ton eşleme API varsayılanları ve belgeler EV anlamıyla uyumlu hale gelir; EXR sıkıştırmalı yazılır. `[color-16]`
- [ ] **F1.27** GGX D, iptal hatasına dayanıklı forma geçer; roughness < 0.016'da inf üretmesin. `[materials-7]`
- [ ] **F1.28** Disney F0 = 0.08·specular (`disney.cpp`, `material_library.cpp`). `[materials-9]`
- [ ] **F1.29** Clearcoat enerji kazancı. Kısa vadede GTR1 ve 0.25 ağırlık ya da taban zayıflatma. `[materials-10]`
- [ ] **F1.30** Alan ışığı yoğunluk slider'ı: NEE ve emissive quad aynı tek kaynaktan okur. `[lighting-2]` `[engine-m3]` `[app-m2]`
- [ ] **F1.31** Son köşede MIS ağırlıklı NEE yapılıyor ama tamamlayıcı BSDF örneği izlenmiyor; enerji kaybı. `[lighting-m1]`
- [ ] **F1.32** İletim lobunda NEE |cos| kullanır. Faz 5'teki `BSDFSample`'a kadar ara çözüm. `[lighting-3]` `[materials-6]`
- [ ] **F1.33** nlohmann-json (MIT) elle yazılmış JSON ayrıştırıcıların yerini alır; stüdyo preset ışıkları gerçekten yüklenir. `[io-3]` `[ui-m1]`
- [ ] **F1.34** Tam render:
  - boş çıktı yolu varsayılan bir dosyaya gider,
  - metin kutusuna yazılan değer kaybolmaz,
  - sonuç gösterilir.
  
  `[app-4]` `[app-m1]`
- [ ] **F1.35** `RNG::uniformFloat` tam 1.0 dönebiliyor. `[core-11]`
- [ ] **F1.36** GL önizleme ortam küp önbelleği aynı boyutlu ortamlar arasında yenilenmiyor. `[lighting-m2]` `[ui-6]`
- [ ] **F1.37** Equirect lookup'ı kutuplarda V'yi sarıyor (zenit ile nadir karışıyor). `[lighting-m3]`
- [ ] **F1.38** Seçim tıklaması bırakışta olur, basışta değil; orbit başlatmak seçimi silmez. `[app-15]` `[ui-7]`
- [ ] **F1.39** CWD'ye göre yazılan dosyalar kullanıcı veri klasörüne taşınır: `photon_ui.ini`, `scene.photon`, `export.png`, `turntable_frames/`, onboarding işareti.
- [ ] **F1.40** Sahte başarı mesajları ve sessiz hatalar. `[app-9]`
- **Öğrenme:** PBRT bölüm 9.3–9.5 (Snell, Fresnel, dielektrik BSDF), 5.4 (film); *Cinematic Color*; C++'ta sahiplik (`shared_ptr<const T>`, anlık görüntü).
- **Kanıt:**
  - `test_export_roundtrip`, `test_snell`, `test_texture_colorspace` (sRGB 0.5 → albedo 0.214, veri 0.502), `test_rough_glass` (içeriden yansıma > 0).
  - ASan preset'iyle betikli senaryoda çökme yok: örnek sahne → render sırasında HDRI değiştir → düğüm sil → Ctrl+Z → malzemesiz glTF içe aktar.

## Faz 2 — Ölçüm altyapısı (1–2 hafta)

- [ ] **F2.1** Sahne mantığı uygulamadan `photon_scene`'e taşınır: `loadProject`, stüdyo/ışık düzeni, ortam kurulumu, örnek ve Cornell sahneleri.
- [ ] **F2.2** Proje dosyası:
  - eksiksiz kaydeder: ışıklar, cam, aniso/sheen/emisyon, prosedürel ortam, görünürlük, silmeler, render ayarları;
  - glTF dokuları yeniden yüklemede kaybolmaz;
  - yazım atomik, sürüm alanı var, yollar göreli;
  - ters eğik çizgi kaçışı düzeltilir.
  
  `[io-4]` `[io-5]` `[io-6]` `[io-m1]` `[app-9]`
- [ ] **F2.3** Modül grafiği:
  - integrators → `engine/scene.h` döngüsü kırılır; render Scene integrator'ın altına iner.
  - `photon_scene` → `photon_engine` yönü ters çevrilir.
  - Her hedefin `src/`'yi PUBLIC include kökü olarak dışa vermesi kaldırılır.
  
  `[build-7]`
- [ ] **F2.4** CLI argümanları `--scene --spp --res --out --seed --threads --denoise`. Çıktı PNG + EXR + `stats.json` (süre, spp, Mrays/s, bellek tepe değeri). Varsayılan derlemede açık. `[engine-15]`
- [ ] **F2.5** `photon_app --smoke`: gizli pencere, örnek sahne, 16 spp, PNG yazar, 0 koduyla çıkar.
- [ ] **F2.6** `test_furnace`: her malzeme × pürüzlülük. Bugünkü clearcoat ve pürüzlü cam hatalarını yakalamalı. `[materials-14]` `[lighting-16]` `[build-10]` **[SEN]** tahminci Σ f·|cos|/pdf.
- [ ] **F2.7** `test_chi2`: `sample()` ile `pdf()` 2D histogramda tutarlı.
- [ ] **F2.8** 64×64 golden görüntüler ve relMSE eşiği.
- [ ] **F2.9** Determinizm: 1, 4 ve 8 thread ile bit bit aynı görüntü; tohumlar karo boyutuna bağlı olmaz. `[core-12]` `[engine-16]`
- [ ] **F2.10** BVH testi: rastgele sahnede kaba kuvvetle karşılaştırma, `intersectAny`, ölçek durumları. `[geometry-15]`
- [ ] **F2.11** Renk testleri: transfer fonksiyonları, ton eşlemede orta gri, export parlaklığı. `[color-14]`
- [ ] **F2.12** Tracy profil bölgeleri: `renderSamplePass`, `BVH::intersect`, Disney sample/eval, denoise.
- [ ] **F2.13** `bench/scenes/` altında 5 sahne (krom küre + softbox, cam şişe, plastik ürün, pürüzlü metal, iç mekân) ve `bench/BASELINE.md` (Mrays/s, spp/s, 64 spp varyans).
- [ ] **F2.14** (İsteğe bağlı) Mitsuba 3 referansı: ortak malzemelerde relMSE.
- [ ] **F2.15** Faz 3–7'nin sırasını gerçek ölçümlere göre gözden geçir.
- **Öğrenme:** PBRT bölüm 2 (Monte Carlo integrasyonu, varyans, önem örnekleme); chi-kare testinin mantığı.

## Faz 3 — Anında tepki: etkileşim ve gürültü giderme (2–3 hafta)

- [ ] **F3.1** UI ile render ayrılır:
  - render kendine ait bir biriktirici kullanır,
  - ekran tamponu mikrosaniyelik bir kilitle takas edilir,
  - sahne `shared_ptr<const Scene>` ve nesil sayacıyla aktarılır.
  
  `[engine-5]` `[app-6]` `[ui-1]` `[color-m3]` `[core-m1]`
- [ ] **F3.2** İptal: karo başına nesil kontrolü, Durdur düğmesi, çıkışta bekleme yok; `markDirty` sonrası bayat kare yanıp sönmez. `[engine-6]` `[core-3]` `[ui-m2]` **[SEN]** karo döngüsündeki nesil kontrolü.
- [ ] **F3.3** Thread havuzu:
  - kalıcı havuz ve atomik karo sayacı,
  - `hardware_concurrency−1` işçi, karolar ortadan dışa sıralı,
  - her pass'te havuz, örnekleyici ve integratör yaratılmaz,
  - tam render her pass'te tüm görüntüyü kopyalamaz.
  
  `[core-m2]`
- [ ] **F3.4** Kamera hareket ederken çözünürlük merdiveni: 1/8 → 1/4 → 1/2 → tam. Durunca viewport kendi piksel boyutunda render edilir (bugün 960 px sınırı var). `[ui-14]`
- [ ] **F3.5** Ton eşleme GPU'da: RGBA16F doku + `glTexSubImage2D`; pozlama, ton eşleme, sRGB ve dither shader'da. `[color-10]`
- [ ] **F3.6** AgX ve Khronos PBR Neutral eklenir; operatörler orta griye eşlenir; Narkowicz ACES fit'i yalnız seçenek olarak kalır. `[color-7]` **[SEN]** bir operatörün shader fonksiyonu.
- [ ] **F3.7** BVH bir kez kurulur (`scene.cpp:52` kalkar). `[geometry-6]` `[lighting-12]` `[app-m3]` `[engine-8]`
- [ ] **F3.8** Malzeme ID tablosu: malzeme, ortam ya da ışık değişikliği yeniden bake gerektirmez. `[io-10]` `[app-7]`
- [ ] **F3.9** OIDN 2.x:
  - resmi Windows paketi `third_party/oidn/` altına, SHA-256 doğrulamasıyla,
  - DLL'ler `photon_app.exe` yanına kopyalanır,
  - `THIRD_PARTY.md` satırı ve doğru kurulum ipucu,
  - OFF anahtarı gerçekten kapatır.
  
  `[build-8]`
- [ ] **F3.10** AOV'ler `samplePixel`'de beauty ile aynı jitter'la, ilk delta olmayan köşede toplanır. OIDN cihazı önbelleğe alınır, denoise worker thread'de çalışır. Kalite: viewport'ta balanced, son render'da high. `[engine-11]`
- [ ] **F3.11** OIDN yoksa düğme dürüstçe etiketlenir ya da à-trous fallback yazılır. `[engine-10]`
- [ ] **F3.12** Adaptif örnekleme skip-mask'ı paralel ya da karo-yerel çalışır.
- **Öğrenme:** ilerlemeli render ve eşzamanlılık (çift tampon, nesil sayacı); OIDN belgeleri (AOV kuralları); AgX'in neden kanal başına ACES'ten iyi olduğu.
- **Kanıt:** orbit sırasında UI ≥ 60 fps; malzeme tıklamasından sonra ilk kare 50 ms'nin altında; OIDN ile 16 spp temiz.

## Faz 4 — Kolay ve şık (3 hafta)

**Kullanım**

- [ ] **F4.1** İlk açılış: örnek ürün sahnesi ve gerçek bir CC0 HDRI (Poly Haven). "Örnek Sahne" boş yüklenip başarı bildirmez. `[app-8]`
- [ ] **F4.2** Her içe aktarmada:
  - otomatik zemin ve kamera kadrajı (`loadSampleScene`'deki AABB/zemin kodu yeniden kullanılır),
  - sahne ölçeğine göre ışık yerleşimi,
  - metre birimi, içe aktarma birimi ve yukarı eksen seçimi.
  
  `[io-14]` `[app-8]`
- [ ] **F4.3** Orbit kamera:
  - doğru yön ve adım; imlece doğru zoom; imleci izleyen pan,
  - F ile seçime odaklanma; sönümleme,
  - sahne ölçeğine göre sınırlar; theta sınırlaması.
  
  `[ui-3]` `[ui-2]` **[SEN]** imlece doğru zoom matematiği.
- [ ] **F4.4** ImGuizmo: taşı/döndür/ölçekle ve ViewManipulate küpü, undo'ya bağlı. Sürüklerken path-traced görüntü de güncellenir. `[app-11]` `[io-m5]`
- [ ] **F4.5** Komut paleti (Ctrl+K), kısayollarıyla.
- [ ] **F4.6** HDRI döndürme ve yoğunluk; arka plan aydınlatmadan ayrılır (HDRI, renk ya da şeffaf). `[lighting-13]`
- [ ] **F4.7** Işık listesi: dönüşüm, renk, boyut; ışıklar sahne ağacında görünür.
- [ ] **F4.8** Fiziksel kamera temeli:
  - 36×24 sensör ve sensör uyumu,
  - açıklık f/N'den hesaplanır,
  - viewport en-boy oranı çıktıyla aynı.
  
  `[engine-12]`
- [ ] **F4.9** İçe aktarma, HDRI ve doku yükleme asenkron.
- [ ] **F4.10** Picking BVH ile yapılır; graph üzerinde kaba kuvvet kalkar.

**Görünüm**

- [ ] **F4.11** Fontlar ve metin:
  - Inter ve JetBrains Mono (OFL) ile Lucide ikon fontu (ISC), birleşik,
  - DPI ölçekleme; `/utf-8` ile doğru Türkçe; TR/EN karışımı temizlenir,
  - WIN32 alt sistemi (konsol penceresi yok); debug `fprintf`'leri kalkar.
  
  `[ui-11]` `[app-16]` `[ui-16]`
- [ ] **F4.12** Tema: nötr palet, yalnız ana eylemde tek vurgu rengi, ImGui 1.92 renk yuvaları, pencere menü üçgenleri kapalı, 8 px aralık ızgarası. `[ui-12]`
- [ ] **F4.13** Viewport öncelikli düzen: kaplama araç çubukları; HUD (spp, süre, ETA, Mrays/s, denoise); `BeginViewportSideBar` durum çubuğu. `[ui-10]`
- [ ] **F4.14** Panel bilgi mimarisi: Render paneli görünür, Kamera her zaman en üstte değil, 7 kütüphane sekmesi taşmasın. `[ui-15]`
- [ ] **F4.15** Malzeme kütüphanesi:
  - ızgara çakışması düzelir,
  - kendi motorunla render edilen 128–256 px küçük resimler, disk önbelleğiyle (bugün metaller siyah, gamma yok, renkli arka plan),
  - sürükle-bırak ve üzerine gelince önizleme.
  
  `[ui-8]` `[ui-9]`
- [ ] **F4.16** Malzeme kütüphanesi JSON'u cam, IOR ve tip ifade edebilir; preset kimliğine sabitlenmiş davranış kalkar. `[io-16]`
- [ ] **F4.17** MTL → Disney eşlemesi; UI'da düzenlenebilir ve kaydedilir. `[io-12]`

**Kod**

- [ ] **F4.18** `application.cpp` (2189 satır, ~52 metot) bölünür: `panels/`, `render_controller`, `scene_ops`.
- **Öğrenme:** Blender, Marmoset ve KeyShot arayüz incelemesi; gizmo matematiği (ekran → dünya ışını, ışın-düzlem kesişimi).
- **Kanıt:** "model aç → zemine koy → 3 parçaya malzeme → HDRI → softbox → 4K PNG" görevi 2 dakikanın altında; önce/sonra ekran görüntüleri.

## Faz 5 — Örnekleme ve ışık taşıma (3 hafta)

- [ ] **F5.1** Owen karıştırmalı Sobol (Burley 2020) ya da ZSobol. `StratifiedSampler` yedek kalır; PCG'ye `advance()` eklenir. `[core-2]` `[engine-3]` `[engine-4]`
- [ ] **F5.2** `BSDFSample {f, wi, pdf, eta, flags}`:
  - kosinüsü malzeme sahiplenir,
  - isabet başına tek resolve,
  - Lambertian çerçeveyi bir kez kurar,
  - `std::pow(x,5)` yerine çarpım.
  
  `[materials-12]` `[lighting-15]` `[lighting-5]` `[materials-5]`
- [ ] **F5.3** Normal haritasında gölgelendirme normali tutarlılığı: bükülmüş normal ya da Schüssler 2017. `[materials-5]`
- [ ] **F5.4** Işık seçimi:
  - güce orantılı alias tablosu, pmf MIS ağırlıklarına girer,
  - `shadowQuality` yalnız ilk köşede uygulanır,
  - emissive küreler de NEE'ye girer,
  - ortam ışığının gücü `m_funcInt`'ten hesaplanır.
  
  `[lighting-8]` `[lighting-9]` **[SEN]** pmf'li güç sezgisel ağırlığı ve alias tablosu kurulumu.
- [ ] **F5.5** `SurfaceInteraction`'a `primId`, `lightId`, barycentric ve `dpdu`/`dpdv`. Emitter pdf'i O(1) olur, `dynamic_cast` döngüsü kalkar. `[lighting-7]` `[geometry-8]`
- [ ] **F5.6** Softbox için küresel dikdörtgen örnekleme (Ureña 2013), mesh ışıkları için küresel üçgen (Arvo); tek yönlü emitter seçeneği.
- [ ] **F5.7** Ortam ışığı: pdf ile eval tutarlı, MIS telafisi (Karlík 2019), yatay ayna kontrolü. `[lighting-14]`
- [ ] **F5.8** Rus ruleti en büyük bileşene göre, ayarlanabilir minimum derinlikle. `[lighting-11]`
- [ ] **F5.9** Wächter–Binder ışın ofseti geometrik normal boyunca, tMin = 0. İsabet noktası barycentric'ten; ön/arka yüz geometrik normalden. `[core-8]` `[geometry-4]` `[geometry-5]` `[lighting-6]`
- [ ] **F5.10** Gölge terminatörü düzeltmesi (Hanika 2021).
- [ ] **F5.11** Adaptif örnekleme: karo başına göreli hata, iki yarım tamponla. `[engine-14]`
- [ ] **F5.12** Albedoya göre lob seçimi (Faz 7'deki VNDF ile birlikte).
- [ ] **F5.13** Ölçek testi: aynı sahne 0.01× ve 100× ölçekte aynı görüntüyü verir.
- [ ] **F5.14** İsteğe bağlı firefly kelepçesi, varsayılan kapalı.
- **Öğrenme:** PBRT bölüm 8, 12, 13–14; Veach tezi bölüm 9; TU Wien dersleri.
- **Kanıt:** eşit sürede varyans düşer (Sobol, yakın softbox, çok ışıklı stüdyo); fırın ve chi-kare testleri yeşil.

## Faz 6 — Ham hız: hızlandırma yapıları (3 hafta)

- [ ] **F6.1** Yaprak sıralı üçgen tamponu; kesişim yalnız (t, u, v, primId) döner; `SurfaceInteraction` isabetten sonra bir kez kurulur. `[geometry-2]`
- [ ] **F6.2** Bölmesiz slab testi; BVH'nin zaten hesapladığı `invDir` kullanılır. `[core-7]` `[geometry-3]`
- [ ] **F6.3** Watertight ışın-üçgen kesişimi (Woop 2013). **[SEN]** kesme (shear) adımı.
- [ ] **F6.4** Yakın/uzak çocuk ayıklama ve sabit boyutlu yığın; iç içe çağrıda yığın paylaşımı hatası. `[geometry-7]` `[geometry-14]`
- [ ] **F6.5** Paralel binned SAH: prefix/suffix tarama, arena düğümleri, yaprak ≤ 4–8. `[geometry-13]` **[SEN]** SAH bölme seçimi.
- [ ] **F6.6** BLAS/TLAS ve örnekleme (instancing); dönüşüm düzenlemesi yalnız TLAS'ı yeniden kurar. `[geometry-6]`
- [ ] **F6.7** Vertex kaynaklama, kopyasız taşıma, bake edilmiş kopyaların kalkması, açıya göre otomatik yumuşatma. `[geometry-11]` `[io-11]`
- [ ] **F6.8** Gölge any-hit: alfa/kesik desteği; ışığın kendi primitifi atlanır. `[geometry-12]`
- [ ] **F6.9** Embree 4.4 (vcpkg, `default-features: false`; triangle, instance, filter-function açık). `PHOTON_USE_EMBREE` bayrağı; `bvh.cpp` referans test olarak kalır.
- [ ] **F6.10** Global `/arch:AVX2`'nin faydası ölçülür; gerekirse varsayılan SSE4.2 olur. `[core-9]` `[build-12]`
- [ ] **F6.11** Küre kesişim hassasiyeti (pbrt-v4 diskriminant formu). `[core-13]`
- **Öğrenme:** Bikker "How to build a BVH" serisi (1–9), PBRT bölüm 7, *Ray Tracing: The Next Week*.
- **Kanıt:** her adımda Mrays/s `BASELINE.md`'ye yazılır; 1M üçgenli içe aktarma süresi ölçülür.

## Faz 7 — Malzeme ve renk gerçekçiliği (4–5 hafta)

- [ ] **F7.1** VNDF örnekleme (Heitz 2018) ve yükseklik-korelasyonlu Smith G2. `[materials-15]` **[SEN]** VNDF (~10 satır).
- [ ] **F7.2** Kulla–Conty çoklu saçılma telafisi. **[SEN]** telafi terimi.
- [ ] **F7.3** F82-tint iletken Fresnel ve metal presetleri (altın, gümüş, bakır, alüminyum, krom).
- [ ] **F7.4** Enerji koruyan kaplama (taban zayıflatma) ve LTC sheen.
- [ ] **F7.5** Beer–Lambert soğurma ve ortam takibi; cam rengi yansımaları boyamaz. `[materials-13]`
- [ ] **F7.6** Doku depolama 8-bit + LUT, makul boyut sınırı; mipmap ve ışın konisi (RTG bölüm 20). `[color-13]`
- [ ] **F7.7** MikkTSpace ya da glTF `TANGENT`, handedness ile. `[geometry-10]`
- [ ] **F7.8** Faktör × doku çarpımı, ORM kanal seçimi, alfa/opaklık. `[materials-11]` `[color-12]`
- [ ] **F7.9** glTF:
  - malzemeler eksiksiz okunur ve uzantılar desteklenir: transmission, ior, volume, clearcoat, sheen, specular, emissive_strength, texture_transform,
  - aynı doku primitive başına yeniden decode edilmez,
  - sparse accessor desteği; Draco/meshopt için sessiz bozulma yerine açık hata.
  
  `[io-8]` `[io-9]` `[io-13]`
- [ ] **F7.10** OpenPBR Surface'e geçiş. Disney referans olarak kalır; her lob fırın ve chi-kare kapısından geçer.
- [ ] **F7.11** Yuvarlatılmış kenar (bevel) gölgelendiricisi.
- [ ] **F7.12** Disney'de eksik loblar (specTrans, IOR) OpenPBR ile gelir.
- **Öğrenme:** PBRT bölüm 9–10; SIGGRAPH "Physically Based Shading" notları; Heitz 2018; OpenPBR şartnamesi.
- **Kanıt:** fırın testinde beyaz pürüzlü metal her pürüzlülükte ≈ 1; ortak malzemelerde Mitsuba relMSE %1'in altında.

## Faz 8 — Rasterizasyon izi (2–3 hafta)

- [ ] **F8.1** Split-sum IBL: ön filtrelenmiş GGX küp haritası, irradiance, BRDF LUT. **[SEN]** BRDF LUT integrali.
- [ ] **F8.2** PCF'li gölge haritası, MSAA, tam çözünürlük.
- [ ] **F8.3** Doku, emisyon ve cam yaklaşımı; seçim konturu; backface tutarlılığı; path tracer ile aynı ton eşleme. `[ui-13]`
- [ ] **F8.4** Equirect→küp dönüşümü bilinear; ışıklar gerçek değerleriyle.
- [ ] **F8.5** GL kaynak ömürleri: küçük resimler yeniden kurulurken eski texture ID'leri.
- [ ] **F8.6** Raster→path-trace geçişindeki sıçrama kalkar.
- **Öğrenme:** LearnOpenGL (PBR, IBL, shadow mapping), *Real-Time Rendering* 4. baskı, Cem Yüksel dersleri.
- **Kanıt:** raster ve path-traced görüntü yan yana, fark ısı haritasıyla; 1080p'de 120 fps'nin üstü.

## Faz 9 — GPU: GTX 1080'de CUDA (6–10 hafta)

- [ ] **F9.1** CUDA 12.x kurulur; VS 18 host derleyici uyumu doğrulanır (gerekirse v143 araç seti).
- [ ] **F9.2** `PHOTON_HD` makrosu; sanal çağrı yerine `switch`.
- [ ] **F9.3** SoA cihaz tamponları, CUDA texture nesneleri.
- [ ] **F9.4** TinyBVH CWBVH CPU'da kurulur; gezinme çekirdeği CUDA'ya taşınır.
- [ ] **F9.5** Önce megakernel; profil diverjans gösterirse wavefront.
- [ ] **F9.6** CUDA–GL interop ile viewport.
- [ ] **F9.7** `RenderDevice {upload, update(delta), renderPass, cancel}` arayüzü; CPU cihazı yedek kalır.
- [ ] **F9.8** CPU–GPU eşleşme testi ve GPU bench.
- [ ] **F9.9** RTX gelince OptiX 9.x ve OIDN CUDA cihazı.
- **Öğrenme:** CUDA Programming Guide; "Accelerated Ray Tracing in One Weekend in CUDA"; Laine 2013; Ylitie 2017.

## Faz 10 — İleri konular (seçmeli)

- [ ] Hacimler ve random-walk SSS.
- [ ] Path guiding (Open PGL, Apache).
- [ ] Spektral mod ve dispersiyon (hero wavelength, Jakob–Hanika).
- [ ] MNEE ve referans için BDPT.
- [ ] Fiziksel kamera (f-stop, enstantane, ISO) ve bokeh şekli.
- [ ] AOV'ler, ışık grupları, shadow catcher, çok katmanlı EXR, Cryptomatte.
- [ ] Turntable (DoF ve denoise ile), anahtar kare, hareket bulanıklığı.
- [ ] Güneş/gökyüzü, spot ve IES ışıklar, ışık bağlama, görünürlük bayrakları.

## Ürünleşme eki (karar bekliyor)

EULA ve gizlilik metni, lisans anahtarı, kod imzalama, kurulum paketi, Linux derlemesi/CI/paketleme, ağ render, STEP/IGES/USD/FBX içe aktarma, Python ve eklenti API'si, KeyShot/Cycles karşılaştırması, patent incelemesi, telemetri. Şimdiden geçerli tek kural: GPL bağımlılık yok.

## Öğrenme kilometre taşları

- [ ] *Ray Tracing in One Weekend* 1. kitap (Faz 0)
- [ ] PBRT bölüm 2, 5.4, 9.3–9.5 ve *Cinematic Color* (Faz 1–2)
- [ ] OIDN belgeleri ve AgX (Faz 3)
- [ ] Gizmo ve kamera matematiği (Faz 4)
- [ ] PBRT bölüm 8, 12–14 ve Veach tezi bölüm 9 (Faz 5)
- [ ] Bikker BVH serisi 1–9 ve *Ray Tracing: The Next Week* (Faz 6)
- [ ] PBRT bölüm 9–10, PBS notları, Heitz 2018, OpenPBR (Faz 7)
- [ ] LearnOpenGL PBR/IBL/gölgeler ve RTR4'ten seçili bölümler (Faz 8)
- [ ] CUDA Guide, Accelerated RTiOW in CUDA, Laine 2013, Ylitie 2017 (Faz 9)
- [ ] Uçtan uca CGI pratiği #1 (Faz 4 sonu): Blender'da ürün modelle → UV → doku → glTF → PhotonEngine'de look-dev ve ışık → Natron'da AOV birleştirme
- [ ] Uçtan uca CGI pratiği #2 (Faz 7 sonu)
- [ ] Uçtan uca CGI pratiği #3 (Faz 10 sonu)
