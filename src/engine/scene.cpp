// scene.cpp — Render sahnesine şekil/ışık ekleme, ışık bağlama/gruplama tabloları ve kesişim sorguları.
#include "engine/scene.h"
#include "geometry/cylinder.h"
#include "geometry/disk.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "lights/area_light.h"
#include "lights/shape_lights.h"
#include "materials/lambertian.h"
#include <atomic>
#include <unordered_map>

namespace photon {

namespace {
const std::string kEmptyName;
constexpr int kMaxSceneLightGroups = 16;
} // namespace

void Scene::addShape(std::shared_ptr<Shape> shape) {
    if (!shape) return;

    auto inst = std::dynamic_pointer_cast<MeshInstance>(shape);
    auto mesh = std::dynamic_pointer_cast<TriangleMesh>(shape);
    m_shapes.push_back(std::move(shape));

    if (inst) {
        const Material* mat = inst->material();
        if (!mat || !inst->mesh()) return;
        if (mat->transmitsShadows()) m_shadowTransmitters = true;
        SurfaceInteraction si;
        const Color3f emission = mat->emitted(si);
        if (emission.isBlack()) return;
        m_meshEmitterBase[inst.get()] = static_cast<int>(m_lightPtrs.size());
        const Transform& xf = inst->transform0();
        const TriangleMesh& m = *inst->mesh();
        for (size_t i = 0; i < m.numTriangles(); ++i) {
            Vec3f p0, p1, p2;
            m.triangleVertices(i, p0, p1, p2);
            auto light = std::make_shared<TriangleLight>(xf.transformPoint(p0), xf.transformPoint(p1),
                                                         xf.transformPoint(p2), emission);
            m_lights.push_back(light);
            m_lightPtrs.push_back(light.get());
        }
        return;
    }

    if (!mesh || !mesh->material()) return;
    if (mesh->material()->transmitsShadows()) m_shadowTransmitters = true;

    // Emisyonlu mesh (ör. LED panel malzemesi) → her üçgeni bir ışık; ışık BVH'si yakın ve bize
    // bakan üçgenleri seçer. Alan ışığı dörtgenleri mesh değil Triangle olduğu için iki kez
    // kaydedilmez. Sıfır alanlı üçgenler de yer tutar ki sıra = base + primIndex kalsın.
    SurfaceInteraction si;
    Color3f emission = mesh->material()->emitted(si);
    if (emission.isBlack()) return;
    m_meshEmitterBase[mesh.get()] = static_cast<int>(m_lightPtrs.size());
    for (size_t i = 0; i < mesh->numTriangles(); ++i) {
        Vec3f p0, p1, p2;
        mesh->triangleVertices(i, p0, p1, p2);
        auto light = std::make_shared<TriangleLight>(p0, p1, p2, emission);
        m_lights.push_back(light);
        m_lightPtrs.push_back(light.get());
    }
}

void Scene::addLight(std::shared_ptr<Light> light) {
    if (!light) return;
    const int index = static_cast<int>(m_lightPtrs.size());
    m_lights.push_back(light);
    m_lightPtrs.push_back(light.get());

    // Alan ışıkları başka türlü kesişilemez: BSDF ile örneklenen bir ışın ışığa
    // çarpınca emisyonunu göremez ve NEE'nin MIS ağırlığı o enerjiyi atar.
    // Aynı yüzeyi BVH'ye yalnız ışık yayan siyah bir Lambertian olarak asarız: dörtgen için iki
    // üçgen, disk/küre/tüp için analitik şekil (örnekleme ile birebir aynı alan).
    auto emitter = [&](const Color3f& radiance, bool oneSided) {
        auto mat = std::make_shared<Lambertian>(Color3f::black(), radiance);
        mat->setOneSidedEmission(oneSided);
        m_emissiveMaterials.push_back(mat);
        return mat.get();
    };
    auto attach = [&](const std::shared_ptr<Shape>& shape, bool visibleToCamera) {
        m_emitterOfShape[shape.get()] = index;
        if (!visibleToCamera) {
            m_cameraInvisible.insert(shape.get());
            shape->setVisibilityMask(static_cast<uint8_t>(VisAll & ~VisCamera));
        }
        addShape(shape);
    };

    if (auto area = std::dynamic_pointer_cast<AreaLight>(light)) {
        const Material* m = emitter(area->radiance(), !area->twoSided());
        const Vec3f p0 = area->position();
        const Vec3f p1 = p0 + area->u();
        const Vec3f p2 = p1 + area->v();
        const Vec3f p3 = p0 + area->v();
        const Vec3f n = area->normal();
        const Vec2f uv0(0.0f, 0.0f);
        const Vec2f uv1(1.0f, 0.0f);
        const Vec2f uv2(1.0f, 1.0f);
        const Vec2f uv3(0.0f, 1.0f);
        attach(std::make_shared<Triangle>(p0, p1, p2, n, n, n, uv0, uv1, uv2, m), area->visibleToCamera());
        attach(std::make_shared<Triangle>(p0, p2, p3, n, n, n, uv0, uv2, uv3, m), area->visibleToCamera());
    } else if (auto disk = std::dynamic_pointer_cast<DiskLight>(light)) {
        const Material* m = emitter(disk->radiance(), !disk->twoSided());
        attach(std::make_shared<Disk>(disk->center(), disk->ex(), disk->ey(), m), disk->visibleToCamera());
    } else if (auto sphere = std::dynamic_pointer_cast<SphereLight>(light)) {
        const Material* m = emitter(sphere->radiance(), true);
        attach(std::make_shared<Sphere>(sphere->center(), sphere->radius(), m), sphere->visibleToCamera());
    } else if (auto tube = std::dynamic_pointer_cast<CylinderLight>(light)) {
        const Material* m = emitter(tube->radiance(), true);
        attach(std::make_shared<Cylinder>(tube->center(), tube->axis() * tube->halfLength(), tube->radius(), m),
               tube->visibleToCamera());
    }
}

void Scene::setEnvironment(std::shared_ptr<EnvironmentLight> envLight) {
    m_environment = std::move(envLight);
    if (m_built) m_lightSampler.build(m_lightPtrs, m_environment.get());
}

void Scene::retainMaterial(std::shared_ptr<const Material> material) {
    if (!material) return;
    if (material->transmitsShadows()) m_shadowTransmitters = true;
    m_materials.push_back(std::move(material));
}

namespace {
std::atomic<bool> gPreferOwnBvh{false};
}

void Scene::setPreferOwnBvh(bool own) {
    gPreferOwnBvh = own;
}

void Scene::buildAccelerator() {
    m_lightSampler.build(m_lightPtrs, m_environment.get());

    // ── Light Linking & Shadow Linking tabloları ──
    m_lightLinks.clear();
    m_lightLinks.resize(m_lightPtrs.size());
    m_hasLightLinks = false;
    m_hasShadowLinks = false;
    for (size_t i = 0; i < m_lightPtrs.size(); ++i) {
        const LightLinkSet& ls = m_lightPtrs[i]->lightLink();
        CompiledLightLink cl;
        cl.mode = ls.mode;
        if (ls.mode != LightLinkSet::Mode::All) {
            m_hasLightLinks = true;
            cl.uids.insert(ls.nodeUids.begin(), ls.nodeUids.end());
        }
        cl.shadowMode = ls.shadowMode;
        if (ls.shadowMode != LightLinkSet::Mode::All) {
            m_hasShadowLinks = true;
            cl.shadowUids.insert(ls.shadowNodeUids.begin(), ls.shadowNodeUids.end());
        }
        m_lightLinks[i] = std::move(cl);
    }

    // ── Light Groups (LG_*) tabloları ──
    m_lightGroupNames.clear();
    m_lightToGroup.assign(m_lightPtrs.size(), -1);
    m_envLightGroupIndex = -1;
    bool anyExplicitGroup = (m_environment && !m_environment->lightGroup().empty());
    for (const Light* l : m_lightPtrs) {
        if (!l->lightGroup().empty()) {
            anyExplicitGroup = true;
            break;
        }
    }
    if (anyExplicitGroup) {
        std::unordered_map<std::string, int> groupIdx;
        auto getGroup = [&](const std::string& name) -> int {
            auto it = groupIdx.find(name);
            if (it != groupIdx.end()) return it->second;
            if (static_cast<int>(m_lightGroupNames.size()) >= kMaxSceneLightGroups) return 0;
            const int id = static_cast<int>(m_lightGroupNames.size());
            m_lightGroupNames.push_back(name);
            groupIdx[name] = id;
            return id;
        };
        for (size_t i = 0; i < m_lightPtrs.size(); ++i) {
            const std::string& g = m_lightPtrs[i]->lightGroup();
            m_lightToGroup[i] = getGroup(g.empty() ? std::string("Default") : g);
        }
        if (m_environment) {
            const std::string& eg = m_environment->lightGroup();
            m_envLightGroupIndex = getGroup(eg.empty() ? std::string("Env") : eg);
        }
    }

    m_built = true;
    m_embree.reset();
    if (!gPreferOwnBvh && EmbreeAccel::available()) {
        m_embree = std::make_unique<EmbreeAccel>();
        m_embree->build(m_shapes);
        m_bvh = BVH{};
        return;
    }
    m_bvh.build(m_shapes);
}

void Scene::reset() {
    m_lights.clear();
    m_lightPtrs.clear();
    m_shapes.clear();
    m_environment.reset();
    m_bvh = BVH{};
    m_embree.reset();
    m_emissiveMaterials.clear();
    m_materials.clear();
    m_shapeOwner.clear();
    m_shapeNames.clear();
    m_materialNames.clear();
    m_emitterOfShape.clear();
    m_meshEmitterBase.clear();
    m_lightSampler = LightSampler{};
    m_built = false;
    m_cameraInvisible.clear();
    m_shadowCatcher = nullptr;
    m_shadowTransmitters = false;
    m_background = Background{};
    m_lightLinks.clear();
    m_hasLightLinks = false;
    m_hasShadowLinks = false;
    m_lightGroupNames.clear();
    m_lightToGroup.clear();
    m_envLightGroupIndex = -1;
}

bool Scene::intersect(Ray& ray, SurfaceInteraction& isect) const {
    return m_embree ? m_embree->intersect(ray, isect) : m_bvh.intersect(ray, isect);
}

bool Scene::intersectAny(const Ray& ray) const {
    return m_embree ? m_embree->intersectAny(ray) : m_bvh.intersectAny(ray);
}

AABB Scene::bounds() const {
    return m_embree ? m_embree->bounds() : m_bvh.bounds();
}

const std::string& Scene::shapeNameOf(const SurfaceInteraction& isect) const {
    auto it = m_shapeNames.find(isect.hitObject);
    return it == m_shapeNames.end() ? kEmptyName : it->second;
}

const std::string& Scene::materialNameOf(const Material* mat) const {
    auto it = m_materialNames.find(mat);
    return it == m_materialNames.end() ? kEmptyName : it->second;
}

bool Scene::hasShadowLinkFor(int lightIdx) const {
    if (!m_hasShadowLinks || lightIdx < 0 || static_cast<size_t>(lightIdx) >= m_lightLinks.size()) return false;
    return m_lightLinks[static_cast<size_t>(lightIdx)].shadowMode != LightLinkSet::Mode::All;
}

bool Scene::isLightLinked(int lightIdx, uint64_t receiverUid) const {
    if (!m_hasLightLinks || lightIdx < 0 || static_cast<size_t>(lightIdx) >= m_lightLinks.size()) return true;
    const CompiledLightLink& cl = m_lightLinks[static_cast<size_t>(lightIdx)];
    if (cl.mode == LightLinkSet::Mode::All) return true;
    const bool found = cl.uids.count(receiverUid) > 0;
    return cl.mode == LightLinkSet::Mode::Include ? found : !found;
}

bool Scene::isShadowLinked(int lightIdx, uint64_t blockerUid) const {
    if (!m_hasShadowLinks || lightIdx < 0 || static_cast<size_t>(lightIdx) >= m_lightLinks.size()) return true;
    const CompiledLightLink& cl = m_lightLinks[static_cast<size_t>(lightIdx)];
    if (cl.shadowMode == LightLinkSet::Mode::All) return true;
    const bool found = cl.shadowUids.count(blockerUid) > 0;
    return cl.shadowMode == LightLinkSet::Mode::Include ? found : !found;
}

int Scene::lightGroupIndex(int lightIdx) const {
    if (lightIdx < 0 || static_cast<size_t>(lightIdx) >= m_lightToGroup.size()) return -1;
    return m_lightToGroup[static_cast<size_t>(lightIdx)];
}

} // namespace photon
