#pragma once

/// @file obj_loader.h
/// @brief Wavefront OBJ model file loader for PhotonEngine.

#include "geometry/mesh.h"
#include <string>
#include <vector>
#include <memory>

namespace photon {

class Material;

/// @brief Utility class to load Wavefront .obj files.
class ObjLoader {
public:
    /// Load mesh groups from a .obj file.
    ///
    /// @param path Path to the .obj file.
    /// @param defaultMaterial Material to assign if no material is specified.
    /// @return Vector of shared pointers to loaded TriangleMesh shapes.
    static std::vector<std::shared_ptr<TriangleMesh>> load(
        const std::string& path,
        const Material* defaultMaterial);
};

} // namespace photon
