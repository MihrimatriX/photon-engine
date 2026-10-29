// model_import.cpp — OBJ (tinyobjloader), glTF (cgltf), FBX (ufbx) ve Alembic yükleyicilerini
// sahne ağacına bağlar.
#include "scene/model_import.h"
#include "io/alembic_loader.h"
#include "io/fbx_loader.h"
#include "io/gltf_loader.h"
#include "io/obj_loader.h"
#include "materials/disney.h"
#include "core/math/constants.h"
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

bool alembicAvailable() {
#ifdef PHOTON_HAS_ALEMBIC
    return true;
#else
    return false;
#endif
}

/// Hiyerarşik içe aktarma sonucunu sahne düğümlerine çevirir. Tek mesh'li ve çocuksuz düğüm
/// tek bir mesh düğümüne katlanır (sahne panelinde "Gövde › Gövde" yerine yalnız "Gövde").
std::unique_ptr<SceneNode> toSceneNode(const ImportedNode& in, const ImportedModel& model,
                                       const std::shared_ptr<Material>& fallback) {
    auto meshNode = [&](int k, const std::string& name, const Transform& local) {
        const ImportedMesh& im = model.meshes[static_cast<size_t>(k)];
        auto node = std::make_unique<SceneNode>(name, SceneNodeType::Mesh);
        node->mesh = im.mesh;
        node->material = im.material ? im.material : fallback;
        node->sourceIndex = k;
        node->localTransform = local;
        return node;
    };
    if (in.meshes.size() == 1 && in.children.empty())
        return meshNode(in.meshes[0], model.meshes[static_cast<size_t>(in.meshes[0])].name, in.local);
    auto group = std::make_unique<SceneNode>(in.name, SceneNodeType::Group);
    group->localTransform = in.local;
    for (int k : in.meshes) group->addChild(meshNode(k, model.meshes[static_cast<size_t>(k)].name, Transform()));
    for (const ImportedNode& c : in.children) group->addChild(toSceneNode(c, model, fallback));
    return group;
}

} // namespace

bool isSupportedModelFile(const std::string& path) {
    const std::string e = lowerExtension(path);
    return e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx" || (e == ".abc" && alembicAvailable());
}

std::string supportedModelPatterns() {
    return alembicAvailable() ? "*.fbx;*.obj;*.gltf;*.glb;*.abc" : "*.fbx;*.obj;*.gltf;*.glb";
}

std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error,
                                           const ModelImportOptions& options, std::vector<std::string>* warnings) {
    const std::string ext = lowerExtension(path);
    std::error_code ec;
    if (!std::filesystem::exists(pathFromUtf8(path), ec)) {
        if (error) *error = "Dosya bulunamadı: " + path;
        return nullptr;
    }
    auto defaultMat = std::make_shared<DisneyMaterial>(Color3f(0.72f), 0.0f, 0.4f, 0.5f);
    const std::filesystem::path p = pathFromUtf8(path);

    std::unique_ptr<SceneNode> group;
    if (ext == ".fbx" || ext == ".abc") {
        ImportedModel model;
        std::string err;
        if (ext == ".fbx") {
            model = FbxLoader::load(path, &err);
        } else {
#ifdef PHOTON_HAS_ALEMBIC
            model = AlembicLoader::load(path, &err, options.alembicTime);
#else
            err = "Bu sürüm Alembic desteği olmadan derlendi (.abc).";
#endif
        }
        if (model.empty()) {
            if (error) *error = err.empty() ? "Dosyada üçgen bulunamadı: " + path : err;
            return nullptr;
        }
        if (warnings) warnings->insert(warnings->end(), model.warnings.begin(), model.warnings.end());
        // Kök her zaman grup: proje dosyası sourcePath'i ondan okur.
        group = std::make_unique<SceneNode>(pathToUtf8(p.stem()), SceneNodeType::Group);
        group->localTransform = model.root.local;
        for (int k : model.root.meshes)
            group->addChild(toSceneNode(ImportedNode{model.meshes[static_cast<size_t>(k)].name, Transform(), {k}, {}},
                                        model, defaultMat));
        for (const ImportedNode& c : model.root.children) group->addChild(toSceneNode(c, model, defaultMat));
    } else {
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
            if (error) *error = "Desteklenmeyen biçim (" + ext + "). FBX, OBJ, glTF" +
                                std::string(alembicAvailable() ? " veya Alembic" : " ") + " kullanın.";
            return nullptr;
        }
        if (meshes.empty()) {
            if (error) *error = "Dosyada üçgen bulunamadı: " + path;
            return nullptr;
        }
        group = std::make_unique<SceneNode>(pathToUtf8(p.stem()), SceneNodeType::Group);
        for (size_t i = 0; i < meshes.size(); ++i) {
            auto node = std::make_unique<SceneNode>("Parça " + std::to_string(i + 1), SceneNodeType::Mesh);
            node->mesh = meshes[i];
            node->material = (i < materials.size() && materials[i]) ? materials[i] : defaultMat;
            node->sourceIndex = static_cast<int>(i);
            group->addChild(std::move(node));
        }
    }
    group->sourcePath = path;
    Transform fix;
    if (options.zUp) fix = Transform::rotateX(-0.5f * PI);
    if (options.scale > 0.0f && options.scale != 1.0f) fix = Transform::scale(Vec3f(options.scale)) * fix;
    group->localTransform = fix * group->localTransform;
    return group;
}

} // namespace photon
