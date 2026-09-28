#pragma once

#include "materials/disney.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

struct MaterialPreset {
    std::string id;
    std::string name;
    std::string category;
    Color3f baseColor{0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float clearCoat = 0.0f;
    float emissive = 0.0f;
};

/// CPU-shaded GGX-ish shader ball → RGBA8 size×size (transparent outside circle).
void renderSphereThumbnail(const MaterialPreset& p, int size, std::vector<uint8_t>& rgbaOut);

class MaterialLibrary {
public:
    bool loadFromDirectory(const std::string& path);
    const std::vector<MaterialPreset>& presets() const { return m_presets; }
    const MaterialPreset* findById(const std::string& id) const;
    std::vector<std::string> categories() const;
    std::vector<const MaterialPreset*> byCategory(const std::string& cat) const;
    std::shared_ptr<DisneyMaterial> createMaterial(const MaterialPreset& p) const;

private:
    std::vector<MaterialPreset> m_presets;
    bool loadFile(const std::string& path);
};

} // namespace photon
