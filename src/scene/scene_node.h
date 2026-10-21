// scene_node.h — Sahne ağacının tek bir düğümü (grup, mesh ya da küre).
//
// Her düğümün hiç tekrar kullanılmayan bir kimliği (uid) vardır: seçim, geri al
// ve viewport'ta tıklayarak seçme bu kimlikle çalışır; ham işaretçiler silinen
// düğümlerde sarkık (dangling) kalacağı için saklanmaz.
#pragma once

#include "core/math/transform.h"
#include "geometry/mesh.h"
#include "materials/material.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

enum class SceneNodeType { Group, Mesh, Sphere, Light };

/// Uygulama ömrü boyunca artan kimlik sayacı (0 = geçersiz).
inline uint64_t allocateNodeUid() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

struct SceneNode {
    std::string name;
    SceneNodeType type = SceneNodeType::Group;
    Transform localTransform;
    std::vector<std::unique_ptr<SceneNode>> children;
    std::shared_ptr<TriangleMesh> mesh;      ///< Değişmez geometri; kopyalar paylaşır
    std::shared_ptr<Material> material;
    float sphereRadius = 0.0f;
    bool visible = true;
    uint32_t pickId = 0;                     ///< Eski GL seçim geçişi için
    uint64_t uid = allocateNodeUid();
    std::string sourcePath;                  ///< İçe aktarılan dosya (.obj/.gltf), grup düğümünde
    int sourceIndex = -1;                    ///< Kaynak dosyadaki mesh sırası (proje yeniden yüklemede)

    SceneNode* parent = nullptr;

    explicit SceneNode(std::string n, SceneNodeType t = SceneNodeType::Group)
        : name(std::move(n)), type(t) {}

    SceneNode* addChild(std::unique_ptr<SceneNode> child) {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }

    Transform worldTransform() const {
        Transform t = localTransform;
        for (const SceneNode* p = parent; p; p = p->parent) {
            t = p->localTransform * t;
        }
        return t;
    }
};

} // namespace photon
