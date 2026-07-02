#pragma once

/// @file gltf_loader.h
/// @brief Stub for glTF model loader in PhotonEngine.

#include "geometry/mesh.h"
#include <string>
#include <vector>
#include <memory>

namespace photon {

class Material;

/// @brief Utility class to load glTF 2.0 files. (Stub for future phases)
class GltfLoader {
public:
    static std::vector<std::shared_ptr<TriangleMesh>> load(
        const std::string& path,
        const Material* defaultMaterial) {
        // TODO: Implement glTF loading using cgltf or tinygltf in Phase 5
        return {};
    }
};

} // namespace photon
