// scene_graph.cpp — Sahne ağacı işlemleri (arama, silme) ve Scene'e derleme (bake).
#include "scene/scene_graph.h"
#include "geometry/triangle.h"
#include "geometry/sphere.h"
#include "scene/material_library.h"
#include <unordered_map>
#include <algorithm>

namespace photon {

SceneGraph::SceneGraph() {
    m_root = std::make_unique<SceneNode>("Root", SceneNodeType::Group);
}

uint32_t SceneGraph::allocatePickId() {
    return m_nextPickId++;
}

SceneNode* SceneGraph::findByPickId(uint32_t id) {
    if (id == 0) return nullptr;
    std::vector<SceneNode*> stack{m_root.get()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->pickId == id) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

SceneNode* SceneGraph::findByUid(uint64_t uid) {
    if (uid == 0) return nullptr;
    std::vector<SceneNode*> stack{m_root.get()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->uid == uid) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

namespace {

std::unique_ptr<SceneNode> cloneNodeRec(const SceneNode& src, bool cloneMaterials,
                                        std::unordered_map<const Material*, std::shared_ptr<Material>>& memo) {
    auto n = std::make_unique<SceneNode>(src.name, src.type);
    n->uid = src.uid;
    n->localTransform = src.localTransform;
    n->mesh = src.mesh;
    n->sphereRadius = src.sphereRadius;
    n->visible = src.visible;
    n->pickId = src.pickId;
    n->sourcePath = src.sourcePath;
    n->sourceIndex = src.sourceIndex;
    if (src.material) {
        if (!cloneMaterials) {
            n->material = src.material;
        } else {
            auto it = memo.find(src.material.get());
            if (it != memo.end()) {
                n->material = it->second;
            } else {
                auto copy = cloneMaterial(*src.material);
                n->material = copy ? copy : src.material;
                memo[src.material.get()] = n->material;
            }
        }
    }
    for (const auto& c : src.children) n->addChild(cloneNodeRec(*c, cloneMaterials, memo));
    return n;
}

} // namespace

std::unique_ptr<SceneNode> SceneGraph::cloneTree(const SceneNode& node, bool cloneMaterials) {
    std::unordered_map<const Material*, std::shared_ptr<Material>> memo;
    return cloneNodeRec(node, cloneMaterials, memo);
}

void SceneGraph::setRoot(std::unique_ptr<SceneNode> root) {
    if (!root) return;
    root->parent = nullptr;
    m_root = std::move(root);
    ++m_revision;
}

void SceneGraph::clear() {
    m_root->children.clear();
    m_nextPickId = 1;
    ++m_revision;
}

bool SceneGraph::removeNode(SceneNode* node) {
    if (!node || !node->parent || node == m_root.get()) return false;
    auto& siblings = node->parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(),
                           [node](const std::unique_ptr<SceneNode>& c) { return c.get() == node; });
    if (it == siblings.end()) return false;
    siblings.erase(it);
    ++m_revision;
    return true;
}

// Mesh'i dünya uzayına taşır. Konumlar tam dönüşümle, normaller ters-transpoz
// matrisle dönüştürülür (ölçek eşit değilse normalin doğru kalması için).
// Dönüşümün determinantı negatifse (ayna, ör. ölçek x = -1) üçgenlerin sarma
// yönü ters döner; geometrik normal içe bakar ve cam giriş/çıkışı karışır.
// Bunu önlemek için her üçgenin iki indeksi yer değiştirir.
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
    const Mat4f& m = xform.matrix();
    const float det3 = m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
                       m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
                       m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
    std::vector<uint32_t> indices = mesh.indices();
    if (det3 < 0.0f) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
    }
    const Material* mat = material ? material : mesh.material();
    return std::make_shared<TriangleMesh>(positions, normals, mesh.uvs(), indices, mat);
}

GroundQuad placeGroundUnder(const AABB& box) {
    float dx = box.pMax.x - box.pMin.x;
    float dz = box.pMax.z - box.pMin.z;
    float dy = box.pMax.y - box.pMin.y;
    float span = std::max({dx, dz, dy, 1e-3f});
    // ponytail: dörtgen, sahne boyutunun 200 katı: pratikte ufka uzanır. Gerçek
    // sonsuz düzlem + gölge yakalayıcı (shadow catcher) ileride.
    float pad = span * 100.0f;
    float cx = (box.pMin.x + box.pMax.x) * 0.5f;
    float cz = (box.pMin.z + box.pMax.z) * 0.5f;
    GroundQuad g;
    g.corner = Vec3f(cx - pad, box.pMin.y - span * 0.0005f, cz - pad);
    g.edgeU = Vec3f(pad * 2.0f, 0.0f, 0.0f);
    g.edgeV = Vec3f(0.0f, 0.0f, pad * 2.0f);
    return g;
}

void SceneGraph::compileNode(const SceneNode& node, Scene& outScene, const Transform& parentXform,
                             bool isolateMaterials) const {
    if (!node.visible) return;
    Transform world = parentXform * node.localTransform;

    // İzole derlemede (son render) malzeme kopyalanır: render sürerken kullanıcı
    // asıl malzemeyi düzenlese bile render'ın gördüğü değerler değişmez.
    std::shared_ptr<Material> mat = node.material;
    if (isolateMaterials && mat) {
        if (auto copy = cloneMaterial(*mat)) mat = copy;
    }

    if (node.type == SceneNodeType::Mesh && node.mesh) {
        auto baked = bakeMesh(*node.mesh, world, mat.get());
        outScene.retainMaterial(mat);
        outScene.tagShape(baked.get(), node.uid);
        outScene.addShape(baked);
    } else if (node.type == SceneNodeType::Sphere && mat && node.sphereRadius > 0) {
        Vec3f center = world.transformPoint(Vec3f(0, 0, 0));
        // Eşit olmayan ölçekte küre elipsoid olmalı; ponytail: en büyük eksen ölçeği alınır.
        Vec3f sx = world.transformVector(Vec3f(1, 0, 0));
        Vec3f sy = world.transformVector(Vec3f(0, 1, 0));
        Vec3f sz = world.transformVector(Vec3f(0, 0, 1));
        float s = std::max({sx.length(), sy.length(), sz.length()});
        outScene.retainMaterial(mat);
        auto sphere = std::make_shared<Sphere>(center, node.sphereRadius * s, mat.get());
        outScene.tagShape(sphere.get(), node.uid);
        outScene.addShape(sphere);
    }

    for (const auto& child : node.children) {
        compileNode(*child, outScene, world, isolateMaterials);
    }
}

void SceneGraph::compile(Scene& outScene, const std::vector<std::shared_ptr<Light>>& lights,
                         std::shared_ptr<EnvironmentLight> environment, bool isolateMaterials) const {
    outScene.reset();
    compileNode(*m_root, outScene, Transform{}, isolateMaterials);
    for (const auto& l : lights) outScene.addLight(l);
    if (environment) outScene.setEnvironment(std::move(environment));
    outScene.buildAccelerator();
}

AABB SceneGraph::nodeWorldBounds(const SceneNode& node) {
    AABB box = AABB::empty();
    struct Walker {
        AABB& box;
        void walk(const SceneNode& n, const Transform& parent) {
            if (!n.visible) return;
            Transform world = parent * n.localTransform;
            if (n.mesh) {
                for (const auto& p : n.mesh->positions()) box.merge(world.transformPoint(p));
            } else if (n.type == SceneNodeType::Sphere && n.sphereRadius > 0) {
                Vec3f c = world.transformPoint(Vec3f(0, 0, 0));
                box.merge(c - Vec3f(n.sphereRadius));
                box.merge(c + Vec3f(n.sphereRadius));
            }
            for (const auto& c : n.children) walk(*c, world);
        }
    } w{box};
    Transform parent;
    if (node.parent) parent = node.parent->worldTransform();
    w.walk(node, parent);
    return box;
}

AABB SceneGraph::worldBounds() const {
    return nodeWorldBounds(*m_root);
}

} // namespace photon

namespace photon {

void SceneGraph::compileInto(Scene& outScene, bool isolateMaterials) const {
    compileNode(*m_root, outScene, Transform{}, isolateMaterials);
}

// ── Hiyerarşi düzenleme ──────────────────────────────────────────────────

bool SceneGraph::isAncestor(const SceneNode& a, const SceneNode& n) {
    for (const SceneNode* p = n.parent; p; p = p->parent)
        if (p == &a) return true;
    return false;
}

std::vector<SceneNode*> SceneGraph::topmost(const std::vector<SceneNode*>& nodes) const {
    std::vector<SceneNode*> out;
    // Ağacı sırayla dolaş; kümedeki bir düğüme rastlayınca onu al ve altına inme.
    auto walk = [&](auto&& self, SceneNode& n) -> void {
        for (auto& c : n.children) {
            if (std::find(nodes.begin(), nodes.end(), c.get()) != nodes.end()) out.push_back(c.get());
            else self(self, *c);
        }
    };
    walk(walk, *m_root);
    return out;
}

void SceneGraph::detachFromSource(SceneNode& node) {
    if (!node.sourcePath.empty()) return; // kendi dosyası olan grup: bağı geçerli
    node.sourceIndex = -1;
    for (auto& c : node.children) detachFromSource(*c);
}

namespace {

size_t indexInParent(const SceneNode& n) {
    const auto& sib = n.parent->children;
    for (size_t i = 0; i < sib.size(); ++i)
        if (sib[i].get() == &n) return i;
    return sib.size();
}

} // namespace

// yerel' = yeniEbeveynDünya⁻¹ · dünya: parça dünyada aynı yerde kalır.
bool SceneGraph::reparent(SceneNode* node, SceneNode* newParent, size_t index) {
    if (!node || !newParent || !node->parent || node == newParent || isAncestor(*node, *newParent)) return false;
    SceneNode* oldParent = node->parent;
    const size_t oldIndex = indexInParent(*node);
    if (oldIndex >= oldParent->children.size()) return false;
    const Mat4f world = node->worldTransform().matrix();
    std::unique_ptr<SceneNode> owned = std::move(oldParent->children[oldIndex]);
    oldParent->children.erase(oldParent->children.begin() + static_cast<std::ptrdiff_t>(oldIndex));
    if (oldParent == newParent) {
        // Aynı ebeveynde yalnız sıra değişir; dönüşüm aynen kalır (yuvarlama hatası yok).
        if (index != SIZE_MAX && oldIndex < index) --index;
    } else {
        owned->localTransform = Transform(newParent->worldTransform().inverseMatrix() * world);
        detachFromSource(*owned);
    }
    owned->parent = newParent;
    auto& to = newParent->children;
    to.insert(to.begin() + static_cast<std::ptrdiff_t>(std::min(index, to.size())), std::move(owned));
    ++m_revision;
    return true;
}

// Sıra: her taşınan düğüm bir öncekinin hemen arkasına gelir. reparent aynı
// ebeveyndeki öndeki bir düğümü çıkarırken hedef sırayı kendisi bir azaltır; o
// durumda dış sayaç sabit kalır, diğer durumlarda bir artar.
size_t SceneGraph::move(const std::vector<SceneNode*>& nodesIn, SceneNode* newParent, size_t index) {
    if (!newParent) return 0;
    std::vector<SceneNode*> nodes = topmost(nodesIn);
    std::erase_if(nodes, [&](SceneNode* n) { return n == newParent || isAncestor(*n, *newParent); });
    size_t moved = 0;
    for (SceneNode* n : nodes) {
        const bool samePrior = index != SIZE_MAX && n->parent == newParent && indexInParent(*n) < index;
        if (!reparent(n, newParent, index)) continue;
        ++moved;
        if (index != SIZE_MAX && !samePrior) ++index;
    }
    return moved;
}

SceneNode* SceneGraph::group(const std::vector<SceneNode*>& nodesIn, const std::string& name) {
    const std::vector<SceneNode*> nodes = topmost(nodesIn);
    if (nodes.empty()) return nullptr;
    SceneNode* parent = nodes.front()->parent;
    for (SceneNode* n : nodes)
        if (n->parent != parent) parent = m_root.get();
    const size_t at = parent == nodes.front()->parent ? indexInParent(*nodes.front()) : parent->children.size();
    auto g = std::make_unique<SceneNode>(name, SceneNodeType::Group);
    SceneNode* gp = g.get();
    g->parent = parent;
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(at), std::move(g));
    for (SceneNode* n : nodes) reparent(n, gp);
    ++m_revision;
    return gp;
}

std::vector<SceneNode*> SceneGraph::ungroup(SceneNode* g) {
    std::vector<SceneNode*> moved;
    if (!g || !g->parent || g->type != SceneNodeType::Group) return moved;
    SceneNode* parent = g->parent;
    size_t at = indexInParent(*g);
    // Çocukların yeni yereli = grubun yereli · çocuğun yereli (ebeveyn aynı kaldığı için).
    while (!g->children.empty()) {
        std::unique_ptr<SceneNode> c = std::move(g->children.front());
        g->children.erase(g->children.begin());
        c->localTransform = g->localTransform * c->localTransform;
        c->parent = parent;
        detachFromSource(*c);
        moved.push_back(c.get());
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(++at), std::move(c));
    }
    removeNode(g);
    return moved;
}

} // namespace photon
