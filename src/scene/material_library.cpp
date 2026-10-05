#include "scene/material_library.h"
#include "materials/dielectric.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdint>
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

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // namespace

void renderSphereThumbnail(const MaterialPreset& p, int size, std::vector<uint8_t>& rgbaOut) {
    size = std::max(8, size);
    rgbaOut.assign(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 0);
    const float inv = 2.0f / static_cast<float>(size);
    const float Lx = 0.45f, Ly = 0.55f, Lz = 0.7f;
    const float Llen = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
    const float lx = Lx / Llen, ly = Ly / Llen, lz = Lz / Llen;
    const float shininess = 8.0f + (1.0f - p.roughness) * (1.0f - p.roughness) * 248.0f;
    const float amb = 0.18f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = x * inv - 1.0f;
            float v = 1.0f - y * inv;
            float r2 = u * u + v * v;
            if (r2 > 1.0f) continue;
            float z = std::sqrt(std::max(0.0f, 1.0f - r2));
            float ndotl = std::max(0.0f, u * lx + v * ly + z * lz);
            // Blinn-Phong highlight (view ≈ +Z)
            float hx = lx, hy = ly, hz = lz + 1.0f;
            float hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
            float ndoth = std::max(0.0f, (u * hx + v * hy + z * hz) / hlen);
            float spec = std::pow(ndoth, shininess);

            Color3f F0 = Color3f(0.08f * p.specular) * (1.0f - p.metallic) + p.baseColor * p.metallic; // Burley: 0.08·specular, disney.cpp ile aynı
            Color3f diffuse = p.baseColor * (amb + ndotl * (1.0f - amb)) * (1.0f - p.metallic);
            Color3f col = diffuse + F0 * (spec * (0.25f + 0.75f * p.metallic + 0.35f * (1.0f - p.roughness)));
            if (p.emissive > 0.0f) col = col + p.baseColor * std::min(p.emissive * 0.08f, 1.5f);
            // soft rim
            float rim = 1.0f - z;
            col = col * (1.0f - rim * 0.25f);

            size_t i = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4u;
            rgbaOut[i] = static_cast<uint8_t>(clampf(col.r, 0.0f, 1.0f) * 255.0f);
            rgbaOut[i + 1] = static_cast<uint8_t>(clampf(col.g, 0.0f, 1.0f) * 255.0f);
            rgbaOut[i + 2] = static_cast<uint8_t>(clampf(col.b, 0.0f, 1.0f) * 255.0f);
            float edge = clampf((1.0f - r2) * 12.0f, 0.0f, 1.0f);
            rgbaOut[i + 3] = static_cast<uint8_t>(edge * 255.0f);
        }
    }
}

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
    p.anisotropy = jsonFloat(json, "anisotropy", 0.0f);
    p.sheen = jsonFloat(json, "sheen", 0.0f);
    p.diffuseTransmission = jsonFloat(json, "diffuseTransmission", 0.0f);
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

std::shared_ptr<Material> MaterialLibrary::createMaterial(const MaterialPreset& p) const {
    if (p.id == "clear_glass")
        return std::make_shared<Dielectric>(1.5f, p.baseColor, 0.0f);
    if (p.id == "frosted_glass")
        return std::make_shared<Dielectric>(1.5f, p.baseColor, std::max(0.001f, p.roughness));
    auto m = std::make_shared<DisneyMaterial>(p.baseColor, p.metallic, p.roughness, p.specular);
    m->setClearCoat(p.clearCoat);
    float aniso = p.anisotropy;
    if (p.id == "brushed_aluminum" && aniso <= 0.0f) aniso = 0.7f;
    m->setAnisotropy(aniso);
    float sheen = p.sheen;
    if ((p.id == "blue_fabric" || p.id == "linen") && sheen <= 0.0f) sheen = 0.6f;
    m->setSheen(sheen);
    m->setDiffuseTransmission(p.diffuseTransmission);
    if (p.emissive > 0.0f) m->setEmission(p.baseColor * p.emissive);
    return m;
}

} // namespace photon
