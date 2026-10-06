// application.h — Masaüstü uygulamasının ana sınıfı.
//
// Uygulama tek bir sınıf, ama kodu sorumluluklarına göre birkaç dosyaya bölünmüş:
//   application.cpp   pencere, ImGui, ana döngü, kısayollar, ekran görüntüsü
//   scene_ops.cpp     belge işlemleri: içe aktarma, malzeme/ortam/stüdyo uygulama,
//                     ışıklar, seçim, geri al, sahne derleme, proje kaydet/aç
//   final_render.cpp  son render ve turntable (arka plan iş parçacığı)
//   ui_*.cpp          paneller: menü, kütüphane, viewport, sahne, özellikler, render
// Hepsi aynı AppState'i paylaşır; paneller yalnız UI thread'inde çalışır.
#pragma once

#include "app/app_state.h"

#include <imgui.h>

#include <functional>
#include <future>
#include <string>

struct GLFWwindow;

namespace photon {

struct LaunchOptions {
    std::string screenshotPath;  ///< Doluysa: kareleri çiz, ekran görüntüsü al, çık
    int screenshotSpp = 16;      ///< Görüntü almadan önce beklenen viewport örnek sayısı
    float screenshotTimeout = 60.0f;
    int windowW = 1600;
    int windowH = 940;
    std::string openPath;        ///< Açılışta yüklenecek proje ya da model
    std::string scene = "sample";///< Boş belge yerine: "sample" | "cornell" | "empty"
    std::string uiState;         ///< Ekran görüntüsü için: "render", "material", ...
    std::string saveProjectPath; ///< Açılış sahnesini bu .photon dosyasına kaydet (test/ölçüm)
};

class Application {
public:
    explicit Application(LaunchOptions options = {});
    ~Application();
    int run();

    void onDrop(const std::vector<std::string>& paths);

private:
    GLFWwindow* m_window = nullptr;
    AppState m_s;
    LaunchOptions m_opts;
    std::vector<std::string> m_pendingDrops;
    unsigned int m_viewTex = 0;
    int m_viewTexW = 0;
    int m_viewTexH = 0;
    bool m_viewHasAlpha = false;
    unsigned int m_checkerTex = 0;
    unsigned int m_logoTex = 0;
    bool m_layoutBuilt = false;

    // ── application.cpp ──
    void initWindow();
    void initImGui();
    void loadAssets();
    void frame(float dt);
    void handleShortcuts();
    void processDrops();
    void uploadViewportFrame();
    bool takeScreenshot(const std::string& path);
    void setStatus(const std::string& msg, bool error = false);

    /// Arka plan işi: @p work başka bir thread'de çalışır ve UI thread'inde çalıştırılacak
    /// bir "tamamlama" fonksiyonu döndürür (belge yalnız UI thread'inde değişir).
    void runAsync(std::string label, std::function<std::function<void()>()> work);
    void pollAsync();
    struct AsyncJob {
        std::string label;
        std::future<std::function<void()>> result;
    };
    std::vector<AsyncJob> m_jobs;
    void scanEnvironments();
    void scanFolderAssets();

    // ── scene_ops.cpp ──
public:
    // Paneller (ui_*.cpp) bunları çağırır.
    void rebuildScene(bool interactive = false);
    void compileIfDirty();
    /// Belgeden render sahnesi. isolate: malzemeler kopyalanır (son render için).
    std::shared_ptr<Scene> buildScene(bool isolate);
    void pushUndo();
    void undo();
    void redo();
    void markDocumentChanged(bool interactive = false);
    SceneNode* selectedNode();
    void selectNode(uint64_t uid);
    void selectLight(int index);
    void clearSelection();
    void newScene();
    void loadSampleScene();
    void loadCornellScene();
    bool importModel(const std::string& path, bool undoable = true);
    void attachImported(const std::string& path, std::unique_ptr<SceneNode> node, bool undoable);
    void applyMaterialPreset(uint64_t nodeUid, const MaterialPreset& preset);
    void applyEnvironment(const EnvAsset& env);
    void applyEnvironmentNow(const EnvAsset& env);
    void applyStudio(const StudioPreset& studio);
    void addLight(LightDesc::Type type);
    void deleteSelection();
    void duplicateSelection();
    void frameSelection();
    void frameAll();
    void placeSelectionOnGround();
    void applyCameraPreset(const char* id);
    CameraParams cameraParams() const;
    uint64_t pickAt(float u, float v, Vec3f* hitPoint = nullptr);
    void saveProjectAs();
    void saveProject();
    void openProjectDialog();
    bool openProject(const std::string& path);
    void openModelDialog();
    void openHdrDialog();
    void exportViewport();
    void saveMaterialToLibrary(const std::string& name);
    void copyMaterial();
    void pasteMaterial();
    template <typename F>
    void editLive(F&& fn) {
        m_s.viewport.edit(std::forward<F>(fn), true);
        m_s.documentDirty = true;
    }

    // ── final_render.cpp ──
    void startFinalRender();
    void startTurntable();
    void cancelFinalRender();
    void joinFinalRender();

private:
    // ── ui_*.cpp ──
    void drawDockspace();
    void buildDefaultLayout(unsigned int dockId);
    void drawMenuBar();
    void drawStatusBar();
    void drawLibrary();
    void drawLibraryMaterials();
    void drawLibraryEnvironments();
    void drawLibraryStudios();
    void drawLibraryModels();
    void drawLibraryTextures();
    void drawViewport();
    void drawViewportOverlay(const ImVec2& origin, const ImVec2& size);
    void drawSelectionOutline(const ImVec2& origin, const ImVec2& size);
    void drawGizmo(const ImVec2& origin, const ImVec2& size);
    void drawWelcome(const ImVec2& origin, const ImVec2& size);
    void drawSceneTree();
    void drawProperties();
    void drawMaterialProps();
    void drawEnvironmentProps();
    void drawLightProps();
    void drawCameraProps();
    void drawImageProps();
    void drawRenderDialog();
    void drawAboutWindows();
    bool textureSlot(const char* label, std::string& path);
};

} // namespace photon
