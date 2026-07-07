#include "scene/material_library.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace photon {
namespace {

// ponytail: minimal JSON field extraction, no dependency
std::string jsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    return json.substr(pos + 1, end - pos - 1);
}

float jsonFloat(const std::string& json, const std::string& key, float def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    return std::strtof(json.c_str() + pos + 1, nullptr);
}

Color3f jsonColor(const std::string& json, const std::string& key, const Color3f& def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return def;
    float r = 0, g = 0, b = 0;
    std::sscanf(json.c_str() + pos, "[%f,%f,%f]", &r, &g, &b);
    return Color3f(r, g, b);
}

} // namespace

bool MaterialLibrary::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string json = ss.str();

    MaterialPreset p;
    p.id = jsonString(json, "id");
    p.name = jsonString(json, "name");
    p.category = jsonString(json, "category");
    if (p.name.empty()) p.name = std::filesystem::path(path).stem().string();
    if (p.category.empty()) p.category = "General";
    p.baseColor = jsonColor(json, "baseColor", Color3f(0.8f));
    p.metallic = jsonFloat(json, "metallic", 0.0f);
    p.roughness = jsonFloat(json, "roughness", 0.5f);
    p.specular = jsonFloat(json, "specular", 0.5f);
    p.clearCoat = jsonFloat(json, "clearCoat", 0.0f);
    p.emissive = jsonFloat(json, "emissive", 0.0f);
    if (p.id.empty()) p.id = p.name;
    m_presets.push_back(std::move(p));
    return true;
}

bool MaterialLibrary::loadFromDirectory(const std::string& path) {
    m_presets.clear();
    namespace fs = std::filesystem;
    if (!fs::exists(path)) return false;
    for (const auto& e : fs::directory_iterator(path)) {
        if (e.path().extension() == ".json") loadFile(e.path().string());
    }
    return !m_presets.empty();
}

std::vector<std::string> MaterialLibrary::categories() const {
    std::vector<std::string> cats;
    for (const auto& p : m_presets) {
        if (std::find(cats.begin(), cats.end(), p.category) == cats.end())
            cats.push_back(p.category);
    }
    std::sort(cats.begin(), cats.end());
    return cats;
}

std::vector<const MaterialPreset*> MaterialLibrary::byCategory(const std::string& cat) const {
    std::vector<const MaterialPreset*> out;
    for (const auto& p : m_presets) {
        if (p.category == cat) out.push_back(&p);
    }
    return out;
}

std::shared_ptr<DisneyMaterial> MaterialLibrary::createMaterial(const MaterialPreset& p) const {
    auto m = std::make_shared<DisneyMaterial>(p.baseColor, p.metallic, p.roughness, p.specular);
    m->setClearCoat(p.clearCoat);
    return m;
}

} // namespace photon
