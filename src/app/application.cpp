#include "app/application.h"
#include "scene/cornell_box.h"
#include "io/obj_loader.h"
#include "io/gltf_loader.h"
#include "ui/drag_drop.h"
#include "ui/theme.h"
#include "ui/file_dialog.h"
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "lights/area_light.h"
#include "materials/disney.h"
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
#include <stdexcept>

namespace photon {
namespace {

Application* gApp = nullptr;

void dropCallback(GLFWwindow*, int count, const char** paths) {
    if (!gApp || count <= 0) return;
    std::string p = paths[0];
    std::string ext = std::filesystem::path(p).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".hdr" || ext == ".exr") gApp->queueHdrDrop(p);
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

} // namespace

Application::Application() {
    gApp = this;
    m_state.assetsRoot = findAssetsRoot();
    m_state.settings.width = 512;
    m_state.settings.height = 512;
    m_state.previewSpp = 128;
    m_state.settings.samplesPerPixel = m_state.previewSpp;
    m_state.orbitCam.radius = 1200.0f;
    m_state.orbitCam.target[0] = 278.0f;
    m_state.orbitCam.target[1] = 273.0f;
    m_state.orbitCam.target[2] = 277.5f;
}

void Application::queueModelDrop(const std::string& path) { m_state.pendingModelDrop = path; }
void Application::queueHdrDrop(const std::string& path) { m_state.pendingHdrDrop = path; }

Application::~Application() {
    m_state.shutdown = true;
    if (m_state.fullRender.thread.joinable()) m_state.fullRender.thread.join();
    if (m_state.renderThread.joinable()) m_state.renderThread.join();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
    gApp = nullptr;
}

void Application::initWindow() {
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    m_window = glfwCreateWindow(1600, 900, "PhotonEngine", nullptr, nullptr);
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
    applyKeyShotTheme();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::loadAssets() {
    m_state.materialLib.loadFromDirectory(m_state.assetsRoot + "/materials");
    buildCornellBox(m_state.graph);
    m_state.lights.push_back(std::make_shared<AreaLight>(
        Vec3f(343, 548.0f, 227), Vec3f(-130, 0, 0), Vec3f(0, 0, 105), Color3f(15.0f)));
    rebuildScene();
}

void Application::rebuildScene() {
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
            if (m_state.renderDirty.load()) {
                std::lock_guard<std::mutex> lock(m_state.imageMutex);
                m_state.accumImage.resize(m_state.settings.width, m_state.settings.height);
                m_state.accumImage.clear();
                m_state.currentSpp = 0;
                m_state.renderDirty = false;
            }
            int spp = m_state.currentSpp.load();
            if (spp < m_state.settings.samplesPerPixel) {
                float aspect = static_cast<float>(m_state.settings.width) / m_state.settings.height;
                auto cam = makeCamera(aspect);
                {
                    std::lock_guard<std::mutex> lock(m_state.imageMutex);
                    m_state.renderer.renderSamplePass(m_state.flatScene, *cam, m_state.settings,
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
    if (m_state.orbitCam.aperture > 0.0f) {
        return std::make_unique<ThinLensCamera>(position, target, Vec3f(0, 1, 0),
            m_state.orbitCam.fov, aspect, m_state.orbitCam.aperture, m_state.orbitCam.focusDistance);
    }
    return std::make_unique<PerspectiveCamera>(position, target, Vec3f(0, 1, 0),
        m_state.orbitCam.fov, aspect);
}

void Application::importModel(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto defaultMat = std::make_shared<DisneyMaterial>(Color3f(0.8f), 0.0f, 0.5f, 0.5f);

    std::vector<std::shared_ptr<TriangleMesh>> meshes;
    std::vector<std::shared_ptr<Material>> materials;

    if (ext == ".obj") {
        meshes = ObjLoader::load(path, defaultMat.get());
        for (size_t i = 0; i < meshes.size(); ++i) materials.push_back(defaultMat);
    } else if (ext == ".gltf" || ext == ".glb") {
        auto r = GltfLoader::load(path, defaultMat.get());
        meshes = std::move(r.meshes);
        materials = std::move(r.materials);
    } else return;

    auto group = std::make_unique<SceneNode>(std::filesystem::path(path).stem().string(), SceneNodeType::Group);
    for (size_t i = 0; i < meshes.size(); ++i) {
        auto node = std::make_unique<SceneNode>("mesh_" + std::to_string(i), SceneNodeType::Mesh);
        node->mesh = meshes[i];
        node->material = i < materials.size() ? materials[i] : defaultMat;
        node->pickId = m_state.graph.allocatePickId();
        group->addChild(std::move(node));
    }
    m_state.graph.root()->addChild(std::move(group));
    rebuildScene();
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
    rebuildScene();
}

void Application::processPendingDrops() {
    if (!m_state.pendingModelDrop.empty()) {
        importModel(m_state.pendingModelDrop);
        m_state.pendingModelDrop.clear();
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
    ImGuiID dockId = ImGui::GetID("MainDock");
    if (ImGui::DockBuilderGetNode(dockId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
        ImGuiID left = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.22f, nullptr, &dockId);
        ImGuiID right = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.28f, nullptr, &dockId);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.08f, nullptr, &dockId);
        ImGui::DockBuilderDockWindow("Kutuphane", left);
        ImGui::DockBuilderDockWindow("Sahne", right);
        ImGui::DockBuilderDockWindow("Inceleyici", right);
        ImGui::DockBuilderDockWindow("Render", right);
        ImGui::DockBuilderDockWindow("Viewport", dockId);
        ImGui::DockBuilderDockWindow("Durum", bottom);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
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
    m_state.settings.exposure = jsonFloatField(json, "exposure", m_state.settings.exposure);
    float lightI = jsonFloatField(json, "lightIntensity", 0.0f);
    if (lightI > 0.0f && !m_state.lights.empty()) {
        if (auto area = std::dynamic_pointer_cast<AreaLight>(m_state.lights.front())) {
            area->setRadiance(Color3f(lightI));
        }
    }
    markDirty();
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
    const std::string outPath = m_state.fullRender.outputPath;
    RenderSettings rs = m_state.settings;
    rs.width = outW;
    rs.height = outH;
    rs.samplesPerPixel = outSpp;

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

            if (!outPath.empty()) {
                if (outPath.size() >= 4 && outPath.substr(outPath.size() - 4) == ".exr") {
                    saveImageEXR(img, outPath);
                } else {
                    saveImagePNG(img, outPath, rs.tmo, rs.exposure);
                }
            }
            m_state.fullRender.status = outPath.empty() ? "Tamamlandi" : "Kaydedildi: " + outPath;
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
        if (ImGui::MenuItem("HDR Ac...")) importHdrDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Kaydet", "Ctrl+S")) saveProject(m_state.projectPath, m_state.graph, "{}");
        if (ImGui::MenuItem("Yukle...")) {
            std::string path = m_state.projectPath;
            FileDialogFilter filters[] = {{"Photon Proje", "*.photon"}, {"Tum Dosyalar", "*.*"}};
            if (showFileDialog(path, FileDialogMode::Open, "Proje Ac", filters, 2)) {
                std::string cam;
                if (loadProject(path, m_state.graph, cam)) {
                    m_state.projectPath = path;
                    rebuildScene();
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
        ImGui::MenuItem("Hizli Onizleme", nullptr, &m_state.fastPreview);
        ImGui::MenuItem("Gelismis Mod", nullptr, &m_state.advancedMode);
        ImGui::MenuItem("Render Paneli", nullptr, &m_state.showRenderPanel);
        ImGui::Separator();
        if (ImGui::MenuItem("Hosgeldiniz")) m_state.showOnboarding = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem("Onizleme (128 SPP)")) applyQualityPreset(128);
        if (ImGui::MenuItem("Yuksek (512 SPP)")) applyQualityPreset(512);
        if (ImGui::MenuItem("Final (1024 SPP)")) applyQualityPreset(1024);
        ImGui::Separator();
        if (ImGui::MenuItem("Yeniden Baslat", "F5")) markDirty();
        if (ImGui::MenuItem("Tam Cozunurluk...")) m_state.showFullRenderDialog = true;
        ImGui::EndMenu();
    }
}

void Application::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_state.undo.undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_state.undo.redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveProject(m_state.projectPath, m_state.graph, "{}");
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) importModelDialog();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) markDirty();
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) m_state.orbitCam.turntable = !m_state.orbitCam.turntable;
}

void Application::drawLibraryPanel() {
    ImGui::Begin("Kutuphane");
  if (ImGui::BeginTabBar("libtabs")) {
    if (ImGui::BeginTabItem("Materyaller")) {
      for (const auto& cat : m_state.materialLib.categories()) {
        if (ImGui::TreeNode(cat.c_str())) {
          int col = 0;
          for (const auto* p : m_state.materialLib.byCategory(cat)) {
            ImGui::PushID(p->id.c_str());
            ImVec4 col4(p->baseColor.r, p->baseColor.g, p->baseColor.b, 1.0f);
            ImGui::ColorButton("##sphere", col4, ImGuiColorEditFlags_NoTooltip, ImVec2(40, 40));
            if (ImGui::BeginDragDropSource()) {
              ImGui::SetDragDropPayload(kPayloadMaterial, p->id.c_str(), p->id.size() + 1);
              ImGui::TextUnformatted(p->name.c_str());
              ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p->name.c_str());
            ImGui::SameLine();
            if (++col % 3 == 0) ImGui::NewLine();
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Ortamlar")) {
      ImGui::TextWrapped("HDR/EXR dosyasini surukleyin veya Dosya > HDR Ac");
      if (m_state.environment) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Ortam yuklu");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Studios")) {
      namespace fs = std::filesystem;
      fs::path studioDir = fs::path(m_state.assetsRoot) / "studios";
      if (fs::exists(studioDir)) {
        for (const auto& e : fs::directory_iterator(studioDir)) {
          if (e.path().extension() == ".json" &&
              ImGui::Selectable(e.path().stem().string().c_str())) {
            applyStudioPreset(e.path().string());
          }
        }
      } else {
        ImGui::TextDisabled("Studios bulunamadi");
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void Application::drawSceneTreePanel() {
    ImGui::Begin("Sahne");
    std::function<void(SceneNode*)> drawNode = [&](SceneNode* n) {
        if (!n || n->type == SceneNodeType::Group && n->name == "Root" && !n->children.empty()) {
            for (auto& c : n->children) drawNode(c.get());
            return;
        }
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_state.selected == n) f |= ImGuiTreeNodeFlags_Selected;
        bool open = ImGui::TreeNodeEx((void*)n, f, "%s", n->name.c_str());
        if (ImGui::IsItemClicked()) m_state.selected = n;
        ImGui::SameLine();
        ImGui::PushID(n);
        if (ImGui::Checkbox("##vis", &n->visible)) rebuildScene();
        ImGui::PopID();
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Sil")) { n->visible = false; rebuildScene(); }
            if (ImGui::MenuItem("Gorunurluk")) { n->visible = !n->visible; rebuildScene(); }
            ImGui::EndPopup();
        }
        if (open) {
            for (auto& c : n->children) drawNode(c.get());
            ImGui::TreePop();
        }
    };
    drawNode(m_state.graph.root());
    ImGui::End();
}

void Application::drawInspectorPanel() {
    ImGui::Begin("Inceleyici");

    if (ImGui::CollapsingHeader("Kamera", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("FOV", &m_state.orbitCam.fov, 10.0f, 120.0f)) markDirty();
        if (ImGui::SliderFloat("DoF Acikligi", &m_state.orbitCam.aperture, 0, 0.05f)) markDirty();
        if (ImGui::SliderFloat("Odak Mesafesi", &m_state.orbitCam.focusDistance, 0.1f, 50.0f)) markDirty();
        ImGui::Checkbox("Turntable", &m_state.orbitCam.turntable);
    }

    ImGui::Separator();
    SceneNode* sel = m_state.selected;
    if (!sel || !sel->material) {
        ImGui::TextDisabled("Parca secin veya modele tiklayin");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Materyal", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto disney = std::dynamic_pointer_cast<DisneyMaterial>(sel->material);
        if (disney) {
            Color3f bc = disney->baseColor();
            float col[3] = {bc.r, bc.g, bc.b};
            if (ImGui::ColorEdit3("Renk", col)) {
                disney->setBaseColor(Color3f(col[0], col[1], col[2]));
                markDirty();
            }
            float metallic = disney->metallic(), rough = disney->roughness(), spec = disney->specular();
            if (ImGui::SliderFloat("Metalik", &metallic, 0, 1)) { disney->setMetallic(metallic); markDirty(); }
            if (ImGui::SliderFloat("Puruzluluk", &rough, 0.001f, 1)) { disney->setRoughness(rough); markDirty(); }
            if (ImGui::SliderFloat("Specular", &spec, 0, 1)) { disney->setSpecular(spec); markDirty(); }
            if (m_state.advancedMode) {
                float cc = disney->clearCoat();
                if (ImGui::SliderFloat("Clear Coat", &cc, 0, 1)) { disney->setClearCoat(cc); markDirty(); }
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadMaterial)) {
                    std::string id((const char*)payload->Data);
                    for (const auto& p : m_state.materialLib.presets()) {
                        if (p.id == id) {
                            sel->material = m_state.materialLib.createMaterial(p);
                            rebuildScene();
                            break;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    ImGui::End();
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
        ImGui::Checkbox("Adaptive Sampling", &m_state.settings.adaptiveSampling);
        ImGui::Checkbox("OIDN Denoise", &m_state.settings.denoiseEnabled);
        if (ImGui::SliderFloat("AO", &m_state.settings.aoStrength, 0, 1)) markDirty();
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
    m_state.viewportW = w;
    m_state.viewportH = h;

    if (m_state.settings.width != w || m_state.settings.height != h) {
        m_state.settings.width = w;
        m_state.settings.height = h;
        markDirty();
    }

    ImGui::Image((ImTextureID)(intptr_t)m_state.cpuTexture.textureId(),
                 ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0, 1), ImVec2(1, 0));

    if (m_state.graph.empty()) {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        const char* msg = "Modeli buraya surukleyin";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f),
            IM_COL32(200, 200, 210, 200), msg);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(p.x + 20, p.y + sz.y - 40),
            IM_COL32(140, 140, 150, 160), "Sol: dondur | Orta/Sag: kaydir | Tekerlek: zoom");
    }

    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_state.orbitCam.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            m_state.orbitCam.pan(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0) m_state.orbitCam.zoom(io.MouseWheel);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float u = (mp.x - rmin.x) / size.x;
            float v = (mp.y - rmin.y) / size.y;
            uint32_t id = m_state.picking.pick(m_state.graph, m_state.flatScene,
                *makeCamera(static_cast<float>(w) / h), w, h, u, v);
            if (id) m_state.selected = m_state.graph.findByPickId(id);
        }
        markDirty();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadMaterial)) {
            std::string id((const char*)payload->Data);
            for (const auto& p : m_state.materialLib.presets()) {
                if (p.id == id && m_state.selected) {
                    m_state.selected->material = m_state.materialLib.createMaterial(p);
                    rebuildScene();
                    break;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void Application::drawStatusBar() {
    ImGui::Begin("Durum");
    int spp = m_state.currentSpp.load();
    int target = m_state.settings.samplesPerPixel;
    ImGui::Text("SPP: %d / %d", spp, target);
    ImGui::SameLine(160);
    ImGui::Text("FPS: %.1f", m_state.fps);
    ImGui::SameLine(280);
    ImGui::Text("%dx%d", m_state.settings.width, m_state.settings.height);
    ImGui::SameLine(400);
    if (m_state.fullRender.active)
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1), "Tam render: %d/%d SPP",
            m_state.fullRender.currentSpp.load(), m_state.fullRender.targetSpp.load());
    else
        ImGui::Text(m_state.fastPreview ? "Hizli onizleme" : "Kalite onizleme");
    ImGui::End();
}

void Application::drawOnboarding() {
    if (!m_state.showOnboarding) return;
    ImGui::OpenPopup("Hosgeldiniz");
    if (ImGui::BeginPopupModal("Hosgeldiniz", &m_state.showOnboarding, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Modeli viewport'a surukleyin.");
        ImGui::Text("Materyal kuresini mesh uzerine birakin.");
        if (ImGui::Button("Tamam")) { m_state.showOnboarding = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
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
        m_state.orbitCam.tick(dt);
        processPendingDrops();
        if (m_state.imageReady.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_state.imageMutex);
            m_state.cpuTexture.upload(m_state.accumImage, m_state.settings.tmo, m_state.settings.exposure);
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

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(m_window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_window);
    }
    return 0;
}

} // namespace photon
