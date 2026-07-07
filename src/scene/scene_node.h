#pragma once

#include "core/math/transform.h"
#include "geometry/mesh.h"
#include "materials/material.h"
#include <memory>
#include <string>
#include <vector>

namespace photon {

enum class SceneNodeType { Group, Mesh, Sphere, Light };

struct SceneNode {
    std::string name;
    SceneNodeType type = SceneNodeType::Group;
    Transform localTransform;
    std::vector<std::unique_ptr<SceneNode>> children;
    std::shared_ptr<TriangleMesh> mesh;
    std::shared_ptr<Material> material;
    float sphereRadius = 0.0f;
    bool visible = true;
    uint32_t pickId = 0;

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
