#include "scene/scene_graph.h"
#include "geometry/triangle.h"
#include "geometry/sphere.h"
#include <algorithm>

namespace photon {

SceneGraph::SceneGraph() {
    m_root = std::make_unique<SceneNode>("Root", SceneNodeType::Group);
}

uint32_t SceneGraph::allocatePickId() {
    return m_nextPickId++;
}

SceneNode* SceneGraph::findByPickId(uint32_t id) {
    std::vector<SceneNode*> stack{m_root.get()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->pickId == id) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

void SceneGraph::clear() {
    m_root->children.clear();
    m_selected = nullptr;
    m_nextPickId = 1;
}

bool SceneGraph::removeNode(SceneNode* node) {
    if (!node || !node->parent || node == m_root.get()) return false;
    auto& siblings = node->parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(),
                           [node](const std::unique_ptr<SceneNode>& c) { return c.get() == node; });
    if (it == siblings.end()) return false;
    if (m_selected == node) m_selected = nullptr;
    siblings.erase(it);
    return true;
}

std::shared_ptr<TriangleMesh> SceneGraph::bakeMesh(const TriangleMesh& mesh, const Transform& xform,
                                                   const Material* material) const {
    std::vector<Vec3f> positions;
    positions.reserve(mesh.positions().size());
    for (const auto& p : mesh.positions()) {
        positions.push_back(xform.transformPoint(p));
    }
    const Material* mat = material ? material : mesh.material();
    return std::make_shared<TriangleMesh>(positions, std::vector<Vec3f>{}, std::vector<Vec2f>{},
                                          mesh.indices(), mat);
}

void SceneGraph::compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform) {
    if (!node.visible) return;
    Transform world = parentXform * node.localTransform;

    if (node.type == SceneNodeType::Mesh && node.mesh) {
        auto baked = bakeMesh(*node.mesh, world, node.material.get());
        for (size_t i = 0; i < baked->numTriangles(); ++i) {
            outScene.addShape(baked->getTriangle(i));
        }
    } else if (node.type == SceneNodeType::Sphere && node.material && node.sphereRadius > 0) {
        Vec3f center = world.transformPoint(Vec3f(0, 0, 0));
        outScene.addShape(std::make_shared<Sphere>(center, node.sphereRadius, node.material.get()));
    }

    for (const auto& child : node.children) {
        compileNode(*child, outScene, world);
    }
}

void SceneGraph::compile(Scene& outScene) {
    outScene.reset();
    compileNode(*m_root, outScene, Transform{});
    outScene.buildAccelerator();
}

} // namespace photon
