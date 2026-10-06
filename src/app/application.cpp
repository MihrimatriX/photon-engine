// application.cpp — Pencere/ImGui kurulumu, ana döngü, kısayollar, sürükle-bırak,
// kütüphane taraması ve otomatik ekran görüntüsü modu.
#include "app/application.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "ui/file_dialog.h"
#include "ui/widgets.h"
#include "scene/model_import.h"
#include "core/image/image_io.h"
#include "core/platform/path.h"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F // Windows gl.h yalnız OpenGL 1.1 tanımlarını içerir
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace photon {

namespace fs = std::filesystem;

namespace {

Application* gApp = nullptr;

void dropCallback(GLFWwindow*, int count, const char** paths) {
    if (!gApp || count <= 0) return;
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.emplace_back(paths[i]); // GLFW yolları UTF-8 verir
    gApp->onDrop(v);
}

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#endif
    return fs::current_path();
}

// assets/ klasörünü exe'nin yanında, sonra çalışma dizininden yukarı doğru arar
// (derleme klasöründen ya da repo kökünden çalıştırmayı destekler).
std::string findAssetsRoot() {
    for (fs::path start : {executableDir(), fs::current_path()}) {
        fs::path cur = start;
        for (int i = 0; i < 6; ++i) {
            if (fs::exists(cur / "assets" / "materials")) return pathToUtf8(cur / "assets");
            if (!cur.has_parent_path() || cur.parent_path() == cur) break;
            cur = cur.parent_path();
        }
    }
    return "assets";
}

// Kullanıcı verisi: %APPDATA%/PhotonEngine (Windows) ya da ~/.photon-engine.
std::string findUserDir() {
    fs::path base;
#ifdef _WIN32
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) base = fs::path(appdata) / "PhotonEngine";
#else
    if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".photon-engine";
#endif
    if (base.empty()) base = fs::current_path() / "photon-user";
    std::error_code ec;
    fs::create_directories(base, ec);
    return pathToUtf8(base);
}

unsigned int makeCheckerTexture() {
    constexpr int N = 16;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const uint8_t v = ((x / 8 + y / 8) % 2) ? 58 : 44;
            const size_t i = static_cast<size_t>(y * N + x) * 4;
            px[i] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

bool hasExt(const fs::path& p, std::initializer_list<const char*> exts) {
    std::string e = pathToUtf8(p.extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}

} // namespace

Application::Application(LaunchOptions options) : m_opts(std::move(options)) {
    gApp = this;
    m_s.assetsRoot = findAssetsRoot();
    m_s.userDir = findUserDir();
}

Application::~Application() {
    cancelFinalRender();
    joinFinalRender();
    m_s.viewport.stop();
    m_s.thumbs.stop();
    if (m_window) {
        if (m_viewTex) glDeleteTextures(1, &m_viewTex);
        if (m_checkerTex) glDeleteTextures(1, &m_checkerTex);
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
    if (!glfwInit()) throw std::runtime_error("GLFW başlatılamadı");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    if (!m_opts.screenshotPath.empty()) glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    m_window = glfwCreateWindow(m_opts.windowW, m_opts.windowH, "PhotonEngine", nullptr, nullptr);
    if (!m_window) throw std::runtime_error("OpenGL 3.3 penceresi açılamadı");
    if (m_opts.screenshotPath.empty()) glfwMaximizeWindow(m_window);
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    glfwSetDropCallback(m_window, dropCallback);
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(m_window, &sx, &sy);
    m_s.dpiScale = std::max(1.0f, sx);
}

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDockingWithShift = false;
    // Panel yerleşimi kullanıcı klasöründe saklanır (çalışma dizinine dosya yazılmaz).
    static std::string iniPath;
    iniPath = pathToUtf8(pathFromUtf8(m_s.userDir) / "layout_v3.ini");
    io.IniFilename = m_opts.screenshotPath.empty() ? iniPath.c_str() : nullptr;
    ui::loadFonts(m_s.assetsRoot);
    ui::applyTheme(m_s.dpiScale);
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    m_checkerTex = makeCheckerTexture();
}

void Application::setStatus(const std::string& msg, bool error) {
    m_s.statusText = msg;
    m_s.statusTimer = error ? 8.0f : 4.0f;
    m_s.statusError = error;
}

void Application::scanEnvironments() {
    m_s.environments.clear();
    const fs::path dir = pathFromUtf8(m_s.assetsRoot) / "environments";
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    // Aynı HDRI'ın 1k ve 2k sürümleri varsa yalnız en büyüğü listelenir.
    for (const auto& p : files) {
        if (!hasExt(p, {".hdr", ".exr"})) continue;
        std::string stem = pathToUtf8(p.stem());
        std::string base = stem;
        for (const char* suf : {"_1k", "_2k", "_4k"})
            if (base.size() > 3 && base.compare(base.size() - 3, 3, suf) == 0) base = base.substr(0, base.size() - 3);
        auto it = std::find_if(m_s.environments.begin(), m_s.environments.end(),
                               [&](const EnvAsset& a) { return a.id == base; });
        if (it != m_s.environments.end()) {
            if (stem > pathToUtf8(pathFromUtf8(it->path).stem())) it->path = pathToUtf8(p);
            continue;
        }
        EnvAsset a;
        a.id = base;
        std::string name = base;
        std::replace(name.begin(), name.end(), '_', ' ');
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        a.name = name;
        a.path = pathToUtf8(p);
        m_s.environments.push_back(a);
    }
    // Prosedürel gökyüzleri (dosya gerektirmez).
    m_s.environments.push_back({"proc_studio", "Stüdyo Gri (prosedürel)", "", Color3f(0.75f, 0.78f, 0.85f), Color3f(0.55f, 0.55f, 0.57f)});
    m_s.environments.push_back({"proc_warm", "Sıcak Gün Batımı (prosedürel)", "", Color3f(0.35f, 0.42f, 0.65f), Color3f(1.0f, 0.62f, 0.38f)});
    m_s.environments.push_back({"proc_dark", "Karanlık Stüdyo (prosedürel)", "", Color3f(0.05f, 0.05f, 0.06f), Color3f(0.12f, 0.12f, 0.13f)});
}

void Application::scanFolderAssets() {
    m_s.models.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(pathFromUtf8(m_s.assetsRoot) / "models", ec))
        if (isSupportedModelFile(pathToUtf8(e.path()))) m_s.models.push_back(pathToUtf8(e.path()));
    std::sort(m_s.models.begin(), m_s.models.end());
    for (const auto& e : fs::directory_iterator(pathFromUtf8(m_s.assetsRoot) / "textures", ec))
        if (hasExt(e.path(), {".png", ".jpg", ".jpeg", ".tga", ".bmp"}))
            m_s.textures.push_back(pathToUtf8(e.path()));
}

void Application::loadAssets() {
    m_s.materials.loadFromDirectory(pathToUtf8(pathFromUtf8(m_s.assetsRoot) / "materials"));
    // Kullanıcının kaydettiği malzemeler ("Benim" kategorisi).
    MaterialLibrary user;
    if (user.loadFromDirectory(pathToUtf8(pathFromUtf8(m_s.userDir) / "materials"))) {
        for (const auto& p : user.presets()) {
            MaterialPreset copy = p;
            copy.category = "Benim";
            m_s.materials.add(copy);
        }
    }
    m_s.studios = loadStudioPresets(pathToUtf8(pathFromUtf8(m_s.assetsRoot) / "studios"));
    scanEnvironments();
    scanFolderAssets();

    std::string studioHdr;
    for (const auto& e : m_s.environments)
        if (e.id == "studio_small_09") studioHdr = e.path;
    m_s.thumbs.start(pathToUtf8(pathFromUtf8(m_s.userDir) / "thumbs"), studioHdr, &m_s.envCache);
    for (const auto& e : m_s.environments) m_s.thumbs.requestEnvironment("env:" + e.id, e.path, e.zenith, e.horizon);
    for (const auto& p : m_s.materials.presets()) m_s.thumbs.requestMaterial("mat:" + p.id, p);
}

void Application::onDrop(const std::vector<std::string>& paths) {
    for (const auto& p : paths) m_pendingDrops.push_back(p);
}

// Dışarıdan bırakılan dosyalar türüne göre yönlendirilir: model → içe aktar,
// HDR/EXR → ortam, .photon → proje, resim → seçili malzemenin taban rengi dokusu.
void Application::processDrops() {
    if (m_pendingDrops.empty()) return;
    std::vector<std::string> drops;
    drops.swap(m_pendingDrops);
    for (const auto& p : drops) {
        const fs::path fp = pathFromUtf8(p);
        if (isSupportedModelFile(p)) {
            importModel(p);
        } else if (hasExt(fp, {".hdr", ".exr"})) {
            EnvAsset a;
            a.id = pathToUtf8(fp.stem());
            a.name = a.id;
            a.path = p;
            if (std::none_of(m_s.environments.begin(), m_s.environments.end(), [&](const EnvAsset& e) { return e.path == p; })) {
                m_s.environments.insert(m_s.environments.begin(), a);
                m_s.thumbs.requestEnvironment("env:" + a.id, a.path, a.zenith, a.horizon);
            }
            applyEnvironment(a);
        } else if (hasExt(fp, {".photon"})) {
            openProject(p);
        } else if (hasExt(fp, {".png", ".jpg", ".jpeg", ".tga", ".bmp"})) {
            if (std::find(m_s.textures.begin(), m_s.textures.end(), p) == m_s.textures.end()) m_s.textures.push_back(p);
            SceneNode* n = selectedNode();
            auto d = n ? std::dynamic_pointer_cast<DisneyMaterial>(n->material) : nullptr;
            if (d) {
                pushUndo();
                editLive([&] { d->setAlbedoMap(p); });
                setStatus("Doku uygulandı: " + ui::fileName(p));
            } else {
                setStatus("Doku kütüphaneye eklendi. Bir parçaya uygulamak için önce parçayı seçin.");
            }
        } else if (fs::is_directory(fp)) {
            setStatus("Klasör bırakılamaz; dosyaları seçip bırakın", true);
        } else {
            setStatus("Desteklenmeyen dosya: " + ui::fileName(p), true);
        }
    }
}

void Application::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    if (ctrl && pressed(ImGuiKey_Z)) { shift ? redo() : undo(); }
    if (ctrl && pressed(ImGuiKey_Y)) redo();
    if (ctrl && pressed(ImGuiKey_S)) { shift ? saveProjectAs() : saveProject(); }
    if (ctrl && pressed(ImGuiKey_O)) { shift ? openProjectDialog() : openModelDialog(); }
    if (ctrl && pressed(ImGuiKey_N)) newScene();
    if (ctrl && pressed(ImGuiKey_P)) m_s.showRenderDialog = true;
    if (ctrl && pressed(ImGuiKey_E)) exportViewport();
    if (ctrl && pressed(ImGuiKey_D)) duplicateSelection();
    if (ctrl && pressed(ImGuiKey_C)) copyMaterial();
    if (ctrl && pressed(ImGuiKey_V)) pasteMaterial();
    if (ctrl) return;
    // Tek tuşlu kısayollar yalnız viewport ya da sahne paneli üzerindeyken değil,
    // metin girişi yokken her yerde çalışır (KeyShot gibi).
    if (pressed(ImGuiKey_F)) frameSelection();
    if (pressed(ImGuiKey_A) && shift) frameAll();
    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) deleteSelection();
    if (pressed(ImGuiKey_Escape)) clearSelection();
    if (pressed(ImGuiKey_Q)) m_s.gizmoOp = 0;
    if (pressed(ImGuiKey_W)) m_s.gizmoOp = 1;
    if (pressed(ImGuiKey_E)) m_s.gizmoOp = 2;
    if (pressed(ImGuiKey_R)) m_s.gizmoOp = 3;
    if (pressed(ImGuiKey_T)) m_s.camera.turntable = !m_s.camera.turntable;
    if (pressed(ImGuiKey_G)) placeSelectionOnGround();
    if (pressed(ImGuiKey_F1)) m_s.showShortcuts = !m_s.showShortcuts;
    if (pressed(ImGuiKey_F5)) m_s.viewport.restart(false);
    if (pressed(ImGuiKey_1)) applyCameraPreset("front");
    if (pressed(ImGuiKey_2)) applyCameraPreset("side");
    if (pressed(ImGuiKey_3)) applyCameraPreset("top");
    if (pressed(ImGuiKey_4)) applyCameraPreset("three_quarter");
}

void Application::uploadViewportFrame() {
    DisplayFrame f;
    if (!m_s.viewport.fetchFrame(f) || f.width <= 0) return;
    if (!m_viewTex) {
        glGenTextures(1, &m_viewTex);
        glBindTexture(GL_TEXTURE_2D, m_viewTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, m_viewTex);
    // RGBA (4 bayt/piksel) satırları her zaman 4'e hizalıdır: GL_UNPACK_ALIGNMENT sorunu yok.
    if (f.width != m_viewTexW || f.height != m_viewTexH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f.width, f.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, f.rgba.data());
        m_viewTexW = f.width;
        m_viewTexH = f.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, GL_RGBA, GL_UNSIGNED_BYTE, f.rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_viewHasAlpha = f.hasAlpha;
}

bool Application::takeScreenshot(const std::string& path) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return saveRGBA8PNG(px.data(), w, h, path, true);
}

void Application::frame(float dt) {
    m_s.fps = m_s.fps * 0.92f + 0.08f * (dt > 0.0f ? 1.0f / dt : 0.0f);
    if (m_s.statusTimer > 0.0f) {
        m_s.statusTimer -= dt;
        if (m_s.statusTimer <= 0.0f) m_s.statusText.clear();
    }
    if (m_s.camera.turntable) m_s.camera.tick(dt);

    m_s.thumbs.uploadReady();
    processDrops();
    pollAsync();
    compileIfDirty();

    // Kamera ve ekran ayarlarını render thread'ine ilet (değişmediyse etkisiz).
    m_s.viewport.setCamera(cameraParams(), true);
    m_s.viewport.setDisplay(m_s.settings.tmo, m_s.settings.exposure);
    m_s.viewport.setSettings(m_s.settings);
    m_s.viewport.setDenoise(m_s.denoise);
    m_s.viewport.setTargetSpp(m_s.previewSpp);
    m_s.viewport.setTransparentPreview(m_s.environment.background.mode == Background::Mode::Transparent);
    m_s.viewport.setPaused(m_s.finalJob.active.load());
    uploadViewportFrame();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    handleShortcuts();
    drawDockspace();
    drawLibrary();
    drawViewport();
    drawSceneTree();
    drawProperties();
    drawRenderDialog();
    drawAboutWindows();
    ImGui::Render();

    int dw = 0, dh = 0;
    glfwGetFramebufferSize(m_window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    const ImVec4 bg = ui::palette().bg0;
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int Application::run() {
    initWindow();
    initImGui();
    loadAssets();
    m_s.viewport.start();

    if (!m_opts.openPath.empty()) {
        if (isSupportedModelFile(m_opts.openPath)) importModel(m_opts.openPath, false);
        else openProject(m_opts.openPath);
    } else if (m_opts.scene == "cornell") {
        loadCornellScene();
    } else if (m_opts.scene == "empty") {
        newScene();
    } else {
        loadSampleScene();
    }
    // Ekran görüntüsü / tanıtım için hazır arayüz durumları (--ui ...).
    const std::string& ui = m_opts.uiState;
    auto selectFirst = [&] {
        for (auto& c : m_s.graph.root()->children)
            if (c->name == "Krom Küre" || c.get() == m_s.graph.root()->children.back().get()) {
                selectNode(c->uid);
                break;
            }
    };
    if (ui == "render") m_s.showRenderDialog = true;
    else if (ui == "shortcuts") m_s.showShortcuts = true;
    else if (ui == "material") { selectFirst(); m_s.rightTab = 1; }
    else if (ui == "object") { selectFirst(); m_s.rightTab = 0; m_s.gizmoOp = 1; }
    else if (ui == "env") { m_s.rightTab = 2; m_s.leftTab = 1; }
    else if (ui == "light") { selectLight(0); m_s.leftTab = 2; }
    else if (ui == "camera") { m_s.rightTab = 4; m_s.leftTab = 3; }
    else if (ui == "image") m_s.rightTab = 5;
    if (!m_opts.saveProjectPath.empty()) {
        m_s.projectPath = m_opts.saveProjectPath;
        saveProject();
    }

    auto last = std::chrono::steady_clock::now();
    const auto started = last;
    int frames = 0;
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        // Pencere küçültülmüşse CPU harcama.
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        frame(dt);
        ++frames;

        if (!m_opts.screenshotPath.empty()) {
            const float elapsed = std::chrono::duration<float>(now - started).count();
            const ViewportStats st = m_s.viewport.stats();
            const bool ready = frames > 30 && !st.interactive && st.spp >= m_opts.screenshotSpp && m_jobs.empty() &&
                               !m_s.thumbs.busy();
            if (ready || elapsed > m_opts.screenshotTimeout) {
                takeScreenshot(m_opts.screenshotPath);
                glfwSwapBuffers(m_window);
                break;
            }
        }
        glfwSwapBuffers(m_window);
        std::string title = "PhotonEngine";
        if (!m_s.projectPath.empty()) title += "  —  " + ui::fileName(m_s.projectPath);
        if (m_s.documentDirty) title += " •";
        static std::string lastTitle;
        if (title != lastTitle) {
            glfwSetWindowTitle(m_window, title.c_str());
            lastTitle = title;
        }
    }
    return 0;
}

} // namespace photon

namespace photon {

void Application::runAsync(std::string label, std::function<std::function<void()>()> work) {
    AsyncJob job;
    job.label = std::move(label);
    job.result = std::async(std::launch::async, [work = std::move(work)]() -> std::function<void()> {
        try {
            return work();
        } catch (const std::exception& e) {
            std::string msg = e.what();
            return [msg] { std::fprintf(stderr, "Arka plan işi hatası: %s\n", msg.c_str()); };
        }
    });
    m_jobs.push_back(std::move(job));
}

// Biten işlerin tamamlama fonksiyonlarını UI thread'inde, başlatılma sırasıyla çalıştır.
void Application::pollAsync() {
    while (!m_jobs.empty() &&
           m_jobs.front().result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::function<void()> done = m_jobs.front().result.get();
        m_jobs.erase(m_jobs.begin());
        if (done) done();
    }
}

} // namespace photon
