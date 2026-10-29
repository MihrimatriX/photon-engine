// material_library.cpp — Preset JSON okuma/yazma ve preset → Material dönüşümü.
#include "scene/material_library.h"
#include "materials/dielectric.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "core/platform/path.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace photon {
namespace {

using json = nlohmann::json;

Color3f readColor(const json& j, const char* key, const Color3f& def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3) return def;
    return Color3f((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

std::string slug(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "malzeme" : out;
}

} // namespace

bool MaterialLibrary::loadFile(const std::string& path) {
    std::ifstream in(pathFromUtf8(path));
    if (!in) return false;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        std::cerr << "Material preset: invalid JSON: " << path << std::endl;
        return false;
    }

    MaterialPreset p;
    p.file = path;
    p.id = j.value("id", std::string());
    p.name = j.value("name", std::string());
    p.category = j.value("category", std::string());
    if (p.name.empty()) p.name = pathToUtf8(pathFromUtf8(path).stem());
    if (p.category.empty()) p.category = "Genel";
    if (p.id.empty()) p.id = slug(p.name);
    p.kind = j.value("type", std::string("generic")) == "glass" ? MaterialKind::Glass : MaterialKind::Generic;
    p.baseColor = readColor(j, "baseColor", Color3f(0.8f));
    p.metallic = j.value("metallic", 0.0f);
    p.roughness = j.value("roughness", 0.5f);
    p.specular = j.value("specular", 0.5f);
    p.clearCoat = j.value("clearCoat", 0.0f);
    p.clearCoatRoughness = j.value("clearCoatRoughness", 0.03f);
    p.anisotropy = j.value("anisotropy", 0.0f);
    p.sheen = j.value("sheen", 0.0f);
    p.diffuseTransmission = j.value("diffuseTransmission", 0.0f);
    p.emissive = j.value("emissive", 0.0f);
    p.ior = j.value("ior", 1.5f);
    // Doku yolları preset klasörünün yanındaki textures/ klasörüne göredir.
    const std::filesystem::path texDir = pathFromUtf8(path).parent_path().parent_path() / "textures";
    auto mapPath = [&](const char* key) -> std::string {
        auto it = j.find("maps");
        if (it == j.end() || !it->is_object() || !it->contains(key)) return {};
        std::filesystem::path p = pathFromUtf8((*it)[key].get<std::string>());
        return pathToUtf8(p.is_absolute() ? p : texDir / p);
    };
    p.albedoMap = mapPath("albedo");
    p.normalMap = mapPath("normal");
    p.roughnessMap = mapPath("roughness");
    p.boxMapping = j.value("mapping", std::string("uv")) == "box";
    p.texScale = j.value("texScale", 1.0f);
    presetAdvancedFromJson(j, p);
    m_presets.push_back(std::move(p));
    return true;
}

bool MaterialLibrary::loadFromDirectory(const std::string& path) {
    m_presets.clear();
    namespace fs = std::filesystem;
    const fs::path dir = pathFromUtf8(path);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) loadFile(pathToUtf8(f));
    return !m_presets.empty();
}

const MaterialPreset* MaterialLibrary::findById(const std::string& id) const {
    for (const auto& p : m_presets) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<std::string> MaterialLibrary::categories() const {
    std::vector<std::string> cats;
    for (const auto& p : m_presets) {
        if (std::find(cats.begin(), cats.end(), p.category) == cats.end())
            cats.push_back(p.category);
    }
    return cats; // dosya sırasıyla (öneklerle düzenlenir: 10_plastik_..., 20_metal_...)
}

std::vector<const MaterialPreset*> MaterialLibrary::byCategory(const std::string& cat) const {
    std::vector<const MaterialPreset*> out;
    for (const auto& p : m_presets) {
        if (p.category == cat) out.push_back(&p);
    }
    return out;
}

std::shared_ptr<Material> MaterialLibrary::createMaterial(const MaterialPreset& p) {
    if (p.kind == MaterialKind::Glass) {
        // Pürüzlülük ~0 ise kusursuz (delta) cam, aksi hâlde GGX buzlu cam.
        const float r = p.roughness <= 0.02f ? 0.0f : p.roughness;
        auto g = std::make_shared<Dielectric>(p.ior, p.baseColor, r);
        applyAdvanced(p, *g);
        return g;
    }
    auto m = std::make_shared<DisneyMaterial>(p.baseColor, p.metallic, p.roughness, p.specular);
    m->setClearCoat(p.clearCoat);
    m->setClearCoatRoughness(p.clearCoatRoughness);
    m->setAnisotropy(p.anisotropy);
    m->setSheen(p.sheen);
    m->setDiffuseTransmission(p.diffuseTransmission);
    if (p.emissive > 0.0f) m->setEmission(p.baseColor * p.emissive);
    if (!p.albedoMap.empty()) m->setAlbedoMap(p.albedoMap);
    if (!p.normalMap.empty()) m->setNormalMap(p.normalMap);
    if (!p.roughnessMap.empty()) m->setRoughnessMap(p.roughnessMap);
    m->setTextureMapping(p.boxMapping ? DisneyMaterial::TextureMapping::Box : DisneyMaterial::TextureMapping::UV);
    m->setTextureScale(p.texScale);
    applyAdvanced(p, *m);
    return m;
}

void MaterialLibrary::applyAdvanced(const MaterialPreset& p, Material& mat) {
    mat.setRoundEdgeRadius(p.roundEdges);
    if (auto* g = dynamic_cast<Dielectric*>(&mat)) {
        g->setThinWalled(p.thinWalled);
        g->setThinFilm(p.thinWalled ? p.thinFilmThickness : 0.0f);
        g->setAbsorption(p.absorbColor, p.absorbDistance);
        g->setScattering(p.scatterDensity, p.scatterColor, p.scatterAnisotropy);
        g->setAbbe(p.abbe);
    } else if (auto* d = dynamic_cast<DisneyMaterial*>(&mat)) {
        d->setDiffuseRoughness(p.diffuseRoughness);
        d->setSheenRoughness(p.sheenRoughness);
        d->setSubsurface(p.subsurface);
        d->setSubsurfaceRadius(p.subsurfaceRadius);
        d->setSubsurfaceScale(p.subsurfaceScale);
        d->setSubsurfaceAnisotropy(p.subsurfaceAnisotropy);
        d->setEdgeTint(p.edgeTint);
        d->setThinFilm(p.thinFilmThickness, p.thinFilmIor);
        d->setFlakes(p.flakeDensity, p.flakeSize, p.flakeRoughness, p.flakeColor);
    }
}

namespace {

json colJson(const Color3f& c) { return json::array({c.r, c.g, c.b}); }

} // namespace

void presetAdvancedToJson(const MaterialPreset& p, json& j) {
    const MaterialPreset def;
    if (p.roundEdges > 0.0f) j["roundEdges"] = p.roundEdges;
    if (p.kind == MaterialKind::Glass) {
        if (p.thinWalled) {
            j["thinWalled"] = true;
            if (p.thinFilmThickness > 0.0f) j["thinFilm"] = {{"thickness", p.thinFilmThickness}};
        }
        if (p.absorbColor != def.absorbColor)
            j["absorption"] = {{"color", colJson(p.absorbColor)}, {"distance", p.absorbDistance}};
        if (p.scatterDensity > 0.0f)
            j["scattering"] = {{"density", p.scatterDensity}, {"color", colJson(p.scatterColor)},
                               {"anisotropy", p.scatterAnisotropy}};
        if (p.abbe > 0.0f) j["abbe"] = p.abbe;
        return;
    }
    if (p.diffuseRoughness > 0.0f) j["diffuseRoughness"] = p.diffuseRoughness;
    if (p.sheenRoughness != def.sheenRoughness) j["sheenRoughness"] = p.sheenRoughness;
    if (p.subsurface > 0.0f)
        j["subsurface"] = {{"weight", p.subsurface}, {"radius", colJson(p.subsurfaceRadius)},
                           {"scale", p.subsurfaceScale}, {"anisotropy", p.subsurfaceAnisotropy}};
    if (p.edgeTint != def.edgeTint) j["edgeTint"] = colJson(p.edgeTint);
    if (p.thinFilmThickness > 0.0f) j["thinFilm"] = {{"thickness", p.thinFilmThickness}, {"ior", p.thinFilmIor}};
    if (p.flakeDensity > 0.0f)
        j["flakes"] = {{"density", p.flakeDensity}, {"size", p.flakeSize}, {"roughness", p.flakeRoughness},
                       {"color", colJson(p.flakeColor)}};
}

void presetAdvancedFromJson(const json& j, MaterialPreset& p) {
    if (auto a = j.find("absorption"); a != j.end() && a->is_object()) {
        p.absorbColor = readColor(*a, "color", p.absorbColor);
        p.absorbDistance = a->value("distance", p.absorbDistance);
    }
    if (auto s = j.find("scattering"); s != j.end() && s->is_object()) {
        p.scatterDensity = s->value("density", p.scatterDensity);
        p.scatterColor = readColor(*s, "color", p.scatterColor);
        p.scatterAnisotropy = s->value("anisotropy", p.scatterAnisotropy);
    }
    p.abbe = j.value("abbe", p.abbe);
    if (auto s = j.find("subsurface"); s != j.end() && s->is_object()) {
        p.subsurface = s->value("weight", p.subsurface);
        p.subsurfaceRadius = readColor(*s, "radius", p.subsurfaceRadius);
        p.subsurfaceScale = s->value("scale", p.subsurfaceScale);
        p.subsurfaceAnisotropy = s->value("anisotropy", p.subsurfaceAnisotropy);
    }
    p.edgeTint = readColor(j, "edgeTint", p.edgeTint);
    p.diffuseRoughness = j.value("diffuseRoughness", p.diffuseRoughness);
    p.sheenRoughness = j.value("sheenRoughness", p.sheenRoughness);
    p.roundEdges = j.value("roundEdges", p.roundEdges);
    p.thinWalled = j.value("thinWalled", p.thinWalled);
    if (auto t = j.find("thinFilm"); t != j.end() && t->is_object()) {
        p.thinFilmThickness = t->value("thickness", p.thinFilmThickness);
        p.thinFilmIor = t->value("ior", p.thinFilmIor);
    }
    if (auto f = j.find("flakes"); f != j.end() && f->is_object()) {
        p.flakeDensity = f->value("density", p.flakeDensity);
        p.flakeSize = f->value("size", p.flakeSize);
        p.flakeRoughness = f->value("roughness", p.flakeRoughness);
        p.flakeColor = readColor(*f, "color", p.flakeColor);
    }
}

std::string presetSignature(const MaterialPreset& p) {
    json j;
    j["k"] = static_cast<int>(p.kind);
    j["c"] = colJson(p.baseColor);
    j["v"] = {p.metallic, p.roughness, p.specular, p.clearCoat, p.clearCoatRoughness, p.anisotropy,
              p.sheen, p.diffuseTransmission, p.emissive, p.ior, p.texScale};
    j["m"] = {p.albedoMap, p.normalMap, p.roughnessMap};
    j["b"] = p.boxMapping;
    presetAdvancedToJson(p, j);
    return j.dump();
}

MaterialPreset MaterialLibrary::presetFromMaterial(const Material& mat, const std::string& name) {
    MaterialPreset p;
    p.name = name;
    p.id = slug(name);
    p.category = "Benim";
    p.roundEdges = mat.roundEdgeRadius();
    if (const auto* d = dynamic_cast<const DisneyMaterial*>(&mat)) {
        p.baseColor = d->baseColor();
        p.metallic = d->metallic();
        p.roughness = d->roughness();
        p.specular = d->specular();
        p.clearCoat = d->clearCoat();
        p.clearCoatRoughness = d->clearCoatRoughness();
        p.anisotropy = d->anisotropy();
        p.sheen = d->sheen();
        p.diffuseTransmission = d->diffuseTransmission();
        SurfaceInteraction si;
        const Color3f e = d->emitted(si);
        const float m = std::max(e.r, std::max(e.g, e.b));
        if (m > 0.0f) {
            p.emissive = m;
            p.baseColor = e / m;
        }
        p.diffuseRoughness = d->diffuseRoughness();
        p.sheenRoughness = d->sheenRoughness();
        p.subsurface = d->subsurface();
        p.subsurfaceRadius = d->subsurfaceRadius();
        p.subsurfaceScale = d->subsurfaceScale();
        p.subsurfaceAnisotropy = d->subsurfaceAnisotropy();
        p.edgeTint = d->edgeTint();
        p.thinFilmThickness = d->thinFilmThickness();
        p.thinFilmIor = d->thinFilmIor();
        p.flakeDensity = d->flakeDensity();
        p.flakeSize = d->flakeSize();
        p.flakeRoughness = d->flakeRoughness();
        p.flakeColor = d->flakeColor();
        p.albedoMap = d->albedoMap();
        p.normalMap = d->normalMap();
        p.roughnessMap = d->roughnessMap();
        p.boxMapping = d->textureMapping() == DisneyMaterial::TextureMapping::Box;
        p.texScale = d->textureScale();
    } else if (const auto* g = dynamic_cast<const Dielectric*>(&mat)) {
        p.kind = MaterialKind::Glass;
        p.baseColor = g->tint();
        p.roughness = g->roughness();
        p.ior = g->ior();
        p.absorbColor = g->absorptionColor();
        p.absorbDistance = g->absorptionDistance();
        p.scatterDensity = g->scatterDensity();
        p.scatterColor = g->scatterColor();
        p.scatterAnisotropy = g->scatterAnisotropy();
        p.abbe = g->abbe();
        p.thinWalled = g->thinWalled();
        p.thinFilmThickness = g->thinFilmThickness();
    }
    return p;
}

bool MaterialLibrary::savePreset(MaterialPreset p, const std::string& dir) {
    namespace fs = std::filesystem;
    if (p.id.empty()) p.id = slug(p.name);
    json j;
    j["id"] = p.id;
    j["name"] = p.name;
    j["category"] = p.category;
    j["type"] = p.kind == MaterialKind::Glass ? "glass" : "generic";
    j["baseColor"] = {p.baseColor.r, p.baseColor.g, p.baseColor.b};
    j["roughness"] = p.roughness;
    if (p.kind == MaterialKind::Glass) {
        j["ior"] = p.ior;
    } else {
        j["metallic"] = p.metallic;
        j["specular"] = p.specular;
        j["clearCoat"] = p.clearCoat;
        j["clearCoatRoughness"] = p.clearCoatRoughness;
        j["anisotropy"] = p.anisotropy;
        j["sheen"] = p.sheen;
        j["diffuseTransmission"] = p.diffuseTransmission;
        j["emissive"] = p.emissive;
    }
    // Kullanıcı preset'lerinde dokular mutlak yolla saklanır (loadFile mutlak yolu olduğu gibi alır).
    json maps = json::object();
    if (!p.albedoMap.empty()) maps["albedo"] = p.albedoMap;
    if (!p.normalMap.empty()) maps["normal"] = p.normalMap;
    if (!p.roughnessMap.empty()) maps["roughness"] = p.roughnessMap;
    if (!maps.empty()) {
        j["maps"] = maps;
        j["mapping"] = p.boxMapping ? "box" : "uv";
        j["texScale"] = p.texScale;
    }
    presetAdvancedToJson(p, j);
    std::error_code ec;
    fs::create_directories(pathFromUtf8(dir), ec);
    const fs::path file = pathFromUtf8(dir) / pathFromUtf8("90_" + p.id + ".json");
    std::ofstream out(file);
    if (!out) return false;
    out << j.dump(2);
    if (!out) return false;
    p.file = pathToUtf8(file);
    std::erase_if(m_presets, [&](const MaterialPreset& e) { return e.id == p.id; });
    m_presets.push_back(std::move(p));
    return true;
}

std::shared_ptr<Material> cloneMaterial(const Material& m) {
    if (const auto* d = dynamic_cast<const DisneyMaterial*>(&m)) return std::make_shared<DisneyMaterial>(*d);
    if (const auto* g = dynamic_cast<const Dielectric*>(&m)) return std::make_shared<Dielectric>(*g);
    if (const auto* l = dynamic_cast<const Lambertian*>(&m)) return std::make_shared<Lambertian>(*l);
    if (const auto* r = dynamic_cast<const Mirror*>(&m)) return std::make_shared<Mirror>(*r);
    return nullptr;
}

} // namespace photon
