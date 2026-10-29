// main.cpp — photon_render: arayüzsüz (komut satırı) renderer ve ölçüm aracı.
//
//   photon_render sahne.photon  [--out render.png] [--spp 256] [--res 1920x1080]
//   photon_render model.obj     [...]               (stüdyo ışığı + otomatik kadraj)
//   photon_render --cornell     [...]
// Ek seçenekler: --threads N, --no-denoise, --bounces N, --stats stats.json,
//   --noise 0.02 (uyarlamalı örnekleme: göreli hata hedefi), --min-spp N,
//   --clamp 10 (dolaylı parlama sınırı), --time SN (süre sınırı; ilerlemeli render),
//   --filter box|triangle|gaussian|blackman-harris, --filter-width PİKSEL,
//   --passes (çok katmanlı EXR: ışık geçişleri, derinlik, konum, ID'ler, albedo, normal),
//   --device auto|cpu|cuda:0 (render cihazı), --list-devices (makinedeki cihazlar)
// stats.json: süre, örnek sayısı, Mray/s (milyon birincil ışın / saniye).
// Render, uygulamanın son render'ıyla aynı yoldan geçer: RenderDevice + RenderSession (Final).
#include "scene/project_io.h"
#include "scene/model_import.h"
#include "scene/cornell_box.h"
#include "scene/document.h"
#include "engine/render_session.h"
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "camera/orthographic_camera.h"
#include "core/image/image_io.h"
#include "core/image/imager.h"
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
    float noise = 0.0f;   ///< > 0 → uyarlamalı örnekleme
    int minSpp = 16;
    float clamp = -1.0f;  ///< < 0 → projedeki değer
    float timeLimit = 0.0f;
    bool passes = false;       ///< çok katmanlı EXR (yalnız .exr çıktı)
    std::string filter;        ///< boş → projedeki değer
    float filterWidth = 0.0f;  ///< ≤ 0 → projedeki / filtrenin varsayılanı
    std::string device = "auto";
    bool listDevices = false;
};

void usage() {
    std::cout << "photon_render <sahne.photon | model.obj | --cornell> [--out dosya.png|.jpg|.exr] [--spp N]\n"
                 "              [--res GxY] [--threads N] [--bounces N] [--no-denoise] [--stats stats.json]\n"
                 "              [--noise 0.02] [--min-spp N] [--clamp X] [--time saniye]\n"
                 "              [--filter box|triangle|gaussian|blackman-harris] [--filter-width piksel]\n"
                 "              [--passes]  (çıktı .exr: render geçişleri tek dosyada)\n"
                 "              [--device auto|cpu|cuda:0] [--list-devices]\n";
}

void printDevices() {
    for (const DeviceInfo& d : enumerateDevices()) {
        std::cout << (d.usable ? "  [x] " : "  [ ] ") << d.id << "  " << deviceKindName(d.kind) << "  " << d.name;
        if (d.memoryBytes > 0) std::cout << "  " << (d.memoryBytes >> 20) << " MB";
        std::cout << "\n        " << d.note << "\n";
    }
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
        auto cam = std::make_unique<ThinLensCamera>(eye, target, Vec3f(0, 1, 0), fov, aspect, aperture, focus);
        cam->setApertureShape(c.value("apertureBlades", 0), c.value("bladeRotation", 0.0f));
        return cam;
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
        else if (a == "--noise") o.noise = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--min-spp") o.minSpp = std::max(2, std::atoi(next().c_str()));
        else if (a == "--clamp") o.clamp = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--time") o.timeLimit = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--filter") o.filter = next();
        else if (a == "--passes") o.passes = true;
        else if (a == "--filter-width") o.filterWidth = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--device") o.device = next();
        else if (a == "--list-devices") o.listDevices = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] != '-') o.input = a;
        else { usage(); return 2; }
    }
    if (o.listDevices) {
        printDevices();
        return 0;
    }
    if (o.input.empty() && !o.cornell) { usage(); return 2; }
    std::string deviceError;
    std::unique_ptr<RenderDevice> device = createDevice(o.device, &deviceError);
    if (!device) {
        std::cerr << deviceError << std::endl;
        return 2;
    }

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
    rs.adaptiveSampling = o.noise > 0.0f;
    if (o.noise > 0.0f) rs.noiseThreshold = o.noise;
    rs.minAdaptiveSamples = o.minSpp;
    rs.clampIndirect = o.clamp >= 0.0f ? o.clamp : data.render.value("clampIndirect", 0.0f);
    {
        // Proje (yoksa kutu: eski projeler öyle render edildi; model/Cornell modunda Blackman–Harris).
        const bool fromProject = !o.cornell && !isSupportedModelFile(o.input);
        const std::string name = !o.filter.empty() ? o.filter
                                 : data.render.value("pixelFilter", std::string(fromProject ? "box" : "blackman-harris"));
        PixelFilter pf{PixelFilterType::Box, 1.0f};
        if (!parsePixelFilter(name.c_str(), pf.type)) {
            std::cerr << "Bilinmeyen filtre: " << name << std::endl;
            return 2;
        }
        pf.width = o.filterWidth > 0.0f ? o.filterWidth
                                        : data.render.value("filterWidth", pf.type == PixelFilterType::Box ? 1.0f : 1.5f);
        rs.filter = pf;
    }
    const ImagerSettings imager = imagerFromJson(data.render.value("imager", nlohmann::json::object()));
    std::shared_ptr<const Camera> camera = cameraFromJson(data.camera, static_cast<float>(o.width) / static_cast<float>(o.height));

    std::string ext = pathToUtf8(pathFromUtf8(o.out).extension());
    if (o.passes && ext != ".exr") {
        std::cerr << "--passes yalnız .exr çıktıyla kullanılabilir (çok katmanlı EXR)" << std::endl;
        return 2;
    }
    // Son render oturumu: hedef örnek, süre sınırı ya da (uyarlamalıda) yakınsama durdurur.
    // Süre sınırıyla biten render da gürültü giderilir (eskiden giderilmiyordu).
    device->setScene(scene);
    RenderSession session(*device);
    SessionConfig sc;
    sc.mode = RenderMode::Final;
    sc.targetSpp = o.spp;
    sc.timeLimit = o.timeLimit;
    sc.aovs = o.denoise || o.passes;
    sc.passes = o.passes;
    sc.batchSeconds = 2.0; // ekran önizlemesi yok: büyük partiler (karo önce, önbellek dostu)
    const auto tRender = std::chrono::steady_clock::now();
    session.begin(camera, rs, sc);
    for (const std::string& w : session.warnings()) std::cerr << "Uyarı: " << w << std::endl;
    while (session.advance()) {
    }
    const int sppDone = session.samples();
    Image img;
    if (o.denoise) {
        Denoiser denoiser;
        img = session.resolveDenoised(denoiser, Denoiser::Quality::High);
    } else {
        img = session.resolve();
    }
    const auto tDone = std::chrono::steady_clock::now();
    if (session.converged() || rs.adaptiveSampling)
        std::cout << "Uyarlamalı: ortalama " << session.averageSamples() << " örnek/piksel (hedef " << o.spp << ")"
                  << (session.converged() ? ", tüm pikseller eşiğin altında" : "") << "\n";

    // Görüntüleyici (bloom, glare, beyaz dengesi...) ekran çıktılarına uygulanır; EXR ham kalır.
    if (ext != ".exr") applyImager(img, imager, rs.exposure);
    bool ok;
    if (ext == ".exr" && o.passes) {
        std::vector<Image> storage;
        const std::vector<ExrLayer> layers = makeExrLayers(session.passes(), session.aovs(), storage);
        const Image alpha = session.resolveAlpha();
        ok = saveMultiLayerEXR(o.out, img, &alpha, layers);
    } else if (ext == ".exr") ok = saveImageEXR(img, o.out);
    else if (ext == ".jpg" || ext == ".jpeg") ok = saveImageJPG(img, o.out, rs.tmo, rs.exposure);
    else ok = saveImagePNG(img, o.out, rs.tmo, rs.exposure);

    const double loadS = std::chrono::duration<double>(tLoad - t0).count();
    const double renderS = std::chrono::duration<double>(tDone - tRender).count();
    const double mrays = static_cast<double>(o.width) * o.height * session.averageSamples() / renderS / 1e6;
    std::cout << "Sahne: " << loadS << " sn, render: " << renderS << " sn, " << mrays << " M örnek/sn ("
              << o.width << "x" << o.height << " @ " << sppDone << " spp, " << rs.maxBounces << " sekme, "
              << device->info().name << ")\n";
    if (!o.stats.empty()) {
        nlohmann::json s = {{"loadSeconds", loadS}, {"renderSeconds", renderS}, {"width", o.width},
                            {"height", o.height}, {"spp", sppDone}, {"averageSpp", session.averageSamples()},
                            {"device", device->info().id}, {"bounces", rs.maxBounces},
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
