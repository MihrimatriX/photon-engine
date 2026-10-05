// project_io.cpp — Proje dosyasının JSON şeması ve okuma/yazma kodu.
//
// Şema (sürüm 2), kısaca:
//   { "format": "photon-project", "version": 2,
//     "camera": {...}, "render": {...},            // uygulamanın kendi alanları
//     "environment": { "hdr", "rotation", "intensity", "background", "ground", ... },
//     "lights": [ { "type", "color", "intensity", "position", ... } ],
//     "materials": [ { "type": "disney"|"glass"|"lambert"|"mirror", ... } ],
//     "nodes": [ { "name", "type", "visible", "xf": [16], "material": i,
//                  "source": "model.obj", "sourceIndex": k, "geometry": {...},
//                  "children": [...] } ] }
// Aynı malzemeyi paylaşan düğümler "materials" tablosunda tek kayda işaret eder.
#include "scene/project_io.h"
#include "scene/model_import.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "geometry/mesh.h"
#include "core/platform/path.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace photon {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ponytail: 512 MB üstü proje reddedilir (yerinde geometri gömülen dev projeler için sınır).
constexpr uintmax_t kMaxProjectBytes = 512ull * 1024ull * 1024ull;

json vec3(const Vec3f& v) { return json::array({v.x, v.y, v.z}); }
json col3(const Color3f& c) { return json::array({c.r, c.g, c.b}); }

Vec3f readVec3(const json& j, const char* key, const Vec3f& def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3) return def;
    return Vec3f((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

Color3f readCol3(const json& j, const char* key, const Color3f& def) {
    Vec3f v = readVec3(j, key, Vec3f(def.r, def.g, def.b));
    return Color3f(v.x, v.y, v.z);
}

// Yol, proje klasörüne göre göreli yazılabiliyorsa göreli; değilse mutlak.
std::string relPath(const std::string& p, const fs::path& base) {
    if (p.empty()) return p;
    std::error_code ec;
    const fs::path abs = fs::absolute(pathFromUtf8(p), ec);
    const fs::path rel = fs::relative(abs, base, ec); // farklı sürücüde boş döner
    const fs::path& chosen = (ec || rel.empty()) ? abs : rel;
    const std::u8string s = chosen.generic_u8string(); // '/' ayırıcı: dosya taşınabilir
    return std::string(s.begin(), s.end());
}

std::string resolvePath(const std::string& p, const fs::path& base) {
    if (p.empty()) return p;
    fs::path fp = pathFromUtf8(p);
    if (fp.is_absolute()) return p;
    return pathToUtf8((base / fp).lexically_normal());
}

json transformJson(const Transform& xf) {
    json a = json::array();
    const Mat4f& m = xf.matrix();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) a.push_back(m(r, c));
    return a;
}

Transform readTransform(const json& j) {
    auto it = j.find("xf");
    if (it == j.end() || !it->is_array() || it->size() != 16) return Transform{};
    Mat4f m = Mat4f::identity();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m(r, c) = (*it)[static_cast<size_t>(r * 4 + c)].get<float>();
    return Transform(m);
}

// ── Malzemeler ───────────────────────────────────────────────────────────

json materialJson(const Material& mat, const fs::path& base) {
    json j;
    SurfaceInteraction si;
    if (const auto* d = dynamic_cast<const DisneyMaterial*>(&mat)) {
        j["type"] = "disney";
        j["baseColor"] = col3(d->baseColor());
        j["metallic"] = d->metallic();
        j["roughness"] = d->roughness();
        j["specular"] = d->specular();
        j["clearCoat"] = d->clearCoat();
        j["clearCoatRoughness"] = d->clearCoatRoughness();
        j["anisotropy"] = d->anisotropy();
        j["sheen"] = d->sheen();
        j["diffuseTransmission"] = d->diffuseTransmission();
        j["emission"] = col3(d->emitted(si));
        json maps = json::object();
        if (!d->albedoMap().empty()) maps["albedo"] = relPath(d->albedoMap(), base);
        if (!d->normalMap().empty()) maps["normal"] = relPath(d->normalMap(), base);
        if (!d->roughnessMap().empty()) maps["roughness"] = relPath(d->roughnessMap(), base);
        if (!d->metalnessMap().empty()) maps["metalness"] = relPath(d->metalnessMap(), base);
        if (!maps.empty()) j["maps"] = maps;
    } else if (const auto* g = dynamic_cast<const Dielectric*>(&mat)) {
        j["type"] = "glass";
        j["ior"] = g->ior();
        j["tint"] = col3(g->tint());
        j["roughness"] = g->roughness();
    } else if (const auto* l = dynamic_cast<const Lambertian*>(&mat)) {
        j["type"] = "lambert";
        j["albedo"] = col3(l->albedo());
        j["emission"] = col3(l->emitted(si));
    } else if (const auto* m = dynamic_cast<const Mirror*>(&mat)) {
        j["type"] = "mirror";
        j["color"] = col3(m->reflectance());
    } else {
        j["type"] = "unknown";
    }
    return j;
}

std::shared_ptr<Material> materialFromJson(const json& j, const fs::path& base) {
    const std::string type = j.value("type", std::string("disney"));
    if (type == "glass") {
        return std::make_shared<Dielectric>(j.value("ior", 1.5f), readCol3(j, "tint", Color3f(1.0f)),
                                            j.value("roughness", 0.0f));
    }
    if (type == "lambert") {
        return std::make_shared<Lambertian>(readCol3(j, "albedo", Color3f(0.8f)),
                                            readCol3(j, "emission", Color3f::black()));
    }
    if (type == "mirror") {
        return std::make_shared<Mirror>(readCol3(j, "color", Color3f(0.95f)));
    }
    auto d = std::make_shared<DisneyMaterial>(readCol3(j, "baseColor", Color3f(0.8f)), j.value("metallic", 0.0f),
                                              j.value("roughness", 0.5f), j.value("specular", 0.5f));
    d->setClearCoat(j.value("clearCoat", 0.0f));
    d->setClearCoatRoughness(j.value("clearCoatRoughness", 0.03f));
    d->setAnisotropy(j.value("anisotropy", 0.0f));
    d->setSheen(j.value("sheen", 0.0f));
    d->setDiffuseTransmission(j.value("diffuseTransmission", 0.0f));
    d->setEmission(readCol3(j, "emission", Color3f::black()));
    if (auto maps = j.find("maps"); maps != j.end() && maps->is_object()) {
        if (maps->contains("albedo")) d->setAlbedoMap(resolvePath((*maps)["albedo"].get<std::string>(), base));
        if (maps->contains("normal")) d->setNormalMap(resolvePath((*maps)["normal"].get<std::string>(), base));
        if (maps->contains("roughness")) d->setRoughnessMap(resolvePath((*maps)["roughness"].get<std::string>(), base));
        if (maps->contains("metalness")) d->setMetalnessMap(resolvePath((*maps)["metalness"].get<std::string>(), base));
    }
    return d;
}

// ── Düğümler ─────────────────────────────────────────────────────────────

struct SaveCtx {
    fs::path base;
    std::unordered_map<const Material*, int> materialIndex;
    json materials = json::array();

    int materialId(const std::shared_ptr<Material>& m) {
        if (!m) return -1;
        auto it = materialIndex.find(m.get());
        if (it != materialIndex.end()) return it->second;
        const int id = static_cast<int>(materials.size());
        materials.push_back(materialJson(*m, base));
        materialIndex[m.get()] = id;
        return id;
    }
};

json nodeJson(const SceneNode& n, SaveCtx& ctx, bool insideSource) {
    json j;
    j["name"] = n.name;
    j["visible"] = n.visible;
    j["xf"] = transformJson(n.localTransform);
    switch (n.type) {
        case SceneNodeType::Group: j["type"] = "group"; break;
        case SceneNodeType::Mesh: j["type"] = "mesh"; break;
        case SceneNodeType::Sphere: j["type"] = "sphere"; break;
        case SceneNodeType::Light: j["type"] = "light"; break;
    }
    if (n.material) j["material"] = ctx.materialId(n.material);
    if (!n.sourcePath.empty()) j["source"] = relPath(n.sourcePath, ctx.base);
    if (n.sourceIndex >= 0) j["sourceIndex"] = n.sourceIndex;
    if (n.type == SceneNodeType::Sphere) j["radius"] = n.sphereRadius;
    // Kaynak dosyadan gelmeyen mesh'lerin geometrisi projeye gömülür (ör. Cornell kutusu).
    if (n.type == SceneNodeType::Mesh && n.mesh && !(insideSource && n.sourceIndex >= 0)) {
        json g;
        json p = json::array(), nn = json::array(), uv = json::array(), idx = json::array();
        for (const auto& v : n.mesh->positions()) { p.push_back(v.x); p.push_back(v.y); p.push_back(v.z); }
        for (const auto& v : n.mesh->normals()) { nn.push_back(v.x); nn.push_back(v.y); nn.push_back(v.z); }
        for (const auto& v : n.mesh->uvs()) { uv.push_back(v.x); uv.push_back(v.y); }
        for (uint32_t i : n.mesh->indices()) idx.push_back(i);
        g["p"] = std::move(p);
        g["n"] = std::move(nn);
        g["uv"] = std::move(uv);
        g["i"] = std::move(idx);
        j["geometry"] = std::move(g);
    }
    const bool source = insideSource || !n.sourcePath.empty();
    json children = json::array();
    for (const auto& c : n.children) children.push_back(nodeJson(*c, ctx, source));
    if (!children.empty()) j["children"] = std::move(children);
    return j;
}

struct LoadCtx {
    fs::path base;
    std::vector<std::shared_ptr<Material>> materials;
    std::vector<std::string> warnings;
};

void applyCommon(SceneNode& n, const json& j, LoadCtx& ctx) {
    n.name = j.value("name", n.name);
    n.visible = j.value("visible", true);
    n.localTransform = readTransform(j);
    const int mi = j.value("material", -1);
    if (mi >= 0 && mi < static_cast<int>(ctx.materials.size())) n.material = ctx.materials[static_cast<size_t>(mi)];
}

std::shared_ptr<TriangleMesh> meshFromJson(const json& g, const Material* mat) {
    std::vector<Vec3f> p, n;
    std::vector<Vec2f> uv;
    std::vector<uint32_t> idx;
    const auto& P = g.value("p", json::array());
    const auto& N = g.value("n", json::array());
    const auto& U = g.value("uv", json::array());
    const auto& I = g.value("i", json::array());
    for (size_t i = 0; i + 2 < P.size(); i += 3) p.emplace_back(P[i].get<float>(), P[i + 1].get<float>(), P[i + 2].get<float>());
    for (size_t i = 0; i + 2 < N.size(); i += 3) n.emplace_back(N[i].get<float>(), N[i + 1].get<float>(), N[i + 2].get<float>());
    for (size_t i = 0; i + 1 < U.size(); i += 2) uv.emplace_back(U[i].get<float>(), U[i + 1].get<float>());
    for (const auto& v : I) {
        const uint32_t k = v.get<uint32_t>();
        if (k >= p.size()) return nullptr; // bozuk dosya: sınır dışı indeks
        idx.push_back(k);
    }
    if (p.empty() || idx.size() % 3 != 0) return nullptr;
    if (n.size() != p.size()) n.clear();
    if (uv.size() != p.size()) uv.clear();
    return std::make_shared<TriangleMesh>(p, n, uv, idx, mat);
}

std::unique_ptr<SceneNode> nodeFromJson(const json& j, LoadCtx& ctx);

// İçe aktarılmış model grubu: dosya yeniden okunur, kaydedilmiş çocukların
// ayarları sourceIndex eşleşmesiyle uygulanır; kayıtta olmayan çocuklar
// (kullanıcının sildiği parçalar) atılır.
std::unique_ptr<SceneNode> sourceGroupFromJson(const json& j, LoadCtx& ctx) {
    const std::string src = resolvePath(j.value("source", std::string()), ctx.base);
    std::string err;
    std::unique_ptr<SceneNode> group = importModelFile(src, &err);
    if (!group) {
        ctx.warnings.push_back(err);
        return nullptr;
    }
    applyCommon(*group, j, ctx);
    std::vector<std::unique_ptr<SceneNode>> imported = std::move(group->children);
    group->children.clear();
    for (const auto& cj : j.value("children", json::array())) {
        const int k = cj.value("sourceIndex", -1);
        auto it = std::find_if(imported.begin(), imported.end(),
                               [k](const std::unique_ptr<SceneNode>& c) { return c && c->sourceIndex == k; });
        if (k < 0 || it == imported.end()) {
            if (auto extra = nodeFromJson(cj, ctx)) group->addChild(std::move(extra));
            continue;
        }
        applyCommon(**it, cj, ctx);
        group->addChild(std::move(*it));
    }
    return group;
}

std::unique_ptr<SceneNode> nodeFromJson(const json& j, LoadCtx& ctx) {
    if (j.contains("source")) return sourceGroupFromJson(j, ctx);
    const std::string type = j.value("type", std::string("group"));
    std::unique_ptr<SceneNode> n;
    if (type == "mesh") {
        n = std::make_unique<SceneNode>("Mesh", SceneNodeType::Mesh);
        applyCommon(*n, j, ctx);
        if (j.contains("geometry")) n->mesh = meshFromJson(j["geometry"], n->material.get());
        if (!n->mesh) return nullptr;
    } else if (type == "sphere") {
        n = std::make_unique<SceneNode>("Küre", SceneNodeType::Sphere);
        applyCommon(*n, j, ctx);
        n->sphereRadius = j.value("radius", 1.0f);
    } else {
        n = std::make_unique<SceneNode>("Grup", SceneNodeType::Group);
        applyCommon(*n, j, ctx);
    }
    for (const auto& c : j.value("children", json::array())) {
        if (auto child = nodeFromJson(c, ctx)) n->addChild(std::move(child));
    }
    return n;
}

// ── Işık ve ortam ────────────────────────────────────────────────────────

json lightJson(const LightDesc& d) {
    json j;
    j["type"] = d.type == LightDesc::Type::Area ? "area" : d.type == LightDesc::Type::Directional ? "directional" : "point";
    j["name"] = d.name;
    j["enabled"] = d.enabled;
    j["color"] = col3(d.color);
    j["intensity"] = d.intensity;
    j["position"] = vec3(d.position);
    j["target"] = vec3(d.target);
    j["width"] = d.width;
    j["height"] = d.height;
    j["azimuth"] = d.azimuthDeg;
    j["elevation"] = d.elevationDeg;
    return j;
}

LightDesc lightFromJson(const json& j) {
    LightDesc d;
    const std::string t = j.value("type", std::string("area"));
    d.type = t == "directional" ? LightDesc::Type::Directional : t == "point" ? LightDesc::Type::Point : LightDesc::Type::Area;
    d.name = j.value("name", d.name);
    d.enabled = j.value("enabled", true);
    d.color = readCol3(j, "color", d.color);
    d.intensity = j.value("intensity", d.intensity);
    d.position = readVec3(j, "position", d.position);
    d.target = readVec3(j, "target", d.target);
    d.width = j.value("width", d.width);
    d.height = j.value("height", d.height);
    d.azimuthDeg = j.value("azimuth", d.azimuthDeg);
    d.elevationDeg = j.value("elevation", d.elevationDeg);
    return d;
}

json environmentJson(const EnvironmentDesc& e, const fs::path& base) {
    json j;
    j["hdr"] = relPath(e.hdrPath, base);
    j["zenith"] = col3(e.zenith);
    j["horizon"] = col3(e.horizon);
    j["rotation"] = e.rotationDeg;
    j["intensity"] = e.intensity;
    const char* mode = e.background.mode == Background::Mode::Color ? "color"
                     : e.background.mode == Background::Mode::Transparent ? "transparent" : "environment";
    j["background"] = {{"mode", mode}, {"color", col3(e.background.color)}};
    j["ground"] = {{"enabled", e.groundEnabled}, {"color", col3(e.groundColor)}, {"roughness", e.groundRoughness}};
    return j;
}

EnvironmentDesc environmentFromJson(const json& j, const fs::path& base) {
    EnvironmentDesc e;
    e.hdrPath = resolvePath(j.value("hdr", std::string()), base);
    e.zenith = readCol3(j, "zenith", e.zenith);
    e.horizon = readCol3(j, "horizon", e.horizon);
    e.rotationDeg = j.value("rotation", 0.0f);
    e.intensity = j.value("intensity", 1.0f);
    if (auto b = j.find("background"); b != j.end() && b->is_object()) {
        const std::string m = b->value("mode", std::string("environment"));
        e.background.mode = m == "color" ? Background::Mode::Color
                          : m == "transparent" ? Background::Mode::Transparent : Background::Mode::Environment;
        e.background.color = readCol3(*b, "color", e.background.color);
    }
    if (auto g = j.find("ground"); g != j.end() && g->is_object()) {
        e.groundEnabled = g->value("enabled", true);
        e.groundColor = readCol3(*g, "color", e.groundColor);
        e.groundRoughness = g->value("roughness", e.groundRoughness);
    }
    return e;
}

} // namespace

bool saveProject(const std::string& path, const SceneGraph& graph, const ProjectData& data, std::string* error) {
    const fs::path file = pathFromUtf8(path);
    SaveCtx ctx;
    std::error_code ec;
    ctx.base = fs::absolute(file, ec).parent_path();

    json root;
    root["format"] = "photon-project";
    root["version"] = 2;
    root["camera"] = data.camera;
    root["render"] = data.render;
    root["environment"] = environmentJson(data.environment, ctx.base);
    json lights = json::array();
    for (const auto& l : data.lights) lights.push_back(lightJson(l));
    root["lights"] = std::move(lights);
    json nodes = json::array();
    for (const auto& c : graph.root()->children) nodes.push_back(nodeJson(*c, ctx, false));
    root["materials"] = std::move(ctx.materials);
    root["nodes"] = std::move(nodes);

    // Atomik yazım: geçici dosya → yeniden adlandır. Yazma yarıda kesilirse eski proje sağlam kalır.
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            if (error) *error = "Dosya yazılamadı: " + path;
            return false;
        }
        out << root.dump(1);
        if (!out) {
            if (error) *error = "Yazma hatası: " + path;
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    if (ec) {
        if (error) *error = "Dosya taşınamadı: " + ec.message();
        return false;
    }
    return true;
}

bool loadProjectJson(const std::string& text, const std::string& baseDir, SceneGraph& graph,
                     ProjectData& data, std::string* error) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error) *error = "Geçersiz proje dosyası (JSON okunamadı)";
        return false;
    }
    if (root.value("version", 0) < 2) {
        if (error) *error = "Eski proje biçimi (sürüm 1) desteklenmiyor";
        return false;
    }
    LoadCtx ctx;
    ctx.base = pathFromUtf8(baseDir);
    try {
        for (const auto& m : root.value("materials", json::array())) ctx.materials.push_back(materialFromJson(m, ctx.base));
        graph.clear();
        for (const auto& n : root.value("nodes", json::array())) {
            if (auto node = nodeFromJson(n, ctx)) graph.root()->addChild(std::move(node));
        }
        data = ProjectData{};
        data.camera = root.value("camera", json::object());
        data.render = root.value("render", json::object());
        if (root.contains("environment")) data.environment = environmentFromJson(root["environment"], ctx.base);
        for (const auto& l : root.value("lights", json::array())) data.lights.push_back(lightFromJson(l));
    } catch (const std::exception& e) {
        if (error) *error = std::string("Proje okunamadı: ") + e.what();
        return false;
    }
    if (error && !ctx.warnings.empty()) {
        std::ostringstream ss;
        for (const auto& w : ctx.warnings) ss << w << "\n";
        *error = ss.str();
    }
    return true;
}

bool loadProject(const std::string& path, SceneGraph& graph, ProjectData& data, std::string* error) {
    const fs::path file = pathFromUtf8(path);
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size == 0 || size > kMaxProjectBytes) {
        if (error) *error = "Proje dosyası boş, eksik ya da çok büyük: " + path;
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadProjectJson(ss.str(), pathToUtf8(fs::absolute(file, ec).parent_path()), graph, data, error);
}

} // namespace photon
