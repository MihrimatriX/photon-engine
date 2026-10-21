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

- Her köşede: ışık örneklemesi (NEE, ışık seçimi düzgün, MIS ağırlığında seçim olasılığı dahil) +
  BSDF örneklemesi; ikisi güç sezgiseliyle birleşir.
- Delta yüzeyler (`Material::isDelta()`): ayna ve pürüzsüz camda NEE yapılmaz.
- Rus ruleti: throughput'un en büyük bileşenine göre.
- Ortam ışığı: luminance × sin θ dağılımından 2B CDF ile önem örneklemesi.
- Örnekleyici: Owen karıştırmalı Sobol (samplers/sobol_sampler.*).

## 6. Kesişim

- `EmbreeAccel` (engine/embree_accel.*): Embree 4 varsa üretim yolu. Embree yalnız t ve
  barisentrikleri verir; yüzey verisi motorun `Triangle::fillHit` koduyla hesaplanır.
- `BVH` (geometry/bvh.*): kendi 12 kovalı SAH BVH'miz; yaprak sırasında yalın üçgen dizisi, yakın
  çocuk önce, gölge ışınlarında erken çıkış. Embree yoksa ve testlerde kullanılır.

## 7. Renk hattı

Sahne doğrusal Rec.709. `Film` (toplam + sayı) → `resolve()` ortalama → (OIDN) → pozlama (2^EV) →
ton eğrisi (PBR Nötr / AgX / ACES / ...) → sRGB OETF → titreşimli 8 bit. EXR çıktısı ton eşlenmemiş
doğrusal radyanstır. Şeffaf arka planda renk önceden çarpılmış biriktiği için PNG'ye yazarken alfaya bölünür.

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
