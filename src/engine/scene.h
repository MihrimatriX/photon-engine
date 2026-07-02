#pragma once

/// @file scene.h
/// @brief Scene class managing geometry, light sources, and acceleration structures in PhotonEngine.

#include "geometry/bvh.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include <vector>
#include <memory>

namespace photon {

/// @brief Represents the scene container containing all renderable items.
class Scene {
public:
    Scene() = default;

    /// Add a geometric shape to the scene
    void addShape(std::shared_ptr<Shape> shape);

    /// Add a light source to the scene
    void addLight(std::shared_ptr<Light> light);

    /// Set the environment map / background light
    void setEnvironment(std::shared_ptr<EnvironmentLight> envLight);

    /// Build the BVH acceleration structure on all added shapes
    void buildAccelerator();

    /// Intersect a ray with the scene geometry
    bool intersect(Ray& ray, SurfaceInteraction& isect) const;

    /// Test if a shadow ray is occluded by any geometry
    bool intersectAny(const Ray& ray) const;

    /// Get list of light sources
    const std::vector<const Light*>& lights() const { return m_lightPtrs; }

    /// Get the environment light (if any)
    const EnvironmentLight* environment() const { return m_environment.get(); }

private:
    std::vector<std::shared_ptr<Shape>> m_shapes;
    std::vector<std::shared_ptr<Light>> m_lights;
    std::shared_ptr<EnvironmentLight> m_environment;

    // Acceleration structure
    BVH m_bvh;

    // Cache of raw pointers of lights for fast access in integrator
    std::vector<const Light*> m_lightPtrs;
};

} // namespace photon
