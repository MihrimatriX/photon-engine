// scene.cpp — Render sahnesine şekil/ışık ekleme ve BVH üzerinden kesişim sorguları.
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
#include "lights/area_light.h"
#include "materials/lambertian.h"
#include <atomic>

namespace photon {

void Scene::addShape(std::shared_ptr<Shape> shape) {
    if (!shape) return;

    auto mesh = std::dynamic_pointer_cast<TriangleMesh>(shape);
    m_shapes.push_back(std::move(shape));
    if (!mesh || !mesh->material()) return;

    // Emisyonlu mesh (ör. LED panel malzemesi) → ışık örneklemesine de girer.
    SurfaceInteraction si;
    Color3f emission = mesh->material()->emitted(si);
    if (emission.isBlack()) return;
    // Alan ışığı dörtgenleri mesh değil Triangle olduğu için iki kez kaydedilmez.
    auto light = std::make_shared<MeshLight>(mesh.get(), emission);
    m_lights.push_back(light);
    m_lightPtrs.push_back(light.get());
}

void Scene::addLight(std::shared_ptr<Light> light) {
    if (!light) return;
    m_lights.push_back(light);
    m_lightPtrs.push_back(light.get());

    // Alan ışıkları başka türlü kesişilemez: BSDF ile örneklenen bir ışın ışığa
    // çarpınca emisyonunu göremez ve NEE'nin MIS ağırlığı o enerjiyi atar.
    // Aynı dörtgeni BVH'ye yalnız ışık yayan siyah bir Lambertian olarak asarız.
    auto area = std::dynamic_pointer_cast<AreaLight>(light);
    if (!area) return;

    auto mat = std::make_shared<Lambertian>(Color3f::black(), area->radiance());
    m_emissiveMaterials.push_back(mat);

    const Vec3f p0 = area->position();
    const Vec3f p1 = p0 + area->u();
    const Vec3f p2 = p1 + area->v();
    const Vec3f p3 = p0 + area->v();
    const Vec3f n = area->normal();
    const Vec2f uv0(0.0f, 0.0f);
    const Vec2f uv1(1.0f, 0.0f);
    const Vec2f uv2(1.0f, 1.0f);
    const Vec2f uv3(0.0f, 1.0f);
    const Material* m = mat.get();
    addShape(std::make_shared<Triangle>(p0, p1, p2, n, n, n, uv0, uv1, uv2, m));
    addShape(std::make_shared<Triangle>(p0, p2, p3, n, n, n, uv0, uv2, uv3, m));
}

void Scene::setEnvironment(std::shared_ptr<EnvironmentLight> envLight) {
    m_environment = std::move(envLight);
}

void Scene::retainMaterial(std::shared_ptr<const Material> material) {
    if (material) m_materials.push_back(std::move(material));
}

namespace {
std::atomic<bool> gPreferOwnBvh{false};
}

void Scene::setPreferOwnBvh(bool own) {
    gPreferOwnBvh = own;
}

void Scene::buildAccelerator() {
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
    m_background = Background{};
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

} // namespace photon
