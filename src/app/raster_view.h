// raster_view.h — Viewport'un GPU (OpenGL 3.3) görüntü modları: Gerçek zamanlı (PBR), Katı,
// Tel kafes, Normaller ve ışın izlenmiş görüntünün üstüne bindirilen tel kafes.
//
// Işın izleyici ürünün "son" görüntüsünü verir ama büyük modelde yavaştır; bu modlar
// geometriyi her karede rasterleştirerek modelleme/yerleştirme sırasında anında
// geri bildirim verir (KeyShot'ın "geometri görünümü", Blender'ın "solid" modu).
// Çıktı ekran dışı bir dokuya (FBO, 4× MSAA) çizilir; viewport onu resim olarak koyar.
//
// Gerçek zamanlı mod oyun motoru gibi çalışır (forward PBR, saniyede onlarca kare): GGX
// metal/pürüzlülük, aynı ışıklar (Kelvin, IES hariç koni), tek gölge haritalı ana ışık (PCF),
// ortamdan SH9 difüz + mip zincirinden parlak yansıma, zemin ve temas gölgesi, dokular, cam.
// Girdiler ışın izleyiciyle aynı kaynaktan hazırlanır (scene/realtime_scene.h).
#pragma once

#include "core/math/mat.h"
#include "core/math/vec.h"
#include "core/color/spectrum.h"
#include "core/image/image.h"
#include "geometry/mesh.h"
#include "scene/realtime_scene.h"
#include "scene/scene_graph.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace photon {

/// Viewport görüntü modu. Render ve Kil ışın izleyiciyle, diğerleri GPU ile çizilir.
/// Sayısal değerler kalıcıdır (yeni mod sona eklenir); menüde ve Z döngüsünde kViewModeOrder sırası.
enum class ViewMode { Render = 0, Clay, Solid, Wire, Normals, Realtime };
constexpr int kViewModeCount = 6;
constexpr ViewMode kViewModeOrder[kViewModeCount] = {ViewMode::Render, ViewMode::Realtime, ViewMode::Clay,
                                                     ViewMode::Solid, ViewMode::Wire, ViewMode::Normals};
const char* viewModeName(ViewMode m);
inline bool isRasterMode(ViewMode m) {
    return m == ViewMode::Solid || m == ViewMode::Wire || m == ViewMode::Normals || m == ViewMode::Realtime;
}

class RasterView {
public:
    /// Gerçek zamanlı modda yüzey (MaterialPreset'ten; dokular malzemenin yüklü görüntüleri).
    struct Material {
        Color3f baseColor{0.7f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.03f;
        Color3f emission;              ///< doğrusal radyans
        bool glass = false;
        float ior = 1.5f;
        Color3f tint{1.0f};            ///< camın geçirgenlik rengi
        std::shared_ptr<const Image> albedo, roughnessMap, metalnessMap, normalMap;
        bool boxMapping = false;
        float texScale = 1.0f;
    };
    struct Item {
        std::shared_ptr<const TriangleMesh> mesh; ///< Yerel uzayda geometri (dokuda önbelleklenir)
        Mat4f model;                              ///< Dünya dönüşümü
        Color3f color{0.7f};                      ///< Katı modda yüzey rengi (malzemenin taban rengi)
        bool selected = false;
        std::shared_ptr<const Material> material; ///< Gerçek zamanlı mod (yoksa color ile mat plastik)
    };
    /// Gerçek zamanlı modun sahne girdileri.
    struct Realtime {
        std::shared_ptr<const RealtimeEnvironment> env;   ///< işaretçi değişince GPU'ya yeniden yüklenir
        std::shared_ptr<const ToneLut> lut;
        std::vector<RealtimeLight> lights;
        float exposure = 1.0f;                            ///< 2^EV
        float whiteBalance[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        int background = 0;                               ///< 0 ortam, 1 düz renk, 2 şeffaf
        Color3f backgroundColor{0.18f};
        bool ground = false;
        bool groundShadowOnly = false;
        GroundQuad groundQuad;
        Color3f groundColor{0.42f};
        float groundRoughness = 0.55f;
        Color3f groundExtra;                              ///< ışıkların zemine verdiği radyans (alt yarıküre)
        AABB sceneBounds;
        /// Temas gölgesi için nesne izleri: (merkez x, merkez z, yarı genişlik x, yarı genişlik z) ve zeminden yükseklik.
        std::vector<Vec4f> footprints;
        std::vector<float> footprintGaps;
    };
    struct Frame {
        int width = 0, height = 0;                ///< Fiziksel piksel
        Mat4f view, proj;
        Vec3f eye;
        Vec3f focus;                              ///< Kameranın baktığı nokta (ızgara buraya göre söner)
        float groundY = 0.0f;                     ///< Izgaranın yüksekliği (zeminin olduğu yer)
        float gridStep = 0.1f;                    ///< Izgara hücresi (sahne boyutundan)
        bool grid = true;
        const Realtime* realtime = nullptr;       ///< mode = Realtime iken
    };

    RasterView() = default;
    RasterView(const RasterView&) = delete;
    RasterView& operator=(const RasterView&) = delete;

    /// GL bağlamı hazırken bir kez. false: sürücü gerekli işlevleri vermedi (modlar kapalı).
    bool init();
    /// GL kaynaklarını bırakır; bağlam yok edilmeden önce çağrılmalı.
    void release();
    bool ready() const { return m_ready; }

    /// @p mode Solid/Wire/Normals: tam görüntü. overlay = true: şeffaf zemin üstünde
    /// yalnız görünür kenarlar (ışın izlenmiş görüntünün üstüne bindirilir).
    /// Dokunun GL kimliğini döndürür (0 = çizilemedi). Doku alttan üste saklanır:
    /// ImGui'de uv (0,1)-(1,0) ile çevrilerek gösterilir.
    unsigned int render(const Frame& f, const std::vector<Item>& items, ViewMode mode, bool overlay);

private:
    struct GpuMesh {
        unsigned int vao = 0, vbo = 0, ibo = 0;
        int indexCount = 0;
        std::weak_ptr<const TriangleMesh> source; ///< Mesh silinince GPU kopyası da bırakılır
    };
    struct GpuTexture {
        unsigned int tex = 0;
        std::weak_ptr<const Image> source;
    };
    GpuMesh* upload(const std::shared_ptr<const TriangleMesh>& mesh);
    unsigned int texture(const std::shared_ptr<const Image>& image, bool srgb);
    void purge();
    bool ensureTargets(int w, int h);
    void renderRealtime(const Frame& f, const std::vector<Item>& items);
    bool ensureShadowTargets();
    void drawMesh(const GpuMesh& g) const;

    bool m_ready = false;
    unsigned int m_progShade = 0;  ///< Katı + normal (uMode ile)
    unsigned int m_progFlat = 0;   ///< Tek renk (tel kafes, ızgara, derinlik ön geçişi)
    unsigned int m_progPbr = 0;    ///< Gerçek zamanlı yüzeyler
    unsigned int m_progDepth = 0;  ///< Gölge haritası
    unsigned int m_progBg = 0;     ///< Arka plan (ortam ya da renk), tam ekran üçgen
    unsigned int m_quadVao = 0, m_quadVbo = 0, m_quadIbo = 0; ///< Birim zemin dörtgeni (y = 0, x,z ∈ [0,1])
    unsigned int m_emptyVao = 0;
    unsigned int m_envTex = 0, m_lutTex = 0;
    int m_envLevels = 1;
    std::weak_ptr<const RealtimeEnvironment> m_envSource;
    std::weak_ptr<const ToneLut> m_lutSource;
    unsigned int m_shadowFbo = 0, m_shadowTex = 0;
    unsigned int m_sampCmp = 0, m_sampRaw = 0;            ///< aynı derinlik dokusu: PCF / engelleyici araması
    unsigned int m_glassFbo = 0, m_glassTex = 0;          ///< yalnız camların derinliği (kısmi gölge), ½ çözünürlük
    bool m_shadowOk = false;
    static constexpr int kShadowSize = 2048;              ///< bir ışığın gölge haritası
    static constexpr int kShadowAtlas = 2 * kShadowSize;  ///< 2×2 atlas: en çok 4 gölgeli ışık
    std::unordered_map<const Image*, GpuTexture> m_textures[2]; ///< [0] doğrusal, [1] sRGB
    unsigned int m_gridVao = 0, m_gridVbo = 0;
    int m_gridVerts = 0;
    unsigned int m_msFbo = 0, m_msColor = 0, m_msDepth = 0; ///< 4× çok örnekli hedef
    unsigned int m_fbo = 0, m_tex = 0;                        ///< Çözülmüş (resolve) doku
    int m_w = 0, m_h = 0;
    std::unordered_map<const TriangleMesh*, GpuMesh> m_meshes;
};

} // namespace photon
