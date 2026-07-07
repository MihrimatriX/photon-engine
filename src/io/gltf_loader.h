#pragma once

#include "geometry/mesh.h"
#include "materials/material.h"
#include <memory>
#include <string>
#include <vector>

namespace photon {

struct GltfLoadResult {
    std::vector<std::shared_ptr<Material>> materials;
    std::vector<std::shared_ptr<TriangleMesh>> meshes;
};

class GltfLoader {
public:
    static GltfLoadResult load(const std::string& path, const Material* defaultMaterial);
};

} // namespace photon
