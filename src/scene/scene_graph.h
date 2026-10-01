#pragma once

#include "scene/scene_node.h"
#include "engine/scene.h"
#include "lights/light.h"
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
    SceneNode* selected() { return m_selected; }
    void setSelected(SceneNode* node) { m_selected = node; }

    void compile(Scene& outScene);
    uint32_t allocatePickId();
    bool removeNode(SceneNode* node);

    void clear();
    bool empty() const { return m_root->children.empty(); }

    /// Bumps on clear/delete so the GL preview can drop VAOs for freed meshes.
    uint32_t geometryRevision() const { return m_revision; }

private:
    std::unique_ptr<SceneNode> m_root;
    SceneNode* m_selected = nullptr;
    uint32_t m_nextPickId = 1;
    uint32_t m_revision = 1;

    void compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform);
    std::shared_ptr<TriangleMesh> bakeMesh(const TriangleMesh& mesh, const Transform& xform,
                                           const Material* material) const;
};

/// Quad under a scene box. edgeU/edgeV lie in the XZ plane, corner.y is below the box.
struct GroundQuad {
    Vec3f corner;
    Vec3f edgeU;
    Vec3f edgeV;
};

GroundQuad placeGroundUnder(const AABB& bounds);

} // namespace photon
