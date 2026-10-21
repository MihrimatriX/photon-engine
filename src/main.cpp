// main.cpp — photon_render: arayüzsüz (komut satırı) renderer ve ölçüm aracı.
//
//   photon_render sahne.photon  [--out render.png] [--spp 256] [--res 1920x1080]
//   photon_render model.obj     [...]               (stüdyo ışığı + otomatik kadraj)
//   photon_render --cornell     [...]
// Ek seçenekler: --threads N, --no-denoise, --bounces N, --stats stats.json
// stats.json: süre, örnek sayısı, Mray/s (milyon birincil ışın / saniye).
#include "scene/project_io.h"
#include "scene/model_import.h"
#include "scene/cornell_box.h"
#include "scene/document.h"
#include "engine/renderer.h"
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "camera/orthographic_camera.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/platform/path.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace photon;

namespace {

struct Options {
    std::string input;
    bool cornell = false;
    std::string out = "render.png";
    std::string stats;
    int spp = 64;
    int width = 1280;
    int height = 720;
    int threads = 0;
    int bounces = -1;
    bool denoise = true;
};

void usage() {
    std::cout << "photon_render <sahne.photon | model.obj | --cornell> [--out dosya.png|.jpg|.exr] [--spp N]\n"
                 "              [--res GxY] [--threads N] [--bounces N] [--no-denoise] [--stats stats.json]\n";
}

// Proje dosyasındaki orbit kamera alanlarından (hedef, yarıçap, θ, φ) kamera.
std::unique_ptr<Camera> cameraFromJson(const nlohmann::json& c, float aspect) {
    Vec3f target(0, 0.5f, 0);
    if (auto t = c.find("target"); t != c.end() && t->is_array() && t->size() == 3)
        target = Vec3f((*t)[0].get<float>(), (*t)[1].get<float>(), (*t)[2].get<float>());
    const float r = c.value("radius", 5.0f), th = c.value("theta", 1.15f), ph = c.value("phi", 0.45f);
    const float fov = c.value("fov", 35.0f);
    const Vec3f eye = target + Vec3f(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)) * r;
    if (c.value("orthographic", false)) {
        const float h = 2.0f * std::tan(fov * DEG_TO_RAD * 0.5f) * r;
        return std::make_unique<OrthographicCamera>(eye, target, Vec3f(0, 1, 0), h, aspect);
    }
    const float aperture = c.value("aperture", 0.0f);
    if (aperture > 0.0f) {
        const float focus = c.value("focusExplicit", false) ? c.value("focusDistance", r) : r;
        return std::make_unique<ThinLensCamera>(eye, target, Vec3f(0, 1, 0), fov, aspect, aperture, focus);
    }
    return std::make_unique<PerspectiveCamera>(eye, target, Vec3f(0, 1, 0), fov, aspect);
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out") o.out = next();
        else if (a == "--spp") o.spp = std::max(1, std::atoi(next().c_str()));
        else if (a == "--res") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--threads") o.threads = std::atoi(next().c_str());
        else if (a == "--bounces") o.bounces = std::atoi(next().c_str());
        else if (a == "--no-denoise") o.denoise = false;
        else if (a == "--stats") o.stats = next();
        else if (a == "--cornell") o.cornell = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] != '-') o.input = a;
        else { usage(); return 2; }
    }
    if (o.input.empty() && !o.cornell) { usage(); return 2; }

    const auto t0 = std::chrono::steady_clock::now();
    SceneGraph graph;
    ProjectData data;
    std::string err;
    if (o.cornell) {
        buildCornellBox(graph);
        data.environment.groundEnabled = false;
        data.environment.zenith = data.environment.horizon = Color3f(0.0f);
        LightDesc l;
        l.position = Vec3f(278, 548, 279.5f);
        l.target = Vec3f(278, 0, 279.5f);
        l.width = 130;
        l.height = 105;
        l.intensity = 17;
        data.lights.push_back(l);
        data.camera = {{"target", {278, 273, 277.5}}, {"radius", 1050}, {"theta", PI * 0.5f}, {"phi", -PI * 0.5f}, {"fov", 40}};
    } else if (isSupportedModelFile(o.input)) {
        auto node = importModelFile(o.input, &err);
        if (!node) { std::cerr << err << std::endl; return 1; }
        // Uygulamadaki "boş sahneye model" yolunun aynısı: modeli zemine oturt ve
        // ortala, "Ürün Stüdyosu" preset'ini (HDRI + softbox'lar) uygula.
        if (const AABB nb = SceneGraph::nodeWorldBounds(*node); nb.pMin.x <= nb.pMax.x) {
            const Vec3f nc = nb.centroid();
            node->localTransform = Transform::translate(Vec3f(-nc.x, -nb.pMin.y, -nc.z)) * node->localTransform;
        }
        graph.root()->addChild(std::move(node));
        const AABB b = graph.worldBounds();
        const float phi = 0.785f;
        const std::string assets = findAssetsRoot();
        for (const auto& studio : loadStudioPresets(pathToUtf8(pathFromUtf8(assets) / "studios"))) {
            if (studio.id != "product_softbox") continue;
            data.lights = placeStudioLights(studio, b, phi * RAD_TO_DEG);
            data.environment.rotationDeg = studio.rotationDeg;
            data.environment.intensity = studio.intensity;
            if (studio.hasExposure) data.render["exposure"] = studio.exposure;
            // Preset ortamı kimlikle verir ("studio_small_09"); dosya adı çözünürlük ekiyle biter.
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(pathFromUtf8(assets) / "environments", ec)) {
                const std::string fn = pathToUtf8(e.path().filename());
                if (fn.rfind(studio.environment, 0) == 0 && e.path().extension() == ".hdr")
                    data.environment.hdrPath = pathToUtf8(e.path());
            }
        }
        if (data.lights.empty()) std::cerr << "Uyarı: stüdyo preset'i bulunamadı (" << assets << "/studios)\n";
        const Vec3f c = b.centroid();
        const float r = (b.pMax - b.pMin).length() * 0.5f / std::sin(17.5f * DEG_TO_RAD) * 1.1f;
        data.camera = {{"target", {c.x, c.y, c.z}}, {"radius", r}, {"theta", 1.15}, {"phi", phi}, {"fov", 35}};
    } else if (!loadProject(o.input, graph, data, &err)) {
        std::cerr << err << std::endl;
        return 1;
    }

    EnvironmentCache cache;
    auto scene = buildRenderScene(graph, data.lights, data.environment, cache, false);
    const auto tLoad = std::chrono::steady_clock::now();

    RenderSettings rs;
    rs.width = o.width;
    rs.height = o.height;
    rs.samplesPerPixel = o.spp;
    rs.numThreads = o.threads;
    rs.maxBounces = o.bounces > 0 ? o.bounces : data.render.value("maxBounces", 8);
    rs.denoiseEnabled = o.denoise;
    rs.tmo = static_cast<ToneMapOperator>(data.render.value("toneMap", static_cast<int>(ToneMapOperator::PBRNeutral)));
    rs.exposure = data.render.value("exposure", 0.0f);
    auto camera = cameraFromJson(data.camera, static_cast<float>(o.width) / static_cast<float>(o.height));

    Renderer renderer;
    const auto tRender = std::chrono::steady_clock::now();
    Image img = renderer.render(*scene, *camera, rs);
    const auto tDone = std::chrono::steady_clock::now();

    std::string ext = pathToUtf8(pathFromUtf8(o.out).extension());
    bool ok;
    if (ext == ".exr") ok = saveImageEXR(img, o.out);
    else if (ext == ".jpg" || ext == ".jpeg") ok = saveImageJPG(img, o.out, rs.tmo, rs.exposure);
    else ok = saveImagePNG(img, o.out, rs.tmo, rs.exposure);

    const double loadS = std::chrono::duration<double>(tLoad - t0).count();
    const double renderS = std::chrono::duration<double>(tDone - tRender).count();
    const double mrays = static_cast<double>(o.width) * o.height * o.spp / renderS / 1e6;
    std::cout << "Sahne: " << loadS << " sn, render: " << renderS << " sn, " << mrays << " M örnek/sn ("
              << o.width << "x" << o.height << " @ " << o.spp << " spp, " << rs.maxBounces << " sekme)\n";
    if (!o.stats.empty()) {
        nlohmann::json s = {{"loadSeconds", loadS}, {"renderSeconds", renderS}, {"width", o.width},
                            {"height", o.height}, {"spp", o.spp}, {"bounces", rs.maxBounces},
                            {"msamplesPerSecond", mrays}, {"threads", o.threads > 0 ? o.threads : static_cast<int>(std::thread::hardware_concurrency())},
                            {"denoise", o.denoise}, {"output", o.out}};
        std::ofstream(pathFromUtf8(o.stats)) << s.dump(2) << "\n";
    }
    if (!ok) {
        std::cerr << "Çıktı yazılamadı: " << o.out << std::endl;
        return 1;
    }
    return 0;
}
