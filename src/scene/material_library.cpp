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
        return std::make_shared<Dielectric>(p.ior, p.baseColor, r);
    }
    auto m = std::make_shared<DisneyMaterial>(p.baseColor, p.metallic, p.roughness, p.specular);
    m->setClearCoat(p.clearCoat);
    m->setClearCoatRoughness(p.clearCoatRoughness);
    m->setAnisotropy(p.anisotropy);
    m->setSheen(p.sheen);
    m->setDiffuseTransmission(p.diffuseTransmission);
    if (p.emissive > 0.0f) m->setEmission(p.baseColor * p.emissive);
    return m;
}

MaterialPreset MaterialLibrary::presetFromMaterial(const Material& mat, const std::string& name) {
    MaterialPreset p;
    p.name = name;
    p.id = slug(name);
    p.category = "Benim";
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
    } else if (const auto* g = dynamic_cast<const Dielectric*>(&mat)) {
        p.kind = MaterialKind::Glass;
        p.baseColor = g->tint();
        p.roughness = g->roughness();
        p.ior = g->ior();
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
