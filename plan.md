# PhotonEngine: hızlı, gerçekçi, şık bir render motoru ve CG öğrenme planı

## Bağlam

PhotonEngine ~13.6k satırlık bir C++20 CPU path tracer. GLFW + ImGui + OpenGL editörü, CMake + vcpkg, 56 GoogleTest var. `docs/ROADMAP.md` 297 maddelik, KeyShot'u geçmeyi hedefleyen ticari bir plan (73–114 hafta).

Yeni hedef iki parçalı:
1. **Ürün:** kullanımı kolay, son derece hızlı ve fiziksel olarak gerçekçi bir motor, şık bir arayüzle.
2. **Öğrenme:** bu proje üzerinden bilgisayar grafiği ve CGI pipeline'ının tamamını öğrenmek.

**Kullanıcı kararları (2026-10-02):**
- Seviye: grafikte yeni, matematik tarafı sağlam.
- Yaklaşım: her parçada en kaliteli sonucu veren yol seçilir.
- Ticari hedef: belirsiz. Kapı açık tutulur, ticari maddeler ertelenir.
- GPU: GTX 1080.

**Makine (tarama sırasında okundu):**
- i7-6700K (4 çekirdek, 8 iş parçacığı, AVX2), 32 GB RAM.
- GTX 1080, sürücü 582.66. R580 dalı Pascal'ın son dalı.
- `VCPKG_ROOT=C:\vcpkg`, ama klasör artık yok.

## Tarama sonucu (22 ajan, bulgular ikinci bir ajanca doğrulandı)

**Sağlam olanlar.** Bunlara dokunulmuyor, üzerine inşa ediliyor:
- Temel matematik: Duff 2017 ONB, kofaktör ters matris, normal için ters-transpoz, PCG32.
- Örnekleme warp'ları ve pdf'leri, kesin dielektrik Fresnel.
- Ortam ışığı CDF'si ve sinθ Jakobiyeni.
- MIS iskeleti; binned SAH BVH (12 kova, 32 B düğüm, sıralı gezinme).
- Thin lens kamera.
- Deterministik integratör test düzeneği (`ScriptedSampler`).

**Sorunlar beş kümede toplanıyor:**

1. **Çökme ve veri yarışı (kritik).** Render `Scene` malzemeleri, HDRI'yi ve dokuları ham işaretçiyle ödünç alıyor; UI bunları kilitsiz değiştiriyor ya da siliyor. Normal kullanımda use-after-free tetikleyen durumlar:
   - render sırasında HDRI değiştirmek,
   - düğüm silmek ve ardından Ctrl+Z,
   - doku değiştirmek,
   - malzemesiz glTF içe aktarmak,
   - tam render sürerken düzenleme yapmak.
   
   Önizleme thread'inde try/catch yok.

2. **Yanlış görüntü (kritik).**
   - PNG/EXR ortalama yerine **örnek toplamını** yazıyor; görüntü spp kat parlak ve dikey olarak ters çıkıyor. Varsayılan denoise bulanıklığı bu hatayı bir yolda maskeliyor.
   - Cam kırılması teğet düzlemde **aynalanıyor**: `refractVec` konvansiyonu yanlış.
   - Buzlu cam: iç yansıma siyah, Jakobiyen yanlış.
   - Normal, roughness ve metal haritaları sRGB'den çözülüyor; düz bir normal haritası yüzeyi ~39° eğiyor.
   - Katmanlı örnekleyici pikseller ve boyutlar arasında ilişkili; ilerlemeli önizlemede ışıklar sırayla "açılıyor".
   - Fiziksel olmayan AO çarpanı varsayılan olarak açık.
   - Küçük üçgenler kayboluyor (mutlak 1e-6 eşiği).
   - Ekrana aktarımda sRGB eğrisi yerine gamma 2.2 kullanılıyor.

3. **Yavaşlık.**
   - UI ~4 fps: render bir pass boyunca mutex'i tutuyor.
   - İptal mekanizması yok.
   - Her düzenlemede tüm sahne yeniden bake ediliyor ve BVH (1 + alan ışığı sayısı) kez kuruluyor.
   - Her aday üçgen için 112 B'lık nesne oluşturuluyor; slab testi her düğümde bölme yapıyor.
   - Işık seçimi tekdüze; mesh ışığı pdf'i lineer taramayla bulunuyor.
   - OIDN yok, yerine 3×3 bulanıklık çalışıyor.
   - Hiçbir şey ölçülmüyor; tüm hızlanma rakamları tahmin.

4. **Kullanım ve arayüz.**
   - Uygulama Cornell kutusuyla açılıyor. Örnek sahne boş, çünkü `*.obj` gitignore'da.
   - Stüdyo preset'i sıfır ışık yüklüyor (JSON ayrıştırma hatası).
   - İçe aktarmada zemin ve kadraj yok.
   - Zoom ters ve tekerlek başına %0.2.
   - Tek font, ikon yok, DPI ölçekleme yok, Türkçe ASCII yazılıyor.
   - Metal küçük resimler siyah; durum çubuğu kesik.
   - `application.cpp` 2189 satırlık bir "god object".

5. **Belgeler.**
   - README'nin "unbiased, work-stealing, Halton/Sobol, GPU, OIDN" iddiaları kodla uyuşmuyor.
   - ROADMAP'in durum bölümü bayat.
   - Modül grafiği döngüsel: integrators → `engine/scene.h`.

**Çürütülen ya da hafifletilen bulgular:**
- Siyah viewport ekran görüntüsünün sebebi `theta=0` değil. Commit edilmemiş RGBA hizalama düzeltmesinden önce alınmış.
- 960 px sınırının etkisi küçük.
- Font aralığı iddiası ImGui 1.92 için geçersiz.

**Teknoloji gerçekleri:**
- OIDN vcpkg'de **yok**; resmi derlenmiş paketten alınacak. Embree 4.4.1 vcpkg'de var.
- CUDA 13, sm_61 (Pascal) desteğini kaldırdı; **CUDA 12.x** kullanılacak.
- OptiX 9.1 Turing ve R590+ sürücü istiyor, bu kartta çalışmaz.
- Vulkan RT pipeline kartta açık ama `ray_query` yok.
- OIDN Pascal'da GPU modunda çalışmaz, yalnız CPU'da.

## Strateji kararları

- **Sıra:** önce doğruluk ve kararlılık, sonra ölçüm, sonra hız. Yanlış görüntüyü hızlandırmak anlamsız.
- **En kaliteli yol:** her kritik parçanın önce yalın, kendi sürümü yazılır (öğrenme ve test referansı için). Üretimde en iyi kütüphane kullanılır: Embree 4 ve OIDN 2.
- **GPU (GTX 1080):** CUDA 12.x (sm_61) ve TinyBVH CWBVH. Çekirdekler `PHOTON_HD` ile CPU ile paylaşılır. OptiX yalnız RTX kart gelince. Vulkan/DXR, ikinci bir shading kod tabanı demek; yalnız seçmeli öğrenme egzersizi.
- **Ticari kapı açık:** GPL yok, LGPL yalnız dinamik bağlanır, her yeni bağımlılık `docs/THIRD_PARTY.md`'ye yazılır. Ticari maddeler ROADMAP'te "Ürünleşme (karar bekliyor)" ekine taşınır.
- **Hafif süreç:** her madde için önce kırmızı sonra yeşil bir test. Görüntü değişiyorsa bench sayısı ya da ekran görüntüsü eklenir. ADR, CHANGELOG ve Linux CI yalnız ürünleşme kararıyla gelir.
- **Rasterizasyon izi eklenir.** CGI'nin gerçek zamanlı yarısı mevcut ROADMAP'te yoktu.

## Fazlar

Her fazda dört bölüm var: işler, öğrenme, senin yazacağın parça, kanıt. Efor tam zaman varsayımıyla verildi. Faz 0–9 toplamı ~27–36 hafta; kısmi zamanda orantılı uzar.

**Öğrenme döngüsü (her madde için):**
1. İlgili bölümü oku.
2. Motorda kodu bul ve neden yanlış ya da eksik olduğunu gör.
3. Kırmızı testi yaz.
4. Kilit 2–10 satırı **sen** yazarsın (`TODO(human)`), gerisini ben yazarım.
5. Test yeşile döner, ölçülür, bir paragraf not alınır.

### Faz 0 — Zemin (2–3 gün)
- Baz commit: 13 değişmiş ve 6 izlenmeyen dosya. Senin onayınla yapılır.
- vcpkg'yi `C:\vcpkg`'ye yeniden kur (`git clone` + `bootstrap-vcpkg.bat`) ya da `VCPKG_ROOT`'u düzelt. `cmake --preset release` ve `ctest` yeşil olmalı.
- `docs/ROADMAP.md` bu plana göre yeniden düzenlenir: durum bölümü gerçeğe göre yazılır, ticari maddeler eke taşınır. README'deki yanlış iddialar düzeltilir.
- **Öğrenme:** *Ray Tracing in One Weekend* (1. kitap, 2–3 akşam) ve Scratchapixel "ray generation". Motora dokunmadan sezgi kazanmak için.

### Faz 1 — Çökme yok, görüntü doğru (2–3 hafta)

**1a Sahiplik ve thread güvenliği**
- Render `Scene` malzemeleri `shared_ptr<const Material>` olarak tutar (`scene.h`, `scene_graph.cpp:62,88,92`).
- `EnvironmentLight` kendi `shared_ptr<const Image>`'ını tutar. Yeni HDRI kenarda kurulur, kilit altında takas edilir (`environment_light.h:51`, `application.cpp:818,859`).
- Doku değişimi copy-on-write olur (`application.cpp:1706-1715`).
- Hiç tekrar kullanılmayan `uint64` düğüm kimliği. Undo, import-undo, proje override'ları ve picking hepsi bunu kullanır (`application.cpp:557,659,1596`).
- glTF'de malzemesiz primitive için `nullptr` döner (`gltf_loader.cpp:231`).
- `m_state.lights` yazımları kilit altına alınır. Önizleme thread'ine try/catch eklenir (`application.cpp:583-622`).

**1b Görüntü doğruluğu**
- `Image` ikiye ayrılır: `Film` (toplam + sayı, `resolve()`) ve doku. PNG/EXR ortalamayı yazar (`image_io.cpp:44,73`).
- Tek raster kuralı: y=0 üstte (`renderer.cpp:34`). `application.cpp:1908` ve `viewport_texture.cpp:61`'deki UV çevirmeleri kalkar.
- Gerçek sRGB OETF kullanılır: `spectrum.h:85` (`linearToSRGB`) zaten var, `tone_mapping.cpp:84`'e bağlanır. Yuvarlama ve dither eklenir.
- Veri dokuları doğrusal okunur: `loadMap`'e `srgb` bayrağı (`disney.cpp:216-248`, `image_io.cpp:221`). OBJ'de V koordinatı çevrilir.
- Pozlama ya da ton eşleme değişince birikim sıfırlanmaz (`application.cpp:1778`).
- UTF-8 yollar (Türkçe dosya adları için): `STBI_WINDOWS_UTF8`, W API'li dosya diyaloğu.

**1c Cam**
- `refractVec` konvansiyonu düzeltilir (`utils.h:140-154`, `dielectric.cpp:141,167`).
- Pürüzlü camda iç yansıma ve Walter 2007 yarım vektörü (`dielectric.cpp:67,78`).
- 1/η² ölçeklemesi (`:150`) ve gölgelendirme normali çerçevesi (`:124`).
- Negatif determinantlı (aynalı) dönüşümler (`scene_graph.cpp:50`).

**1d Küçük ama görünür**
- AO çarpanı varsayılan 0 olur ve güzellik geçişinden çıkar (`path_tracer.cpp:113`, `application.cpp:381`).
- NaN/Inf/negatif örnek tek yerde reddedilir. `obj_loader.cpp:64-69`'daki bit-testli `finiteFloat` core'a taşınır.
- Möller–Trumbore'un mutlak 1e-6 eşiği kalkar (`triangle.cpp:12`).
- Küre tangent işareti (`sphere.cpp:53`) ve slerp nokta çarpımı (`quaternion.h:125`).
- GGX D, iptal hatasına dayanıklı forma geçer (`disney.cpp:14`). F0 = 0.08·specular (`disney.cpp:143`, `material_library.cpp:77`).
- Alan ışığı slider'ı ile emissive quad'ın parlaklığı ayrışıyor (`application.cpp:1560`, `scene.cpp:36`).
- Son köşede NEE enerji kaybı (`path_tracer.cpp:70`).
- Elle yazılmış JSON ayrıştırıcılar yerine nlohmann-json (MIT) gelir (`application.cpp:61-97,989-1065`). Böylece stüdyo ışıkları gerçekten yüklenir.
- Örnek OBJ'ler gitignore'dan çıkarılır (`!assets/models/*.obj`).
- Tam render: boş çıktı yolu ve kaybolan metin kutusu (`application.cpp:1141,1813`).

**Öğrenme:** PBRT 4e bölüm 9.3–9.5 (Snell, Fresnel, dielektrik BSDF) ve 5.4 (film). *Cinematic Color* (lineer iş akışı, OETF). C++'ta sahiplik ve anlık görüntü fikri.

**Senin yazacağın:** `refractVec`'in vektör formu (~6 satır) ve parçalı sRGB OETF.

**Kanıt:**
- `test_export_roundtrip`: 4×`addSample` → PNG → okuma, ortalama ve satır sırası doğru.
- `test_snell`: el ile hesaplanmış bir yön; `refractVec`'i çağırmaz.
- `test_texture_colorspace`: albedo sRGB 0.5 → 0.214, veri dokusu → 0.502.
- ASan preset'iyle betikli senaryo, çökme olmamalı: örnek sahne → render sırasında HDRI değiştir → düğüm sil → Ctrl+Z → malzemesiz glTF içe aktar.

### Faz 2 — Ölçüm altyapısı (1–2 hafta)
- Sahne mantığı uygulamadan `photon_scene`'e taşınır: `loadProject`, stüdyo ve ışık düzeni, ortam kurulumu (`application.cpp:379-490,756-806,989-1065`).
- Proje dosyası ışıkları, camı ve render ayarlarını da saklar. Yazım atomik olur, sürüm alanı eklenir, ters eğik çizgi kaçışı düzeltilir (`project_io.cpp:157,272,344`).
- CLI (`src/main.cpp`):
  - Argümanlar: `--scene --spp --res --out --seed --threads --denoise`.
  - Çıktı: PNG + EXR + `stats.json` (süre, spp, Mrays/s, bellek tepe değeri).
  - Varsayılan derlemede açık.
- `photon_app --smoke`: gizli pencere, örnek sahne, 16 spp, PNG yazar, 0 koduyla çıkar.
- Testler:
  - Beyaz fırın: her malzeme × pürüzlülük.
  - Chi-kare: `sample` ile `pdf` tutarlılığı.
  - 64×64 golden görüntüler (relMSE eşiği).
  - Determinizm: 1, 4 ve 8 thread ile bit bit aynı.
- Tracy (BSD-3) profil bölgeleri. `bench/scenes/` altında 5 sahne ve `bench/BASELINE.md`. İsteğe bağlı Mitsuba 3 referansı.
- **Öğrenme:** PBRT bölüm 2 (Monte Carlo integrasyonu, varyans, önem örnekleme). Chi-kare testinin mantığı.
- **Senin yazacağın:** fırın testi tahmincisi Σ f·|cos|/pdf (~5 satır).
- **Kanıt:**
  - Fırın testi bugünkü clearcoat enerji kazancını ve pürüzlü cam hatasını yakalar.
  - `BASELINE.md`'nin ilk satırı gerçek Mrays/s ile yazılır.
  - Sonraki fazların sırası bu sayılarla yeniden gözden geçirilir.

### Faz 3 — Anında tepki: etkileşim ve gürültü giderme (2–3 hafta)
- UI ile render ayrılır:
  - Render kendine ait bir biriktirici kullanır; ekran tamponu mikrosaniyelik bir kilitle takas edilir.
  - Sahne `shared_ptr<const Scene>` ve bir nesil sayacıyla aktarılır (`application.cpp:609-618,2139`).
- İptal: `parallelFor2D` her karoda nesil sayacını kontrol eder (`parallel.h:55`). Durdur düğmesi eklenir, çıkışta beklenmez.
- Thread havuzu:
  - Kalıcı havuz ve atomik karo sayacı (`thread_pool.h:42-63`, `renderer.cpp:247`).
  - `hardware_concurrency−1` işçi; karolar ortadan dışa doğru işlenir.
  - Kayıp uyanma yarışı düzeltilir (`thread_pool.cpp:35`).
- Kamera hareket ederken çözünürlük merdiveni: 1/8 → 1/4 → 1/2 → tam.
- Ton eşleme GPU'ya taşınır:
  - Doku RGBA16F; güncelleme `glTexSubImage2D` ile.
  - Shader'da pozlama, ton eşleme, sRGB ve dither.
  - **AgX** ve **Khronos PBR Neutral** operatörleri eklenir.
- Artımlı düzenleme, adım 1: BVH tek kez kurulur (`scene.cpp:52` kaldırılır). Malzeme ID tablosu sayesinde malzeme, ortam ve ışık değişikliği yeniden bake gerektirmez.
- OIDN 2.x:
  - Kaynak: resmi Windows paketi, `third_party/oidn/` altına SHA-256 doğrulamasıyla.
  - Albedo ve normal AOV'leri `samplePixel` içinde, beauty ile aynı jitter'la toplanır.
  - Cihaz önbelleğe alınır; denoise worker thread'de asenkron çalışır.
  - Kalite: viewport'ta "balanced", son render'da "high".
  - OIDN yoksa düğme dürüstçe etiketlenir.
- **Öğrenme:** ilerlemeli render ve eşzamanlılık (çift tampon, nesil sayacı), OIDN belgeleri (AOV kuralları), AgX'in neden kanal başına ACES'ten iyi olduğu.
- **Senin yazacağın:** karo döngüsündeki nesil kontrolü (~4 satır) ve AgX ya da PBR Neutral shader fonksiyonu.
- **Kanıt:**
  - Kamera döndürürken UI ≥ 60 fps.
  - Malzeme tıklamasından sonra ilk kare 50 ms'nin altında (`stats.json`).
  - OIDN ile 16 spp önizleme temiz.

### Faz 4 — Kolay ve şık: ilk deneyim ve arayüz (3 hafta)

**Kullanım kolaylığı**
- İlk açılış: örnek ürün sahnesi ve gerçek bir CC0 HDRI (Poly Haven). Prosedürel "sahte HDRI" kalkar.
- Her içe aktarmada:
  - otomatik zemin ve kamera kadrajı (`application.cpp:428-454`'teki AABB/zemin kodu yeniden kullanılır),
  - sahne ölçeğine göre ışık yerleşimi,
  - metre birimi ve içe aktarma birimi seçimi.
- Orbit kamera:
  - doğru yön ve adım (`orbit_camera.cpp:41`),
  - imlece doğru zoom, imleci izleyen pan,
  - F ile seçime odaklanma, sönümleme.
- ImGuizmo (MIT):
  - taşı / döndür / ölçekle ve ViewManipulate küpü,
  - undo yığınına bağlı,
  - sürüklerken path-traced görüntü de güncellenir.
- Komut paleti (Ctrl+K).
- HDRI döndürme ve yoğunluk; arka planın aydınlatmadan ayrılması.
- Işık listesi: dönüşüm, renk ve boyut.

**Görünüm**
- Tipografi:
  - Inter + JetBrains Mono (OFL) ve Lucide ikon fontu (ISC), tek fontta birleşik.
  - DPI ölçekleme; `/utf-8` ile doğru Türkçe karakterler.
- Tema: nötr palet, yalnız ana eylemde tek vurgu rengi, 8 px aralık ızgarası, ImGui 1.92 renk yuvaları (`theme.cpp`).
- Viewport öncelikli düzen:
  - yarı saydam kaplama araç çubukları,
  - HUD: spp, süre, ETA, Mrays/s,
  - `BeginViewportSideBar` ile durum çubuğu.
- Malzeme kütüphanesi:
  - ızgara hatası düzeltilir (`application.cpp:1291`),
  - kendi motorunla render edilen 128–256 px küçük resimler, disk önbelleğiyle,
  - sürükle-bırak ve üzerine gelince canlı önizleme.

**Kod yapısı**
- `application.cpp` bölünür: `panels/`, `render_controller`, `scene_ops`.

**Öğrenme:** Blender, Marmoset ve KeyShot arayüz incelemesi. Gizmo matematiği: ekran noktasından dünya ışını, ışın-düzlem kesişimi.

**Senin yazacağın:** imlece doğru zoom matematiği.

**Kanıt:**
- Görev süresi ölçülür: model aç → zemine koy → 3 parçaya malzeme → HDRI → softbox → 4K PNG. Hedef 2 dakikanın altı.
- Önce ve sonra ekran görüntüleri.

### Faz 5 — Örnekleme ve ışık taşıma (3 hafta)
- **Örnekleyici:** Owen karıştırmalı Sobol (Burley 2020) ya da ZSobol. Piksel başına dekorelasyon, mavi gürültü dağılımı. `StratifiedSampler` yedek olarak kalır.
- **Malzeme–integratör sözleşmesi:**
  - `BSDFSample {f, wi, pdf, eta, flags}`; kosinüsü malzeme sahiplenir.
  - Her isabette malzeme tek kez çözümlenir (`material.h:29`, `path_tracer.cpp:117,196`).
  - Aynı kökten gelen üç bulgu birlikte düzelir: lighting-5, materials-5, materials-6.
  - NEE iletimde |cos| kullanır.
- **Işık seçimi:**
  - Güce orantılı alias tablosu; pmf MIS ağırlıklarına girer (`path_tracer.cpp:122,167`).
  - Emitter pdf'i O(1) olur: `SurfaceInteraction`'a `primId`/`lightId` (`area_light.cpp:129`).
- **Işık örnekleme:**
  - Softbox'lar için küresel dikdörtgen örnekleme (Ureña 2013), mesh ışıkları için küresel üçgen (Arvo).
- **Ortam ışığı:**
  - pdf ile eval tutarlı hale gelir (`environment_light.cpp:120-146`).
  - MIS telafisi (Karlík 2019).
  - Kutupta sarma hatası (`image.cpp:41`) ve yatay ayna kontrolü.
- **Sağlamlık:**
  - Rus ruleti en büyük bileşene göre, ayarlanabilir minimum derinlikle.
  - Wächter–Binder ışın ofseti: geometrik normal boyunca, tMin = 0.
  - Gölge terminatörü düzeltmesi (Hanika 2021).
  - Karo başına göreli hatayla adaptif örnekleme.
- **Öğrenme:** PBRT bölüm 8, 12, 13–14; Veach tezi bölüm 9; TU Wien dersleri.
- **Senin yazacağın:** pmf'li güç sezgisel ağırlığı ve alias tablosu kurulumu.
- **Kanıt:**
  - Eşit sürede varyans azalır: Sobol, yakın softbox, çok ışıklı stüdyo sahnelerinde.
  - Fırın ve chi-kare testleri yeşil.
  - Ölçek testi: aynı sahne 0.01× ve 100× ölçekte aynı görüntüyü verir.

### Faz 6 — Ham hız: hızlandırma yapıları (3 hafta)
- **Kendi yalın çekirdeğin** (öğrenme ve referans için):
  - Yaprak sıralı üçgen tamponu (v0, e1, e2).
  - Kesişim testi yalnız (t, u, v, primId) döner; `SurfaceInteraction` isabetten sonra bir kez kurulur (`mesh.cpp:71-89`, `triangle.cpp:37-82`).
  - Bölmesiz slab testi (`aabb.h:54`).
  - Watertight kesişim (Woop 2013).
  - Yakın/uzak çocuk ayıklama ve sabit boyutlu yığın (`bvh.cpp:217-256`).
  - Paralel binned SAH kurulumu.
- **İki seviyeli yapı:**
  - Mesh başına BLAS, içe aktarmada bir kez kurulur; örnekler üzerinde TLAS.
  - Dönüşüm düzenlemesi yalnız TLAS'ı yeniden kurar.
  - Vertex kaynaklama: yükleyiciler bugün de-index ediyor (`obj_loader.cpp:182`, `gltf_loader.cpp:300`).
  - Baked kopyalar kalkar.
- **Embree 4.4** (vcpkg, Apache-2.0):
  - Varsayılan özellikler kapalı; triangle, instance ve filter-function açık. TBB kullanılmaz.
  - `PHOTON_USE_EMBREE` bayrağıyla.
  - `bvh.cpp` test referansı olarak kalır.
- **Öğrenme:** Bikker'ın "How to build a BVH" serisi (1–9. bölümler), PBRT bölüm 7, *Ray Tracing: The Next Week*.
- **Senin yazacağın:** SAH bölme seçimi ve watertight testin kesme (shear) adımı.
- **Kanıt:**
  - Her adımda Mrays/s `BASELINE.md`'ye yazılır. Tahmin: yalın çekirdek 2–4×, Embree ile toplam 3–8×.
  - BVH sonucu rastgele sahnede kaba kuvvetle eşleşir.
  - 1M üçgenli içe aktarma süresi ölçülür.

### Faz 7 — Malzeme ve renk gerçekçiliği (4–5 hafta)
- **GGX:** VNDF örnekleme (Heitz 2018), yükseklik-korelasyonlu Smith G2, albedoya göre lob seçimi.
- **Enerji:**
  - Kulla–Conty çoklu saçılma telafisi; pürüzlü metal artık kararmaz.
  - F82-tint iletken Fresnel ve metal presetleri.
  - Enerji koruyan kaplama; LTC sheen.
- **Cam rengi:** Beer–Lambert soğurma ve ortam takibi; renk kalınlığa göre değişir.
- **Dokular:**
  - Depolama 8-bit + LUT (bugün texel başına 16 B).
  - Mipmap ve ışın konisi (RTG bölüm 20).
  - MikkTSpace ya da glTF `TANGENT`.
  - Faktör × doku çarpımı, ORM kanal seçimi (`disney.cpp:274`), alfa/kesik.
- **glTF:**
  - Uzantılar: transmission, ior, volume, clearcoat, sheen, specular, emissive_strength, texture_transform.
  - Aynı doku tekrar tekrar decode edilmez (`gltf_loader.cpp:197,230`).
- **OpenPBR Surface'e geçiş.** Disney test referansı olarak kalır. Her lob fırın ve chi-kare kapısından geçer.
- **Yuvarlatılmış kenar** (bevel) gölgelendiricisi: CAD ürünlerinin gerçek görünümü için.
- **Öğrenme:** PBRT bölüm 9–10; SIGGRAPH "Physically Based Shading" notları; Heitz 2018; OpenPBR şartnamesi.
- **Senin yazacağın:** VNDF örnekleme (~10 satır) ve Kulla–Conty telafi terimi.
- **Kanıt:**
  - Beyaz pürüzlü metal her pürüzlülükte ≈ 1 (fırın testi).
  - Ortak malzemelerde Mitsuba relMSE %1'in altında.

### Faz 8 — Rasterizasyon izi: gerçek zamanlı yarı (2–3 hafta)
- `gl_preview.cpp`'de:
  - split-sum IBL: ön filtrelenmiş GGX küp haritası, irradiance, BRDF LUT,
  - PCF'li bir gölge haritası ve MSAA,
  - doku, emisyon ve cam yaklaşımı,
  - seçim konturu,
  - path tracer ile aynı ton eşleme.
- 24 spp'deki raster→path-trace geçişindeki sıçrama kalkar.
- **Öğrenme:** LearnOpenGL (PBR, IBL, shadow mapping), *Real-Time Rendering* 4. baskı, Cem Yüksel dersleri.
- **Senin yazacağın:** BRDF LUT integrali (split-sum'un ikinci terimi).
- **Kanıt:**
  - Raster ve path-traced görüntü yan yana, fark ısı haritasıyla.
  - 1080p'de 120 fps'nin üstü.

### Faz 9 — GPU: GTX 1080'de CUDA (6–10 hafta)
- **Araçlar:** CUDA 12.x sabitlenir. VS 18 host derleyici uyumu doğrulanır; gerekirse v143 araç seti yan yana kurulur.
- **Paylaşılan çekirdek:** `PHOTON_HD` makrosu math, Color3f, örnekleyici, BSDF ve ışık örnekleme kodunu CPU ile GPU arasında paylaştırır. Sanal çağrı yerine `switch`.
- **Hızlandırma:**
  - SoA cihaz tamponları.
  - TinyBVH (MIT) CWBVH'yi CPU'da kurar; gezinme çekirdeği CUDA'ya taşınır.
  - Önce megakernel; profil diverjans gösterirse wavefront (Laine 2013).
- **Entegrasyon:**
  - CUDA–GL interop ile viewport.
  - `RenderDevice {upload, update(delta), renderPass, cancel}` arayüzü.
  - CPU cihazı tam işlevli yedek olarak kalır.
- **Doğruluk:** CPU–GPU eşleşme testi. RTX gelince gezinme OptiX'e geçer, çekirdekler aynı kalır.
- **Öğrenme:** CUDA Programming Guide; "Accelerated Ray Tracing in One Weekend in CUDA"; Ylitie 2017 (CWBVH).
- **Kanıt:**
  - GPU bench sonucu (tahmin: bugünkü CPU'nun 10–50 katı).
  - Eşleşme testi yeşil.

### Faz 10 — İleri konular (seçmeli)
- Hacimler ve random-walk SSS.
- Path guiding (Open PGL, Apache).
- Spektral mod ve dispersiyon.
- MNEE (cam arkası kostikler).
- Fiziksel kamera (f-stop, enstantane, ISO; sensör uyumu `orthographic_camera.h:12`) ve bokeh şekli.
- AOV'ler, ışık grupları, shadow catcher ve çok katmanlı EXR (compositing'e köprü).
- Animasyon: turntable, anahtar kare, hareket bulanıklığı.

**Ürünleşme eki (karar bekliyor):** EULA, lisans anahtarı, kod imzalama, kurulum paketi, Linux CI ve paketleme, ağ render, STEP/USD/FBX, Python API, KeyShot karşılaştırması, patent incelemesi. Şimdiden geçerli tek kural: GPL bağımlılık yok.

## Öğrenme müfredatı: CG/CGI pipeline'ı → bu motor

| Aşama | Motorda nerede | Faz | Ana kaynak |
|---|---|---|---|
| Matematik (vektör, matris, dönüşüm, quaternion) | `core/math` | 1 | Scratchapixel, CMU 15-462 (Keenan Crane) |
| Kamera ve görüntü oluşumu, film, filtre | `camera/`, `renderer.cpp`, Film | 1, 10 | PBRT bölüm 5 |
| Işın-geometri kesişimi ve hızlandırma | `geometry/` | 6 | RTiOW 1–2, Bikker BVH serisi, PBRT bölüm 6–7 |
| Monte Carlo ve örnekleme (QMC, Sobol) | `samplers/`, `sampling.h` | 2, 5 | PBRT bölüm 2 ve 8, Burley 2020 |
| Işık taşıma (render denklemi, NEE, MIS) | `integrators/` | 5 | Veach tezi, PBRT bölüm 13–14, TU Wien |
| Malzemeler / BSDF | `materials/` | 1, 7 | PBRT bölüm 9–10, SIGGRAPH PBS notları, OpenPBR |
| Işıklar ve ortam | `lights/` | 5 | PBRT bölüm 12 |
| Doku, UV, filtreleme | Image/Texture, glTF | 7 | PBRT bölüm 10, RTG bölüm 20, xatlas |
| Renk bilimi (lineer iş akışı, OETF, AgX) | `core/image`, GPU ton eşleme | 1, 3 | *Cinematic Color*, Khronos PBR Neutral |
| Gürültü giderme | `engine/denoiser` | 3 | OIDN belgeleri |
| Rasterizasyon ve gerçek zamanlı GPU | `preview/gl_preview` | 8 | LearnOpenGL, RTR4, Cem Yüksel |
| Paralellik ve GPU programlama | thread havuzu, CUDA | 3, 9 | CUDA Guide, Laine 2013, Ylitie 2017 |
| Sahne tanımı ve varlık pipeline'ı (glTF, USD) | `scene/`, `io/` | 2, 7 | glTF 2.0 şartnamesi, OpenUSD belgeleri |
| Geometri işleme (normal, kaynaklama, bevel) | `io/`, `geometry/` | 6, 7 | Crane DDG (15-458) |
| Compositing (AOV, premultiplied alfa) | AOV/EXR | 10 | Natron belgeleri |
| Animasyon | turntable | 10 | CMU 15-462, Orange Duck blogu |

**Uçtan uca CGI pratiği.** Faz 4, 7 ve 10'un sonunda tekrarlanır; her seferinde aynı ürün, ilerleme ölçülür:
1. Blender'da basit bir ürün modelle (şişe ya da kulaklık).
2. UV aç ve dokula.
3. glTF olarak dışa aktar.
4. PhotonEngine'de look-dev ve ışıklandırma yap.
5. AOV'leri Natron'da birleştir.

## Yeniden kullanılacak mevcut parçalar
- `fresnelDielectric` (`utils.h:183`), Duff ONB (`frame.h:24`), örnekleme warp'ları (`sampling.h`), PCG32 (`rng.h`; `advance()` eklenecek).
- `linearToSRGB` (`spectrum.h:85-90`) ve `getAveragedPixel` (export için).
- `ScriptedSampler` test düzeneği (`test_area_light.cpp:20-43`): fırın ve chi-kare testlerinin temeli.
- Bit-testli `finiteFloat` (`obj_loader.cpp:64-69`); `/fp:fast` bunu katlayamaz.
- `loadSampleScene` içindeki AABB yürüyüşü ve `placeGroundUnder` (`application.cpp:428-454`): içe aktarmada otomatik kadraj için.
- OIDN CMake kancası ve AOV yolu (`engine/CMakeLists.txt:21-28`, `denoiser.cpp:89-147`).
- ASan preset'i (`CMakePresets.json`), undo yığını (`m_state.undo`).
- GLPreview'ün GGX/IBL shader'ı (küçük resimler ve Faz 8 için), ImGui 1.92'nin `BeginViewportSideBar`'ı.
- `bvh.cpp`: Embree geldiğinde referans test olarak kalır.

## Yeni bağımlılıklar (hepsi ticari dostu; uygulama sırasında vcpkg'de varlıkları doğrulanır)

| Bağımlılık | Lisans | Nereden | Faz |
|---|---|---|---|
| nlohmann-json | MIT | vcpkg | 1 |
| Tracy | BSD-3 | vcpkg | 2 |
| OIDN 2.x | Apache-2.0 | resmi derlenmiş paket (vcpkg'de yok) | 3 |
| ImGuizmo | MIT | vcpkg | 4 |
| Inter, JetBrains Mono / Lucide | OFL / ISC | depoya gömülü | 4 |
| nativefiledialog-extended | zlib | vcpkg (UTF-8 diyaloglar) | 1 veya 4 |
| Embree 4.4 | Apache-2.0 | vcpkg | 6 |
| TinyBVH | MIT | FetchContent (sabit commit) | 9 |
| CUDA 12.x | NVIDIA EULA | NVIDIA | 9 |

## Doğrulama
- **Her faz sonunda:**
  - `cmake --preset release` ve ardından `ctest --preset release`.
  - CLI ile bench; `bench/BASELINE.md` güncellenir.
  - ASan preset'iyle betikli uygulama senaryosu (Faz 1'den sonra her fazda).
  - `photon_app --smoke` (Faz 2'den itibaren).
- **Faz 1 testleri:** `test_export_roundtrip`, `test_snell`, `test_texture_colorspace`.
- **Faz 2 testleri:** `test_furnace`, `test_chi2`, `test_determinism`, golden görüntüler.
- **Görsel:** `docs/screenshots/` altında önce/sonra görüntüleri. Faz 7'de Mitsuba relMSE.
- **Etkileşim (Faz 3):** UI fps ve malzeme tıklaması gecikmesi `stats.json`/HUD'dan okunur.
- **Kullanım (Faz 4):** görev süresi testi.

## Kritik dosyalar
- `src/app/application.cpp`
- `src/engine/scene.{h,cpp}`, `src/engine/renderer.cpp`
- `src/scene/scene_graph.cpp`, `src/scene/project_io.cpp`
- `src/core/image/{image,image_io,tone_mapping}.cpp`
- `src/materials/{dielectric,disney}.cpp`, `src/core/math/utils.h`
- `src/integrators/path_tracer.cpp`
- `src/lights/{environment_light,area_light}.cpp`
- `src/samplers/stratified_sampler.cpp`
- `src/geometry/{bvh,mesh,triangle}.cpp`, `src/core/math/aabb.h`
- `src/core/threading/{parallel.h,thread_pool.cpp}`
- `src/io/gltf_loader.cpp`
- `src/ui/{theme,orbit_camera}.cpp`, `src/preview/{gl_preview,viewport_texture}.cpp`
- `src/main.cpp`
- `CMakeLists.txt`, `vcpkg.json`, `.gitignore`
- `docs/ROADMAP.md`, `README.md`

## Riskler ve açık noktalar
- Tüm hızlanma rakamları tahmin. Faz 2'deki gerçek ölçümlerden sonra Faz 3–7'nin sırası yeniden değerlendirilir.
- CUDA 12.x ile VS 18 (MSVC 14.5x) host derleyici uyumu doğrulanmadı.
- OIDN, TBB DLL'iyle birlikte dağıtılır; DLL'lerin `photon_app.exe` yanına kopyalanması gerekir.
- `C:\vcpkg` eksik. Mevcut `build-vcpkg/` önbelleği yeniden yapılandırmaya kadar çalışabilir, ama Faz 0'da düzeltilmeli.
- Her fazda kilit algoritma parçası `TODO(human)` olarak bırakılır. Sen yazana kadar o madde bekler; bu öğrenme hedefinin bilinçli bir maliyeti.
