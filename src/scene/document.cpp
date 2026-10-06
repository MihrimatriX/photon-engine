// document.cpp — Işık/ortam tanımlarından render nesneleri ve stüdyo preset'leri.
#include "scene/document.h"
#include "lights/area_light.h"
#include "lights/directional_light.h"
#include "lights/point_light.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/platform/path.h"
#include "geometry/mesh.h"
#include "materials/disney.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace photon {

namespace {

// (azimut, yükseklik) → birim yön. Yükseklik ufuktan yukarı, azimut +X'ten +Z'ye.
Vec3f sphericalDir(float azimuthDeg, float elevationDeg) {
    const float az = azimuthDeg * DEG_TO_RAD;
    const float el = elevationDeg * DEG_TO_RAD;
    return Vec3f(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
}

} // namespace

Vec3f directionalTravelDir(const LightDesc& d) {
    return -sphericalDir(d.azimuthDeg, d.elevationDeg);
}

// Alan ışığı dörtgeni: merkez c, hedefe bakan normal n. Dörtgenin kenarları
// n'ye dik iki vektör (u, v) olarak kurulur; AreaLight köşe + iki kenar ister.
std::vector<std::shared_ptr<Light>> buildLights(const std::vector<LightDesc>& descs) {
    std::vector<std::shared_ptr<Light>> out;
    for (const LightDesc& d : descs) {
        if (!d.enabled || d.intensity <= 0.0f) continue;
        const Color3f L = d.color * d.intensity;
        switch (d.type) {
            case LightDesc::Type::Area: {
                Vec3f n = d.target - d.position;
                if (n.lengthSquared() < 1e-12f) n = Vec3f(0, -1, 0);
                n = n.normalized();
                Vec3f up = std::abs(n.y) > 0.95f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0);
                Vec3f u = up.cross(n).normalized();
                Vec3f v = n.cross(u).normalized();
                u = u * std::max(1e-4f, d.width);
                v = v * std::max(1e-4f, d.height);
                const Vec3f corner = d.position - u * 0.5f - v * 0.5f;
                out.push_back(std::make_shared<AreaLight>(corner, u, v, L));
                break;
            }
            case LightDesc::Type::Directional:
                out.push_back(std::make_shared<DirectionalLight>(directionalTravelDir(d), L));
                break;
            case LightDesc::Type::Point:
                out.push_back(std::make_shared<PointLight>(d.position, L));
                break;
        }
    }
    return out;
}

// Dikey gradyan (zenit → ufuk → zemin) ve iki parlak dikdörtgen "pencere".
// Gerçek HDRI olmadan bile krom/cam yüzeylerde okunaklı yansımalar verir.
std::shared_ptr<const Image> makeProceduralSky(const Color3f& zenith, const Color3f& horizon) {
    constexpr int W = 512, H = 256;
    auto img = std::make_shared<Image>(W, H);
    for (int y = 0; y < H; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(H - 1);
        Color3f c;
        if (t < 0.5f) {
            const float k = t * 2.0f;
            c = zenith * (1.0f - k) + horizon * k;
        } else {
            const float k = (t - 0.5f) * 2.0f;
            c = horizon * (1.0f - 0.65f * k);
        }
        for (int x = 0; x < W; ++x) img->setPixel(x, y, c);
    }
    auto stamp = [&](int x0, int y0, int x1, int y1, const Color3f& c) {
        for (int y = std::max(0, y0); y < std::min(H, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(W, x1); ++x) img->setPixel(x, y, c);
    };
    stamp(W * 40 / 100, H * 14 / 100, W * 56 / 100, H * 34 / 100, Color3f(26.0f, 25.0f, 23.0f));
    stamp(W * 6 / 100, H * 22 / 100, W * 14 / 100, H * 40 / 100, Color3f(9.0f, 10.0f, 12.0f));
    return img;
}

std::shared_ptr<const Image> EnvironmentCache::get(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_images.find(path);
    if (it != m_images.end()) return it->second;
    std::optional<Image> img = loadImageHDR(path);
    if (!img) img = loadImageEXR(path);
    if (!img) return nullptr;
    auto shared = std::make_shared<const Image>(std::move(*img));
    m_images[path] = shared;
    return shared;
}

void EnvironmentCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_images.clear();
}

std::shared_ptr<EnvironmentLight> buildEnvironment(const EnvironmentDesc& desc, EnvironmentCache& cache) {
    std::shared_ptr<const Image> image;
    if (!desc.hdrPath.empty()) image = cache.get(desc.hdrPath);
    if (!image) image = makeProceduralSky(desc.zenith, desc.horizon);
    return std::make_shared<EnvironmentLight>(image, desc.rotationDeg * DEG_TO_RAD, desc.intensity);
}

std::vector<StudioPreset> loadStudioPresets(const std::string& dir) {
    namespace fs = std::filesystem;
    using json = nlohmann::json;
    std::vector<StudioPreset> out;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(pathFromUtf8(dir), ec)) {
        if (e.path().extension() == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f);
        json j = json::parse(in, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            std::cerr << "Studio preset: invalid JSON " << pathToUtf8(f) << std::endl;
            continue;
        }
        StudioPreset p;
        p.id = j.value("id", pathToUtf8(f.stem()));
        p.name = j.value("name", p.id);
        p.description = j.value("description", std::string());
        p.environment = j.value("environment", std::string());
        p.rotationDeg = j.value("rotation", 0.0f);
        p.intensity = j.value("intensity", 1.0f);
        if (j.contains("exposure")) {
            p.exposure = j.value("exposure", 0.0f);
            p.hasExposure = true;
        }
        for (const auto& l : j.value("lights", json::array())) {
            StudioPreset::Rig r;
            const std::string type = l.value("type", std::string("area"));
            r.type = type == "directional" ? LightDesc::Type::Directional
                   : type == "point"       ? LightDesc::Type::Point
                                           : LightDesc::Type::Area;
            r.name = l.value("name", std::string("Işık"));
            r.azimuthDeg = l.value("azimuth", 0.0f);
            r.elevationDeg = l.value("elevation", 45.0f);
            r.distance = l.value("distance", 2.5f);
            r.size = l.value("size", 1.0f);
            r.intensity = l.value("intensity", 10.0f);
            if (l.contains("color") && l["color"].is_array() && l["color"].size() >= 3)
                r.color = Color3f(l["color"][0].get<float>(), l["color"][1].get<float>(), l["color"][2].get<float>());
            p.lights.push_back(r);
        }
        out.push_back(std::move(p));
    }
    return out;
}

// Işıklar sahnenin sınır küresine göre yerleşir: konum = merkez + R·mesafe·yön,
// boyut = R·size. Mesafe ve boyut birlikte ölçeklendiği için ışığın konudan
// görünen katı açısı (dolayısıyla aydınlatma) sahne ölçeğinden bağımsızdır.
std::vector<LightDesc> placeStudioLights(const StudioPreset& preset, const AABB& sceneBounds,
                                         float azimuthOffsetDeg) {
    Vec3f center(0.0f, 0.5f, 0.0f);
    float radius = 1.0f;
    if (sceneBounds.pMin.x <= sceneBounds.pMax.x) {
        center = sceneBounds.centroid();
        radius = std::max(1e-3f, (sceneBounds.pMax - sceneBounds.pMin).length() * 0.5f);
    }
    std::vector<LightDesc> out;
    for (const auto& r : preset.lights) {
        LightDesc d;
        d.type = r.type;
        d.name = r.name;
        d.color = r.color;
        d.intensity = r.intensity;
        d.azimuthDeg = r.azimuthDeg + azimuthOffsetDeg;
        d.elevationDeg = r.elevationDeg;
        d.position = center + sphericalDir(d.azimuthDeg, r.elevationDeg) * (radius * r.distance);
        d.target = center;
        d.width = d.height = radius * r.size;
        out.push_back(d);
    }
    return out;
}

} // namespace photon

namespace photon {

std::shared_ptr<Scene> buildRenderScene(const SceneGraph& graph, const std::vector<LightDesc>& lights,
                                        const EnvironmentDesc& env, EnvironmentCache& cache, bool isolate) {
    auto scene = std::make_shared<Scene>();
    graph.compileInto(*scene, isolate);
    // Otomatik zemin: modellerin altına, sahne boyutunun çok katı genişlikte bir dörtgen.
    if (env.groundEnabled && !graph.empty()) {
        const AABB box = graph.worldBounds();
        if (box.pMin.x <= box.pMax.x) {
            const GroundQuad g = placeGroundUnder(box);
            auto mat = std::make_shared<DisneyMaterial>(env.groundColor, 0.0f, env.groundRoughness, 0.5f);
            const Vec3f p0 = g.corner, p1 = g.corner + g.edgeU, p2 = p1 + g.edgeV, p3 = g.corner + g.edgeV;
            auto mesh = std::make_shared<TriangleMesh>(
                std::vector<Vec3f>{p0, p1, p2, p3}, std::vector<Vec3f>{}, std::vector<Vec2f>{},
                std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, mat.get());
            scene->retainMaterial(mat);
            scene->tagShape(mesh.get(), kGroundNodeUid);
            scene->addShape(mesh);
        }
    }
    for (const auto& l : buildLights(lights)) scene->addLight(l);
    scene->setEnvironment(buildEnvironment(env, cache));
    scene->setBackground(env.background);
    scene->buildAccelerator();
    return scene;
}

} // namespace photon
