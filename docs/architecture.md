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
photon_render (CLI) ──► photon_scene
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
                                                    pass: parallelFor2D karoları
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
| `final_render.cpp` | son render ve turntable işleri |
| `thumbnails.*` | malzeme küreleri ve HDRI önizlemeleri (arka planda, disk önbellekli) |
| `ui_layout.cpp` | menü, panel yerleşimi, durum çubuğu, kısayollar |
| `ui_library.cpp`, `ui_viewport.cpp`, `ui_scene.cpp`, `ui_properties.cpp`, `ui_render.cpp` | paneller |
| `ui_common.*` | panel başlığı, küçük resim kartı, TRS ayrıştırma |
