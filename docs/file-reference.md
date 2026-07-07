# PhotonEngine — Dosya Referansı

Bu belge, PhotonEngine deposundaki kaynak dosyaların ne işe yaradığını modül modül açıklar. Mimari genel bakış için [`architecture.md`](architecture.md) dosyasına bakın.

---

## İçindekiler

- [Kök dizin](#kök-dizin)
- [src/ — Kaynak kod](#src--kaynak-kod)
  - [core](#core--temel-altyapı)
  - [geometry](#geometry--geometri)
  - [materials](#materials--malzemeler)
  - [lights](#lights--ışık-kaynakları)
  - [camera](#camera--kamera)
  - [integrators](#integrators--ışık-bütünleştiriciler)
  - [samplers](#samplers--örnekleyiciler)
  - [io](#io--dosya-yükleme)
  - [engine](#engine--render-motoru)
  - [scene](#scene--editör-sahne-verisi)
  - [preview](#preview--opengl-önizleme)
  - [ui](#ui--kullanıcı-arayüzü)
  - [app](#app--masaüstü-uygulama)
- [assets/ — Hazır varlıklar](#assets--hazır-varlıklar)
- [tests/ — Birim testleri](#tests--birim-testleri)
- [docs/ — Dokümantasyon](#docs--dokümantasyon)

---

## Kök dizin

| Dosya | Açıklama |
|-------|----------|
| `CMakeLists.txt` | Proje yapılandırması. C++20 standardı, SIMD (AVX2), harici bağımlılıklar (stb, tinyobjloader, tinyexr, cgltf, GLFW, ImGui, glm) ve derleme hedeflerini (`photon_render`, `photon_app`, testler) tanımlar. |

---

## src/ — Kaynak kod

Motor iki giriş noktasına sahiptir:

| Hedef | Giriş dosyası | Ne yapar? |
|-------|-----------------|-----------|
| `photon_render` | `main.cpp` | Komut satırı renderer. Cornell Box sahnesi kurar, path tracer ile render alır, PNG kaydeder. |
| `photon_app` | `app/main_app.cpp` | GLFW + ImGui masaüstü editör uygulaması. |

```
photon_render (CLI)          photon_app (GUI)
      │                            │
      └──────── photon_engine ──────┘
                    │
    ┌───────────────┼───────────────┐
    │               │               │
photon_core   photon_geometry  photon_materials
    │               │          photon_lights
    │               │          photon_camera
    │               │          photon_samplers
    │               │          photon_integrators
    │               │          photon_io
    │               │
    └──── photon_scene (editör) ──┘
              │
         photon_preview → photon_ui
```

---

### core — Temel altyapı

Tüm modüllerin üzerine inşa edildiği temel katman. Matematik, bellek, rastgele sayı üretimi, görüntü işleme ve iş parçacığı yönetimini sağlar.

#### `core/CMakeLists.txt`
`photon_core` statik kütüphanesini tanımlar.

#### `core/color/`

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `spectrum.h` | Motor genelinde kullanılan RGB renk/spektrum temsili. | `Color3f` |

#### `core/math/`

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `vec.h` | 2D, 3D ve 4D float vektör tipleri ile vektör aritmetiği. | `Vec2f`, `Vec3f`, `Vec4f` |
| `vec.cpp` | `vec.h` için derleme birimi (header-only yapıya anchor). | — |
| `mat.h` | 4×4 dönüşüm matrisi; çarpma, ters alma, perspektif projeksiyon. | `Mat4f` |
| `ray.h` | Işın temsili: `origin + t * direction`. | `Ray` |
| `transform.h` | Affine dönüşüm; matris ve önbelleğe alınmış ters matris. | `Transform` |
| `transform.cpp` | Nokta, vektör, normal ve ışın dönüşümlerinin uygulanması. | `transformPoint()`, `transformRay()` |
| `quaternion.h` | 3D rotasyon temsili; eksen-açı, Euler, matrise dönüşüm. | `Quaternion` |
| `frame.h` | Yüzey gölgelendirme için ortonormal koordinat çerçevesi. | `Frame` |
| `aabb.h` | Eksen hizalı sınır kutusu; slab yöntemiyle ışın kesişimi. | `AABB` |
| `utils.h` | Clamp, lerp, Fresnel, yansıma/kırılma ve örnekleme yardımcıları. | `clamp()`, `fresnelDielectric()`, `reflect()` |
| `constants.h` | π, epsilon ve açı dönüşüm sabitleri. | `PI`, `TWO_PI`, `DEG_TO_RAD` |

#### `core/image/`

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `image.h` | HDR görüntü tamponu; thread-safe örnek biriktirme. | `Image` |
| `image.cpp` | Piksel okuma/yazma, örnek ortalaması ve temizleme. | `addSample()`, `getAveragedPixel()` |
| `image_io.h` | PNG, EXR ve HDR görüntü yükleme/kaydetme arayüzü. | `saveImagePNG()`, `loadImageHDR()` |
| `image_io.cpp` | stb ve tinyexr kütüphaneleriyle görüntü I/O uygulaması. | — |
| `tone_mapping.h` | HDR → LDR ton eşleme operatörleri. | `ToneMapOperator`, `toneMapACES()` |
| `tone_mapping.cpp` | Reinhard, ACES ve Filmic ton eşleme uygulamaları. | — |

#### `core/memory/`

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `aligned.h` | Platforma özgü hizalı bellek ayırma ve serbest bırakma. | `alignedAlloc()`, `alignedFree()` |
| `arena_allocator.h` | Blok tabanlı arena bellek ayırıcı. | `ArenaAllocator` |
| `arena_allocator.cpp` | Arena allocate, reset ve release uygulaması. | — |
| `pool_allocator.h` | Sabit boyutlu nesneler için serbest liste havuz ayırıcı. | `PoolAllocator<T>` |

#### `core/random/`

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `rng.h` | PCG-benzeri hızlı pseudo-random sayı üreteci. | `RNG`, `uniformFloat()` |

#### `core/sampling/`

| Dosya | Açıklama | Ana fonksiyonlar |
|-------|----------|------------------|
| `sampling.h` | Monte Carlo örnekleme yardımcıları. | `uniformSampleSphere()`, `cosineSampleHemisphere()` |

#### `core/threading/`

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `thread_pool.h` | Görev kuyruklu iş parçacığı havuzu. | `ThreadPool`, `submit()`, `waitAll()` |
| `thread_pool.cpp` | Worker döngüsü ve yaşam döngüsü yönetimi. | — |
| `parallel.h` | Global thread pool üzerinde paralel döngü yardımcıları. | `parallelFor()`, `parallelFor2D()` |

---

### geometry — Geometri

Işın–sahne kesişim sorguları ve hızlandırma yapıları.

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `CMakeLists.txt` | `photon_geometry` statik kütüphanesini tanımlar. | — |
| `shape.h` | Tüm geometrik primitifler için soyut arayüz. | `Shape` |
| `surface_interaction.h` | Işın–yüzey kesişim noktası geometrik bilgisi. | `SurfaceInteraction` |
| `sphere.h` / `sphere.cpp` | Küre primitifi ve ikinci derece denklemle kesişim. | `Sphere::intersect()` |
| `triangle.h` / `triangle.cpp` | Üçgen primitifi; Möller–Trumbore kesişim algoritması. | `Triangle` |
| `mesh.h` / `mesh.cpp` | Vertex/index tamponlu üçgen ağı; normal hesaplama. | `TriangleMesh` |
| `bvh.h` / `bvh.cpp` | Doğrusal BVH hızlandırma yapısı (32 byte düğümler). | `BVH::build()`, `intersect()` |

---

### materials — Malzemeler

Fiziksel tabanlı gölgelendirme modelleri. Her malzeme `Material` arayüzünü uygular.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_materials` statik kütüphanesini tanımlar. | — |
| `material.h` | BSDF/BRDF soyut temel sınıf (PBRT konvansiyonları). | `Material` — `sample()`, `eval()`, `pdf()` |
| `lambertian.h` / `.cpp` | İdeal difüz (Lambert) yansıma malzemesi. | `Lambertian` |
| `mirror.h` / `.cpp` | Mükemmel ayna; delta speküler yansıma. | `Mirror` |
| `dielectric.h` / `.cpp` | Cam/şeffaf dielektrik; Fresnel + kırılma. | `Dielectric` |
| `disney.h` / `.cpp` | Disney Principled BRDF; Burley difüz + GGX speküler. | `DisneyMaterial` |

---

### lights — Işık kaynakları

Sahneyi aydınlatan kaynakların soyut arayüzü ve somut uygulamaları.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_lights` statik kütüphanesini tanımlar. | — |
| `light.h` | Işık kaynağı soyut arayüzü ve örnekleme sonucu. | `Light`, `LightSample` |
| `point_light.h` / `.cpp` | Her yöne eşit yayan nokta ışık; ters kare mesafe sönümlemesi. | `PointLight` |
| `directional_light.h` / `.cpp` | Sonsuz uzaklıktaki yönlü ışık (güneş). | `DirectionalLight` |
| `area_light.h` / `.cpp` | Dikdörtgen alan ışığı; uniform nokta örneklemesi. | `AreaLight` |
| `environment_light.h` / `.cpp` | HDR ortam haritası; küresel yön örneklemesi. | `EnvironmentLight` |

---

### camera — Kamera

Birincil ışın üreten kamera modelleri.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_camera` statik kütüphanesini tanımlar. | — |
| `camera.h` | Kamera soyut temel sınıfı. | `Camera`, `generateRay()` |
| `perspective_camera.h` / `.cpp` | İdeal iğne deliği perspektif kamera. | `PerspectiveCamera` |
| `thin_lens_camera.h` / `.cpp` | Alan derinliği simülasyonlu ince lens kamera. | `ThinLensCamera` |

---

### integrators — Işık bütünleştiriciler

Piksel radyansını hesaplayan Monte Carlo algoritmaları.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_integrators` statik kütüphanesini tanımlar. | — |
| `integrator.h` | Render bütünleştirici soyut arayüzü. | `Integrator`, `Li()` |
| `path_tracer.h` / `.cpp` | Çok sekmeli path tracer; Rus ruleti destekli. | `PathTracer::Li()` |

---

### samplers — Örnekleyiciler

Monte Carlo varyans azaltma için rastgele örnek üreticileri.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_samplers` statik kütüphanesini tanımlar. | — |
| `sampler.h` | 1D/2D örnek üreten soyut sınıf. | `Sampler` — `get1D()`, `get2D()` |
| `independent_sampler.h` / `.cpp` | Bağımsız beyaz gürültü örnekleyici. | `IndependentSampler` |
| `stratified_sampler.h` / `.cpp` | Katmanlı (stratified) örnekleyici; varyans azaltma. | `StratifiedSampler` |

---

### io — Dosya yükleme

Harici model ve sahne dosyalarının yüklenmesi.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_io` statik kütüphanesini tanımlar. | — |
| `obj_loader.h` / `.cpp` | Wavefront OBJ model yükleyici (tinyobjloader). | `ObjLoader::load()` |
| `gltf_loader.h` / `.cpp` | glTF 2.0 model yükleyici; PBR malzeme + mesh çıkarma (cgltf). | `GltfLoader`, `GltfLoadResult` |

---

### engine — Render motoru

Sahne yönetimi ve çok iş parçacıklı render orkestrasyonu.

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `CMakeLists.txt` | `photon_engine` statik kütüphanesi; tüm alt modülleri bağlar. | — |
| `scene.h` / `scene.cpp` | Render sahnesi; geometri, ışıklar, BVH ve ortam haritası. | `Scene::addShape()`, `buildAccelerator()` |
| `render_settings.h` | Render kalite ve çıktı ayarları (çözünürlük, SPP, ton eşleme). | `RenderSettings` |
| `renderer.h` / `renderer.cpp` | Karo tabanlı çok iş parçacıklı renderer. | `render()`, `renderProgressive()`, `renderSamplePass()` |

---

### scene — Editör sahne verisi

Masaüstü uygulamasının düzenlenebilir sahne grafiği ve proje yönetimi.

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `CMakeLists.txt` | `photon_scene` statik kütüphanesini tanımlar. | — |
| `scene_node.h` | Hiyerarşik sahne düğümü (grup, mesh, küre, ışık). | `SceneNode`, `SceneNodeType` |
| `scene_graph.h` / `.cpp` | Düzenlenebilir sahne grafiği; düz `Scene`'e derleme. | `SceneGraph::compile()`, `findByPickId()` |
| `material_library.h` / `.cpp` | JSON malzeme preset kütüphanesi; `assets/materials/` dizininden yükleme. | `MaterialLibrary`, `createMaterial()` |
| `project_io.h` / `.cpp` | Proje kaydetme/yükleme ve geri alma yığını. | `UndoStack`, `saveProject()`, `loadProject()` |
| `cornell_box.h` | SceneGraph için Cornell Box kurulum yardımcıları (inline). | `buildCornellBox()`, `addQuad()`, `addBox()` |

---

### preview — OpenGL önizleme

Render çıktısının viewport'ta gösterilmesi ve nesne seçimi.

| Dosya | Açıklama | Ana tipler |
|-------|----------|------------|
| `CMakeLists.txt` | `photon_preview` statik kütüphanesini tanımlar. | — |
| `gl_preview.h` / `.cpp` | GPU hızlı önizleme ve nesne seçme (picking) geçişleri. | `GLPreview`, `PickingPass` |
| `viewport_texture.h` / `.cpp` | Render çıktısını OpenGL dokuya yükleyip çizer; ton eşleme + gamma. | `ViewportTexture::upload()`, `draw()` |

> **Not:** `GLPreview` şu an stub durumunda; gerçek GPU rasterizasyon yolu henüz açık değil.

---

### ui — Kullanıcı arayüzü

ImGui tabanlı masaüstü arayüz yardımcıları.

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `CMakeLists.txt` | `photon_ui` statik kütüphanesini tanımlar. | — |
| `theme.h` / `.cpp` | ImGui KeyShot benzeri koyu tema uygulayıcı. | `applyKeyShotTheme()` |
| `drag_drop.h` | Sürükle-bırak payload sabitleri (malzeme, model, doku, HDR). | `kPayloadMaterial`, `kPayloadModel` |
| `orbit_camera.h` / `.cpp` | Küresel koordinatlı orbit kamera kontrolcüsü. | `OrbitCamera::orbit()`, `pan()`, `zoom()` |

---

### app — Masaüstü uygulama

GLFW + ImGui ile tam özellikli görsel editör.

| Dosya | Açıklama | Ana tipler / fonksiyonlar |
|-------|----------|---------------------------|
| `CMakeLists.txt` | `photon_app` çalıştırılabilir hedefi; `assets/` dizinini çıktıya kopyalar. | — |
| `main_app.cpp` | GUI uygulaması giriş noktası. | `main()` |
| `application.h` | Tüm uygulama durumu ve ImGui panel yapısı. | `AppState`, `Application` |
| `application.cpp` | Pencere yönetimi, render thread, model/HDR import, viewport/inspector/library panelleri. | `Application::run()`, `importModel()` |

---

## assets/ — Hazır varlıklar

Uygulama başlangıcında yüklenen önceden tanımlı malzeme ve stüdyo preset'leri.

### `assets/materials/`

Her dosya bir malzeme preset'ini JSON formatında tanımlar. `MaterialLibrary` bu dosyaları okuyarak `DisneyMaterial` örnekleri oluşturur.

| Dosya | Açıklama |
|-------|----------|
| `blue_fabric.json` | Mavi kumaş; düşük metalik, yüksek roughness. |
| `brushed_aluminum.json` | Fırçalanmış alüminyum; orta metalik, orta roughness. |
| `chrome.json` | Parlak krom; tam metalik, çok düşük roughness. |
| `clear_glass.json` | Şeffaf cam; dielektrik özellikler. |
| `frosted_glass.json` | Buzlu cam; yüksek roughness, yarı saydam. |
| `glossy_black.json` | Parlak siyah plastik; düşük roughness. |
| `gold_metal.json` | Altın metal; sıcak baseColor, tam metalik. |
| `led_panel.json` | LED panel; emissive özellikli aydınlatma yüzeyi. |
| `linen.json` | Keten kumaş; mat, difüz yüzey. |
| `neon_red.json` | Neon kırmızı; emissive, yüksek parlaklık. |
| `red_plastic.json` | Kırmızı plastik; difüz, düşük metalik. |
| `white_plastic.json` | Beyaz plastik; nötr difüz yüzey. |

**Ortak JSON alanları:** `id`, `name`, `category`, `baseColor`, `metallic`, `roughness`, `specular`

### `assets/studios/`

Stüdyo ortamı preset'leri; kamera, ışık yoğunluğu ve pozlama ayarlarını içerir.

| Dosya | Açıklama |
|-------|----------|
| `product_studio.json` | Ürün fotoğrafçılığı stüdyosu; dar FOV, yüksek ışık yoğunluğu. |
| `outdoor_hdri.json` | Dış mekan HDR ortam haritası ayarları. |

---

## tests/ — Birim testleri

Google Test ile yazılmış birim testleri. `photon_engine` kütüphanesine bağlanır.

| Dosya | Açıklama |
|-------|----------|
| `CMakeLists.txt` | `photon_tests` çalıştırılabilir hedefini ve CTest kaydını tanımlar. |
| `test_vec.cpp` | Vektör aritmetiği testleri. |
| `test_ray.cpp` | Işın oluşturma ve parametrizasyon testleri. |
| `test_aabb.cpp` | AABB kesişim ve genişletme testleri. |
| `test_color.cpp` | Renk/spektrum işlemleri testleri. |
| `test_sampling.cpp` | Monte Carlo örnekleme fonksiyonları testleri. |
| `test_sphere.cpp` | Küre–ışın kesişim testleri. |
| `test_triangle.cpp` | Üçgen–ışın kesişim testleri. |
| `test_bvh.cpp` | BVH inşa ve kesişim testleri. |

---

## docs/ — Dokümantasyon

| Dosya | Açıklama |
|-------|----------|
| `architecture.md` | Modül bağımlılık grafiği, veri akışı ve derleme hedefleri. |
| `file-reference.md` | Bu belge; her kaynak dosyanın ne işe yaradığını açıklar. |

---

## Bilinen sınırlamalar

| Alan | Durum |
|------|-------|
| `GLPreview` | Stub; gerçek GPU rasterizasyon henüz yok. |
| `saveProject` / `loadProject` | Minimal stub; şimdilik yalnızca kamera JSON'u. |
| `DisneyMaterial` doku yolları | Saklanır ama CPU integrator henüz kullanmaz. |
| `gpu/` modülü | `PHOTON_BUILD_GPU=ON` ile opsiyonel; henüz plan aşamasında. |
