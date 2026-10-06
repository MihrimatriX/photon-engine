// raster_view.h — Viewport'un GPU (OpenGL 3.3) görüntü modları: Katı, Tel kafes,
// Normaller ve ışın izlenmiş görüntünün üstüne bindirilen tel kafes.
//
// Işın izleyici ürünün "son" görüntüsünü verir ama büyük modelde yavaştır; bu modlar
// geometriyi her karede rasterleştirerek modelleme/yerleştirme sırasında anında
// geri bildirim verir (KeyShot'ın "geometri görünümü", Blender'ın "solid" modu).
// Çıktı ekran dışı bir dokuya (FBO, 4× MSAA) çizilir; viewport onu resim olarak koyar.
#pragma once

#include "core/math/mat.h"
#include "core/math/vec.h"
#include "core/color/spectrum.h"
#include "geometry/mesh.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace photon {

/// Viewport görüntü modu. Render ve Kil ışın izleyiciyle, diğerleri GPU ile çizilir.
enum class ViewMode { Render = 0, Clay, Solid, Wire, Normals };
constexpr int kViewModeCount = 5;
const char* viewModeName(ViewMode m);
inline bool isRasterMode(ViewMode m) { return m == ViewMode::Solid || m == ViewMode::Wire || m == ViewMode::Normals; }

class RasterView {
public:
    struct Item {
        std::shared_ptr<const TriangleMesh> mesh; ///< Yerel uzayda geometri (dokuda önbelleklenir)
        Mat4f model;                              ///< Dünya dönüşümü
        Color3f color{0.7f};                      ///< Katı modda yüzey rengi (malzemenin taban rengi)
        bool selected = false;
    };
    struct Frame {
        int width = 0, height = 0;                ///< Fiziksel piksel
        Mat4f view, proj;
        Vec3f eye;
        Vec3f focus;                              ///< Kameranın baktığı nokta (ızgara buraya göre söner)
        float groundY = 0.0f;                     ///< Izgaranın yüksekliği (zeminin olduğu yer)
        float gridStep = 0.1f;                    ///< Izgara hücresi (sahne boyutundan)
        bool grid = true;
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
    GpuMesh* upload(const std::shared_ptr<const TriangleMesh>& mesh);
    void purge();
    bool ensureTargets(int w, int h);

    bool m_ready = false;
    unsigned int m_progShade = 0;  ///< Katı + normal (uMode ile)
    unsigned int m_progFlat = 0;   ///< Tek renk (tel kafes, ızgara, derinlik ön geçişi)
    unsigned int m_gridVao = 0, m_gridVbo = 0;
    int m_gridVerts = 0;
    unsigned int m_msFbo = 0, m_msColor = 0, m_msDepth = 0; ///< 4× çok örnekli hedef
    unsigned int m_fbo = 0, m_tex = 0;                        ///< Çözülmüş (resolve) doku
    int m_w = 0, m_h = 0;
    std::unordered_map<const TriangleMesh*, GpuMesh> m_meshes;
};

} // namespace photon
