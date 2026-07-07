#pragma once

#include "scene/scene_graph.h"
#include "scene/material_library.h"
#include "scene/project_io.h"
#include "ui/orbit_camera.h"
#include "preview/viewport_texture.h"
#include "preview/gl_preview.h"
#include "engine/renderer.h"
#include "engine/render_settings.h"
#include "core/image/image.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include "camera/camera.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;

namespace photon {

struct FullRenderState {
    std::atomic<bool> active{false};
    std::atomic<bool> done{false};
    std::atomic<int> currentSpp{0};
    std::atomic<int> targetSpp{0};
    int width = 1920;
    int height = 1080;
    int spp = 512;
    std::string outputPath;
    std::string status;
    std::thread thread;
};

struct AppState {
    SceneGraph graph;
    Scene flatScene;
    std::vector<std::shared_ptr<Light>> lights;
    std::shared_ptr<EnvironmentLight> environment;
    Image envMap;

    MaterialLibrary materialLib;
    OrbitCamera orbitCam;
    RenderSettings settings;
    Renderer renderer;

    ViewportTexture cpuTexture;
    ViewportTexture gpuTexture;
    GLPreview glPreview;
    PickingPass picking;
    UndoStack undo;

    Image accumImage;
    std::atomic<int> currentSpp{0};
    std::atomic<bool> renderDirty{true};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> imageReady{false};
    std::mutex imageMutex;
    std::thread renderThread;

    FullRenderState fullRender;
    bool showFullRenderDialog = false;
    bool showRenderPanel = true;

    SceneNode* selected = nullptr;
    bool advancedMode = false;
    bool fastPreview = true;
    bool showOnboarding = true;
    float fps = 0.0f;
    int viewportW = 512;
    int viewportH = 512;
    int previewSpp = 128;

    std::string assetsRoot;
    std::string projectPath = "scene.photon";
    std::string exportPath = "export.png";
    std::string pendingModelDrop;
    std::string pendingHdrDrop;
};

class Application {
public:
    Application();
    ~Application();
    int run();

    void queueModelDrop(const std::string& path);
    void queueHdrDrop(const std::string& path);

private:
    GLFWwindow* m_window = nullptr;
    AppState m_state;

    void initWindow();
    void initImGui();
    void setupDocking();
    void loadAssets();
    void rebuildScene();
    void startRenderThread();
    void markDirty();
    void applyQualityPreset(int spp);
    void applyStudioPreset(const std::string& path);
    void startFullRender();
    void exportImage(bool exr);
    void importModelDialog();
    void importHdrDialog();
    std::unique_ptr<Camera> makeCamera(float aspect) const;
    void importModel(const std::string& path);
    void applyHdrEnvironment(const std::string& path);
    void handleShortcuts();
    void drawMenubar();
    void drawLibraryPanel();
    void drawViewportPanel();
    void drawSceneTreePanel();
    void drawInspectorPanel();
    void drawRenderPanel();
    void drawStatusBar();
    void drawOnboarding();
    void drawFullRenderDialog();
    void processPendingDrops();
};

} // namespace photon
