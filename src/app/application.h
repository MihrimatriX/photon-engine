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
#include <unordered_map>
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
    int spp = 256;
    int numThreads = 0; // 0 = hardware_concurrency
    std::string outputPath;
    std::string status;
    std::thread thread;
};

struct EnvEntry {
    std::string id;
    std::string name;
    std::string path;       ///< HDR path (empty = procedural sky)
    Color3f preview{0.5f, 0.55f, 0.7f};
    Color3f zenith{0.45f, 0.55f, 0.85f};
    Color3f horizon{0.75f, 0.7f, 0.65f};
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
    std::string activeEnvPath;
    std::string pendingModelDrop;
    std::string pendingHdrDrop;
    std::string pendingTextureDrop;
    std::string statusMessage;
    float statusMessageT = 0.0f;
    std::unordered_map<std::string, unsigned int> materialThumbs;
    std::vector<EnvEntry> environments;
    std::unordered_map<std::string, unsigned int> envThumbs;
    std::vector<std::string> textureLibrary; ///< Dokular tab + recent drops
    int turntableExportFrames = 36;
    int turntableExportSpp = 8;
};

class Application {
public:
    Application();
    ~Application();
    int run();

    void queueModelDrop(const std::string& path);
    void queueHdrDrop(const std::string& path);
    void queueTextureDrop(const std::string& path);

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
    SceneNode* importModelInternal(const std::string& path); ///< no undo push
    void applyProjectFile(const ProjectFile& proj);
    std::string serializeCameraJson() const;
    void applyCameraJson(const std::string& json);
    void pushMaterialUndo(SceneNode* node, std::shared_ptr<Material> before, std::shared_ptr<Material> after);
    void dismissOnboarding();
    void applyHdrEnvironment(const std::string& path);
    void applyEnvironmentEntry(const EnvEntry& e);
    void addAreaLight();
    void addDirectionalLight();
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
    void scanEnvironments();
    void buildMaterialThumbnails();
    void destroyMaterialThumbnails();
    void buildEnvThumbnails();
    void destroyEnvThumbnails();
    void applyMaterialPreset(SceneNode* node, const std::string& presetId);
    bool drawTextureSlot(const char* label, std::string& path);
    void loadSampleScene();
    void loadCornellScene();
    void frameProductCamera();
    bool sampleModelsAvailable() const;
    void setStatus(const std::string& msg);
    void registerTexture(const std::string& path);
    void scanTextures();
    void applyCameraPreset(const std::string& id);
    void exportTurntableSequence();
};

} // namespace photon
