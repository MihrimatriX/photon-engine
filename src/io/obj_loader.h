// obj_loader.h — Wavefront OBJ (+ MTL) model yükleyicisinin arayüzü.
// Her OBJ şekli malzeme kimliğine göre ayrı TriangleMesh'lere bölünür; MTL'deki Kd
// (dağınık renk) Lambertian malzemeye dönüşür. Sahne katmanındaki içe aktarma bunu çağırır.
#pragma once

/// @file obj_loader.h
/// @brief Wavefront OBJ model file loader for PhotonEngine.

#include "geometry/mesh.h"
#include <string>
#include <vector>
#include <memory>

namespace photon {

class Material;

struct ObjLoadResult {
    std::vector<std::shared_ptr<TriangleMesh>> meshes;
    /// Parallel to meshes. Null means the caller-supplied default material.
    std::vector<std::shared_ptr<Material>> materials;
};

/// @brief Utility class to load Wavefront .obj files.
class ObjLoader {
public:
    /// Load mesh groups from a .obj file.
    ///
    /// @param path Path to the .obj file.
    /// @param defaultMaterial Material to assign if no MTL Kd is present.
    static ObjLoadResult load(const std::string& path, const Material* defaultMaterial);
};

} // namespace photon
