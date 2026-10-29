# PhotonEngine — Mimari

Her alt sistem `src/<modül>/` altında statik bir kütüphanedir. Her hedef `src/`'yi include kökü
olarak verir: `#include "core/math/vec.h"`.

Bu sayfa ağacın bugünkü halini anlatır. Planlanan işler: [`memory-bank/01-gorevler.md`](../memory-bank/01-gorevler.md).

---

## 1. Bağlantı grafiği

```
photon_app ──► photon_ui (tema, widget, orbit kamera, dosya diyaloğu) ──► imgui, photon_core
     ├──► photon_scene ──► photon_engine ──► integrators, io, camera, samplers, lights,
     │                         │              materials, geometry, core
     │                         ├──► embree4 (varsa)     └──► OIDN (varsa)
     │                         
     └──► imguizmo, glfw, OpenGL
photon_render (CLI) ──► photon_scene (render: RenderDevice + RenderSession, uygulamayla aynı yol)
photon_tests ──► photon_engine, photon_scene, gtest
```

## 2. Bir karenin yolculuğu (viewport)

```
 UI thread (ImGui, ~60 fps)                      Render thread (RenderController)
 ─────────────────────────                       ────────────────────────────────
 kullanıcı düzenler ──► belge (AppState):
   SceneGraph, LightDesc[], EnvironmentDesc
        │  markDocumentChanged()
        ▼
 compileIfDirty(): buildRenderScene()
   ağaç → dünya uzayına bake, zemin, ışıklar,
   ortam (HDRI + CDF), Embree/BVH
        │  setScene(shared_ptr<const Scene>) ──────► sıradaki pass yeni sahneyi alır
 her kare: setCamera / setDisplay ─────────────────► değiştiyse restart (birikim = 0)
                                                    RenderSession(Interactive).advance()
                                                      → RenderDevice::render (CPU: karolar)
                                                      tracePixel → PathTracer::Li
                                                      Film += örnek, AOV += albedo/normal/alfa
                                                    etkileşimde: 1/2–1/8 çözünürlük + OIDN(fast)
                                                    durunca: tam çözünürlük, 2^k örnekte OIDN
                                                    ton eşleme → RGBA8 → publish()
 uploadViewportFrame() ◄── fetchFrame() ◄──────────
   glTexSubImage2D → ImGui::Image
```

Kurallar:
- **UI hiçbir zaman bir pass'i beklemez.** Sahne değişince yeni, değişmez bir `Scene` kurulur ve
  `shared_ptr` ile verilir; render thread'i eskisini pass bitene kadar tutar.
- **Yerinde değişen tek şey malzeme parametreleridir** (kaydırıcı sürüklerken her karede sahne
  derlemek pahalı olurdu). Bunun için `RenderController::edit()` / `Application::editLive()`:
  mevcut pass iptal edilir (karo/satır düzeyinde, milisaniyeler), sahne kilidi alınır, değer
  yazılır, birikim yeniden başlar.
- **İptal:** `parallelFor2D` bir `atomic<bool>` alır; karolar ve satırlar başlamadan önce kontrol eder.
- **Son render** malzemeleri kopyalanmış (izole) ayrı bir sahneyle kendi iş parçacığında çalışır;
  viewport bu sırada duraklatılır.

## 2b. Cihazlar ve modlar (engine/render_device, engine/render_session)

Render iki eksende ayrılır. Hangi cihazda ("nerede") ve hangi modda ("nasıl") çalışacağı birbirinden
bağımsızdır. Yol izleme çekirdeği (`PathTracer`) her ikisinde de aynıdır.

```
             viewport        son render / turntable        photon_render (CLI)
                 │                     │                            │
                 ▼                     ▼                            ▼
          RenderSession(Interactive)   RenderSession(Final) ◄───────┘
          1 örneklik pass'ler          hedef örnek / süre / yakınsama, büyüyen partiler
                 └──────────────┬──────────────┘
                                ▼
                RenderDevice  ──  setScene() yalnız sahne değişince
                    │             render(kamera, ayarlar, hedef filmler, örnek aralığı)
     ┌──────────────┼───────────────────┬───────────────────────────┐
     ▼              ▼                   ▼                           ▼
 CpuDevice       CUDA (plan, Faz 9)   OptiX / Vulkan RT / DXR      (Realtime modu:
 Renderer +      GTX 1080 sm_61:      RT çekirdekli kartlar         kare başına 1 örnek
 Embree +        hesaplama, CWBVH     (Turing+)                     + zamansal yeniden
 iş parçacıkları                                                    kullanım + RT denoise)
```

Katmanların görevleri:
- **`RenderDevice`**: sahneyi yükler ve bir örnek aralığını hedef filmlere biriktirir.
  - `features()` hangi çıktıları desteklediğini söyler. Yeni bir GPU arka ucu ilk günden her şeyi
    desteklemeyebilir; oturum desteklenmeyen çıktıyı kapatır ve uyarır.
  - `enumerateDevices()` CPU'yu ve CUDA sürücüsüyle bulunan GPU'ları listeler. Sürücü çalışma
    zamanında yüklenir, derlemede bağımlılık yoktur.
  - Arka ucu olmayan cihaz nedeniyle birlikte "kullanılamaz" görünür. Örnek: "Pascal, sm_61; RT
    çekirdeği yok (OptiX 9, DXR 1.1+ ve RTXPT çalışmaz)".
- **`RenderSession`**: mod politikasını taşır: parti boyu, durma koşulları (hedef, süre,
  uyarlamalı yakınsama), çıktılar (AOV, geçişler) ve gürültü gidermeli çözüm.
  - Final modda parti boyu ölçülen hıza göre ~`batchSeconds` sürecek şekilde büyür. Karo-önce
    örnekleme 1 örneklik pass'lerden ~%7 hızlıdır. Süre sınırı aşılmaz.
  - Örnekleyici (piksel, örnek indeksi) ikilisine göre deterministiktir. 8 örneklik bir parti,
    sekiz tek örneklik pass ile bit bit aynı filmi verir; viewport ve son render aynı görüntüye
    yakınsar (`RenderDevice.BatchEqualsSinglePassesBitForBit`).
- **Sahne yükleme**: viewport, sahneyi bir sürüm sayacıyla takip eder (`setScene` ve yerinde
  malzeme düzenlemesinde artar). Kamera hareketi yeniden yükleme yapmaz. CPU'da bu bedavadır; bir
  GPU cihazında tampon kopyasını yalnız gerektiğinde yapmak anlamına gelir.

**RTXPT ile ilişkisi.** NVIDIA RTX Path Tracing'in (v1.9) kullanılabilir fikri, tek bir yol izleme
çekirdeğinin Reference ve Realtime modlarında paylaşılmasıdır; bu katman o ayrımı kurar. RTXPT
kodu doğrudan alınmaz:
- **Lisans:** NVIDIA RTX SDKs lisansı, açık kaynak değil. Kaynağın açık kaynak lisansına girmesini
  yasaklar; DLSS/NGX yalnız NVIDIA GPU'lu sistemler için lisanslıdır.
- **Platform:** Windows-only, D3D12 (Vulkan isteğe bağlı). DXR 1.1 + SM 6.6 + Resource Binding
  Tier 3 ister; varsayılan Agility SDK ile DXR 1.2 / SM 6.9.
- **Sürücü:** 595.71+ ister. Pascal'ın son sürücü dalı R580 olduğundan GTX 1080'de çalışmaz.

Alınacak fikirlerin kaynakları yayımlanmış makalelerdir: ReSTIR DI/GI, SHaRC radyans önbelleği,
ışık BVH'si + uyarlamalı NEE, ışın konileriyle MIP seçimi, opaklık mikro haritaları.

## 2c. Gerçek zamanlı mod (app/raster_view, scene/realtime_scene)

Viewport'un "Gerçek zamanlı" modu ışın izlemez: geometriyi OpenGL 3.3 ile rasterleştirir ve her
pikseli bir kez gölgelendirir (forward PBR). Amaç oyun motoru hızında **aynı** sahneyi göstermektir;
bu yüzden bütün girdiler ışın izleyicinin tanımlarından hazırlanır ve hazırlık GL'den bağımsız,
testli bir modüldedir (`scene/realtime_scene.*`, `tests/test_realtime_scene.cpp`):

| Girdi | Kaynak | Not |
|---|---|---|
| Ortam | `EnvironmentLight::evalSky` → 1024×512 equirect (dünya yönleri) | Fiziksel gökte güneş zaten ayrı; HDRI'de güneş lekesi **ayıklanır** (`extractSunFromMap`: tepe ≥ 300× ortalama ve bölge < ~3.5°) ve gölgeli yönlü ışık olur. Zemin açıkken alt yarıküre aydınlanmış zemin rengi (ρ·E/π). |
| Difüz ortam | SH9 ışınımı (Ramamoorthi–Hanrahan) | Işıkların zemine verdiği pay `uGroundExtra` ile analitik eklenir (alt yarıküre π/2·(1 − n_y)); ışık değişince harita yeniden pişmez. |
| Parlak ortam | equirect mip zinciri, `textureLod` (α → teksel açısı) + Karis'in analitik env-BRDF'i | Küp harita ve BRDF LUT yerine (F8.1'in yalın sürümü). |
| Işıklar | `packRealtimeLights`: aynı `lightEmission` (Kelvin), nokta/spot I, alan L·A·|cos|, yönlü E | Alan ışığı küre ışık yaklaşımıyla (Karis 2013: en yakın nokta + lob genişletme). Sahne merkezine verdiği ışınıma göre sıralı; en çok 16. |
| Gölgeler | 2×2 derinlik atlası (4096²), en güçlü 4 ışık | `shadowProjection`: sahne kutusu + kutunun zemine düşen gölge izi kapsanır. PCSS: 16 örnekli engelleyici araması + 32 örnekli Vogel diski; kadraj kenarında söner. Camlar ayrı atlasta, gölgeleri kısmidir (%55). |
| Temas gölgesi | en üst düzey nesnelerin (grup ise bir alt düzey) zemin izleri | eliptik Gauss; ekran uzayı AO'nun titreşimsiz karşılığı. |
| Malzeme | `MaterialLibrary::presetFromMaterial` + `DisneyMaterial::*Image()` | Taban rengi, metal, pürüzlülük, specular (F0 = 0.08·s), vernik, emisyon; albedo (sRGB doku), ORM kanalları (G pürüzlülük, B metal), normal (türevlerden kotanjant çerçevesi), kutu eşleme. |
| Ton eşleme | `buildToneLut`: viewport'un operatörü 64³ LUT (log2 shaper) | Shader'da eğri yeniden yazılmaz; sapma ≤ ~4/255. Beyaz dengesi `whiteBalanceMatrix` ile aynı. |

Çizim sırası (`RasterView::renderRealtime`): gölge atlası → cam derinlik atlası → arka plan (ortam ya
da düz renk) → opak yüzeyler → zemin (gölge yakalayıcıda yalnız alfa karartması) → cam (arkadan öne;
çarpımsal geçirgenlik, sonra ön çarpımlı Fresnel bindirme: F·yansıma + (1 − F)·w·kırılan ortam).
Ton eşleme piksel başına yapılır, MSAA çözümü ekran değerlerinde olur (kenarlar yumuşak).

**Bilinen sınırlar:** ekran uzayı yansıma/kırılma yok (krom kürede nesneler değil ortam görünür), IES
profilleri yalnız koni olarak, SSS/dispersiyon/kostik yok, görüntüleyiciden (imager) yalnız beyaz dengesi.

## 3. İki sahne temsili

| | `SceneGraph` (scene/) | `Scene` (engine/) |
|---|---|---|
| Kim kullanır | UI, proje dosyası, geri al | render, seçim ışınları |
| İçerik | düğüm ağacı: yerel dönüşüm, mesh (değişmez, paylaşılan), malzeme, görünürlük, uid | dünya uzayına bake edilmiş mesh'ler, ışık listesi, ortam, arka plan, BVH/Embree |
| Değişebilir mi | evet | hayır (her değişiklikte yenisi) |
| Köprü | `buildRenderScene()` (scene/document.cpp) | |

Işıklar ve ortam da belgede sade tanımlar olarak tutulur (`LightDesc`, `EnvironmentDesc`); her
derlemede `buildLights()` / `buildEnvironment()` ile değişmez render nesnelerine dönüşür.

## 4. Geri al

`UndoStack<DocSnapshot>`: her düzenlemeden önce belgenin tam kopyası (ağaç + ışıklar + ortam).
Geometri değişmez olduğu için paylaşılır; yalnız malzemeler ve dönüşümler kopyalanır. Kaydırıcı
sürüklemeleri tek adımda birleşir (`undoPoint`).

## 5. Işık taşıma (integrators/path_tracer.cpp)

Tek bir `PathTracer::Li` döngüsü; her yineleme bir doğru parçası + bir yüzey köşesidir.

```
 kesişim ─► (kameraya görünmez ışık? içinden geç)
   │
   ├─ ortam yığınının tepesinde bir Medium varsa: Beer–Lambert / serbest uçuş
   │     saçılma olduysa ─► HG faz yönü, 'continue' (sekme sayılmaz)
   ├─ ıskaladı: arka plan / ortam (+ güneş konisi), MIS
   ├─ gölge yakalayıcı + kamera ışını: arka plan × görünen ışık oranı, dur
   ├─ yayıcı: MIS (önceki köşenin LightPicker'ı + Light::pdfLiHit, O(1))
   ├─ ilk dispersiyonlu yüzey: λ seç, throughput ×= w(λ)/pdf
   ├─ yuvarlatılmış kenar: gölgelendirme normali komşu yüzeylerle harmanlanır
   ├─ NEE (delta değilse): LightPicker.build(p, n) → ışık seç → sampleLi → gölge
   │     (ince duvarlı camda renkli geçirgenlik) → f·Li·|cos|/pdf × MIS
   └─ BSDF: Material::sampleFull →
         subsurface  → rastgele yürüyüş, çıkışta beyaz Lambert köşesi (NEE dahil)
         passThrough → ince duvar: MIS durumu korunur, sekme sayılmaz
         normal      → throughput, hacim sınırıysa ortam yığınına gir/çık, Rus ruleti
```

- **Malzeme sözleşmesi** (`materials/material.h`): `sampleFull` → `BSDFSample {wi, f, pdf, delta,
  subsurface, passThrough, weight}`; `isVolumeBoundary`/`interiorMedium` (hacim), `isDispersive`,
  `subsurfaceCoeffs`, `transmitsShadows`/`shadowTransmittance`, `roundEdgeRadius`.
- **Ortamlar:** `Medium {σa, σs, g}` homojen. Renkli σt'de kanal throughput'a orantılı seçilir,
  pdf kanalların karışımıdır (spektral MIS, PBRT-v4 §14.2). İçeride NEE yapılmaz: ışıklar kırıcı
  sınırın dışındadır; saçılmadan sonra bulunan yayım tam ağırlık alır.
- **SSS:** Chiang 2016 ters çevirmesi (A, d → α, σt), anizotropide benzerlik ilkesi; yürüyüş
  `randomWalkSubsurface` (en çok 256 adım, 8. adımdan sonra Rus ruleti).
- **Dispersiyon:** `core/color/spectral.*`: Wyman–Sloan–Shirley CIE eğrileri → doğrusal sRGB,
  negatifler kırpılır, kanallar beyaza normalize; λ, r+g+b'ye orantılı tablodan seçilir.
- **Işık seçimi:** `LightPicker` her köşede `Light::importance(p, n)` (kaba ışınım tahmini) ile
  %85 orantılı + %15 düzgün karışım kurar; 33'ten çok ışıkta düzgün seçime döner. Aynı dağılım
  bir sonraki isabette MIS için kullanılır.
- **Alan ışığı:** `AreaLight::sampleLi` küresel dikdörtgen (Ureña 2013); Ω < 1e-3 sr'de alan örneklemesi.
- **Ortam ışığı:** luminance × sin θ dağılımından 2B CDF + isteğe bağlı güneş konisi (karışım pdf'i).
- **Delta yüzeyler** (`Material::isDelta()`): ayna ve pürüzsüz camda NEE yapılmaz.
- **Rus ruleti:** throughput'un en büyük bileşenine göre. **Parlama sınırı** (isteğe bağlı) yalnız
  ikinci ve sonraki delta-olmayan köşeden bulunan ışığa uygulanır.
- Örnekleyici: Owen karıştırmalı Sobol (samplers/sobol_sampler.*).

## 5b. Malzeme modeli (materials/)

- `DisneyMaterial`: Burley difüz, VNDF örneklemeli anizotrop GGX, sheen, vernik, ince yüzey geçirgenliği,
  SSS lobu, **F82 kenar tonu** ve **ince film** (`fresnel.h`: karmaşık Fresnel, Gulbrandsen, Airy),
  **Turquin 2019 çoklu saçılma telafisi** (`microfacet.cpp`: 32×32 E(μ, α) tablosu), **pullar**
  (3B Voronoi hücreleri; pulda taban yerine dar GGX lobu).
- `Dielectric`: delta/pürüzlü cam (PBRT-v4 DielectricBxDF), iç ortam (soğurma + saçılma), Abbe
  dispersiyonu, ince duvar (+ film).
- Parametreler `MaterialPreset` ile taşınır; preset dosyaları ve proje dosyası aynı JSON anahtarlarını
  kullanır (`presetAdvancedToJson/FromJson`), küçük resim önbelleği `presetSignature` ile anahtarlanır.

## 5c. Ortam ve gök (lights/)

- `sky_model.*`: Nishita tek saçılma (Rayleigh + Mie + ozon çadır profili), 512×256 equirect; güneş
  diski ayrı döner (atmosfer geçirgenliğiyle renklenir). NOAA güneş konumu. `EnvironmentCache::sky`
  son 4 sonucu tutar.
- `EnvironmentDesc::Kind::Sky` → `buildEnvironment` gök görüntüsü + `EnvironmentLight::setSun`.
- `spot_light.*` + `ies.*`: koni düşüşü (smoothstep) ve IES LM-63 profili (simetri katlama).

## 6. Kesişim

- `EmbreeAccel` (engine/embree_accel.*): Embree 4 varsa üretim yolu. Embree yalnız t ve
  barisentrikleri verir; yüzey verisi motorun `Triangle::fillHit` koduyla hesaplanır.
- `BVH` (geometry/bvh.*): kendi 12 kovalı SAH BVH'miz; yaprak sırasında yalın üçgen dizisi, yakın
  çocuk önce, gölge ışınlarında erken çıkış. Embree yoksa ve testlerde kullanılır.

## 7. Renk hattı

Sahne doğrusal Rec.709. `Film` (toplam + sayı + parlaklık²) → `resolve()` ortalama → (OIDN) →
**görüntüleyici** (`core/image/imager.*`: renk sapması → bloom → glare → beyaz dengesi → kontrast /
doygunluk → vinyet) → pozlama (2^EV) → ton eğrisi (PBR Nötr / AgX / ACES / ...) → sRGB OETF →
titreşimli 8 bit. EXR çıktısı ton eşlenmemiş ve görüntüleyicisiz doğrusal radyanstır. Şeffaf arka
planda renk önceden çarpılmış biriktiği için PNG'ye yazarken alfaya bölünür; gölge yakalayıcıda
alfa gölgenin opaklığıdır.

Uyarlamalı örnekleme `Film::relativeError` (sqrt(var/n) / (ortalama + 0.01)) ile karar verir: son
render'da piksel başına her 8 örnekte, ilerlemeli pass'lerde 3×3 komşulukla.

## 8. Uygulama dosyaları (src/app)

| Dosya | Sorumluluk |
|---|---|
| `application.*` | pencere, ImGui, ana döngü, kısayollar, sürükle-bırak, ekran görüntüsü modu |
| `render_controller.*` | viewport render thread'i, çözünürlük merdiveni, OIDN, yayınlama |
| `scene_ops.cpp` | belge işlemleri, geri al, proje kaydet/aç, seçim ışını |
| `final_render.cpp` | son render, turntable ve kamera animasyonu işleri |
| `raster_view.*` | GPU modları: Gerçek zamanlı (PBR, gölge atlası), Katı, Tel kafes, Normaller |
| `live_link.*` | DCC canlı bağlantı sunucusu (127.0.0.1, jeton, satır başına JSON) |
| `animation.cpp`, `camera_keys.h` | zaman çizelgesi, kamera anahtar kareleri, oynatma |
| `thumbnails.*` | malzeme küreleri ve HDRI önizlemeleri (arka planda, disk önbellekli) |
| `ui_layout.cpp` | menü, panel yerleşimi, durum çubuğu, kısayollar |
| `ui_library.cpp`, `ui_viewport.cpp`, `ui_scene.cpp`, `ui_properties.cpp`, `ui_render.cpp` | paneller |
| `ui_common.*` | panel başlığı, küçük resim kartı, TRS ayrıştırma |
