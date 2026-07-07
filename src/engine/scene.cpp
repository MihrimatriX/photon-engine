#include "engine/scene.h"
#include "geometry/mesh.h"

namespace photon {

void Scene::addShape(std::shared_ptr<Shape> shape) {
    if (!shape) return;
    
    // If the shape is a TriangleMesh, we unpack its individual triangles
    // into the acceleration list to have a single, unified BVH.
    // Otherwise, we add the shape directly.
    auto mesh = std::dynamic_pointer_cast<TriangleMesh>(shape);
    if (mesh) {
        size_t numTris = mesh->numTriangles();
        for (size_t i = 0; i < numTris; ++i) {
            m_shapes.push_back(mesh->getTriangle(i));
        }
    } else {
        m_shapes.push_back(shape);
    }
}

void Scene::addLight(std::shared_ptr<Light> light) {
    if (!light) return;
    m_lights.push_back(light);
    m_lightPtrs.push_back(light.get());
}

void Scene::setEnvironment(std::shared_ptr<EnvironmentLight> envLight) {
    m_environment = envLight;
}

void Scene::buildAccelerator() {
    m_bvh.build(m_shapes);
}

void Scene::reset() {
    m_shapes.clear();
    m_lights.clear();
    m_lightPtrs.clear();
    m_environment.reset();
    m_bvh = BVH{};
}

bool Scene::intersect(Ray& ray, SurfaceInteraction& isect) const {
    return m_bvh.intersect(ray, isect);
}

bool Scene::intersectAny(const Ray& ray) const {
    return m_bvh.intersectAny(ray);
}

} // namespace photon
