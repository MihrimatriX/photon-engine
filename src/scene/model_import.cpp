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
#include <cmath>
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

namespace {

/// Dosyanın dünya uzayındaki (kök düğümün ebeveyni) kamera ve ışıkları grubun yerel uzayına alır.
void collectExtras(const ImportedModel& model, ImportExtras& out) {
    const Transform toLocal = model.root.local.inverse();
    for (const ImportedCamera& c : model.cameras) {
        CameraTrack t;
        t.name = c.name;
        t.fps = std::clamp(c.fps, 1.0f, 240.0f);
        t.smooth = true; // her kare örneklendi: Hermite eğrisi örnekleri birebir izler
        for (const ImportedCameraKey& k : c.keys) {
            CameraKey key;
            key.time = k.time;
            key.eye = toLocal.transformPoint(k.eye);
            key.target = toLocal.transformPoint(k.target);
            key.fovDeg = k.fovDeg;
            t.keys.push_back(key);
        }
        if (!t.empty()) out.cameras.push_back(std::move(t));
    }
    for (const ImportedLight& l : model.lights) {
        LightDesc d;
        d.name = l.name;
        d.color = l.color;
        d.intensity = l.intensity;
        d.visibleToCamera = false;
        const Vec3f pos = toLocal.transformPoint(l.position);
        const Vec3f aim = toLocal.transformPoint(l.position + l.direction * l.aimDistance);
        switch (l.type) {
            case ImportedLight::Type::Point:
                d.type = LightDesc::Type::Point;
                d.position = pos;
                break;
            case ImportedLight::Type::Spot:
                d.type = LightDesc::Type::Spot;
                d.position = pos;
                d.target = aim;
                d.coneAngleDeg = l.coneAngleDeg;
                d.coneBlend = l.coneBlend;
                break;
            case ImportedLight::Type::Area:
                d.type = LightDesc::Type::Area;
                d.position = pos;
                d.target = aim;
                d.width = l.width;
                d.height = l.height;
                break;
            case ImportedLight::Type::Directional: {
                d.type = LightDesc::Type::Directional;
                const Vec3f from = -toLocal.transformVector(l.direction).normalized();
                d.elevationDeg = std::asin(std::clamp(from.y, -1.0f, 1.0f)) * RAD_TO_DEG;
                d.azimuthDeg = std::atan2(from.z, from.x) * RAD_TO_DEG;
                d.position = pos;
                break;
            }
        }
        out.lights.push_back(std::move(d));
    }
}

} // namespace

void transformImportExtras(ImportExtras& extras, const Transform& toWorld) {
    // Ortalama ölçek: birim dönüşümü ve kullanıcı ölçeği tekdüzedir.
    const float s = (toWorld.transformVector(Vec3f(1, 0, 0)).length() + toWorld.transformVector(Vec3f(0, 1, 0)).length() +
                     toWorld.transformVector(Vec3f(0, 0, 1)).length()) / 3.0f;
    for (CameraTrack& t : extras.cameras)
        for (CameraKey& k : t.keys) {
            k.eye = toWorld.transformPoint(k.eye);
            k.target = toWorld.transformPoint(k.target);
            k.focusDistance *= s;
        }
    for (LightDesc& d : extras.lights) {
        const Vec3f from = sphericalDirection(d.azimuthDeg, d.elevationDeg);
        d.position = toWorld.transformPoint(d.position);
        d.target = toWorld.transformPoint(d.target);
        d.width *= s;
        d.height *= s;
        if (d.type == LightDesc::Type::Point || d.type == LightDesc::Type::Spot) d.intensity *= s * s;
        if (d.type == LightDesc::Type::Directional) {
            const Vec3f f = toWorld.transformVector(from).normalized();
            d.elevationDeg = std::asin(std::clamp(f.y, -1.0f, 1.0f)) * RAD_TO_DEG;
            d.azimuthDeg = std::atan2(f.z, f.x) * RAD_TO_DEG;
        }
    }
}

std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error,
                                           const ModelImportOptions& options, std::vector<std::string>* warnings,
                                           ImportExtras* extras) {
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
            model = AlembicLoader::load(path, &err, options.alembicTime, options.unitMeters);
#else
            err = "Bu sürüm Alembic desteği olmadan derlendi (.abc).";
#endif
        }
        if (model.empty()) {
            if (error) *error = err.empty() ? "Dosyada üçgen bulunamadı: " + path : err;
            return nullptr;
        }
        if (warnings) warnings->insert(warnings->end(), model.warnings.begin(), model.warnings.end());
        if (extras) collectExtras(model, *extras);
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
    float s = options.scale > 0.0f ? options.scale : 1.0f;
    if (ext == ".obj" && options.unitMeters > 0.0f) s *= options.unitMeters;
    if (s != 1.0f) fix = Transform::scale(Vec3f(s)) * fix;
    // Kamera ve ışıklar grubun yerel uzayında: düzeltme (Z yukarı, ölçek) grubun dönüşümünde,
    // uygulama onları grubun dünya dönüşümüyle birlikte alır.
    group->localTransform = fix * group->localTransform;
    return group;
}

} // namespace photon
