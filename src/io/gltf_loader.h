// gltf_loader.h — glTF 2.0 (.gltf / .glb) sahne yükleyicisinin arayüzü.
// Düğüm ağacı dünya uzayına düzleştirilir; her üçgen primitive ayrı bir TriangleMesh olur,
// PBR metallic-roughness malzemesi DisneyMaterial'e çevrilir.
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
