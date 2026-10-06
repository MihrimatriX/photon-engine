# Dosya başvurusu

Her kaynak dosyanın başındaki açıklama yorumundan üretildi (`scripts/gen_file_reference.sh`).
Ayrıntı için dosyanın kendisine bakın.

### `src/app/app_state.h`

app_state.h — Uygulamanın tüm durumu: belge (sahne, ışıklar, ortam), görünüm
(kamera, render ayarları), seçim, kütüphaneler ve arayüz bayrakları.

"Belge" kaydedilen ve geri alınabilen kısımdır (DocSnapshot). Görünüm ve arayüz
durumu geri al geçmişine girmez (kamera hariç tutulur: KeyShot'ta da kamera
hareketi geri alınmaz, kamera kayıtları ayrıdır).

### `src/app/application.cpp`

application.cpp — Pencere/ImGui kurulumu, ana döngü, kısayollar, sürükle-bırak,
kütüphane taraması ve otomatik ekran görüntüsü modu.

### `src/app/application.h`

application.h — Masaüstü uygulamasının ana sınıfı.

Uygulama tek bir sınıf, ama kodu sorumluluklarına göre birkaç dosyaya bölünmüş:
  application.cpp   pencere, ImGui, ana döngü, kısayollar, ekran görüntüsü
  scene_ops.cpp     belge işlemleri: içe aktarma, malzeme/ortam/stüdyo uygulama,
                    ışıklar, seçim, geri al, sahne derleme, proje kaydet/aç
  final_render.cpp  son render ve turntable (arka plan iş parçacığı)
  ui_*.cpp          paneller: menü, kütüphane, viewport, sahne, özellikler, render
Hepsi aynı AppState'i paylaşır; paneller yalnız UI thread'inde çalışır.

### `src/app/final_render.cpp`

final_render.cpp — Dosyaya son render ve turntable (360° kare dizisi).

Son render, viewport'tan bağımsız bir iş parçacığında, malzemeleri kopyalanmış
(izole) bir sahneyle çalışır: kullanıcı render sürerken düzenleme yapsa bile
çıktı etkilenmez. Viewport bu sırada duraklatılır ki tüm çekirdekler render'a
gitsin. Örnek sayısı ya da süre sınırına ulaşınca OIDN (yüksek kalite) ile
gürültü giderilir ve seçilen biçimde kaydedilir.

### `src/app/main_app.cpp`

main_app.cpp — photon_app giriş noktası: komut satırı seçenekleri ve hata kutusu.

  photon_app [dosya.photon | model.obj]
  photon_app --scene sample|cornell|empty
  photon_app --screenshot cikti.png [--spp 16] [--size 1600x940] [--ui render]
--screenshot: arayüzü çizer, viewport en az --spp örneğe ulaşınca pencerenin
ekran görüntüsünü alır ve çıkar (otomatik görsel kontrol için).

### `src/app/render_controller.cpp`

render_controller.cpp — Viewport render thread'inin döngüsü ve yayınlama mantığı.

### `src/app/render_controller.h`

render_controller.h — Viewport'un arka planda sürekli çalışan ilerlemeli renderer'ı.

UI thread'i hiçbir zaman bir render pass'ini beklemez. Akış:
  1. UI sahneyi derler ve setScene() ile değişmez bir Scene (shared_ptr) verir.
     Render thread'i eski sahneyi pass bitene kadar tutar; bellek güvenli kalır.
  2. Kamera ya da ayar değişince restart(): birikim sıfırlanır.
  3. Kullanıcı sürüklerken "etkileşimli" mod: görüntü 1/2–1/8 çözünürlükte
     render edilir (çözünürlük merdiveni) ve her kare OIDN ile temizlenir.
     200 ms hareketsizlikten sonra tam çözünürlüğe geçilir.
  4. Her pass sonunda HDR görüntü çözülür, gerekirse gürültüsü giderilir, ton
     eşlenir ve RGBA8 olarak yayınlanır; UI yalnız kısa bir kilitle alır.
Malzeme gibi yerinde değiştirilen nesneler için lockScene()/edit(): mevcut
pass iptal edilir ve thread boşa çıkana kadar (en fazla bir satır) beklenir.

### `src/app/scene_ops.cpp`

scene_ops.cpp — Belge üzerindeki tüm işlemler: sahne derleme, geri al/yinele,
seçim, model içe aktarma, malzeme/ortam/stüdyo uygulama, ışıklar, kamera,
proje kaydetme/açma ve viewport görüntüsünü dışa aktarma.

### `src/app/thumbnails.cpp`

thumbnails.cpp — Küçük resimlerin arka planda render edilmesi ve GL'ye yüklenmesi.

### `src/app/thumbnails.h`

thumbnails.h — Kütüphane küçük resimleri: malzeme küreleri ve HDRI önizlemeleri.

Malzeme küçük resimleri motorun kendisiyle (path tracer + OIDN) bir stüdyo
ortamında render edilir; kütüphanede görünen küre son render'la birebir aynı
fiziği kullanır. İş arka plan thread'inde yapılır, sonuçlar diske (kullanıcı
önbellek klasörü) yazılır: ikinci açılışta anında yüklenir. OpenGL dokuları
yalnız UI thread'inde (uploadReady) oluşturulur.

### `src/app/ui_common.cpp`

ui_common.cpp — Panel başlığı, küçük resim kartı ve TRS ayrıştırma/birleştirme.

### `src/app/ui_common.h`

ui_common.h — Panellerin paylaştığı küçük yardımcılar: panel başlığı, küçük
resim kartı, dönüşümü konum/dönüş/ölçeğe ayırma ve geri birleştirme.

### `src/app/ui_layout.cpp`

ui_layout.cpp — Ana pencere düzeni: menü çubuğu, panel yerleşimi (docking),
alt durum çubuğu ve kısayollar/hakkında pencereleri.

### `src/app/ui_library.cpp`

ui_library.cpp — Sol panel: malzeme, ortam (HDRI), stüdyo, model ve doku kütüphaneleri.

Her öğe sürüklenebilir (ImGui drag & drop yükü olarak kimliğini taşır) ve
viewport'a ya da sahne ağacına bırakılabilir. Malzemeler çift tıkla seçili
parçaya uygulanır.

### `src/app/ui_properties.cpp`

ui_properties.cpp — Sağ alt panel: seçime ve sahneye ait tüm ayarlar.

Sekmeler: Nesne (dönüşüm), Malzeme, Ortam (HDRI, arka plan, zemin), Işıklar,
Kamera (lens, alan derinliği) ve Görüntü (ton eşleme, pozlama, önizleme).

Düzenleme kuralları:
  * Malzeme parametreleri render thread'inin okuduğu nesnede YERİNDE değişir:
    editLive() mevcut pass'i iptal edip sahne kilidini alır (bkz. RenderController).
  * Işık/ortam/dönüşüm değişiklikleri belgeyi değiştirir; sahne yeniden derlenir.
  * Geri al: sürekli düzenlemeler (kaydırıcı sürükleme) tek adımda birleşir.

### `src/app/ui_render.cpp`

ui_render.cpp — "Render" penceresi: çıktı çözünürlüğü, kalite, biçim, dosya yolu,
canlı ilerleme önizlemesi ve turntable kare dizisi.

### `src/app/ui_scene.cpp`

ui_scene.cpp — Sağ üst panel: sahne ağacı (modeller ve parçalar), ışıklar ve
ortam/zemin girdileri. Görünürlük gözü, sağ tık menüsü, çift tıkla yeniden
adlandırma ve malzemeyi ağaçtaki bir düğüme bırakma burada.

### `src/app/ui_viewport.cpp`

ui_viewport.cpp — Ortadaki render görünümü.

İçerik: path-traced görüntü (şeffaf modda dama deseni üstünde), fare ile
kamera kontrolü (orbit/pan/zoom), tıklayarak seçme, çift tıkla pivot,
seçili parçanın 3B sınır kutusu, ImGuizmo ile taşı/döndür/ölçekle, sağ altta
yön küpü, üstte araç çubuğu ve bilgi rozeti (HUD), boş sahnede karşılama
kartı, kütüphaneden sürükle-bırak hedefi.

### `src/camera/camera.h`

camera.h — Soyut kamera arayüzü: ekran koordinatı (u, v) → dünya uzayında birincil ışın.
Renderer her piksel örneği için raster (x+jitter, y+jitter) noktasını [0,1]² ekran
koordinatına çevirir (y ters çevrilir: raster üst-sol, ekran alt-sol köken) ve buraya verir.
Somut kameralar: iğne deliği (perspective), ince mercek (thin lens), ortografik.

### `src/camera/orthographic_camera.cpp`

orthographic_camera.cpp — Ortografik kamera: tüm ışınlar paralel, kökenleri değişir.
Perspektif bozulma yoktur (uzak nesne küçülmez); teknik/ürün çizimleri için uygundur.

### `src/camera/orthographic_camera.h`

orthographic_camera.h — Ortografik (paralel ışınlı) kamera ve odak uzunluğu ↔ görüş açısı
dönüşümleri (35 mm tam kare sensör varsayımıyla).

### `src/camera/perspective_camera.cpp`

perspective_camera.cpp — İğne deliği (pinhole) kamera: tüm ışınlar tek bir noktadan çıkar.
Her şey keskin odaktadır (alan derinliği yok). Ekran → ışın eşlemesi "sanal görüntü
düzlemi" ile yapılır (Shirley, "Ray Tracing in One Weekend" kamera kurgusu).

### `src/camera/perspective_camera.h`

perspective_camera.h — İdeal iğne deliği perspektif kamera (alan derinliği yok).
Açıklık > 0 ise renderer bunun yerine ThinLensCamera kullanır.

### `src/camera/thin_lens_camera.cpp`

thin_lens_camera.cpp — İnce mercek kamera: sonlu açıklık → alan derinliği (DoF, bokeh).
İnce mercek modelinde odak düzlemindeki bir nokta, merceğin neresinden geçerse geçsin
aynı sensör noktasına düşer. Bu yüzden: (1) odak düzleminde hedef noktayı bul,
(2) mercek diskinde rastgele bir nokta seç, (3) ışını mercek noktasından hedefe yolla.
Odak düzlemindeki nesneler keskin, ondan uzaklaştıkça bulanık çıkar (PBRT 4. baskı, 5.2.3).

### `src/camera/thin_lens_camera.h`

thin_lens_camera.h — İnce mercek yaklaşımıyla alan derinliği (DoF) olan perspektif kamera.
apertureRadius = mercek açıklığının yarıçapı (büyüdükçe bulanıklık artar),
focusDistance = keskin görünen düzlemin kameraya uzaklığı.

### `src/core/color/spectrum.h`

spectrum.h — Color3f: renderer'ın "spektrumu" olarak kullanılan doğrusal RGB üçlüsü.
Gerçek spektral (dalga boyu) render yerine 3 kanallı RGB ile çalışılır; radyans,
albedo ve yol "throughput"u bu türdedir. Çarpım kanal bazlıdır (renk filtresi gibi).

### `src/core/color/transfer.cpp`

transfer.cpp — sRGB aktarım eğrileri (doğrusal ↔ kodlanmış) ve 8-bit nicemleme + titreşim
(dither). PNG'ye yazarken ve ekrana gönderirken doğrusal ışık bu dosyadan geçer.

### `src/core/color/transfer.h`

transfer.h — sRGB aktarım fonksiyonları (encode/decode) ve 8-bit nicemleme + TPDF dither.
Renderer doğrusal ışıkla çalışır; 8-bit dosyalar ve ekran ise algısal olarak düzgün
(gamma benzeri) kodlama bekler. Bu iki dünya arasındaki köprü burasıdır.

### `src/core/image/film.cpp`

film.cpp — Film uygulaması: örnek ekleme (geçersizleri reddederek) ve ortalama çözümleme.

### `src/core/image/film.h`

film.h — Film: örnek biriktirici (fiziksel kameradaki film/sensörün karşılığı).
Her piksel için radyans toplamı ve örnek sayısı tutulur; piksel değeri = toplam / sayı,
yani Monte Carlo tahmincisinin ortalaması. resolve() bu ortalamayı Image'a çevirir.

### `src/core/image/image.cpp`

image.cpp — Image sınıfının uygulaması; en önemli kısmı sarmalayan (wrap) bilineer örnekleme.

### `src/core/image/image.h`

image.h — Düz float RGB görüntü: dokular, ortam (HDR) haritaları, çözülmüş render ve AOV'ler.
Bellek düzeni satır-öncelikli ve iç içe (interleaved) RGB: indeks = (y·w + x)·3.
Örnek birikimi burada değil, Film'de yapılır; Image yalnızca son/ham piksel değerlerini tutar.

### `src/core/image/image_io.cpp`

image_io.cpp — Görüntü dosyası G/Ç uygulaması: stb_image (PNG/JPG/HDR okuma),
stb_image_write (PNG/JPG yazma) ve tinyexr (EXR okuma/yazma). Bu dosya her üç tek-başlık
kütüphanenin uygulamasını (IMPLEMENTATION makroları) derleyen tek yerdir.
Akış (yazma): doğrusal radyans → pozlama (EV) → ton eşleme → sRGB → dither → 8-bit.
Narrow paths are UTF-8 on every platform; on Windows stb converts them to UTF-16.

### `src/core/image/image_io.h`

image_io.h — Görüntü okuma/yazma: PNG/JPG (8-bit, ton eşlenmiş), EXR (half float, doğrusal),
HDR/EXR/LDR yükleme. 8-bit dokular sRGB ya da doğrusal (veri) olarak çözülür.
Kural: renk dokuları sRGB kodludur ve doğrusala çevrilir; normal/pürüzlülük haritaları
"veri"dir, eğri uygulanmaz.

### `src/core/image/tone_mapping.cpp`

tone_mapping.cpp — Ton eşleme eğrilerinin uygulaması.

### `src/core/image/tone_mapping.h`

tone_mapping.h — HDR (sınırsız parlaklık) görüntüyü ekranın [0,1] aralığına sıkıştırma.

Path tracer fiziksel radyans üretir: güneşli bir pencere 1000, gölge 0.01 olabilir.
Ekran yalnız 0..1 gösterebildiği için bir "ton eşleme" eğrisi gerekir.
Sıra: pozlama (EV, 2^EV ile çarpım) → eğri → sRGB kodlama (OETF) → 8 bit.

### `src/core/math/aabb.h`

aabb.h — Eksen hizalı sınırlayıcı kutu (AABB).
BVH düğümlerinin sınırları, sahne sınırları ve SAH maliyet hesabı bu yapıyı kullanır.
Işın–kutu kesişimi "slab" (dilim) yöntemiyle yapılır (PBRT 4. baskı, bölüm 6.1.2).

### `src/core/math/constants.h`

constants.h — Derleme zamanı matematik sabitleri (π, 1/π, epsilon, derece↔radyan).
1/π ve 1/(2π) özellikle pdf'lerde sık geçer: örn. Lambert BRDF = albedo/π,
küre üzerinde düzgün pdf = 1/(4π).

### `src/core/math/float_bits.h`

float_bits.h — Hızlı-matematik (fast-math) derleme bayraklarında bile çalışan NaN/Inf testleri.
Örnek birikiminde tek bir NaN/Inf tüm pikseli bozar; bu yüzden Film bu testi kullanır.

### `src/core/math/frame.h`

frame.h — Ortonormal koordinat çerçevesi (s, t, n) = yerel "gölgeleme uzayı".
BSDF'ler hep yerel uzayda yazılır: normal = +Z, bu yüzden cosθ = w.z gibi formüller
basitleşir. Kesişim noktasında Frame kurulur; yön world→local→BSDF→world dolaşır.

### `src/core/math/mat.h`

mat.h — 4×4 matris (Mat4f): afin dönüşümler (öteleme/döndürme/ölçek), lookAt ve izdüşüm.
Transform sınıfı bu matrisi ve tersini birlikte saklar; noktalar/vektörler/normaller
sahne ↔ nesne uzayı arasında bu matrislerle taşınır.

### `src/core/math/quaternion.h`

quaternion.h — Birim kuaterniyon ile 3B döndürme (eksen-açı, Euler, matris, SLERP).
q = (sin(θ/2)·eksen, cos(θ/2)); v' = q·v·q* bir θ dönmesidir. Gimbal kilidi yoktur ve
iki yönelim arasında pürüzsüz ara değer (SLERP) almak kolaydır.

### `src/core/math/ray.h`

ray.h — Işın: P(t) = origin + t·direction, geçerli aralık [tMin, tMax].
Kesişim testleri tMax'ı en yakın isabete kısaltarak ilerler; tMin > 0 ise yüzeyden
çıkan ışının aynı yüzeye tekrar çarpmasını ("shadow acne") önler.

### `src/core/math/transform.cpp`

transform.cpp — Transform uygulaması: nokta, yön, normal, ışın ve AABB dönüşümleri.
Üç farklı nesne üç farklı kuralla dönüşür: nokta (w=1, ötelenir), yön (w=0, ötelenmez),
normal (ters-transpoz ile). Hepsi PBRT 4. baskı, bölüm 3.10'daki kurallardır.
/ @file transform.cpp
/ @brief Implementation of the Transform class for the PhotonEngine.

### `src/core/math/transform.h`

transform.h — Afin dönüşüm: matris + önceden hesaplanmış tersi birlikte saklanır.
Sahne grafiği ve model yükleyiciler mesh köşelerini/normallerini bununla dünya uzayına
taşır; normaller ters-transpoz gerektirdiğinden tersi önbelleğe almak maliyet kazandırır.

### `src/core/math/utils.h`

utils.h — Küçük matematik yardımcıları: clamp/lerp, ikinci derece denklem, MIS güç sezgiseli,
yansıma/kırılma vektörleri ve Fresnel denklemleri. Renderer'ın her yerinden kullanılır.

### `src/core/math/vec.cpp`

vec.cpp — vec.h için boş derleme birimi. Tüm vektör işlemleri başlıkta inline/constexpr;
bu dosya yalnızca derleme sistemine bir çapa sağlar.
/ @file vec.cpp
/ @brief Compilation unit for vec.h.
/
/ All Vec2f/Vec3f/Vec4f methods are inline or constexpr and live entirely
/ in the header. This .cpp exists as a build-system anchor so the
/ translation unit is compiled and any ODR-use of static constexpr
/ members is satisfied.

### `src/core/math/vec.h`

vec.h — 2B/3B/4B float vektörler (Vec2f, Vec3f, Vec4f). Renderer'ın temel taşı:
Vec3f hem konum/yön/normal hem de RGB renk (bileşen bazlı çarpım = renk filtresi)
olarak kullanılır. Vec4f homojen koordinatlar (w=1 nokta, w=0 yön) içindir.

### `src/core/memory/aligned.h`

aligned.h — Platformdan bağımsız hizalı bellek ayırma/serbest bırakma.
SIMD yüklemeleri ve önbellek satırı (64 bayt) hizası için kullanılır; Windows'ta
_aligned_malloc/_aligned_free, diğer sistemlerde std::aligned_alloc/free çiftidir.

### `src/core/memory/arena_allocator.cpp`

arena_allocator.cpp — Bölge (arena) ayırıcısının uygulaması.
Ayırma = işaretçiyi ileri kaydırmak (bump allocation); tek tek free yoktur,
reset() ile tüm bölge bir kerede yeniden kullanılır.

### `src/core/memory/arena_allocator.h`

arena_allocator.h — Bölge (arena) bellek ayırıcısı: büyük bloklardan ardışık ayırma.
Kısa ömürlü, birlikte ölen nesneler (örn. bir örnek/iş parçası boyunca geçici veriler)
için idealdir: ayırma O(1), toplu serbest bırakma reset() ile tek adımda (PBRT ScratchBuffer).

### `src/core/memory/pool_allocator.h`

pool_allocator.h — Sabit boyutlu nesne havuzu (T türünde yuvalar).
Boş yuvalar "gömülü serbest liste" (intrusive free list) ile bağlanır: boş bir yuvanın
kendi belleği bir sonraki boş yuvanın işaretçisini tutar, ek bellek gerekmez.
allocate/deallocate O(1); yapıcı/yıkıcı çağrılmaz (yalnızca ham bellek verir).

### `src/core/platform/path.h`

path.h — UTF-8 string ↔ std::filesystem::path dönüşümü.
Windows'ta std::string'den path kurmak ANSI kod sayfasıyla çözer ve Türkçe karakterli
yolları ("Masaüstü/şişe.obj") bozar; u8string üzerinden geçmek bunu engeller.

### `src/core/random/rng.h`

rng.h — PCG32 sözde-rastgele sayı üreteci (O'Neill 2014, XSH-RR çıkışı). Hızlı, küçük
durumlu ve tohumlanabilir; örnekleyiciler ve testler bunu kullanır.

### `src/core/sampling/sampling.h`

sampling.h — Monte Carlo örnekleme dönüşümleri ("warp"): [0,1)² düzgün sayıları küre,
yarıküre, disk ve üçgen üzerine eşler; her birinin pdf'i de yanında verilir.
Yöntem: ters CDF (inversion) — hedef dağılımın kümülatifini tersine çevirmek
(PBRT 4. baskı, bölüm A.5 ve 2.3). Tahminci f(x)/pdf(x) olduğundan pdf doğru olmalı.

### `src/core/threading/parallel.h`

parallel.h — Görüntüyü karolara bölüp thread havuzunda paralel işleyen yardımcılar.

parallelFor2D, W×H görüntüyü tileSize×tileSize karolara böler ve her karoyu
havuza bir iş olarak verir. İsteğe bağlı `cancel` bayrağı set edilince henüz
başlamamış karolar hemen döner; böylece kamera hareket ettiğinde eski pass
bir karo süresi içinde (milisaniyeler) durur.

### `src/core/threading/thread_pool.cpp`

thread_pool.cpp — ThreadPool'un işçi döngüsü, kapatma ve waitAll uygulaması.

### `src/core/threading/thread_pool.h`

thread_pool.h — Sabit sayıda işçi thread'i olan basit görev havuzu.

Render karoları (tile) bu havuza iş olarak atılır. Her işçi kuyruktan bir
görev alır ve çalıştırır. submit() bir std::future döndürür; çağıran taraf
future.get() ile işin bitmesini bekler ve işteki istisnayı (exception) alır.

### `src/engine/denoiser.cpp`

denoiser.cpp — OIDN sarmalayıcısı ve OIDN yoksa kullanılan yumuşatma yedeği.

### `src/engine/denoiser.h`

denoiser.h — Intel Open Image Denoise (OIDN) ile gürültü giderme.

Path tracing az örnekle gürültülü görüntü üretir. OIDN, yapay sinir ağıyla bu
gürültüyü temizler; albedo ve normal kanalları (AOV) verilirse dokuları ve
kenarları çok daha iyi korur. OIDN derlemede yoksa yalnız 3×3 yumuşatma
uygulanır (gerçek bir gürültü giderici değildir; arayüz bunu açıkça söyler).

### `src/engine/embree_accel.cpp`

embree_accel.cpp — Sahne şekillerini Embree geometrilerine çevirme ve ışın sorguları.

### `src/engine/embree_accel.h`

embree_accel.h — Intel Embree 4 ile ışın-sahne kesişimi (üretim hızlandırıcısı).

Embree, SIMD (AVX2) ile aynı anda birçok kutu/üçgen test eden, çok iyi optimize
edilmiş bir BVH kütüphanesidir. Motorun kendi BVH'si (geometry/bvh.*) öğrenme ve
test referansı olarak kalır; Embree derlemede bulunursa Scene onu kullanır.

Embree yalnız "ışın neye, hangi t'de, hangi barisentrik (u,v) ile çarptı" sorusunu
cevaplar. Normal, UV ve teğet gibi gölgelendirme verisi motorun kendi kodunda
(Triangle::fillHit) hesaplanır; böylece iki hızlandırıcı aynı görüntüyü üretir.

### `src/engine/render_settings.h`

render_settings.h — Render kalite ve çıktı ayarları: çözünürlük, örnek sayısı, sekme
sayısı, ton eşleme, pozlama, gürültü giderme. Viewport ve son render aynı yapıyı kullanır.

### `src/engine/renderer.cpp`

renderer.cpp — Piksel örnekleme, karo dağıtımı, iptal ve gürültü giderme akışı.

### `src/engine/renderer.h`

renderer.h — Karo tabanlı, çok iş parçacıklı render sürücüsü.

Renderer kameradan her piksel için ışın üretir, integrator'e (path tracer)
verir ve sonucu Film'e biriktirir. İki çalışma biçimi var:
  * render():            karo karo, her karonun TÜM örnekleri (CLI, turntable)
  * renderSamplePass():  tüm görüntüye piksel başına 1 örnek (ilerlemeli viewport
                         ve son render). Her pass görüntüyü biraz daha temizler.
Pass'ler `cancel` bayrağıyla karo düzeyinde durdurulabilir.

### `src/engine/scene.cpp`

scene.cpp — Render sahnesine şekil/ışık ekleme ve BVH üzerinden kesişim sorguları.

### `src/engine/scene.h`

scene.h — Render'a hazır, "düzleştirilmiş" sahne: dünya uzayına bake edilmiş
geometri, ışıklar, ortam ışığı ve tek bir BVH hızlandırma yapısı.

Düzenlenebilir sahne ağacı (SceneGraph) her değişiklikte bu yapıya derlenir.
Render thread'i yalnız bu nesneyi okur. Scene, referans verdiği malzemeleri
shared_ptr ile tutar; böylece UI bir malzemeyi silse bile render sürerken
bellek serbest kalmaz.

### `src/geometry/bvh.cpp`

bvh.cpp — BVH yapımı (12 kovalı SAH, düzleştirilmiş 32 baytlık düğümler) ve gezinme:
en yakın isabet için yakın-çocuk-önce sıralı gezinme, gölge ışınları için erken çıkış.

### `src/geometry/bvh.h`

bvh.h — Sınırlayıcı hacim hiyerarşisi (BVH): ışın-sahne kesişimini O(n) yerine
~O(log n) yapan ağaç. Kutular iç içedir; ışın bir kutuya çarpmıyorsa içindeki
hiçbir üçgen test edilmez. Yapım: 12 kovalı SAH (yüzey alanı sezgiseli).
Embree yoksa ve testlerde kullanılan, motorun kendi (öğrenme amaçlı) hızlandırıcısı.

### `src/geometry/mesh.cpp`

mesh.cpp — TriangleMesh: sınırlar, eksik normallerin hesaplanması, tek üçgen kesişimi
ve dışarıda (Embree) bulunan isabet için yüzey verisinin doldurulması.

### `src/geometry/mesh.h`

mesh.h — Üçgen ağı (TriangleMesh): köşe konumları, normaller, UV'ler ve indeks tamponu.
BVH ve Embree tek tek üçgenlere (index) erişir; yığında ayrı Triangle nesnesi tutulmaz.

### `src/geometry/shape.h`

shape.h — Işınla kesişebilen tüm geometrilerin soyut arayüzü (Shape).
Küre, üçgen ve TriangleMesh bunu uygular; BVH yalnız bounds() ve intersect()'i görür.

### `src/geometry/sphere.cpp`

Analitik küre: ışın-küre kesişimi (ikinci derece denklem), dışa dönük normal,
küresel UV ve u yönündeki teğet.

### `src/geometry/sphere.h`

sphere.h — Analitik küre şekli: merkez + yarıçap, üçgenleştirmeden tam kesişim.

### `src/geometry/surface_interaction.h`

surface_interaction.h — Işın–yüzey kesişiminin kaydı (nokta, normaller, UV, malzeme).
Şekiller bunu doldurur; malzemeler ve integrator ışığın orada nasıl saçılacağını hesaplar.

### `src/geometry/triangle.cpp`

Tek üçgen ışın kesişimi (Möller–Trumbore 1997) ve kesişim noktasında normal, UV,
teğet enterpolasyonu. Mesh dışındaki bağımsız üçgenler (ör. alan ışığı quad'ları) bunu kullanır.

### `src/geometry/triangle.h`

triangle.h — Tek üçgen şekli. Mesh dışındaki bağımsız üçgenler (alan ışığı dörtgenleri)
bunu kullanır; mesh üçgenleri de isabet sonrası yüzey verisi için geçici olarak kurulur.

### `src/integrators/integrator.h`

integrator.h — Integrator arayüzü: bir kamera ışını boyunca gelen radyansı tahmin eder.
Render denklemi L_o = L_e + ∫ f · L_i · |cos θ| dω'yı Monte Carlo ile çözen sınıflar
(ör. PathTracer) bunu uygular. Renderer her piksel örneği için Li()'yi çağırıp ortalar.

### `src/integrators/path_tracer.cpp`

Çok sekmeli yol izleyici (path tracer): her yüzey noktasında ışık örneklemesi (NEE)
ve BSDF örneklemesi yapılır, ikisi Veach'in güç sezgiseli (MIS) ile birleştirilir.
Rus ruleti, NaN/Inf koruması ve isteğe bağlı kontak AO da burada.

### `src/integrators/path_tracer.h`

Yol izleyici integratörün arayüzü. maxDepth = en fazla saçılma (sekme) sayısı
(PBRT kuralı): 1 → yalnızca doğrudan ışık, 2 → bir sekme dolaylı ışık, ...

### `src/io/gltf_loader.cpp`

gltf_loader.cpp — cgltf ile glTF 2.0 okuma ve motor türlerine dönüştürme.
glTF, ikili tamponlara işaret eden bir JSON'dur: buffer → bufferView → accessor zinciri.
Bu zincirdeki her uzunluk/ofset güvenilmez girdi sayılıp burada doğrulanır.
Dönüşümler: düğüm matrisi (sütun-öncelikli) → Transform, konum/normal → dünya uzayı,
pbrMetallicRoughness → DisneyMaterial, baseColor dokusu → albedo görüntüsü.

### `src/io/gltf_loader.h`

gltf_loader.h — glTF 2.0 (.gltf / .glb) sahne yükleyicisinin arayüzü.
Düğüm ağacı dünya uzayına düzleştirilir; her üçgen primitive ayrı bir TriangleMesh olur,
PBR metallic-roughness malzemesi DisneyMaterial'e çevrilir.

### `src/io/obj_loader.cpp`

obj_loader.cpp — tinyobjloader üzerine güvenli OBJ okuyucu.
Dosya güvenilmez girdi sayılır: boyut sınırları, indeks doğrulaması ve sonlu sayı
(NaN/Inf olmayan) kontrolü burada yapılır; bozuk dosya yarım değil BOŞ sonuç döner.
Çokgenler üçgen yelpazesine bölünür, köşeler malzeme kimliğine göre kovalara ayrılır.

### `src/io/obj_loader.h`

obj_loader.h — Wavefront OBJ (+ MTL) model yükleyicisinin arayüzü.
Her OBJ şekli malzeme kimliğine göre ayrı TriangleMesh'lere bölünür; MTL'deki Kd
(dağınık renk) Lambertian malzemeye dönüşür. Sahne katmanındaki içe aktarma bunu çağırır.

### `src/lights/area_light.cpp`

area_light.cpp — Dikdörtgen ve mesh alan ışıklarının örneklenmesi ve pdf'i.

### `src/lights/area_light.h`

area_light.h — Alan ışıkları: dikdörtgen (AreaLight) ve yayıcı üçgen mesh (MeshLight).
Delta ışıkların aksine yüzeyleri vardır: ışınlar onlara çarpabilir, yumuşak gölge verirler.
Yüzeyde düzgün (uniform) alan örneklemesi yapılır, pdf katı açıya çevrilir. pdfLi aynı pdf'i
verir ki BSDF örneklemesi ışığa çarptığında MIS ağırlığı hesaplanabilsin.

### `src/lights/directional_light.cpp`

directional_light.cpp — Yönlü ışığın örneklenmesi.

### `src/lights/directional_light.h`

directional_light.h — Yönlü (güneş benzeri) ışık: sonsuz uzakta, paralel ışınlar.
Delta ışıktır: tek bir yönden gelir, konumu yoktur; gölge ışınları sonsuza kadar uzanır.

### `src/lights/environment_light.cpp`

environment_light.cpp — HDRI ortam ışığının örneklenmesi ve değerlendirilmesi.

### `src/lights/environment_light.h`

environment_light.h — Sonsuz uzaktaki ortam ışığı (HDRI / gökyüzü).

Equirectangular (enlem-boylam) bir HDR görüntü sahneyi her yönden aydınlatır.
Önem örnekleme (importance sampling) için görüntünün parlaklık × sin(θ)
dağılımından 2B bir CDF kurulur: parlak pencereler/güneş daha sık seçilir,
böylece aynı örnek sayısında çok daha az gürültü olur.

### `src/lights/light.h`

light.h — Tüm ışık kaynaklarının ortak arayüzü (Light) ve örnek sonucu (LightSample).
Path tracer her yüzey noktasında bir ışık seçip sampleLi ile ona doğru bir yön örnekler
(next-event estimation, NEE). Tahminci: f · Li · |cos θ| / pdf. Bu yüzden pdf'in KATI AÇI
(solid angle, sr⁻¹) cinsinden olması şart; alan ışıkları kendi alan pdf'lerini çevirip verir.

### `src/lights/point_light.cpp`

point_light.cpp — Nokta ışığın örneklenmesi ve toplam gücü.

### `src/lights/point_light.h`

point_light.h — Nokta ışık: tek bir noktadan her yöne eşit ışıyan delta ışık.
İdeal bir kavramdır (sonsuz küçük kaynak): keskin gölge verir, kameraya görünmez.

### `src/main.cpp`

main.cpp — photon_render: arayüzsüz (komut satırı) renderer ve ölçüm aracı.

  photon_render sahne.photon  [--out render.png] [--spp 256] [--res 1920x1080]
  photon_render model.obj     [...]               (stüdyo ışığı + otomatik kadraj)
  photon_render --cornell     [...]
Ek seçenekler: --threads N, --no-denoise, --bounces N, --stats stats.json
stats.json: süre, örnek sayısı, Mray/s (milyon birincil ışın / saniye).

### `src/materials/dielectric.cpp`

Cam malzemesinin uygulaması. Pürüzsüz cam: Fresnel olasılığıyla delta yansıma ya da
Snell kırılması. Pürüzlü cam: PBRT-v4 DielectricBxDF'nin birebir karşılığı
(genelleştirilmiş yarı vektör, mikro-yüzey TIR, Walter 2007 Jacobian'ı, VNDF örnekleme).
Hepsi yerel gölgeleme çerçevesinde hesaplanır: +z = dışarı bakan gölgeleme normali.

### `src/materials/dielectric.h`

Cam / şeffaf dielektrik malzeme. Pürüzsüz (roughness ≈ 0) hali delta yansıma +
kırılma; pürüzlü hali GGX mikro-yüzey BSDF'si (PBRT-v4 DielectricBxDF, Walter 2007).

### `src/materials/disney.cpp`

Basitleştirilmiş Disney Principled BRDF (Burley 2012/2015): Burley difüz, GGX
speküler (anizotrop olabilir), sheen, ince-yüzey difüz geçirgenlik ve enerjiyi
koruyan bir clearcoat katmanı. Örnekleme Heitz 2018 görünür normalleri (VNDF) ile.

### `src/materials/disney.h`

Disney Principled BRDF malzemesinin arayüzü: parametreler (metallic, roughness, specular,
clearcoat, sheen, anizotropi, difüz geçirgenlik) ve doku haritaları. Matematik disney.cpp'de.

### `src/materials/lambertian.cpp`

lambertian.cpp — Lambert BRDF'in örneklenmesi, değerlendirilmesi ve pdf'i.

### `src/materials/lambertian.h`

lambertian.h — Lambert (ideal mat/dağınık) malzeme: ışığı her yöne eşit saçar.
BRDF sabittir: f = albedo / π. İsteğe bağlı 'emission' ile yüzey ışık da yayabilir
(sahne yayıcı mesh'leri MeshLight olarak ışık listesine ekler).

### `src/materials/material.h`

Malzeme (BSDF) arayüzü: sample / eval / pdf / emitted. Tüm malzemeler bunu uygular.
Yön kuralı PBRT ile aynı: wo ve wi yüzeyden DIŞARI bakar, birim uzunluktadır.

### `src/materials/microfacet.h`

Trowbridge-Reitz (GGX) mikro-yüzey yardımcıları: dağılım D, Smith gölgeleme G
ve görünür normal (VNDF) örneklemesi. Disney ve Dielectric aynı kodu kullanır.
Tüm vektörler yerel gölgeleme çerçevesindedir: +z = yüzey normali.
Kaynaklar: Walter vd. 2007 (GGX), Heitz 2014 (Smith), Heitz 2018 (VNDF), PBRT-v4 §9.6.

### `src/materials/mirror.cpp`

mirror.cpp — Delta yansıma: wi = yansıma(wo), ağırlık = reflectance.

### `src/materials/mirror.h`

mirror.h — Mükemmel ayna: gelen ışık tek bir yöne (yansıma yönüne) gider.
BRDF bir Dirac deltasıdır: isDelta() true, eval/pdf 0 döner, ışık yalnız sample() ile izlenir.

### `src/samplers/independent_sampler.cpp`

independent_sampler.cpp — Bağımsız örnekleyici: her sayı PCG32'den doğrudan gelir.
En basit ve yansız seçenek; ama noktalar kümelenebilir/boşluk bırakabilir, bu yüzden
gürültü N örnekte ~1/√N hızında azalır (düşük tutarsızlıklı dizilerden daha yavaş).

### `src/samplers/independent_sampler.h`

independent_sampler.h — Bağımsız (beyaz gürültü) örnekleyici; referans/test için temel çizgi.

### `src/samplers/sampler.h`

sampler.h — Soyut örnekleyici arayüzü: Monte Carlo integrali için [0,1) sayıları üretir.
Bir piksel örneği bir "yol"dur ve her get1D/get2D çağrısı o yolun bir sonraki boyutunu
(piksel jitter, lens, BSDF yönü, ışık seçimi, Rus ruleti...) besler. İyi örnekleyici bu
boyutların her birinde noktaları düzgün yayarak aynı örnek sayısında daha az gürültü verir.
Kullanım: startPixel(x,y) → her örnek için startSample(i) → get1D/get2D çağrıları.

### `src/samplers/sobol_sampler.cpp`

sobol_sampler.cpp — Burley 2020 hash tabanlı Owen karıştırmalı Sobol örnekleyicisinin uygulaması.
Yalnızca Sobol'ün ilk iki boyutu kullanılır (bit ters çevirme + Pascal üçgeni
üreteci); daha yüksek boyutlar her çağrıda bağımsız karıştırma ile "dolgu" yapılır.

### `src/samplers/sobol_sampler.h`

sobol_sampler.h — Owen-karıştırmalı Sobol örnekleyici (Burley 2020, "Practical Hash-based Owen
Scrambling", JCGT 9(4)). Her piksel ve her boyut için bağımsız karıştırılmış,
ilerlemeli (progressive) render'da her ön-ek (1, 2, 4, ... örnek) iyi tabakalanmış
düşük-tutarsızlıklı (low-discrepancy) noktalar üretir. Durum yalnızca
startPixel/startSample ile belirlenir: aynı (piksel, örnek, boyut) hep aynı sayıyı verir.

### `src/samplers/stratified_sampler.cpp`

stratified_sampler.cpp — Tabakalı (jittered) örnekleme: [0,1)² alanı nx × ny eşit hücreye
(tabaka/stratum) bölünür; i. örnek i. hücreye düşer ve hücre içinde rastgele kaydırılır.
Böylece N örnek alanı kümelenmeden kaplar; varyans bağımsız örneklemeden asla büyük
olmaz, düzgün integrandlarda belirgin biçimde küçülür (PBRT 4. baskı, bölüm 8.5).

### `src/samplers/stratified_sampler.h`

stratified_sampler.h — Tabakalı (jittered) örnekleyici: alanı nx × ny hücreye bölüp her
hücreye bir örnek düşürür. Örnek sayısı nx·ny ile sabittir; sayı önceden bilinmeyen
ilerlemeli render'da Owen-karıştırmalı Sobol daha uygundur.

### `src/scene/cornell_box.h`

cornell_box.h — Klasik Cornell kutusu test sahnesini SceneGraph'a kuran yardımcılar.
555 birimlik kutu: kırmızı/yeşil yan duvarlar renk taşmasını (color bleeding), cam küre
kırılma ve kostikleri, altın küre metal BRDF'i, ayna kutu yansımaları sınar.

### `src/scene/document.cpp`

document.cpp — Işık/ortam tanımlarından render nesneleri ve stüdyo preset'leri.

### `src/scene/document.h`

document.h — Kullanıcının düzenlediği ışık ve ortam tanımları ("belge" verisi).

Arayüz Light/EnvironmentLight nesnelerini doğrudan değiştirmez; bu sade,
kopyalanabilir tanımları (LightDesc, EnvironmentDesc) düzenler. Her sahne
derlemesinde tanımlardan yeni, değişmez render nesneleri üretilir. Böylece
geri al, proje kaydetme ve render thread'i güvenliği kendiliğinden çözülür.

### `src/scene/material_library.cpp`

material_library.cpp — Preset JSON okuma/yazma ve preset → Material dönüşümü.

### `src/scene/material_library.h`

material_library.h — Hazır malzeme kütüphanesi (assets/materials/*.json).

Her JSON dosyası bir "preset"tir: ad, kategori, tür (genel/cam) ve fiziksel
parametreler. Kütüphane panelinde küçük resimleriyle listelenir; bir parçaya
sürüklenince createMaterial() ile gerçek bir Material nesnesine dönüşür.

### `src/scene/model_import.cpp`

model_import.cpp — OBJ (tinyobjloader) ve glTF (cgltf) yükleyicilerini sahne
ağacına bağlar.

### `src/scene/model_import.h`

model_import.h — OBJ/glTF dosyasını sahne ağacına bir grup düğümü olarak yükleme.

### `src/scene/project_io.cpp`

project_io.cpp — Proje dosyasının JSON şeması ve okuma/yazma kodu.

Şema (sürüm 2), kısaca:
  { "format": "photon-project", "version": 2,
    "camera": {...}, "render": {...},            // uygulamanın kendi alanları
    "environment": { "hdr", "rotation", "intensity", "background", "ground", ... },
    "lights": [ { "type", "color", "intensity", "position", ... } ],
    "materials": [ { "type": "disney"|"glass"|"lambert"|"mirror", ... } ],
    "nodes": [ { "name", "type", "visible", "xf": [16], "material": i,
                 "source": "model.obj", "sourceIndex": k, "geometry": {...},
                 "children": [...] } ] }
Aynı malzemeyi paylaşan düğümler "materials" tablosunda tek kayda işaret eder.

### `src/scene/project_io.h`

project_io.h — .photon proje dosyası (JSON) okuma/yazma.

Proje, sahnenin tam halini saklar: düğüm ağacı (dönüşüm, görünürlük, malzeme),
içe aktarılan modellerin kaynak yolları, yerinde oluşturulmuş geometri, ışıklar,
ortam ve uygulamanın kendi kamera/render ayarları (opak JSON olarak).
Yollar mümkünse proje dosyasına göre göreli yazılır; böylece klasör taşınabilir.

### `src/scene/scene_graph.cpp`

scene_graph.cpp — Sahne ağacı işlemleri (arama, silme) ve Scene'e derleme (bake).

### `src/scene/scene_graph.h`

scene_graph.h — Düzenlenebilir sahne ağacı (hiyerarşi) ve render sahnesine derleme.

Kullanıcının gördüğü "Sahne" paneli bu ağaçtır: gruplar, mesh'ler, küreler.
Her düğümün yerel dönüşümü (konum/dönüş/ölçek), malzemesi ve görünürlüğü var.
Render için compile() ağacı dolaşır, her mesh'i dünya uzayına "bake" eder
(köşeleri dönüştürür) ve düz bir Scene + BVH üretir.

### `src/scene/scene_node.h`

scene_node.h — Sahne ağacının tek bir düğümü (grup, mesh ya da küre).

Her düğümün hiç tekrar kullanılmayan bir kimliği (uid) vardır: seçim, geri al
ve viewport'ta tıklayarak seçme bu kimlikle çalışır; ham işaretçiler silinen
düğümlerde sarkık (dangling) kalacağı için saklanmaz.

### `src/scene/undo_stack.h`

undo_stack.h — Geri al / yinele yığını (anlık görüntü tabanlı).

Her düzenlemeden ÖNCE belgenin tam bir kopyası (sahne ağacı + ışıklar + ortam)
alınır. Geometri değişmez olduğu için kopya ucuzdur: mesh'ler paylaşılır,
yalnız malzemeler ve dönüşümler kopyalanır. Komut-tabanlı geri almaya göre
çok daha az hata yapar: silinen bir düğüme işaret eden komut kalmaz.

### `src/ui/drag_drop.h`

drag_drop.h — Sürükle-bırak yük (payload) türleri ve dosya uzantısı yardımcıları.
Kütüphaneden viewport'a sürüklenen öğe, türünü bu adlarla taşır (ImGui drag & drop).

### `src/ui/file_dialog.cpp`

file_dialog.cpp — İşletim sisteminin dosya aç/kaydet penceresi.

Windows'ta Unicode (W) API kullanılır ve yollar UTF-8 <-> UTF-16 çevrilir;
böylece "Masaüstü/şişe.obj" gibi Türkçe adlar bozulmaz. Diğer platformlarda
şimdilik diyalog yok (false döner).

### `src/ui/file_dialog.h`

file_dialog.h — İşletim sisteminin yerel dosya aç/kaydet penceresi (UTF-8 yollar).

### `src/ui/icons.h`

icons.h — Lucide ikon fontunun (ISC lisansı, assets/fonts/lucide.ttf) kod noktaları.

Her makro, ikonun UTF-8 kodlanmış karakteridir; metin içinde kullanılır:
  ImGui::Button(ICON_SAVE " Kaydet");
Font, Inter ile birleştirilerek (merge) yüklenir (ui/fonts.cpp).
Yeni ikon: assets/fonts ile gelen lucide-static paketindeki codepoints.json.

### `src/ui/orbit_camera.cpp`

orbit_camera.cpp — Orbit kameranın fare etkileşimi ve kadrajlama matematiği.

### `src/ui/orbit_camera.h`

orbit_camera.h — Bir hedef nokta etrafında dönen etkileşimli kamera.

Konum küresel koordinatlarla tutulur: hedef + yarıçap · (sinθ cosφ, cosθ, sinθ sinφ).
θ (theta) dikey açı (0 = tam tepe), φ (phi) yatay açıdır. Fare sürüklemesi bu
açıları değiştirir; tekerlek yarıçapı üstel olarak küçültür/büyütür.

### `src/ui/theme.cpp`

theme.cpp — Renk paleti, font yükleme ve ImGui stilinin uygulanması.

### `src/ui/theme.h`

theme.h — Arayüzün renk paleti, yazı tipleri ve ImGui stil ayarları.

Tüm renkler tek yerde (Palette) tanımlıdır; paneller sabit renk yazmak yerine
buradaki adlandırılmış renkleri kullanır. Tek vurgu rengi (turuncu, "foton")
yalnız ana eylemlerde ve seçimde görünür; geri kalan her şey nötr gri tonlar.

### `src/ui/widgets.cpp`

widgets.cpp — Ortak arayüz bileşenlerinin çizimi.

### `src/ui/widgets.h`

widgets.h — Uygulamaya özel, tutarlı görünümlü ImGui bileşenleri.

Paneller ham ImGui çağrıları yerine bunları kullanır; böylece her yerde aynı
hizalama (solda etiket, sağda kontrol), aynı boşluk ve aynı vurgu dili olur.
