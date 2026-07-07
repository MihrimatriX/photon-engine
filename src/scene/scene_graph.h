#pragma once

#include "scene/scene_node.h"
#include "engine/scene.h"
#include "lights/light.h"
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

    void clear();
    bool empty() const { return m_root->children.empty(); }

private:
    std::unique_ptr<SceneNode> m_root;
    SceneNode* m_selected = nullptr;
    uint32_t m_nextPickId = 1;

    void compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform);
    std::shared_ptr<TriangleMesh> bakeMesh(const TriangleMesh& mesh, const Transform& xform) const;
};

} // namespace photon
