# Eksik analizi: "en iyi render uygulaması" için ne kaldı?

Tarih 2026-10-29. Kıyas noktaları KeyShot 2025, V-Ray 7, Arnold 7, Cycles 4.x, Octane 2025 ve
Redshift'tir. Her madde için **neden önemli**, **önerilen yol** ve **alternatifler** verilir.
Bu oturumda kapatılanlar en alttaki tablodadır, ölçümleriyle birlikte.

## Öncelik 1: kullanıcının hemen hissedeceği eksikler

| # | Eksik | Neden önemli | Önerilen yol | Alternatif (artı / eksi) |
|---|---|---|---|---|
| 1 | **GPU render** | KeyShot, Octane ve Redshift GPU'da 10–50× hızlı. Etkileşimli look-dev bunu bekliyor. | Cihaz/mod katmanı hazır (`RenderDevice` + `RenderSession`, docs/architecture.md §2b); GPU yeni bir `RenderDevice` olarak eklenecek. CUDA 12.x megakernel. CWBVH (TinyBVH) ile başla, kodu `PHOTON_HD` ile CPU'yla paylaş (Faz 9). | **RTXPT'yi almak**: gerçek zamanlı mod için en olgun örnek, ama NVIDIA RTX SDKs lisanslı, yalnız Windows/D3D12 ve sürücü 595.71+ ister; GTX 1080'de (R580 son dal) çalışmaz. Fikirleri (ReSTIR, SHaRC, tek çekirdek + iki mod) makalelerden alınır. **OptiX 9**: RT çekirdekleri ve OIDN-CUDA verir, ama Turing+ ister ve GTX 1080'de çalışmaz. **Vulkan RT**: üretici bağımsız, ama ikinci bir shading kod tabanı demek. **Wavefront**: diverjansa dayanıklı, ama daha karmaşık; profil gösterirse geçilir. |
| 2 | **Kostikler** (cam arkası, su yüzeyi, parlak metalin yansıttığı ışık) | Ürün renderında şişe, mücevher ve parfüm sahneleri. Şu an parlama sınırıyla kesiliyor. | **Path guiding** (Intel Open PGL, Apache-2.0). Cycles 4 ve Karma bunu kullanıyor, genel çözüm. | **MNEE**: cam arkası gölgede çok güçlü ama dar kapsamlı. **BDPT/VCM**: genel ama yavaş, GPU'ya zor. **Foton haritası**: hızlı ama yanlı. |
| 3 | **Alfa kesikleri (cutout)** | Yaprak, etiket, file, ızgara. FBX/glTF'te opaklık haritası şimdilik yok sayılıyor (uyarı veriliyor). | Embree filtre fonksiyonu ile any-hit testi. Stokastik opaklık için gölge ışını şeffaflığı (F6.8). | Ağı kesip yeniden üçgenlemek: render ucuz kalır ama içe aktarma pahalı ve kalitesiz. |
| 4 | **Doku belleği ve filtreleme** | 4K × 50 doku float RGB olarak tutuluyor ve RAM patlıyor. Uzakta aliasing var, mipmap yok. | 8-bit + sRGB LUT ile sakla. Mipmap ve ışın konisi kullan (RTG bölüm 20, F7.6). Gerekirse istek üzerine yüklenen döşemeli doku önbelleği. | **OIIO TextureSystem**: endüstri standardı ama ağır bir bağımlılık (Boost geçmişi). **Ray differentials**: daha doğru ama türev taşıma maliyeti var. |
| 5 | **Artımlı sahne güncellemesi ve render seviyesinde instancing** | Büyük modelde ışık ya da ortam değişince tüm sahne yeniden bake ediliyor. FBX örnekleri bellekte kopyalanıyor. | Embree iki seviyeli BVH: BLAS/TLAS (F6.6). Dönüşüm değişince yalnız TLAS yenilenir, malzeme değişince yalnız malzeme tablosu (F3.8). | Kendi BVH'mizde refit: basit ama yalnız deformasyonda işe yarar. |
| 6 | **Otomatik kayıt ve çökme kurtarma** | KeyShot'ta var. Kullanıcı bir çökmede saatlerini kaybetmemeli. | Her N dakikada `%APPDATA%/PhotonEngine/autosave/` altına atomik kayıt (yazım zaten atomik). Açılışta "kurtar" sorusu, artı minidump. | Yalnız geri al geçmişini diske yazmak: daha ince taneli ama daha karmaşık. |

## Öncelik 2: pipeline entegrasyonu (diğer 3B uygulamalar)

| # | Eksik | Önerilen yol | Not |
|---|---|---|---|
| 7 | **USD / USDZ** (Omniverse, Houdini Solaris, Apple AR) | OpenUSD (TinyUSDZ ilk adım için yeterli, Apache-2.0; tam OpenUSD ağır ama standart). `UsdPreviewSurface` ve `UsdGeom`, ardından Hydra delegesi. | Uzun vadede bir **Hydra render delegesi**: motor Houdini, Maya ve Blender içinde viewport renderer'ı olur. |
| 8 | **CAD: STEP / IGES / 3DM** (KeyShot'un ana pazarı) | Open CASCADE (LGPL-2.1, yalnız dinamik bağlanır) ile tessellation. Kalite kaydırıcısı (açı/sapma) ve parça adları. | Rhino 3DM için openNURBS (MIT). SolidWorks ve Creo yerel biçimleri lisans gerektirir. |
| 9 | ~~Canlı bağlantı~~ **yapıldı** (aşağıda). Kalan: artımlı güncelleme (yalnız değişen nesne), DCC tarafında malzeme eşleme (V-Ray/Corona → Principled). | KeyShot LiveLinking, V-Ray Vision. |
| 10 | **Nesne/deformasyon animasyonu ve hareket bulanıklığı** | Kamera animasyonu yapıldı. Kalan: nesne dönüşüm animasyonu, Alembic/FBX deformasyonu kare kare, dönüşüm ve deformasyon hareket bulanıklığı. | ufbx `ufbx_evaluate_scene`, Alembic zaman örnekleri hazır. |
| 11 | **glTF malzeme uzantıları** | transmission, volume, ior, clearcoat, sheen, specular, emissive_strength, texture_transform (F7.9). | Bu oturumda gelen sheen ve vernik lobu bunları doğrudan karşılıyor; eşleme işi kaldı. |
| 12 | **MaterialX / OpenPBR** | OpenPBR Surface'e geçiş (F7.10). MaterialX ile Substance, Houdini ve Maya'dan malzeme alışverişi. | Disney, referans ve fırın testleriyle kalır. |
| 13 | **Cryptomatte, ışık grupları, ışık bağlama** | Cryptomatte (MurmurHash3 + iki derinlik katmanı, EXR meta verisi). Işık grupları: geçişler zaten lob başına; ışık başına ek film. | Bu oturumda gelen nesne ve malzeme ID katmanları basit maskeler için yeterli. |
| 14 | **Python API ve toplu iş** | pybind11 ile `photon` modülü: sahne yükle, malzeme ata, render al. | CLI (`photon_render`) şimdiden toplu işi karşılıyor. |

## Öncelik 3: doğruluk ve kalite derinliği

- **Normal haritası tutarlılığı** (Schüssler 2017 mikro-yüzey yaması, F5.3): şu an kırpılıyor, siyah lekelere yol açabilir.
- **MikkTSpace teğetleri** (F7.7): normal haritası dikişleri.
- **Watertight üçgen kesişimi** (Woop 2013, F6.3): ince kenarlarda sızıntı.
- **Heterojen hacimler (VDB)**, hacim NEE'si: duman, bulut, buhar.
- **Spektral render**: hero wavelength (şimdilik yol başına tek λ, yalnız dispersiyonda).
- **Mitsuba 3 referans karşılaştırması** (F2.14) ve altın görüntü (golden) regresyonu (F2.8).
- **Windows/MSVC**: bu oturumun değişiklikleri yalnız GCC 13 ile derlendi; `/W4` ve FetchContent ile Alembic (CMake ≥ 3.29, VS 18 ile geliyor) denenmeli.

## Gerçek zamanlı modun sıradaki adımları

| Eksik | Önerilen yol | Not |
|---|---|---|
| Ekran uzayı yansıma ve kırılma | Hi-Z ile SSR; cam için arka tamponu bulanık örnekleme | Krom kürede bugün nesneler değil ortam görünüyor. |
| Doğru alan ışığı | LTC (Heitz 2016): dikdörtgen softbox'ın parlaması ve difüzü analitik | Bugün küre ışık yaklaşımı (yuvarlak parlama). |
| Ortam kapatma ve dolaylı ışık | GTAO; ileride RTGI/ReSTIR GI (GPU ışın izleme gelince) | Bugün yalnız zemin temas gölgesi. |
| Kenar yumuşatma ve gürültü | TAA (PCSS'in döndürülmüş disk gürültüsünü de temizler) | Bugün 4× MSAA. |
| Görüntüleyici | bloom, vinyet, kontrast, doygunluk shader'da | Bugün yalnız beyaz dengesi + ton eşleme. |
| IES | 1B profil dokusu (γ açısına göre) | Bugün yalnız koni. |

## Bu oturumda kapatılanlar (ölçümlü)

| Alan | Yapılan | Kanıt |
|---|---|---|
| Malzeme doğruluğu | Principled katmanlama enerji korunur (Kulla–Conty albedo ölçekleme), EON difüz, Charlie sheen. ORM kanal hatası (G/B) düzeltildi. | Beyaz plastik fırın testi 1.58'den her açıda ≈ 1.00'e indi. Malzeme × pürüzlülük × açı durumlarında chi-kare örnek/pdf testi (Bonferroni düzeltmeli). |
| Görüntü oluşumu | Piksel filtresi (kutu, üçgen, Gauss, Blackman-Harris), filtre önem örneklemesi. | `test_pixel_filter`. Eski projeler kutu filtresiyle açılır (görüntü değişmez). |
| Sağlamlık | Kesişim hata sınırlı ışın başlangıcı, tMin = 0. Gölge terminatörü düzeltmesi. | 10000× ölçekte görüntü %11 kararıyordu, artık aynı. Düşük poligonlu kürede kendini gölgeleme %7.9'dan %0.02'ye indi. |
| Çok ışık | Işık BVH'si, yayıcı üçgen başına ayrı ışık. | 2000 üçgenli LED şeritte relMSE 0.395'ten 0.137'ye indi (2.9×). |
| HDRI | MIS telafisi (Karlík 2019). | relMSE: güneşli gök 0.345 → 0.051, stüdyo HDRI'ları %12–33 daha düşük. |
| Compositing | 9 ışık geçişi, derinlik, konum, normal, albedo, ID. Çok katmanlı EXR. | Geçişlerin toplamı beauty'ye 1e-4 içinde eşit. Kanal adları Nuke/Blender düzeninde. |
| Entegrasyon | FBX (ufbx) ve Alembic içe aktarma: hiyerarşi, adlar, instancing, birim ve eksen, malzeme eşleme, gömülü dokular. | Blender 2.79/2.93 ve 3ds Max 2009 dosyalarıyla testler, test içinde yazılan Alembic arşivi. |
| Arayüz | İçe aktarma penceresi, imlecin altına bırakma, çoklu dosya, son dosyalar, kütüphaneden modeli yüzeye bırakma. | Uygulamanın ekran görüntüleriyle doğrulandı (Xvfb). |
| Cihaz/mod katmanı | `RenderDevice` + `RenderSession`; viewport, son render, turntable ve CLI tek yoldan. | Toplu (batch) render tek tek geçişlerle bit bit aynı (`test_render_session`). |
| 3ds Max / Maya / Blender | Canlı bağlantı (jetonlu yerel TCP, yerinde güncelleme, malzemeler korunur), `.max/.ma/.mb/.blend` doğrudan açma (kurulu DCC ile, önbellekli), FBX kamera + ışık + kamera animasyonu, `.photonpack`. | Sahte DCC araçlarıyla uçtan uca testler; uygulamada iki gönderim → tek yerinde güncelleme. Gerçek Max/Maya bu ortamda denenemedi. |
| Kamera ve ışık araçları | Parlamayı yerleştir, Kelvin, lens kaydırma + iki noktalı perspektif, kompozisyon kılavuzları, çıktı kadrajı. | Render testi: krom kürede en parlak piksel tıklanan pikselde (formül bozulunca 3–7 px sapıyor). |
| Gerçek zamanlı mod | GPU'da PBR: aynı ışıklar/ortam/malzemeler, 4 ışığa PCSS gölge, HDRI güneşi ayıklama, dokular, cam, zemin, aynı ton eşleme (LUT). | Işın izleyiciyle yan yana ekran görüntüleri (stüdyo, güneşli HDRI, dokulu, gölge yakalayıcı); alan ışığı ışınımı Monte Carlo'ya %2 içinde; LUT sapması ≤ 4/255; llvmpipe'ta ~50 fps. |

Test sayısı 133'ten 203'e çıktı.
