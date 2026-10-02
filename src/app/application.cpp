#include "app/application.h"
#include "scene/cornell_box.h"
#include "io/obj_loader.h"
#include "io/gltf_loader.h"
#include "ui/drag_drop.h"
#include "ui/theme.h"
#include "ui/file_dialog.h"
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "camera/orthographic_camera.h"
#include "lights/area_light.h"
#include "lights/directional_light.h"
#include "materials/disney.h"
#include "engine/denoiser.h"
#include "core/image/image_io.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <cstdio>
#include <cmath>

namespace photon {
namespace {

Application* gApp = nullptr;

void dropCallback(GLFWwindow*, int count, const char** paths) {
    if (!gApp || count <= 0) return;
    std::string p = paths[0];
    if (isHdrExtension(p)) gApp->queueHdrDrop(p);
    else if (isTextureExtension(p)) gApp->queueTextureDrop(p);
    else gApp->queueModelDrop(p);
}

std::string findAssetsRoot() {
    namespace fs = std::filesystem;
    fs::path cur = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(cur / "assets" / "materials")) return (cur / "assets").string();
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return "assets";
}

float jsonFloatField(const std::string& json, const std::string& key, float def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    return std::strtof(json.c_str() + pos + 1, nullptr);
}

std::string jsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

Color3f jsonColorField(const std::string& json, const std::string& key, const Color3f& def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return def;
    float r = 0, g = 0, b = 0;
    if (std::sscanf(json.c_str() + pos, "[%f,%f,%f]", &r, &g, &b) != 3) return def;
    return Color3f(r, g, b);
}

Vec3f jsonVec3Field(const std::string& json, const std::string& key, const Vec3f& def) {
    Color3f c = jsonColorField(json, key, Color3f(def.x, def.y, def.z));
    return Vec3f(c.r, c.g, c.b);
}

void fillProceduralSky(Image& out, const Color3f& zenith, const Color3f& horizon) {
    constexpr int W = 256, H = 128;
    out.resize(W, H);
    for (int y = 0; y < H; ++y) {
        float t = static_cast<float>(y) / static_cast<float>(H - 1);
        Color3f c(
            zenith.r * (1.0f - t) + horizon.r * t,
            zenith.g * (1.0f - t) + horizon.g * t,
            zenith.b * (1.0f - t) + horizon.b * t);
        for (int x = 0; x < W; ++x) out.setPixel(x, y, c);
    }
    // Two studio windows so chrome reflects a key and a rim without an HDR file.
    auto stamp = [&](int x0, int y0, int x1, int y1, const Color3f& c) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(W, x1);
        y1 = std::min(H, y1);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) out.setPixel(x, y, c);
    };
    stamp(W * 42 / 100, H * 12 / 100, W * 58 / 100, H * 36 / 100, Color3f(48.0f, 46.0f, 40.0f));
    stamp(W * 6 / 100, H * 18 / 100, W * 18 / 100, H * 40 / 100, Color3f(22.0f, 28.0f, 48.0f));
}

unsigned int makeEnvThumbTex(const Color3f& zenith, const Color3f& horizon) {
    constexpr int W = 64, H = 36;
    std::vector<uint8_t> rgba(static_cast<size_t>(W * H * 4));
    for (int y = 0; y < H; ++y) {
        float t = static_cast<float>(y) / static_cast<float>(H - 1);
        float r = zenith.r * (1.0f - t) + horizon.r * t;
        float g = zenith.g * (1.0f - t) + horizon.g * t;
        float b = zenith.b * (1.0f - t) + horizon.b * t;
        for (int x = 0; x < W; ++x) {
            size_t i = static_cast<size_t>((y * W + x) * 4);
            rgba[i + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
            rgba[i + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
            rgba[i + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace

Application::Application() {
    gApp = this;
    m_state.assetsRoot = findAssetsRoot();
    m_state.settings.width = 512;
    m_state.settings.height = 512;
    m_state.previewSpp = 128;
    m_state.settings.samplesPerPixel = m_state.previewSpp;
    // Product-viz default framing; Cornell overrides when loaded
    m_state.orbitCam.radius = 5.5f;
    m_state.orbitCam.target[0] = 0.0f;
    m_state.orbitCam.target[1] = 0.7f;
    m_state.orbitCam.target[2] = 0.0f;
    m_state.orbitCam.fov = 35.0f;
}

void Application::queueModelDrop(const std::string& path) { m_state.pendingModelDrop = path; }
void Application::queueHdrDrop(const std::string& path) { m_state.pendingHdrDrop = path; }
void Application::queueTextureDrop(const std::string& path) {
    m_state.pendingTextureDrop = path;
    registerTexture(path);
}

Application::~Application() {
    m_state.shutdown = true;
    if (m_state.fullRender.thread.joinable()) m_state.fullRender.thread.join();
    if (m_state.renderThread.joinable()) m_state.renderThread.join();
    if (m_window) {
        destroyMaterialThumbnails();
        destroyEnvThumbnails();
        m_state.glPreview.shutdown();
        m_state.picking.shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    gApp = nullptr;
}

void Application::initWindow() {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    m_window = glfwCreateWindow(1680, 960, "PhotonEngine", nullptr, nullptr);
    if (!m_window) throw std::runtime_error("glfwCreateWindow failed");
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    glfwSetDropCallback(m_window, dropCallback);
}

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = "photon_ui.ini";

    // Prefer a real UI font over ProggyClean — biggest "not a toy" upgrade.
#ifdef _WIN32
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\calibri.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    bool fontOk = false;
    for (const char* path : fontCandidates) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, 16.0f);
            fontOk = true;
            break;
        }
    }
    if (!fontOk) io.Fonts->AddFontDefault();
#else
    io.Fonts->AddFontDefault();
#endif

    applyKeyShotTheme();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::setStatus(const std::string& msg) {
    m_state.statusMessage = msg;
    m_state.statusMessageT = 4.0f;
}

void Application::registerTexture(const std::string& path) {
    if (path.empty()) return;
    for (const auto& t : m_state.textureLibrary) {
        if (t == path) return;
    }
    m_state.textureLibrary.push_back(path);
}

void Application::scanTextures() {
    namespace fs = std::filesystem;
    fs::path texDir = fs::path(m_state.assetsRoot) / "textures";
    if (!fs::exists(texDir)) return;
    for (const auto& e : fs::directory_iterator(texDir)) {
        if (!e.is_regular_file()) continue;
        if (isTextureExtension(e.path().string())) registerTexture(e.path().string());
    }
}

void Application::applyCameraPreset(const std::string& id) {
    auto& c = m_state.orbitCam;
    if (id == "front") {
        c.theta = 1.5708f; c.phi = 0.0f; c.fov = 40.0f;
    } else if (id == "three_quarter") {
        c.theta = 1.15f; c.phi = 0.7f; c.fov = 35.0f;
    } else if (id == "top") {
        c.theta = 0.15f; c.phi = 0.0f; c.fov = 45.0f;
    } else if (id == "product") {
        frameProductCamera();
    } else if (id == "side") {
        c.theta = 1.5708f; c.phi = 1.5708f; c.fov = 40.0f;
    } else {
        return;
    }
    markDirty();
    setStatus(std::string("Kamera: ") + id);
}

void Application::exportTurntableSequence() {
    // ponytail: PNG frame sequence (no ffmpeg dep). GIF/MP4 = external tool / Faz 7.
    if (m_state.fullRender.active) {
        setStatus("Tam render devam ediyor");
        return;
    }
    namespace fs = std::filesystem;
    std::string dir = "turntable_frames";
    FileDialogFilter filters[] = {{"PNG dizi", "*"}, {"Tum Dosyalar", "*.*"}};
    if (!showFileDialog(dir, FileDialogMode::Save, "Turntable PNG dizini (ornek: turntable)", filters, 2))
        return;
    fs::path outDir = dir;
    if (outDir.has_extension()) outDir = outDir.parent_path() / outDir.stem();
    std::error_code ec;
    fs::create_directories(outDir, ec);

    const int frames = std::max(4, m_state.turntableExportFrames);
    const int spp = std::max(1, m_state.turntableExportSpp);
    const int w = std::max(64, std::min(m_state.settings.width, 1280));
    const int h = std::max(64, std::min(m_state.settings.height, 720));
    const float startPhi = m_state.orbitCam.phi;
    const float theta = m_state.orbitCam.theta;
    const float radius = m_state.orbitCam.radius;
    const float fov = m_state.orbitCam.fov;
    const Vec3f target(m_state.orbitCam.target[0], m_state.orbitCam.target[1], m_state.orbitCam.target[2]);
    constexpr float kTwoPi = 6.2831853f;

    setStatus("Turntable export basladi...");
    if (m_state.fullRender.thread.joinable()) m_state.fullRender.thread.join();
    m_state.fullRender.done = false;
    m_state.fullRender.active = true;
    m_state.fullRender.targetSpp = frames;
    m_state.fullRender.currentSpp = 0;
    m_state.fullRender.status = "Turntable...";

    const bool ortho = m_state.orbitCam.orthographic;
    m_state.fullRender.thread = std::thread([this, outDir, frames, spp, w, h, startPhi, theta, radius, fov, target, ortho]() {
        try {
            RenderSettings rs = m_state.settings;
            rs.width = w;
            rs.height = h;
            rs.samplesPerPixel = spp;
            rs.adaptiveSampling = false;
            rs.denoiseEnabled = false;

            Scene scene;
            {
                std::lock_guard<std::mutex> lock(m_state.imageMutex);
                m_state.graph.compile(scene);
                for (auto& l : m_state.lights) scene.addLight(l);
                if (m_state.environment) scene.setEnvironment(m_state.environment);
            }

            float aspect = static_cast<float>(w) / static_cast<float>(h);
            for (int i = 0; i < frames; ++i) {
                if (m_state.shutdown) break;
                float phi = startPhi + (kTwoPi * static_cast<float>(i)) / static_cast<float>(frames);
                Vec3f eye(
                    target.x + radius * std::sin(theta) * std::cos(phi),
                    target.y + radius * std::cos(theta),
                    target.z + radius * std::sin(theta) * std::sin(phi));
                std::unique_ptr<Camera> cam;
                if (ortho) {
                    float height = 2.0f * std::tan(deg2rad(fov) * 0.5f) * std::max(radius, 0.01f);
                    cam = std::make_unique<OrthographicCamera>(eye, target, Vec3f(0, 1, 0), height, aspect);
                } else {
                    cam = std::make_unique<PerspectiveCamera>(eye, target, Vec3f(0, 1, 0), fov, aspect);
                }
                Image img = m_state.renderer.render(scene, *cam, rs);
                char name[64];
                std::snprintf(name, sizeof(name), "frame_%04d.png", i);
                saveImagePNG(img, (outDir / name).string(), rs.tmo, rs.exposure);
                m_state.fullRender.currentSpp = i + 1;
            }
            m_state.fullRender.status =
                "Turntable: " + outDir.string() + " (" + std::to_string(frames) + " PNG)";
        } catch (const std::exception& e) {
            m_state.fullRender.status = std::string("Turntable hata: ") + e.what();
        }
        m_state.fullRender.active = false;
        m_state.fullRender.done = true;
    });
}

bool Application::sampleModelsAvailable() const {
    namespace fs = std::filesystem;
    fs::path models = fs::path(m_state.assetsRoot) / "models";
    return fs::exists(models / "product_stand.obj") && fs::exists(models / "sphere.obj");
}

void Application::frameProductCamera() {
    m_state.orbitCam.target[0] = 0.0f;
    m_state.orbitCam.target[1] = 0.7f;
    m_state.orbitCam.target[2] = 0.0f;
    m_state.orbitCam.radius = 5.5f;
    m_state.orbitCam.theta = 1.15f;
    m_state.orbitCam.phi = 0.42f;
    m_state.orbitCam.fov = 35.0f;
}

void Application::loadSampleScene() {
    namespace fs = std::filesystem;
    m_state.settings.aoStrength = 0.35f;
    m_state.graph.clear();
    m_state.selected = nullptr;
    m_state.lights.clear();
    m_state.undo.clear();

    fs::path models = fs::path(m_state.assetsRoot) / "models";
    if (fs::exists(models / "floor.obj"))
        importModelInternal((models / "floor.obj").string());
    SceneNode* stand = nullptr;
    SceneNode* ball = nullptr;
    if (fs::exists(models / "product_stand.obj"))
        stand = importModelInternal((models / "product_stand.obj").string());
    if (fs::exists(models / "sphere.obj"))
        ball = importModelInternal((models / "sphere.obj").string());
    if (ball)
        ball->localTransform = Transform::translate(Vec3f(0.0f, 0.95f, 0.0f));

    auto applyPresetQuiet = [&](SceneNode* node, const char* id) {
        if (!node) return;
        const MaterialPreset* p = m_state.materialLib.findById(id);
        if (!p) return;
        auto mat = m_state.materialLib.createMaterial(*p);
        std::function<void(SceneNode*)> visit = [&](SceneNode* n) {
            if (!n) return;
            if (n->mesh || n->type == SceneNodeType::Sphere) n->material = mat;
            for (auto& c : n->children) visit(c.get());
        };
        visit(node);
    };
    applyPresetQuiet(stand, "brushed_aluminum");
    applyPresetQuiet(ball, "chrome");
    if (m_state.graph.root() && !m_state.graph.root()->children.empty())
        applyPresetQuiet(m_state.graph.root()->children.front().get(), "white_plastic");

    fs::path studio = fs::path(m_state.assetsRoot) / "studios" / "product_studio.json";
    if (fs::exists(studio)) applyStudioPreset(studio.string());
    else {
        for (const auto& e : m_state.environments) {
            if (e.id == "studio_soft") { applyEnvironmentEntry(e); break; }
        }
        m_state.lights.push_back(std::make_shared<AreaLight>(
            Vec3f(1.8f, 3.5f, 1.2f), Vec3f(-1.4f, 0, 0), Vec3f(0, 0, 1.0f), Color3f(18.0f)));
        m_state.lights.push_back(std::make_shared<DirectionalLight>(
            Vec3f(0.55f, -0.75f, 0.25f), Color3f(2.2f)));
        frameProductCamera();
    }
    AABB box = AABB::empty();
    std::function<void(const SceneNode*, const Transform&)> walk =
        [&](const SceneNode* n, const Transform& parent) {
            if (!n || !n->visible) return;
            Transform world = parent * n->localTransform;
            if (n->mesh) {
                for (const auto& p : n->mesh->positions())
                    box.merge(world.transformPoint(p));
            }
            for (const auto& c : n->children) walk(c.get(), world);
        };
    walk(m_state.graph.root(), Transform{});
    if (box.pMin.x <= box.pMax.x) {
        GroundQuad g = placeGroundUnder(box);
        std::vector<Vec3f> pos = {
            g.corner, g.corner + g.edgeU, g.corner + g.edgeU + g.edgeV, g.corner + g.edgeV};
        std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
        auto mat = std::make_shared<DisneyMaterial>(Color3f(0.55f), 0.0f, 0.9f, 0.15f);
        auto mesh = std::make_shared<TriangleMesh>(
            pos, std::vector<Vec3f>{}, std::vector<Vec2f>{}, idx, mat.get());
        auto node = std::make_unique<SceneNode>("Ground", SceneNodeType::Mesh);
        node->mesh = mesh;
        node->material = mat;
        node->pickId = m_state.graph.allocatePickId();
        m_state.graph.root()->addChild(std::move(node));
    }
    frameProductCamera();
    m_state.orbitCam.focusExplicit = false;
    rebuildScene();
    setStatus("Ornek urun sahnesi yuklendi");
}

void Application::loadCornellScene() {
    m_state.graph.clear();
    m_state.selected = nullptr;
    m_state.lights.clear();
    m_state.undo.clear();
    buildCornellBox(m_state.graph);
    for (const auto& e : m_state.environments) {
        if (e.id == "studio_soft") { applyEnvironmentEntry(e); break; }
    }
    if (!m_state.environment) {
        fillProceduralSky(m_state.envMap, Color3f(0.75f, 0.8f, 0.95f), Color3f(0.55f, 0.55f, 0.58f));
        m_state.environment = std::make_shared<EnvironmentLight>(&m_state.envMap, 0.0f, 1.0f);
    }
    m_state.lights.push_back(std::make_shared<AreaLight>(
        Vec3f(343, 548.0f, 227), Vec3f(-130, 0, 0), Vec3f(0, 0, 105), Color3f(15.0f)));
    m_state.lights.push_back(std::make_shared<DirectionalLight>(
        Vec3f(0.6f, -0.7f, 0.2f), Color3f(2.0f)));
    m_state.orbitCam.radius = 1200.0f;
    m_state.orbitCam.focusExplicit = false;
    m_state.settings.aoStrength = 0.0f;
    m_state.orbitCam.target[0] = 278.0f;
    m_state.orbitCam.target[1] = 273.0f;
    m_state.orbitCam.target[2] = 277.5f;
    m_state.orbitCam.fov = 40.0f;
    // theta=0 puts the eye on +Y, parallel to world up, so lookAt/camera basis
    // collapses and every ray misses the open face. Sit outside z=0 looking in.
    m_state.orbitCam.theta = 1.5708f;
    m_state.orbitCam.phi = -1.5708f;
    rebuildScene();
    setStatus("Cornell Box yuklendi");
}

void Application::loadAssets() {
    m_state.materialLib.loadFromDirectory(m_state.assetsRoot + "/materials");
    buildMaterialThumbnails();
    scanEnvironments();
    buildEnvThumbnails();
    scanTextures();

    m_state.settings.tmo = ToneMapOperator::ACES;
    m_state.fullRender.spp = 256;
    m_state.settings.denoiseEnabled = true;

    if (sampleModelsAvailable()) loadSampleScene();
    else loadCornellScene();

    namespace fs = std::filesystem;
    m_state.showOnboarding = !fs::exists(fs::path(m_state.assetsRoot) / ".onboarded");
}

void Application::buildMaterialThumbnails() {
    destroyMaterialThumbnails();
    constexpr int kThumb = 64;
    std::vector<uint8_t> rgba;
    for (const auto& p : m_state.materialLib.presets()) {
        renderSphereThumbnail(p, kThumb, rgba);
        unsigned int tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kThumb, kThumb, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        m_state.materialThumbs[p.id] = tex;
    }
}

void Application::destroyMaterialThumbnails() {
    for (auto& kv : m_state.materialThumbs) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    m_state.materialThumbs.clear();
}

void Application::applyMaterialPreset(SceneNode* node, const std::string& presetId) {
    if (!node) return;
    const MaterialPreset* p = m_state.materialLib.findById(presetId);
    if (!p) return;
    auto after = m_state.materialLib.createMaterial(*p);
    std::function<void(SceneNode*)> visit = [&](SceneNode* n) {
        if (!n) return;
        if (n->mesh || n->type == SceneNodeType::Sphere) {
            auto before = n->material;
            n->material = after;
            pushMaterialUndo(n, before, after);
        }
        for (auto& c : n->children) visit(c.get());
    };
    visit(node);
    m_state.selected = node;
    rebuildScene();
}

void Application::pushMaterialUndo(SceneNode* node, std::shared_ptr<Material> before,
                                   std::shared_ptr<Material> after) {
    if (!node || before == after) return;
    m_state.undo.push(
        [this, node, before]() {
            if (!node) return;
            node->material = before;
            rebuildScene();
        },
        [this, node, after]() {
            if (!node) return;
            node->material = after;
            rebuildScene();
        });
}

void Application::rebuildScene() {
    std::lock_guard<std::mutex> lock(m_state.imageMutex);
    m_state.graph.compile(m_state.flatScene);
    for (auto& l : m_state.lights) m_state.flatScene.addLight(l);
    if (m_state.environment) m_state.flatScene.setEnvironment(m_state.environment);
    markDirty();
}

void Application::markDirty() {
    m_state.renderDirty = true;
    m_state.currentSpp = 0;
}

void Application::startRenderThread() {
    m_state.renderThread = std::thread([this]() {
        while (!m_state.shutdown) {
            if (m_state.fullRender.active.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (m_state.renderDirty.load()) {
                std::lock_guard<std::mutex> lock(m_state.imageMutex);
                // Progressive preview is capped; full-res lives in Tam Render dialog.
                const int vw = std::max(1, m_state.viewportW);
                const int vh = std::max(1, m_state.viewportH);
                const float scale = std::min(1.0f, 960.0f / static_cast<float>(std::max(vw, vh)));
                const int tw = std::max(64, static_cast<int>(vw * scale));
                const int th = std::max(64, static_cast<int>(vh * scale));
                m_state.settings.width = tw;
                m_state.settings.height = th;
                m_state.accumImage.resize(tw, th);
                m_state.accumImage.clear();
                m_state.currentSpp = 0;
                m_state.renderDirty = false;
            }
            int spp = m_state.currentSpp.load();
            if (spp < m_state.settings.samplesPerPixel) {
                RenderSettings rs;
                std::unique_ptr<Camera> cam;
                {
                    std::lock_guard<std::mutex> lock(m_state.imageMutex);
                    rs = m_state.settings;
                    float aspect = static_cast<float>(rs.width) / std::max(1, rs.height);
                    cam = makeCamera(aspect);
                    // Hold scene lock for the pass so rebuildScene can't free BVH under us.
                    m_state.renderer.renderSamplePass(m_state.flatScene, *cam, rs,
                                                      m_state.accumImage, spp);
                    m_state.currentSpp = spp + 1;
                    m_state.imageReady = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

std::unique_ptr<Camera> Application::makeCamera(float aspect) const {
    float pos[3];
    m_state.orbitCam.getPosition(pos);
    Vec3f position(pos[0], pos[1], pos[2]);
    Vec3f target(m_state.orbitCam.target[0], m_state.orbitCam.target[1], m_state.orbitCam.target[2]);
    if (m_state.orbitCam.orthographic) {
        float height = 2.0f * std::tan(deg2rad(m_state.orbitCam.fov) * 0.5f) *
                       std::max(m_state.orbitCam.radius, 0.01f);
        return std::make_unique<OrthographicCamera>(position, target, Vec3f(0, 1, 0), height, aspect);
    }
    if (m_state.orbitCam.aperture > 0.0f) {
        return std::make_unique<ThinLensCamera>(position, target, Vec3f(0, 1, 0),
            m_state.orbitCam.fov, aspect, m_state.orbitCam.aperture,
            effectiveFocusDistance(m_state.orbitCam));
    }
    return std::make_unique<PerspectiveCamera>(position, target, Vec3f(0, 1, 0),
        m_state.orbitCam.fov, aspect);
}

void Application::importModel(const std::string& path) {
    if (lowerExt(path) == ".fbx") {
        setStatus("FBX desteklenmiyor - OBJ veya glTF kullanin");
        return;
    }
    SceneNode* added = importModelInternal(path);
    if (!added) {
        setStatus("Model yuklenemedi");
        return;
    }
    setStatus(std::string("Model: ") + std::filesystem::path(path).filename().string());
    std::string name = added->name;
    std::string src = path;
    m_state.undo.push(
        [this, name]() {
            for (auto& c : m_state.graph.root()->children) {
                if (c->name == name) {
                    if (m_state.selected == c.get() ||
                        (m_state.selected && m_state.selected->parent == c.get()))
                        m_state.selected = nullptr;
                    m_state.graph.removeNode(c.get());
                    break;
                }
            }
            rebuildScene();
        },
        [this, src]() { importModelInternal(src); });
}

SceneNode* Application::importModelInternal(const std::string& path) {
    std::string ext = lowerExt(path);
    auto defaultMat = std::make_shared<DisneyMaterial>(Color3f(0.8f), 0.0f, 0.5f, 0.5f);

    std::vector<std::shared_ptr<TriangleMesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;

    if (ext == ".obj") {
        auto loaded = ObjLoader::load(path, defaultMat.get());
        meshes = std::move(loaded.meshes);
        materials = std::move(loaded.materials);
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (i >= materials.size()) materials.push_back(nullptr);
            if (!materials[i]) materials[i] = defaultMat;
        }
    } else if (ext == ".gltf" || ext == ".glb") {
        auto r = GltfLoader::load(path, defaultMat.get());
        meshes = std::move(r.meshes);
        materials = std::move(r.materials);
    } else return nullptr;

    auto group = std::make_unique<SceneNode>(std::filesystem::path(path).stem().string(), SceneNodeType::Group);
    group->sourcePath = path;
    for (size_t i = 0; i < meshes.size(); ++i) {
        auto node = std::make_unique<SceneNode>("mesh_" + std::to_string(i), SceneNodeType::Mesh);
        node->mesh = meshes[i];
        node->material = i < materials.size() ? materials[i] : defaultMat;
        node->pickId = m_state.graph.allocatePickId();
        group->addChild(std::move(node));
    }
    SceneNode* added = m_state.graph.root()->addChild(std::move(group));
    rebuildScene();
    return added;
}

std::string Application::serializeCameraJson() const {
    const auto& c = m_state.orbitCam;
    std::ostringstream ss;
    ss << "{\"target\":[" << c.target[0] << "," << c.target[1] << "," << c.target[2]
       << "],\"radius\":" << c.radius
       << ",\"theta\":" << c.theta
       << ",\"phi\":" << c.phi
       << ",\"fov\":" << c.fov
       << ",\"focalLengthMm\":" << c.focalLengthMm
       << ",\"orthographic\":" << (c.orthographic ? "true" : "false")
       << ",\"aperture\":" << c.aperture
       << ",\"focusDistance\":" << c.focusDistance
       << ",\"focusExplicit\":" << (c.focusExplicit ? "true" : "false") << "}";
    return ss.str();
}

void Application::applyCameraJson(const std::string& json) {
    if (json.empty()) return;
    auto& c = m_state.orbitCam;
    Vec3f t = jsonVec3Field(json, "target", Vec3f(c.target[0], c.target[1], c.target[2]));
    c.target[0] = t.x; c.target[1] = t.y; c.target[2] = t.z;
    c.radius = jsonFloatField(json, "radius", c.radius);
    c.theta = jsonFloatField(json, "theta", c.theta);
    c.phi = jsonFloatField(json, "phi", c.phi);
    c.fov = jsonFloatField(json, "fov", c.fov);
    if (json.find("\"focalLengthMm\"") != std::string::npos)
        c.focalLengthMm = jsonFloatField(json, "focalLengthMm", c.focalLengthMm);
    else
        c.focalLengthMm = focalMmFromFovDegrees(c.fov);
    auto ortho = json.find("\"orthographic\"");
    if (ortho != std::string::npos) {
        auto tru = json.find("true", ortho);
        auto fal = json.find("false", ortho);
        c.orthographic = tru != std::string::npos && (fal == std::string::npos || tru < fal);
    }
    c.aperture = jsonFloatField(json, "aperture", c.aperture);
    if (json.find("\"focusDistance\"") != std::string::npos)
        c.focusDistance = jsonFloatField(json, "focusDistance", c.focusDistance);
    auto fe = json.find("\"focusExplicit\"");
    if (fe != std::string::npos) {
        auto tru = json.find("true", fe);
        auto fal = json.find("false", fe);
        c.focusExplicit = tru != std::string::npos && (fal == std::string::npos || tru < fal);
    } else {
        c.focusExplicit = false;
    }
}

void Application::applyProjectFile(const ProjectFile& proj) {
    m_state.graph.clear();
    m_state.selected = nullptr;
    m_state.lights.clear();
    m_state.undo.clear();

    if (proj.includeCornell) {
        buildCornellBox(m_state.graph);
        m_state.lights.push_back(std::make_shared<AreaLight>(
            Vec3f(343, 548.0f, 227), Vec3f(-130, 0, 0), Vec3f(0, 0, 105), Color3f(15.0f)));
    }
    for (const auto& imp : proj.imports) {
        if (!imp.path.empty()) importModelInternal(imp.path);
    }
    for (const auto& m : proj.materials) {
        SceneNode* n = findNodeByPath(m_state.graph.root(), m.nodePath);
        if (!n) continue;
        auto d = std::make_shared<DisneyMaterial>(
            Color3f(m.baseColor[0], m.baseColor[1], m.baseColor[2]),
            m.metallic, m.roughness, m.specular);
        d->setClearCoat(m.clearCoat);
        d->setClearCoatRoughness(m.clearCoatRoughness);
        if (!m.albedoMap.empty()) d->setAlbedoMap(m.albedoMap);
        if (!m.normalMap.empty()) d->setNormalMap(m.normalMap);
        if (!m.roughnessMap.empty()) d->setRoughnessMap(m.roughnessMap);
        if (!m.metalnessMap.empty()) d->setMetalnessMap(m.metalnessMap);
        n->material = d;
    }
    for (const auto& t : proj.transforms) {
        SceneNode* n = findNodeByPath(m_state.graph.root(), t.nodePath);
        if (!n) continue;
        constexpr float kDeg2Rad = 3.14159265f / 180.0f;
        Transform xf = Transform::translate(Vec3f(t.translate[0], t.translate[1], t.translate[2]));
        if (t.hasRotateScale) {
            xf = xf
                * Transform::rotateZ(t.rotateDeg[2] * kDeg2Rad)
                * Transform::rotateY(t.rotateDeg[1] * kDeg2Rad)
                * Transform::rotateX(t.rotateDeg[0] * kDeg2Rad)
                * Transform::scale(Vec3f(t.scale[0], t.scale[1], t.scale[2]));
        }
        n->localTransform = xf;
    }
    applyCameraJson(proj.cameraJson);
    if (!proj.environmentPath.empty()) {
        namespace fs = std::filesystem;
        fs::path p = proj.environmentPath;
        if (!p.is_absolute()) p = fs::path(m_state.assetsRoot) / p;
        if (fs::exists(p)) applyHdrEnvironment(p.string());
    }
    rebuildScene();
}

void Application::dismissOnboarding() {
    m_state.showOnboarding = false;
    namespace fs = std::filesystem;
    std::ofstream(fs::path(m_state.assetsRoot) / ".onboarded");
}

void Application::applyHdrEnvironment(const std::string& path) {
    auto img = loadImageHDR(path);
    if (!img) img = loadImageEXR(path);
    if (!img) return;
    m_state.envMap.resize(img->width(), img->height());
    for (int y = 0; y < img->height(); ++y) {
        for (int x = 0; x < img->width(); ++x) {
            m_state.envMap.setPixel(x, y, img->getPixel(x, y));
        }
    }
    m_state.environment = std::make_shared<EnvironmentLight>(&m_state.envMap, 0.0f, 1.0f);
    m_state.activeEnvPath = path;

    // Keep dropped HDR visible in Environments tab
    namespace fs = std::filesystem;
    std::string id = fs::path(path).stem().string();
    bool found = false;
    for (auto& e : m_state.environments) {
        if (e.path == path || e.id == id) {
            e.path = path;
            found = true;
            break;
        }
    }
    if (!found) {
        EnvEntry e;
        e.id = id;
        e.name = id;
        e.path = path;
        m_state.environments.push_back(e);
        buildEnvThumbnails();
    }
    rebuildScene();
}

void Application::applyEnvironmentEntry(const EnvEntry& e) {
    if (!e.path.empty()) {
        namespace fs = std::filesystem;
        fs::path p = e.path;
        if (!p.is_absolute()) p = fs::path(m_state.assetsRoot) / p;
        if (fs::exists(p)) {
            applyHdrEnvironment(p.string());
            return;
        }
    }
    fillProceduralSky(m_state.envMap, e.zenith, e.horizon);
    m_state.environment = std::make_shared<EnvironmentLight>(&m_state.envMap, 0.0f, 1.0f);
    m_state.activeEnvPath.clear();
    rebuildScene();
}

void Application::addAreaLight() {
    Vec3f center(m_state.orbitCam.target[0], m_state.orbitCam.target[1] + 200.0f,
                 m_state.orbitCam.target[2]);
    m_state.lights.push_back(std::make_shared<AreaLight>(
        center, Vec3f(-80, 0, 0), Vec3f(0, 0, 80), Color3f(10.0f)));
    rebuildScene();
}

void Application::addDirectionalLight() {
    m_state.lights.push_back(std::make_shared<DirectionalLight>(
        Vec3f(0.35f, -1.0f, 0.25f), Color3f(4.0f)));
    rebuildScene();
}

void Application::scanEnvironments() {
    namespace fs = std::filesystem;
    m_state.environments.clear();
    fs::path envDir = fs::path(m_state.assetsRoot) / "environments";
    if (!fs::exists(envDir)) return;

    for (const auto& entry : fs::directory_iterator(envDir)) {
        const auto& p = entry.path();
        std::string ext = lowerExt(p.string());
        if (ext == ".json") {
            std::ifstream f(p);
            if (!f) continue;
            std::ostringstream ss;
            ss << f.rdbuf();
            std::string json = ss.str();
            EnvEntry e;
            e.id = jsonStringField(json, "id");
            if (e.id.empty()) e.id = p.stem().string();
            e.name = jsonStringField(json, "name");
            if (e.name.empty()) e.name = e.id;
            e.path = jsonStringField(json, "path");
            e.preview = jsonColorField(json, "preview", e.preview);
            e.zenith = jsonColorField(json, "zenith", e.zenith);
            e.horizon = jsonColorField(json, "horizon", e.horizon);
            m_state.environments.push_back(std::move(e));
        } else if (ext == ".hdr" || ext == ".exr") {
            EnvEntry e;
            e.id = p.stem().string();
            e.name = e.id;
            e.path = p.string();
            m_state.environments.push_back(std::move(e));
        }
    }
}

void Application::buildEnvThumbnails() {
    destroyEnvThumbnails();
    for (const auto& e : m_state.environments) {
        m_state.envThumbs[e.id] = makeEnvThumbTex(e.zenith, e.horizon);
    }
}

void Application::destroyEnvThumbnails() {
    for (auto& kv : m_state.envThumbs) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    m_state.envThumbs.clear();
}

void Application::processPendingDrops() {
    if (!m_state.pendingModelDrop.empty()) {
        std::string p = m_state.pendingModelDrop;
        m_state.pendingModelDrop.clear();
        if (!isModelExtension(p)) {
            setStatus("Desteklenmeyen model (OBJ/glTF). FBX = v2.");
        } else {
            importModel(p);
        }
    }
    if (!m_state.pendingHdrDrop.empty()) {
        applyHdrEnvironment(m_state.pendingHdrDrop);
        m_state.pendingHdrDrop.clear();
    }
}

void Application::setupDocking() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("DockRoot", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGuiID dockId = ImGui::GetID("MainDock_v2");
    if (ImGui::DockBuilderGetNode(dockId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
        ImGuiID left = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.20f, nullptr, &dockId);
        ImGuiID right = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.24f, nullptr, &dockId);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.045f, nullptr, &dockId);
        ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.38f, nullptr, &right);
        ImGui::DockBuilderDockWindow("Kutuphane", left);
        ImGui::DockBuilderDockWindow("Sahne", rightTop);
        ImGui::DockBuilderDockWindow("Inceleyici", right);
        ImGui::DockBuilderDockWindow("Render", right);
        ImGui::DockBuilderDockWindow("Viewport", dockId);
        ImGui::DockBuilderDockWindow("Durum", bottom);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    if (ImGui::BeginMenuBar()) {
        drawMenubar();
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

void Application::applyQualityPreset(int spp) {
    m_state.previewSpp = spp;
    m_state.settings.samplesPerPixel = spp;
    markDirty();
}

void Application::applyStudioPreset(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    m_state.orbitCam.fov = jsonFloatField(json, "fov", m_state.orbitCam.fov);
    m_state.orbitCam.radius = jsonFloatField(json, "radius", m_state.orbitCam.radius);
    m_state.orbitCam.theta = jsonFloatField(json, "yaw", m_state.orbitCam.theta);
    m_state.orbitCam.phi = jsonFloatField(json, "pitch", m_state.orbitCam.phi);
    m_state.settings.exposure = jsonFloatField(json, "exposure", m_state.settings.exposure);
    if (json.find("\"aoStrength\"") != std::string::npos)
        m_state.settings.aoStrength = jsonFloatField(json, "aoStrength", m_state.settings.aoStrength);

    std::string hdr = jsonStringField(json, "hdr");
    if (!hdr.empty()) {
        namespace fs = std::filesystem;
        fs::path hp = hdr;
        if (!hp.is_absolute()) hp = fs::path(m_state.assetsRoot) / hp;
        if (fs::exists(hp)) applyHdrEnvironment(hp.string());
        else {
            // Match env library id / stem
            for (const auto& e : m_state.environments) {
                if (e.id == hdr || e.name == hdr) {
                    applyEnvironmentEntry(e);
                    break;
                }
            }
        }
    }

    // 3-point (or any) light pack from "lights": [ { ... }, ... ]
    auto lightsPos = json.find("\"lights\"");
    if (lightsPos != std::string::npos) {
        auto arr = json.find('[', lightsPos);
        auto arrEnd = json.find(']', arr);
        if (arr != std::string::npos && arrEnd != std::string::npos && arrEnd > arr) {
            std::string arrJson = json.substr(arr, arrEnd - arr + 1);
            m_state.lights.clear();
            size_t search = 0;
            while (true) {
                auto objStart = arrJson.find('{', search);
                if (objStart == std::string::npos) break;
                auto objEnd = arrJson.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string obj = arrJson.substr(objStart, objEnd - objStart + 1);
                std::string type = jsonStringField(obj, "type");
                float intensity = jsonFloatField(obj, "intensity", 5.0f);
                if (type == "directional") {
                    Vec3f dir = jsonVec3Field(obj, "direction", Vec3f(0.3f, -1.0f, 0.2f));
                    m_state.lights.push_back(
                        std::make_shared<DirectionalLight>(dir, Color3f(intensity)));
                } else {
                    // default / "area"
                    Vec3f pos = jsonVec3Field(obj, "position",
                        Vec3f(m_state.orbitCam.target[0], m_state.orbitCam.target[1] + 250.0f,
                              m_state.orbitCam.target[2]));
                    Vec3f u = jsonVec3Field(obj, "u", Vec3f(-100, 0, 0));
                    Vec3f v = jsonVec3Field(obj, "v", Vec3f(0, 0, 100));
                    m_state.lights.push_back(
                        std::make_shared<AreaLight>(pos, u, v, Color3f(intensity)));
                }
                search = objEnd + 1;
            }
        }
    } else {
        // Legacy flat lightIntensity → scale first area light
        float lightI = jsonFloatField(json, "lightIntensity", 0.0f);
        if (lightI > 0.0f && !m_state.lights.empty()) {
            if (auto area = std::dynamic_pointer_cast<AreaLight>(m_state.lights.front())) {
                area->setRadiance(Color3f(lightI));
            }
        }
    }
    rebuildScene();
}

void Application::importModelDialog() {
    std::string path;
    FileDialogFilter filters[] = {
        {"3D Modeller", "*.obj;*.gltf;*.glb"},
        {"Tum Dosyalar", "*.*"},
    };
    if (showFileDialog(path, FileDialogMode::Open, "Model Ac", filters, 2)) importModel(path);
}

void Application::importHdrDialog() {
    std::string path;
    FileDialogFilter filters[] = {
        {"HDR/EXR", "*.hdr;*.exr"},
        {"Tum Dosyalar", "*.*"},
    };
    if (showFileDialog(path, FileDialogMode::Open, "Ortam Haritasi Ac", filters, 2)) applyHdrEnvironment(path);
}

void Application::exportImage(bool exr) {
    std::string path = exr ? "export.exr" : m_state.exportPath;
    FileDialogFilter filters[] = {
        {exr ? "OpenEXR" : "PNG", exr ? "*.exr" : "*.png"},
        {"Tum Dosyalar", "*.*"},
    };
    if (!showFileDialog(path, FileDialogMode::Save, exr ? "EXR Kaydet" : "PNG Kaydet", filters, 1)) return;
    std::lock_guard<std::mutex> lock(m_state.imageMutex);
    if (exr) saveImageEXR(m_state.accumImage, path);
    else saveImagePNG(m_state.accumImage, path, m_state.settings.tmo, m_state.settings.exposure);
    if (!exr) m_state.exportPath = path;
}

void Application::startFullRender() {
    if (m_state.fullRender.active) return;
    if (m_state.fullRender.thread.joinable()) m_state.fullRender.thread.join();
    m_state.fullRender.done = false;
    m_state.fullRender.active = true;
    m_state.fullRender.currentSpp = 0;
    m_state.fullRender.targetSpp = m_state.fullRender.spp;
    m_state.fullRender.status = "Baslatiliyor...";

    const int outW = m_state.fullRender.width;
    const int outH = m_state.fullRender.height;
    const int outSpp = m_state.fullRender.spp;
    const int outThreads = m_state.fullRender.numThreads;
    const std::string outPath = m_state.fullRender.outputPath;
    RenderSettings rs = m_state.settings;
    rs.width = outW;
    rs.height = outH;
    rs.samplesPerPixel = outSpp;
    rs.numThreads = outThreads;

    m_state.fullRender.thread = std::thread([this, rs, outPath]() {
        try {
            Scene scene;
            std::unique_ptr<Camera> cam;
            {
                std::lock_guard<std::mutex> lock(m_state.imageMutex);
                m_state.graph.compile(scene);
                for (auto& l : m_state.lights) scene.addLight(l);
                if (m_state.environment) scene.setEnvironment(m_state.environment);
                float aspect = static_cast<float>(rs.width) / rs.height;
                cam = makeCamera(aspect);
            }

            Image img(rs.width, rs.height);
            m_state.renderer.renderProgressive(scene, *cam, rs, [&](const Image& partial, int spp) {
                m_state.fullRender.currentSpp = spp;
                img = partial;
            });

            // renderProgressive already denoises when enabled; note stub vs real OIDN in status.
            const bool wantedDenoise = rs.denoiseEnabled;
            const bool hadOidn = denoiseAvailable();

            if (!outPath.empty()) {
                if (outPath.size() >= 4 && outPath.substr(outPath.size() - 4) == ".exr") {
                    saveImageEXR(img, outPath);
                } else {
                    saveImagePNG(img, outPath, rs.tmo, rs.exposure);
                }
            }
            std::string status = outPath.empty() ? "Tamamlandi" : ("Kaydedildi: " + outPath);
            if (wantedDenoise) {
                status += hadOidn ? " (OIDN)" : " (soft blur)";
            }
            m_state.fullRender.status = status;
        } catch (const std::exception& e) {
            m_state.fullRender.status = std::string("Hata: ") + e.what();
        }
        m_state.fullRender.active = false;
        m_state.fullRender.done = true;
    });
}

void Application::drawMenubar() {
    if (ImGui::BeginMenu("Dosya")) {
        if (ImGui::MenuItem("Model Ac...", "Ctrl+O")) importModelDialog();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("OBJ / glTF ice aktar (FBX henuz yok)");
        if (ImGui::MenuItem("HDR Ac...")) importHdrDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Ornek Sahne")) loadSampleScene();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stand + kure + Product Studio test ortami");
        if (ImGui::MenuItem("Cornell Box")) loadCornellScene();
        ImGui::Separator();
        if (ImGui::MenuItem("Kaydet", "Ctrl+S")) {
            saveProject(m_state.projectPath, m_state.graph, serializeCameraJson(), m_state.activeEnvPath);
            setStatus("Proje kaydedildi");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projeyi .photon JSON olarak kaydet");
        if (ImGui::MenuItem("Yukle...")) {
            std::string path = m_state.projectPath;
            FileDialogFilter filters[] = {{"Photon Proje", "*.photon"}, {"Tum Dosyalar", "*.*"}};
            if (showFileDialog(path, FileDialogMode::Open, "Proje Ac", filters, 2)) {
                std::string json;
                if (loadProject(path, m_state.graph, json)) {
                    ProjectFile proj;
                    if (parseProjectJson(json, proj)) {
                        m_state.projectPath = path;
                        applyProjectFile(proj);
                        setStatus("Proje yuklendi");
                    }
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Disa Aktar PNG...")) exportImage(false);
        if (ImGui::MenuItem("Disa Aktar EXR...")) exportImage(true);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Duzenle")) {
        if (ImGui::MenuItem("Geri Al", "Ctrl+Z", false, m_state.undo.canUndo())) m_state.undo.undo();
        if (ImGui::MenuItem("Yinele", "Ctrl+Y", false, m_state.undo.canRedo())) m_state.undo.redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Gorunum")) {
        if (ImGui::MenuItem("Hizli onizleme", nullptr, m_state.fastPreview)) {
            m_state.fastPreview = true;
        }
        if (ImGui::MenuItem("Kalite onizleme", nullptr, !m_state.fastPreview)) {
            m_state.fastPreview = false;
        }
        ImGui::MenuItem("Gelismis Mod", nullptr, &m_state.advancedMode);
        ImGui::MenuItem("Render Paneli", nullptr, &m_state.showRenderPanel);
        ImGui::Separator();
        if (ImGui::MenuItem("Test Ortami")) loadSampleScene();
        if (ImGui::MenuItem("Hosgeldiniz")) m_state.showOnboarding = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem("Onizleme (128 SPP)")) applyQualityPreset(128);
        if (ImGui::MenuItem("Yuksek (512 SPP)")) applyQualityPreset(512);
        if (ImGui::MenuItem("Final (1024 SPP)")) applyQualityPreset(1024);
        ImGui::Separator();
        if (ImGui::MenuItem("Yeniden Baslat", "F5")) markDirty();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Progressive onizlemeyi sifirla");
        if (ImGui::MenuItem("Tam Cozunurluk...")) m_state.showFullRenderDialog = true;
        ImGui::Separator();
        ImGui::SliderInt("Turntable kare", &m_state.turntableExportFrames, 8, 72);
        ImGui::SliderInt("Turntable SPP", &m_state.turntableExportSpp, 1, 64);
        if (ImGui::MenuItem("Turntable PNG Dizisi...")) exportTurntableSequence();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tam tur PNG kareleri (GIF/MP4 icin harici arac)");
        ImGui::EndMenu();
    }

    // Right-side action cluster
    float rightW = 300.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > rightW) ImGui::SameLine(ImGui::GetCursorPosX() + avail - rightW);
    ImGui::TextDisabled("%.0f fps", m_state.fps);
    ImGui::SameLine();
    ImGui::TextDisabled("%d spp", m_state.currentSpp.load());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.38f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.48f, 0.62f, 1.0f));
    if (ImGui::SmallButton("  Render  ")) m_state.showFullRenderDialog = true;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::SmallButton(" Export ")) exportImage(false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("PNG disa aktar");
}

void Application::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_state.undo.undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_state.undo.redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        saveProject(m_state.projectPath, m_state.graph, serializeCameraJson(), m_state.activeEnvPath);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) importModelDialog();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) markDirty();
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyCtrl)
        m_state.orbitCam.turntable = !m_state.orbitCam.turntable;
}

void Application::drawLibraryPanel() {
    ImGui::Begin("Kutuphane");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    if (ImGui::BeginTabBar("libtabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    if (ImGui::BeginTabItem("Materyal")) {
      ImGui::TextDisabled("Surukle -> mesh");
      ImGui::Spacing();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
      for (const auto& cat : m_state.materialLib.categories()) {
        if (ImGui::TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          int col = 0;
          for (const auto* p : m_state.materialLib.byCategory(cat)) {
            ImGui::PushID(p->id.c_str());
            auto it = m_state.materialThumbs.find(p->id);
            unsigned int tex = (it != m_state.materialThumbs.end()) ? it->second : 0;
            if (tex) {
              ImGui::ImageButton("##sphere", (ImTextureID)(intptr_t)tex, ImVec2(64, 64));
            } else {
              ImVec4 col4(p->baseColor.r, p->baseColor.g, p->baseColor.b, 1.0f);
              ImGui::ColorButton("##sphere", col4, ImGuiColorEditFlags_NoTooltip, ImVec2(64, 64));
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
              ImGui::SetDragDropPayload(kPayloadMaterial, p->id.c_str(), p->id.size() + 1);
              if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(40, 40));
              ImGui::TextUnformatted(p->name.c_str());
              ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p->name.c_str());
            if (++col % 3 != 0) ImGui::SameLine();
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
      }
      ImGui::PopStyleVar();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Ortam")) {
      ImGui::TextDisabled("HDR birakin veya secin");
      if (m_state.environment) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.78f, 0.55f, 1), "aktif");
      }
      ImGui::Spacing();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
      int col = 0;
      for (size_t i = 0; i < m_state.environments.size(); ++i) {
        const EnvEntry& e = m_state.environments[i];
        ImGui::PushID(static_cast<int>(i));
        auto it = m_state.envThumbs.find(e.id);
        unsigned int tex = (it != m_state.envThumbs.end()) ? it->second : 0;
        if (tex) {
          if (ImGui::ImageButton("##env", (ImTextureID)(intptr_t)tex, ImVec2(88, 48))) {
            applyEnvironmentEntry(e);
          }
        } else {
          ImVec4 c(e.preview.r, e.preview.g, e.preview.b, 1.0f);
          if (ImGui::ColorButton("##env", c, ImGuiColorEditFlags_NoTooltip, ImVec2(88, 48))) {
            applyEnvironmentEntry(e);
          }
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          const std::string& payload = !e.path.empty() ? e.path : e.id;
          ImGui::SetDragDropPayload(kPayloadHdr, payload.c_str(), payload.size() + 1);
          ImGui::TextUnformatted(e.name.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.name.c_str());
        if (++col % 2 != 0) ImGui::SameLine();
        ImGui::PopID();
      }
      ImGui::PopStyleVar();
      if (m_state.environments.empty()) {
        ImGui::TextDisabled("assets/environments bos");
        if (ImGui::Button("HDR Ac...", ImVec2(-1, 0))) importHdrDialog();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Doku")) {
      ImGui::TextWrapped("PNG/JPG birakin; inspector slotuna surukleyin");
      ImGui::Spacing();
      if (ImGui::Button("Doku Ac...", ImVec2(-1, 0))) {
        std::string path;
        FileDialogFilter filters[] = {
            {"Dokular", "*.png;*.jpg;*.jpeg;*.tga;*.bmp"},
            {"Tum Dosyalar", "*.*"}};
        if (showFileDialog(path, FileDialogMode::Open, "Doku Ac", filters, 2)) {
          registerTexture(path);
          m_state.pendingTextureDrop = path;
          setStatus(std::string("Doku: ") + std::filesystem::path(path).filename().string());
        }
      }
      ImGui::Separator();
      if (m_state.textureLibrary.empty()) {
        ImGui::TextDisabled("Henuz doku yok (assets/textures veya surukle)");
      }
      for (size_t i = 0; i < m_state.textureLibrary.size(); ++i) {
        const std::string& path = m_state.textureLibrary[i];
        std::string label = std::filesystem::path(path).filename().string();
        ImGui::PushID(static_cast<int>(i));
        ImGui::Selectable(label.c_str());
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload(kPayloadTexture, path.c_str(), path.size() + 1);
          ImGui::TextUnformatted(label.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Studio")) {
      namespace fs = std::filesystem;
      fs::path studioDir = fs::path(m_state.assetsRoot) / "studios";
      if (fs::exists(studioDir)) {
        for (const auto& e : fs::directory_iterator(studioDir)) {
          if (e.path().extension() != ".json") continue;
          std::string stem = e.path().stem().string();
          std::string full = e.path().string();
          ImGui::PushID(stem.c_str());
          if (ImGui::Selectable(stem.c_str(), false, 0, ImVec2(0, 28))) applyStudioPreset(full);
          if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadStudio, full.c_str(), full.size() + 1);
            ImGui::TextUnformatted(stem.c_str());
            ImGui::EndDragDropSource();
          }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Studio: %s", stem.c_str());
          ImGui::PopID();
        }
      } else {
        ImGui::TextDisabled("Studios bulunamadi");
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Isik")) {
      ImGui::TextWrapped("Alan veya yonlu isik ekleyin / viewport'a surukleyin");
      ImGui::Spacing();
      if (ImGui::Button("Alan Isigi", ImVec2(-1, 32))) addAreaLight();
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const char* t = "area";
        ImGui::SetDragDropPayload(kPayloadLight, t, 5);
        ImGui::TextUnformatted("Alan Isigi");
        ImGui::EndDragDropSource();
      }
      if (ImGui::Button("Yonlu Isik", ImVec2(-1, 32))) addDirectionalLight();
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const char* t = "directional";
        ImGui::SetDragDropPayload(kPayloadLight, t, 12);
        ImGui::TextUnformatted("Yonlu Isik");
        ImGui::EndDragDropSource();
      }
      ImGui::Separator();
      ImGui::Text("Sahne isiklari: %d", static_cast<int>(m_state.lights.size()));
      if (ImGui::Button("Isiklari Temizle", ImVec2(-1, 0)) && !m_state.lights.empty()) {
        m_state.lights.clear();
        rebuildScene();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Ornekler")) {
      namespace fs = std::filesystem;
      fs::path models = fs::path(m_state.assetsRoot) / "models";
      ImGui::TextDisabled("Ornek modeller - cift tikla veya surukle");
      ImGui::Spacing();
      if (ImGui::Button("Ornek Sahne Yukle", ImVec2(-1, 36))) loadSampleScene();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stand + kure + Product Studio");
      ImGui::Separator();
      if (fs::exists(models)) {
        for (const auto& e : fs::directory_iterator(models)) {
          auto ext = e.path().extension().string();
          for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          if (ext != ".obj" && ext != ".gltf" && ext != ".glb") continue;
          std::string full = e.path().string();
          std::string stem = e.path().stem().string();
          ImGui::PushID(stem.c_str());
          if (ImGui::Selectable(stem.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 26))) {
            if (ImGui::IsMouseDoubleClicked(0)) importModel(full);
          }
          if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadModel, full.c_str(), full.size() + 1);
            ImGui::TextUnformatted(stem.c_str());
            ImGui::EndDragDropSource();
          }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.path().filename().string().c_str());
          ImGui::PopID();
        }
      } else {
        ImGui::TextDisabled("assets/models yok");
      }
      ImGui::Separator();
      ImGui::TextDisabled("FBX: desteklenmiyor (Assimp yok)");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Kamera")) {
      ImGui::TextWrapped("Preset secin veya viewport'a surukleyin");
      ImGui::Spacing();
      struct CamItem { const char* id; const char* label; };
      const CamItem cams[] = {
        {"product", "Urun (3/4)"},
        {"front", "On"},
        {"side", "Yan"},
        {"three_quarter", "Uc cevre"},
        {"top", "Ust"},
      };
      for (const auto& c : cams) {
        ImGui::PushID(c.id);
        if (ImGui::Selectable(c.label, false, 0, ImVec2(0, 26))) applyCameraPreset(c.id);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload(kPayloadCamera, c.id, std::strlen(c.id) + 1);
          ImGui::TextUnformatted(c.label);
          ImGui::EndDragDropSource();
        }
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void Application::drawSceneTreePanel() {
    ImGui::Begin("Sahne");
    SceneNode* pendingDelete = nullptr;
    std::function<void(SceneNode*)> drawNode = [&](SceneNode* n) {
        if (!n) return;
        if (n == m_state.graph.root()) {
            for (auto& c : n->children) drawNode(c.get());
            return;
        }
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (n->children.empty()) f |= ImGuiTreeNodeFlags_Leaf;
        if (m_state.selected == n) f |= ImGuiTreeNodeFlags_Selected;
        bool open = ImGui::TreeNodeEx((void*)n, f, "%s", n->name.c_str());
        if (ImGui::IsItemClicked()) m_state.selected = n;
        ImGui::SameLine();
        ImGui::PushID(n);
        if (ImGui::Checkbox("##vis", &n->visible)) rebuildScene();
        ImGui::PopID();
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Sil")) pendingDelete = n;
            if (ImGui::MenuItem("Gorunurluk")) { n->visible = !n->visible; rebuildScene(); }
            ImGui::EndPopup();
        }
        if (open) {
            for (auto& c : n->children) drawNode(c.get());
            ImGui::TreePop();
        }
    };
    drawNode(m_state.graph.root());
    if (pendingDelete) {
        if (m_state.selected == pendingDelete) m_state.selected = nullptr;
        m_state.graph.removeNode(pendingDelete);
        rebuildScene();
    }
    ImGui::End();
}

void Application::drawInspectorPanel() {
    ImGui::Begin("Inceleyici");

    if (ImGui::CollapsingHeader("Kamera", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Ortografik", &m_state.orbitCam.orthographic)) markDirty();
        if (ImGui::SliderFloat("Odak (mm)", &m_state.orbitCam.focalLengthMm, 12.0f, 200.0f)) {
            m_state.orbitCam.fov = fovDegreesFromFocalMm(m_state.orbitCam.focalLengthMm);
            markDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("36mm sensor yuksekligi");
        if (ImGui::SliderFloat("FOV", &m_state.orbitCam.fov, 10.0f, 120.0f)) {
            m_state.orbitCam.focalLengthMm = focalMmFromFovDegrees(m_state.orbitCam.fov);
            markDirty();
        }
        if (ImGui::SliderFloat("DoF Acikligi", &m_state.orbitCam.aperture, 0, 0.05f)) markDirty();
        if (ImGui::SliderFloat("Odak Mesafesi", &m_state.orbitCam.focusDistance, 0.1f, 50.0f)) {
            m_state.orbitCam.focusExplicit = true;
            markDirty();
        }
        if (ImGui::Checkbox("Turntable", &m_state.orbitCam.turntable)) {}
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Space: otomatik kamera orbit");
        if (m_state.orbitCam.turntable)
            ImGui::SliderFloat("Hiz", &m_state.orbitCam.turntableSpeed, 0.05f, 2.0f);
        if (ImGui::Button("Turntable PNG...")) exportTurntableSequence();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("PNG kare dizisi — GIF/MP4 harici (ffmpeg vb.)");
    }

    if (ImGui::CollapsingHeader("Ortam / Isik")) {
        ImGui::Text("Isik sayisi: %d", static_cast<int>(m_state.lights.size()));
        ImGui::Text(m_state.environment ? "HDR ortam: aktif" : "HDR ortam: yok");
        if (ImGui::Button("Alan +")) addAreaLight();
        ImGui::SameLine();
        if (ImGui::Button("Yonlu +")) addDirectionalLight();
        if (!m_state.lights.empty()) {
            if (auto area = std::dynamic_pointer_cast<AreaLight>(m_state.lights.front())) {
                float i = area->radiance().r;
                if (ImGui::SliderFloat("Ilk isik yogunlugu", &i, 0.1f, 40.0f)) {
                    area->setRadiance(Color3f(i));
                    markDirty();
                }
            }
        }
        if (m_state.advancedMode) {
            if (ImGui::SliderFloat("AO Gucu", &m_state.settings.aoStrength, 0, 1)) markDirty();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kisa mesafe contact AO (yaklasik)");
            if (ImGui::SliderInt("Golge Kalitesi", &m_state.settings.shadowQuality, 1, 4)) markDirty();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("NEE + AO ornek carpani");
            if (ImGui::SliderInt("GI Sekme", &m_state.settings.maxBounces, 1, 16)) markDirty();
        }
    }

    ImGui::Separator();
    SceneNode* sel = m_state.selected;
    if (!sel) {
        ImGui::TextDisabled("Parca secin veya modele tiklayin");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform beforeXf = sel->localTransform;
        const Mat4f& m = beforeXf.matrix();
        float t[3] = {m(0, 3), m(1, 3), m(2, 3)};
        static Transform undoFrom;
        if (ImGui::DragFloat3("Konum", t, 1.0f)) {
            sel->localTransform = withTranslation(beforeXf, Vec3f(t[0], t[1], t[2]));
            markDirty();
        }
        if (ImGui::IsItemActivated()) undoFrom = beforeXf;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Transform from = undoFrom;
            Transform to = sel->localTransform;
            SceneNode* node = sel;
            m_state.undo.push(
                [this, node, from]() {
                    if (!node) return;
                    node->localTransform = from;
                    rebuildScene();
                },
                [this, node, to]() {
                    if (!node) return;
                    node->localTransform = to;
                    rebuildScene();
                });
            rebuildScene();
        }
    }

    if (!sel->material) {
        ImGui::TextDisabled("Bu dugumde materyal yok");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Materyal", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto disney = std::dynamic_pointer_cast<DisneyMaterial>(sel->material);
        if (disney) {
            // Snapshot for undo on edit-end
            struct Snap {
                Color3f bc;
                float metallic, roughness, specular, clearCoat, clearCoatRoughness;
            };
            static Snap beforeSnap;
            auto takeSnap = [](const DisneyMaterial& d) {
                return Snap{d.baseColor(), d.metallic(), d.roughness(), d.specular(),
                            d.clearCoat(), d.clearCoatRoughness()};
            };
            auto applySnap = [](DisneyMaterial& d, const Snap& s) {
                d.setBaseColor(s.bc);
                d.setMetallic(s.metallic);
                d.setRoughness(s.roughness);
                d.setSpecular(s.specular);
                d.setClearCoat(s.clearCoat);
                d.setClearCoatRoughness(s.clearCoatRoughness);
            };
            auto pushSnapUndo = [&](const Snap& before, const Snap& after) {
                auto mat = disney;
                m_state.undo.push(
                    [this, mat, before, applySnap]() {
                        if (!mat) return;
                        applySnap(*mat, before);
                        rebuildScene();
                    },
                    [this, mat, after, applySnap]() {
                        if (!mat) return;
                        applySnap(*mat, after);
                        rebuildScene();
                    });
            };

            Color3f bc = disney->baseColor();
            float col[3] = {bc.r, bc.g, bc.b};
            Snap pre = takeSnap(*disney);
            if (ImGui::ColorEdit3("Renk", col)) {
                disney->setBaseColor(Color3f(col[0], col[1], col[2]));
                markDirty();
            }
            if (ImGui::IsItemActivated()) beforeSnap = pre;
            if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));

            float metallic = disney->metallic(), rough = disney->roughness(), spec = disney->specular();
            pre = takeSnap(*disney);
            if (ImGui::SliderFloat("Metalik", &metallic, 0, 1)) { disney->setMetallic(metallic); markDirty(); }
            if (ImGui::IsItemActivated()) beforeSnap = pre;
            if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));

            pre = takeSnap(*disney);
            if (ImGui::SliderFloat("Puruzluluk", &rough, 0.001f, 1)) { disney->setRoughness(rough); markDirty(); }
            if (ImGui::IsItemActivated()) beforeSnap = pre;
            if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));

            pre = takeSnap(*disney);
            if (ImGui::SliderFloat("Specular", &spec, 0, 1)) { disney->setSpecular(spec); markDirty(); }
            if (ImGui::IsItemActivated()) beforeSnap = pre;
            if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));

            if (m_state.advancedMode) {
                float cc = disney->clearCoat();
                float ccr = disney->clearCoatRoughness();
                pre = takeSnap(*disney);
                if (ImGui::SliderFloat("Clear Coat", &cc, 0, 1)) { disney->setClearCoat(cc); markDirty(); }
                if (ImGui::IsItemActivated()) beforeSnap = pre;
                if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));
                pre = takeSnap(*disney);
                if (ImGui::SliderFloat("Coat Roughness", &ccr, 0.001f, 1)) {
                    disney->setClearCoatRoughness(ccr);
                    markDirty();
                }
                if (ImGui::IsItemActivated()) beforeSnap = pre;
                if (ImGui::IsItemDeactivatedAfterEdit()) pushSnapUndo(beforeSnap, takeSnap(*disney));
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadMaterial)) {
                    applyMaterialPreset(sel, (const char*)payload->Data);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SeparatorText("Dokular");
            std::string albedo = disney->albedoMap();
            std::string normal = disney->normalMap();
            std::string roughMap = disney->roughnessMap();
            std::string metalMap = disney->metalnessMap();
            if (drawTextureSlot("Albedo", albedo)) disney->setAlbedoMap(albedo);
            if (drawTextureSlot("Normal", normal)) disney->setNormalMap(normal);
            if (drawTextureSlot("Roughness", roughMap)) disney->setRoughnessMap(roughMap);
            if (drawTextureSlot("Metalness", metalMap)) disney->setMetalnessMap(metalMap);
            // OS texture drop fallback → albedo when no slot hovered
            if (!m_state.pendingTextureDrop.empty()) {
                disney->setAlbedoMap(m_state.pendingTextureDrop);
                m_state.pendingTextureDrop.clear();
                markDirty();
            }
        } else {
            ImGui::TextDisabled("Disney olmayan materyal");
        }
    }
    ImGui::End();
}

bool Application::drawTextureSlot(const char* label, std::string& path) {
    ImGui::PushID(label);
    bool changed = false;
    std::string btn = path.empty()
        ? (std::string("+ ") + label)
        : std::filesystem::path(path).filename().string();
    ImGui::Button(btn.c_str(), ImVec2(-1, 0));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            path = (const char*)payload->Data;
            changed = true;
            markDirty();
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered() && !m_state.pendingTextureDrop.empty()) {
        path = m_state.pendingTextureDrop;
        m_state.pendingTextureDrop.clear();
        changed = true;
        markDirty();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nDosya birakin veya sag tikla temizle", path.empty() ? label : path.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !path.empty()) {
        path.clear();
        changed = true;
        markDirty();
    }
    ImGui::PopID();
    return changed;
}

void Application::drawRenderPanel() {
    if (!m_state.showRenderPanel) return;
    ImGui::Begin("Render");

    if (ImGui::CollapsingHeader("Onizleme", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Cozunurluk: %dx%d", m_state.settings.width, m_state.settings.height);
        if (ImGui::SliderInt("SPP", &m_state.previewSpp, 1, 2048)) {
            m_state.settings.samplesPerPixel = m_state.previewSpp;
            markDirty();
        }
        if (ImGui::SliderInt("Max Sekme", &m_state.settings.maxBounces, 1, 16)) markDirty();
        ImGui::Text("Is Parcacigi: %u (otomatik)", static_cast<unsigned>(std::thread::hardware_concurrency()));
        ImGui::ProgressBar(
            static_cast<float>(m_state.currentSpp.load()) / std::max(1, m_state.settings.samplesPerPixel),
            ImVec2(-1, 0), "Ilerleme");
    }

    if (ImGui::CollapsingHeader("Tonlama")) {
        int tmo = static_cast<int>(m_state.settings.tmo);
        if (ImGui::Combo("Operator", &tmo, "Reinhard\0Reinhard Extended\0ACES\0Filmic\0")) {
            m_state.settings.tmo = static_cast<ToneMapOperator>(tmo);
            markDirty();
        }
        if (ImGui::SliderFloat("Pozlama (EV)", &m_state.settings.exposure, -3.0f, 3.0f)) markDirty();
    }

    if (m_state.advancedMode && ImGui::CollapsingHeader("Gelismis")) {
        if (ImGui::SliderFloat("AO Gucu", &m_state.settings.aoStrength, 0, 1)) markDirty();
        if (ImGui::SliderInt("Golge Kalitesi", &m_state.settings.shadowQuality, 1, 4)) markDirty();
        if (ImGui::SliderInt("GI Sekme", &m_state.settings.maxBounces, 1, 16)) markDirty();
        ImGui::Separator();
        ImGui::Checkbox("Adaptive Sampling", &m_state.settings.adaptiveSampling);
        ImGui::Checkbox("OIDN Denoise", &m_state.settings.denoiseEnabled);
        if (m_state.settings.denoiseEnabled && !denoiseAvailable()) {
            ImGui::TextDisabled("OIDN yok — soft blur fallback (vcpkg: openimagedenoise)");
        }
    }

    if (ImGui::Button("Yeniden Baslat (F5)", ImVec2(-1, 0))) markDirty();
    if (ImGui::Button("Tam Cozunurluk...", ImVec2(-1, 0))) m_state.showFullRenderDialog = true;

    ImGui::End();
}

void Application::drawFullRenderDialog() {
    if (m_state.showFullRenderDialog) ImGui::OpenPopup("Tam Cozunurluk Render");
    if (!ImGui::BeginPopupModal("Tam Cozunurluk Render", &m_state.showFullRenderDialog, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::InputInt("Genislik", &m_state.fullRender.width);
    ImGui::InputInt("Yukseklik", &m_state.fullRender.height);
    ImGui::InputInt("SPP", &m_state.fullRender.spp);
    int threads = m_state.fullRender.numThreads;
    if (threads <= 0) threads = static_cast<int>(std::thread::hardware_concurrency());
    if (ImGui::InputInt("Thread", &threads)) {
        m_state.fullRender.numThreads = std::max(1, threads);
    }
    ImGui::Checkbox("Adaptive Sampling", &m_state.settings.adaptiveSampling);
    ImGui::Checkbox("OIDN Denoise", &m_state.settings.denoiseEnabled);
    char outPath[512] = {};
    std::strncpy(outPath, m_state.fullRender.outputPath.c_str(), sizeof(outPath) - 1);
    ImGui::InputText("Cikti Dosyasi", outPath, sizeof(outPath));
    if (ImGui::Button("Gozat...")) {
        std::string path = outPath[0] ? outPath : "render.png";
        FileDialogFilter filters[] = {{"PNG/EXR", "*.png;*.exr"}, {"Tum Dosyalar", "*.*"}};
        if (showFileDialog(path, FileDialogMode::Save, "Render Ciktisi", filters, 2)) {
            m_state.fullRender.outputPath = path;
            std::strncpy(outPath, path.c_str(), sizeof(outPath) - 1);
        }
    }

    if (m_state.fullRender.active) {
        float p = static_cast<float>(m_state.fullRender.currentSpp.load()) /
                  std::max(1, m_state.fullRender.targetSpp.load());
        ImGui::ProgressBar(p, ImVec2(-1, 0));
        ImGui::Text("SPP: %d / %d", m_state.fullRender.currentSpp.load(), m_state.fullRender.targetSpp.load());
    } else if (m_state.fullRender.done) {
        ImGui::TextWrapped("%s", m_state.fullRender.status.c_str());
    }

    if (!m_state.fullRender.active) {
        if (ImGui::Button("Baslat", ImVec2(120, 0))) {
            m_state.fullRender.outputPath = outPath;
            startFullRender();
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Baslat", ImVec2(120, 0));
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Kapat", ImVec2(120, 0))) {
        m_state.showFullRenderDialog = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Application::drawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImVec2 size = ImGui::GetContentRegionAvail();
    int w = std::max(1, static_cast<int>(size.x));
    int h = std::max(1, static_cast<int>(size.y));
    // Track display size only — progressive preview resolution is derived in the render thread.
    if (m_state.viewportW != w || m_state.viewportH != h) {
        m_state.viewportW = w;
        m_state.viewportH = h;
        markDirty();
    }

    const int spp = m_state.currentSpp.load();
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    {
        static int once = 0;
        if (once < 4) {
            ++once;
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            std::fprintf(stderr,
                         "vp skip=%d pos=%.0f,%.0f size=%.0f,%.0f clip=%.0f,%.0f-%.0f,%.0f content=%d,%d spp=%d\n",
                         win->SkipItems ? 1 : 0, win->Pos.x, win->Pos.y, win->Size.x, win->Size.y,
                         win->ClipRect.Min.x, win->ClipRect.Min.y, win->ClipRect.Max.x, win->ClipRect.Max.y,
                         w, h, spp);
            std::fflush(stderr);
        }
    }

    // Dual-layer: GPU instant preview fades out as CPU SPP accumulates (fast mode).
    float gpuAlpha = 0.0f;
    if (m_state.fastPreview && m_state.glPreview.ready()) {
        const float fadeSpp = 24.0f;
        gpuAlpha = std::clamp(1.0f - static_cast<float>(spp) / fadeSpp, 0.0f, 1.0f);
        if (gpuAlpha > 0.01f) {
            float pos[3];
            m_state.orbitCam.getPosition(pos);
            Vec3f eye(pos[0], pos[1], pos[2]);
            Vec3f target(m_state.orbitCam.target[0], m_state.orbitCam.target[1], m_state.orbitCam.target[2]);
            Mat4f view = Mat4f::lookAt(eye, target, Vec3f(0, 1, 0));
            float zNear = std::max(0.1f, m_state.orbitCam.radius * 0.01f);
            float zFar = std::max(zNear + 1.0f, m_state.orbitCam.radius * 40.0f);
            float orthoH = 2.0f * std::tan(m_state.orbitCam.fov * DEG_TO_RAD * 0.5f) *
                           std::max(m_state.orbitCam.radius, 0.01f);
            Mat4f proj = m_state.orbitCam.orthographic
                ? Mat4f::ortho(orthoH, aspect, zNear, zFar)
                : Mat4f::perspective(m_state.orbitCam.fov * DEG_TO_RAD, aspect, zNear, zFar);
            const Image* envImg = (m_state.environment && m_state.envMap.width() > 0)
                ? &m_state.envMap : nullptr;
            m_state.glPreview.render(
                m_state.graph, view, proj, eye, w, h, PreviewQuality::Fast,
                envImg, m_state.settings.exposure, previewLightDirection(m_state.lights));
        }
    }

    ImVec2 imageSize(static_cast<float>(w), static_cast<float>(h));
    ImVec2 uv0(0, 1), uv1(1, 0);

    // CPU progressive as base (always under GPU when blending)
    if (m_state.cpuTexture.textureId() && spp > 0) {
        ImGui::Image((ImTextureID)(intptr_t)m_state.cpuTexture.textureId(),
                     imageSize, uv0, uv1);
    } else {
        // Empty placeholder until first CPU sample (or when GPU-only)
        ImGui::Dummy(imageSize);
    }

    if (gpuAlpha > 0.01f && m_state.glPreview.colorTexture()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImGui::SetCursorScreenPos(rmin);
        ImGui::Image((ImTextureID)(intptr_t)m_state.glPreview.colorTexture(),
                     imageSize, uv0, uv1,
                     ImVec4(1, 1, 1, gpuAlpha), ImVec4(0, 0, 0, 0));
    }

    if (m_state.graph.empty()) {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Dim overlay
        dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(12, 12, 14, 210));
        // Center drop card
        float cardW = std::min(420.0f, sz.x * 0.72f);
        float cardH = 120.0f;
        ImVec2 c0(p.x + (sz.x - cardW) * 0.5f, p.y + (sz.y - cardH) * 0.5f);
        ImVec2 c1(c0.x + cardW, c0.y + cardH);
        dl->AddRectFilled(c0, c1, IM_COL32(28, 28, 32, 230), 10.0f);
        dl->AddRect(c0, c1, IM_COL32(90, 120, 150, 180), 10.0f, 0, 1.5f);
        const char* msg = "Modeli buraya birak";
        const char* sub = "OBJ / glTF  |  Dosya > Ornek Sahne";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImVec2 ss = ImGui::CalcTextSize(sub);
        dl->AddText(ImVec2(c0.x + (cardW - ts.x) * 0.5f, c0.y + 36.0f),
                    IM_COL32(230, 230, 235, 255), msg);
        dl->AddText(ImVec2(c0.x + (cardW - ss.x) * 0.5f, c0.y + 66.0f),
                    IM_COL32(140, 145, 155, 220), sub);
        dl->AddText(ImVec2(p.x + 16, p.y + sz.y - 28),
                    IM_COL32(120, 120, 130, 180),
                    "Sol: orbit  |  Orta/Sag: pan  |  Tekerlek: zoom");
    }

    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        bool camChanged = false;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_state.orbitCam.orbit(io.MouseDelta.x, io.MouseDelta.y);
            camChanged = true;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_state.orbitCam.pan(io.MouseDelta.x, io.MouseDelta.y);
            camChanged = true;
        }
        if (io.MouseWheel != 0) {
            m_state.orbitCam.zoom(io.MouseWheel);
            camChanged = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float u = (mp.x - rmin.x) / size.x;
            float v = (mp.y - rmin.y) / size.y;
            uint32_t id = m_state.picking.pick(m_state.graph, m_state.flatScene,
                *makeCamera(static_cast<float>(w) / h), w, h, u, v);
            m_state.selected = id ? m_state.graph.findByPickId(id) : nullptr;
        }
        if (camChanged) markDirty();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadMaterial)) {
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rsz = ImGui::GetItemRectSize();
            float u = rsz.x > 0 ? (mp.x - rmin.x) / rsz.x : 0.5f;
            float v = rsz.y > 0 ? (mp.y - rmin.y) / rsz.y : 0.5f;
            uint32_t id = m_state.picking.pick(m_state.graph, m_state.flatScene,
                *makeCamera(static_cast<float>(w) / std::max(h, 1)), w, h, u, v);
            SceneNode* target = id ? m_state.graph.findByPickId(id) : m_state.selected;
            applyMaterialPreset(target, (const char*)payload->Data);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadHdr)) {
            const char* data = (const char*)payload->Data;
            if (isHdrExtension(data)) {
                applyHdrEnvironment(data);
            } else {
                bool applied = false;
                for (const auto& e : m_state.environments) {
                    if (e.id == data || e.path == data) {
                        applyEnvironmentEntry(e);
                        applied = true;
                        break;
                    }
                }
                if (!applied) applyHdrEnvironment(data);
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadStudio)) {
            applyStudioPreset((const char*)payload->Data);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadLight)) {
            const char* t = (const char*)payload->Data;
            if (std::strcmp(t, "directional") == 0) addDirectionalLight();
            else addAreaLight();
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadModel)) {
            importModel((const char*)payload->Data);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadCamera)) {
            applyCameraPreset((const char*)payload->Data);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
    {
        static int once = 0;
        if (once < 3) {
            ++once;
            ImGuiWindow* win = ImGui::FindWindowByName("Viewport");
            std::fprintf(stderr, "after cmds=%d vtx=%d hidden=%d ch=%d\n",
                         win ? win->DrawList->CmdBuffer.Size : -1,
                         win ? win->DrawList->VtxBuffer.Size : -1,
                         win && win->Hidden ? 1 : 0,
                         win ? win->DrawList->_Splitter._Count : -1);
            std::fflush(stderr);
        }
    }
    ImGui::PopStyleVar();
}

void Application::drawStatusBar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
    ImGui::Begin("Durum", nullptr, flags);
    int spp = m_state.currentSpp.load();
    int target = m_state.settings.samplesPerPixel;
    float progress = static_cast<float>(spp) / static_cast<float>(std::max(1, target));

    ImGui::Text("SPP %d/%d", spp, target);
    ImGui::SameLine(110);
    ImGui::ProgressBar(progress, ImVec2(120, 14), "");
    ImGui::SameLine(250);
    ImGui::Text("%.0f fps", m_state.fps);
    ImGui::SameLine(330);
    ImGui::Text("%dx%d", m_state.settings.width, m_state.settings.height);
    ImGui::SameLine(430);
    if (m_state.fullRender.active) {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1), "Tam render %d/%d",
            m_state.fullRender.currentSpp.load(), m_state.fullRender.targetSpp.load());
    } else {
        ImGui::TextDisabled("%s", m_state.fastPreview ? "Hizli onizleme" : "Kalite onizleme");
    }
    if (!m_state.statusMessage.empty()) {
        ImGui::SameLine(620);
        ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.85f, 1), "%s", m_state.statusMessage.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Application::drawOnboarding() {
    if (!m_state.showOnboarding) return;
    ImGui::OpenPopup("Hosgeldiniz");
    bool open = m_state.showOnboarding;
    if (ImGui::BeginPopupModal("Hosgeldiniz", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextUnformatted("PhotonEngine");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextDisabled("Urun gorsellestirme — surukle birak ile basla");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::BulletText("Dosya > Ornek Sahne ile test ortamini ac");
        ImGui::BulletText("Modeli viewport'a surukle (OBJ / glTF)");
        ImGui::BulletText("Materyal kuresini mesh uzerine birak");
        ImGui::BulletText("HDR veya Studio preset'ini ortama birak");
        ImGui::Spacing();
        ImGui::TextDisabled("Kisayollar");
        ImGui::BulletText("Ctrl+S  proje kaydet (.photon)");
        ImGui::BulletText("Ctrl+Z / Ctrl+Y  geri al / yinele");
        ImGui::BulletText("F5  onizlemeyi sifirla");
        ImGui::BulletText("Space  turntable orbit");
        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::Button("Ornek Sahne", ImVec2(160, 36))) {
            loadSampleScene();
            dismissOnboarding();
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Basla", ImVec2(120, 36))) {
            dismissOnboarding();
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open && m_state.showOnboarding) dismissOnboarding();
}

int Application::run() {
    initWindow();
    initImGui();
    m_state.cpuTexture.init();
    m_state.glPreview.init();
    m_state.picking.init();
    loadAssets();
    startRenderThread();

    auto last = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        m_state.fps = 0.9f * m_state.fps + 0.1f * (dt > 0 ? 1.0f / dt : 0.0f);
        if (m_state.statusMessageT > 0.0f) {
            m_state.statusMessageT -= dt;
            if (m_state.statusMessageT <= 0.0f) m_state.statusMessage.clear();
        }
        if (m_state.orbitCam.turntable) {
            m_state.orbitCam.tick(dt);
            markDirty();
        }
        processPendingDrops();
        if (m_state.imageReady.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_state.imageMutex);
            const int spp = m_state.currentSpp.load();
            if (m_state.settings.denoiseEnabled && spp > 0 &&
                spp >= m_state.settings.samplesPerPixel) {
                Image shown = denoiseCopy(m_state.accumImage);
                m_state.cpuTexture.upload(shown, m_state.settings.tmo, m_state.settings.exposure);
            } else {
                m_state.cpuTexture.upload(m_state.accumImage, m_state.settings.tmo, m_state.settings.exposure);
            }
        }
        handleShortcuts();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        setupDocking();
        drawLibraryPanel();
        drawViewportPanel();
        drawSceneTreePanel();
        drawInspectorPanel();
        drawRenderPanel();
        drawStatusBar();
        drawOnboarding();
        drawFullRenderDialog();

        {
            static int once = 0;
            if (once < 2) {
                ++once;
                ImGuiWindow* win = ImGui::FindWindowByName("Viewport");
                std::fprintf(stderr, "render cmds=%d ch=%d hidden=%d\n",
                             win ? win->DrawList->CmdBuffer.Size : -1,
                             win ? win->DrawList->_Splitter._Count : -1,
                             win && win->Hidden ? 1 : 0);
                std::fflush(stderr);
            }
        }

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(m_window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.078f, 0.078f, 0.082f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_window);
    }
    return 0;
}

} // namespace photon
