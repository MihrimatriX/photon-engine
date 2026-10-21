// model_import.cpp — OBJ (tinyobjloader) ve glTF (cgltf) yükleyicilerini sahne
// ağacına bağlar.
#include "scene/model_import.h"
#include "io/obj_loader.h"
#include "io/gltf_loader.h"
#include "materials/disney.h"
#include "core/platform/path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace photon {

namespace {

std::string lowerExtension(const std::string& path) {
    std::string e = pathToUtf8(pathFromUtf8(path).extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

} // namespace

bool isSupportedModelFile(const std::string& path) {
    const std::string e = lowerExtension(path);
    return e == ".obj" || e == ".gltf" || e == ".glb";
}

std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error) {
    const std::string ext = lowerExtension(path);
    std::error_code ec;
    if (!std::filesystem::exists(pathFromUtf8(path), ec)) {
        if (error) *error = "Dosya bulunamadı: " + path;
        return nullptr;
    }
    auto defaultMat = std::make_shared<DisneyMaterial>(Color3f(0.72f), 0.0f, 0.4f, 0.5f);

    std::vector<std::shared_ptr<TriangleMesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;
    if (ext == ".obj") {
        auto r = ObjLoader::load(path, defaultMat.get());
        meshes = std::move(r.meshes);
        materials = std::move(r.materials);
    } else if (ext == ".gltf" || ext == ".glb") {
        auto r = GltfLoader::load(path, defaultMat.get());
        meshes = std::move(r.meshes);
        materials = std::move(r.materials);
    } else {
        if (error) *error = "Desteklenmeyen biçim (" + ext + "). OBJ veya glTF kullanın.";
        return nullptr;
    }
    if (meshes.empty()) {
        if (error) *error = "Dosyada üçgen bulunamadı: " + path;
        return nullptr;
    }

    const std::filesystem::path p = pathFromUtf8(path);
    auto group = std::make_unique<SceneNode>(pathToUtf8(p.stem()), SceneNodeType::Group);
    group->sourcePath = path;
    for (size_t i = 0; i < meshes.size(); ++i) {
        auto node = std::make_unique<SceneNode>("Parça " + std::to_string(i + 1), SceneNodeType::Mesh);
        node->mesh = meshes[i];
        node->material = (i < materials.size() && materials[i]) ? materials[i] : defaultMat;
        node->sourceIndex = static_cast<int>(i);
        group->addChild(std::move(node));
    }
    return group;
}

} // namespace photon
