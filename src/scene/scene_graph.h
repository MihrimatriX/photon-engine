// scene_graph.h — Düzenlenebilir sahne ağacı (hiyerarşi) ve render sahnesine derleme.
//
// Kullanıcının gördüğü "Sahne" paneli bu ağaçtır: gruplar, mesh'ler, küreler.
// Her düğümün yerel dönüşümü (konum/dönüş/ölçek), malzemesi ve görünürlüğü var.
// Render için compile() ağacı dolaşır, her mesh'i dünya uzayına "bake" eder
// (köşeleri dönüştürür) ve düz bir Scene + BVH üretir.
#pragma once

#include "scene/scene_node.h"
#include "engine/scene.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include "core/math/aabb.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace photon {

class SceneGraph {
public:
    SceneGraph();

    SceneNode* root() { return m_root.get(); }
    const SceneNode* root() const { return m_root.get(); }

    SceneNode* findByPickId(uint32_t id);
    SceneNode* findByUid(uint64_t uid);

    /// Ağacı @p outScene'e derle: geometri + ışıklar + ortam, sonra BVH (tek sefer).
    void compile(Scene& outScene,
                 const std::vector<std::shared_ptr<Light>>& lights = {},
                 std::shared_ptr<EnvironmentLight> environment = nullptr,
                 bool isolateMaterials = false) const;

    /// Ağacın derin kopyası. Geometri (değişmez) paylaşılır; cloneMaterials true ise
    /// malzemeler de kopyalanır (aynı malzemeyi paylaşan düğümler kopyada da paylaşır).
    /// uid'ler korunur: geri alma sonrası seçim aynı düğümü bulur.
    static std::unique_ptr<SceneNode> cloneTree(const SceneNode& node, bool cloneMaterials);
    /// Kök düğümü değiştir (geri al / yinele).
    void setRoot(std::unique_ptr<SceneNode> root);

    uint32_t allocatePickId();
    bool removeNode(SceneNode* node);

    void clear();
    bool empty() const { return m_root->children.empty(); }

    /// Silme/temizlemede artar; GL önizleme serbest kalan mesh'lerin VAO'larını bırakır.
    uint32_t geometryRevision() const { return m_revision; }
    void bumpRevision() { ++m_revision; }

    /// Görünür düğümlerin dünya uzayı sınırları (boşsa AABB::empty()).
    AABB worldBounds() const;
    /// Tek bir alt ağacın dünya uzayı sınırları.
    static AABB nodeWorldBounds(const SceneNode& node);

private:
    std::unique_ptr<SceneNode> m_root;
    uint32_t m_nextPickId = 1;
    uint32_t m_revision = 1;

    void compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform,
                     bool isolateMaterials) const;
    std::shared_ptr<TriangleMesh> bakeMesh(const TriangleMesh& mesh, const Transform& xform,
                                           const Material* material) const;
};

/// Sahne kutusunun altına yerleşen zemin dörtgeni. edgeU/edgeV XZ düzleminde.
struct GroundQuad {
    Vec3f corner;
    Vec3f edgeU;
    Vec3f edgeV;
};

GroundQuad placeGroundUnder(const AABB& bounds);

} // namespace photon
