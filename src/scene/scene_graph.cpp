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
    ++m_revision;
}

bool SceneGraph::removeNode(SceneNode* node) {
    if (!node || !node->parent || node == m_root.get()) return false;
    auto& siblings = node->parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(),
                           [node](const std::unique_ptr<SceneNode>& c) { return c.get() == node; });
    if (it == siblings.end()) return false;
    if (m_selected == node) m_selected = nullptr;
    siblings.erase(it);
    ++m_revision;
    return true;
}

std::shared_ptr<TriangleMesh> SceneGraph::bakeMesh(const TriangleMesh& mesh, const Transform& xform,
                                                   const Material* material) const {
    std::vector<Vec3f> positions;
    positions.reserve(mesh.positions().size());
    for (const auto& p : mesh.positions()) {
        positions.push_back(xform.transformPoint(p));
    }
    std::vector<Vec3f> normals;
    if (!mesh.normals().empty()) {
        normals.reserve(mesh.normals().size());
        for (const auto& n : mesh.normals()) {
            Vec3f tn = xform.transformNormal(n);
            if (tn.lengthSquared() > 0.0f) tn = tn.normalized();
            normals.push_back(tn);
        }
    }
    const Material* mat = material ? material : mesh.material();
    return std::make_shared<TriangleMesh>(positions, normals, mesh.uvs(), mesh.indices(), mat);
}

GroundQuad placeGroundUnder(const AABB& box) {
    float dx = box.pMax.x - box.pMin.x;
    float dz = box.pMax.z - box.pMin.z;
    if (dx < 1.0f) dx = 1.0f;
    if (dz < 1.0f) dz = 1.0f;
    float span = dx > dz ? dx : dz;
    // ponytail: pad is 2x the longest footprint (quad is 4x). Not an infinite plane.
    float pad = span * 2.0f;
    float cx = (box.pMin.x + box.pMax.x) * 0.5f;
    float cz = (box.pMin.z + box.pMax.z) * 0.5f;
    GroundQuad g;
    g.corner = Vec3f(cx - pad, box.pMin.y - 0.02f, cz - pad);
    g.edgeU = Vec3f(pad * 2.0f, 0.0f, 0.0f);
    g.edgeV = Vec3f(0.0f, 0.0f, pad * 2.0f);
    return g;
}

void SceneGraph::compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform) {
    if (!node.visible) return;
    Transform world = parentXform * node.localTransform;

    if (node.type == SceneNodeType::Mesh && node.mesh) {
        auto baked = bakeMesh(*node.mesh, world, node.material.get());
        outScene.addShape(baked);
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
