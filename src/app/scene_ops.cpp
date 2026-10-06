// scene_ops.cpp — Belge üzerindeki tüm işlemler: sahne derleme, geri al/yinele,
// seçim, model içe aktarma, malzeme/ortam/stüdyo uygulama, ışıklar, kamera,
// proje kaydetme/açma ve viewport görüntüsünü dışa aktarma.
#include "app/application.h"
#include "scene/model_import.h"
#include "scene/project_io.h"
#include "scene/cornell_box.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "ui/file_dialog.h"
#include "ui/widgets.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/platform/path.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace photon {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr uint64_t kGroundUid = kGroundNodeUid;

void reassignUids(SceneNode& n) {
    n.uid = allocateNodeUid();
    for (auto& c : n.children) reassignUids(*c);
}

void forEachMesh(SceneNode& n, const std::function<void(SceneNode&)>& fn) {
    if (n.type == SceneNodeType::Mesh || n.type == SceneNodeType::Sphere) fn(n);
    for (auto& c : n.children) forEachMesh(*c, fn);
}

} // namespace

// ── Derleme ──────────────────────────────────────────────────────────────

void Application::rebuildScene(bool interactive) {
    m_s.sceneDirty = true;
    m_s.sceneDirtyInteractive = m_s.sceneDirtyInteractive || interactive;
}

// Belgeden yeni, değişmez bir render sahnesi kurar ve viewport'a verir. Eski
// sahne render thread'i işini bitirene kadar shared_ptr ile yaşamaya devam eder.
std::shared_ptr<Scene> Application::buildScene(bool isolate) {
    return buildRenderScene(m_s.graph, m_s.lights, m_s.environment, m_s.envCache, isolate);
}

void Application::compileIfDirty() {
    if (!m_s.sceneDirty) return;
    const bool interactive = m_s.sceneDirtyInteractive;
    m_s.sceneDirty = false;
    m_s.sceneDirtyInteractive = false;
    auto scene = buildScene(false);
    m_s.compiled = scene;
    m_s.viewport.setScene(scene, interactive);
}

void Application::markDocumentChanged(bool interactive) {
    m_s.documentDirty = true;
    rebuildScene(interactive);
}

// ── Geri al ──────────────────────────────────────────────────────────────

void Application::pushUndo() {
    DocSnapshot s;
    s.root = SceneGraph::cloneTree(*m_s.graph.root(), true);
    s.lights = m_s.lights;
    s.environment = m_s.environment;
    m_s.undo.push(std::move(s));
    m_s.documentDirty = true;
}

void Application::undo() {
    DocSnapshot cur;
    cur.root = SceneGraph::cloneTree(*m_s.graph.root(), false);
    cur.lights = m_s.lights;
    cur.environment = m_s.environment;
    if (!m_s.undo.undo(cur)) {
        setStatus("Geri alınacak işlem yok");
        return;
    }
    m_s.graph.setRoot(std::move(cur.root));
    m_s.lights = std::move(cur.lights);
    m_s.environment = std::move(cur.environment);
    if (m_s.selKind == SelectionKind::Node && !m_s.graph.findByUid(m_s.selUid)) clearSelection();
    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= static_cast<int>(m_s.lights.size())) clearSelection();
    markDocumentChanged();
    setStatus("Geri alındı");
}

void Application::redo() {
    DocSnapshot cur;
    cur.root = SceneGraph::cloneTree(*m_s.graph.root(), false);
    cur.lights = m_s.lights;
    cur.environment = m_s.environment;
    if (!m_s.undo.redo(cur)) {
        setStatus("Yinelenecek işlem yok");
        return;
    }
    m_s.graph.setRoot(std::move(cur.root));
    m_s.lights = std::move(cur.lights);
    m_s.environment = std::move(cur.environment);
    if (m_s.selKind == SelectionKind::Node && !m_s.graph.findByUid(m_s.selUid)) clearSelection();
    markDocumentChanged();
    setStatus("Yinelendi");
}

// ── Seçim ────────────────────────────────────────────────────────────────

SceneNode* Application::selectedNode() {
    return m_s.selKind == SelectionKind::Node ? m_s.graph.findByUid(m_s.selUid) : nullptr;
}

void Application::selectNode(uint64_t uid) {
    if (uid == kGroundUid) {
        m_s.selKind = SelectionKind::Ground;
        m_s.selUid = 0;
        m_s.rightTab = 2; // Ortam sekmesi zemin ayarlarını gösterir
        return;
    }
    if (!uid || !m_s.graph.findByUid(uid)) {
        clearSelection();
        return;
    }
    m_s.selKind = SelectionKind::Node;
    m_s.selUid = uid;
    if (m_s.rightTab != 0 && m_s.rightTab != 1) m_s.rightTab = 1; // Malzeme
}

void Application::selectLight(int index) {
    if (index < 0 || index >= static_cast<int>(m_s.lights.size())) {
        clearSelection();
        return;
    }
    m_s.selKind = SelectionKind::Light;
    m_s.selLight = index;
    m_s.rightTab = 3; // Işıklar
}

void Application::clearSelection() {
    m_s.selKind = SelectionKind::None;
    m_s.selUid = 0;
    m_s.selLight = -1;
}

// ── Sahneler ─────────────────────────────────────────────────────────────

void Application::newScene() {
    m_s.graph.clear();
    m_s.lights.clear();
    m_s.environment = EnvironmentDesc{};
    for (const auto& e : m_s.environments)
        if (e.id == "studio_small_09") m_s.environment.hdrPath = e.path;
    m_s.undo.clear();
    m_s.projectPath.clear();
    m_s.savedCameras.clear();
    m_s.documentDirty = false;
    clearSelection();
    m_s.camera = OrbitCamera{};
    m_s.settings.exposure = 0.0f;
    markDocumentChanged();
    m_s.documentDirty = false;
}

// Ürün vitrini: siyah lake kaide, krom küre, cam küre, araç boyalı küp, altın küçük küre.
void Application::loadSampleScene() {
    newScene();
    const fs::path models = pathFromUtf8(m_s.assetsRoot) / "models";
    auto place = [&](const char* file, const char* name, const Transform& xf, const char* presetId) -> SceneNode* {
        std::string err;
        auto node = importModelFile(pathToUtf8(models / file), &err);
        if (!node) return nullptr;
        node->name = name;
        node->localTransform = xf;
        if (const MaterialPreset* p = m_s.materials.findById(presetId)) {
            auto mat = MaterialLibrary::createMaterial(*p);
            forEachMesh(*node, [&](SceneNode& n) { n.material = mat; });
        }
        return m_s.graph.root()->addChild(std::move(node));
    };
    place("product_stand.obj", "Kaide", Transform{}, "piano_black");
    place("sphere.obj", "Krom Küre", Transform::translate(Vec3f(0.0f, 0.95f, 0.0f)), "chrome");
    place("sphere.obj", "Cam Küre",
          Transform::translate(Vec3f(1.25f, 0.42f, 0.35f)) * Transform::scale(Vec3f(0.84f)), "clear_glass");
    place("cube.obj", "Küp",
          Transform::translate(Vec3f(-1.3f, 0.0f, 0.1f)) * Transform::rotateY(0.45f) * Transform::scale(Vec3f(0.78f)),
          "red_car_paint");
    place("sphere.obj", "Altın Küre",
          Transform::translate(Vec3f(-0.55f, 0.2f, 1.05f)) * Transform::scale(Vec3f(0.4f)), "gold");
    if (m_s.graph.empty()) {
        setStatus("Örnek modeller bulunamadı (assets/models)", true);
        return;
    }
    m_s.camera.theta = 1.2f;
    m_s.camera.phi = 1.15f;
    frameAll();
    for (const auto& s : m_s.studios)
        if (s.id == "product_softbox") applyStudio(s);
    m_s.undo.clear();
    m_s.camera.radius *= 0.92f;
    m_s.documentDirty = false;
    setStatus("Örnek ürün sahnesi yüklendi");
}

void Application::loadCornellScene() {
    newScene();
    buildCornellBox(m_s.graph);
    m_s.environment.groundEnabled = false;
    m_s.environment.hdrPath.clear();
    m_s.environment.zenith = Color3f(0.02f);
    m_s.environment.horizon = Color3f(0.02f);
    LightDesc l;
    l.name = "Tavan ışığı";
    l.type = LightDesc::Type::Area;
    l.position = Vec3f(278.0f, 548.0f, 279.5f);
    l.target = Vec3f(278.0f, 0.0f, 279.5f);
    l.width = 130.0f;
    l.height = 105.0f;
    l.intensity = 17.0f;
    m_s.lights.push_back(l);
    m_s.camera.target[0] = 278.0f;
    m_s.camera.target[1] = 273.0f;
    m_s.camera.target[2] = 277.5f;
    m_s.camera.fov = 40.0f;
    m_s.camera.theta = PI * 0.5f;
    m_s.camera.phi = -PI * 0.5f;
    m_s.camera.radius = 1050.0f;
    markDocumentChanged();
    m_s.undo.clear();
    m_s.documentDirty = false;
    setStatus("Cornell kutusu yüklendi");
}

// Model dosyası arka planda okunur (büyük OBJ'ler saniyeler sürebilir); sahneye
// ekleme, zemine oturtma ve kadrajlama UI thread'inde tamamlamada yapılır.
bool Application::importModel(const std::string& path, bool undoable) {
    runAsync("İçe aktarılıyor: " + ui::fileName(path), [this, path, undoable]() -> std::function<void()> {
        auto err = std::make_shared<std::string>();
        auto holder = std::make_shared<std::unique_ptr<SceneNode>>(importModelFile(path, err.get()));
        return [this, path, undoable, holder, err] {
            if (!*holder) {
                setStatus(err->empty() ? "Model yüklenemedi" : *err, true);
                return;
            }
            attachImported(path, std::move(*holder), undoable);
        };
    });
    return true;
}

void Application::attachImported(const std::string& path, std::unique_ptr<SceneNode> node, bool undoable) {
    if (undoable) pushUndo();
    const bool wasEmpty = m_s.graph.empty();
    // Zemine oturt: modelin en alt noktası y = 0 olsun; boş sahnede merkeze al.
    const AABB box = SceneGraph::nodeWorldBounds(*node);
    if (box.pMin.x <= box.pMax.x) {
        Vec3f shift(0.0f, -box.pMin.y, 0.0f);
        if (wasEmpty) {
            const Vec3f c = box.centroid();
            shift.x = -c.x;
            shift.z = -c.z;
        }
        node->localTransform = Transform::translate(shift) * node->localTransform;
    }
    const uint64_t uid = node->uid;
    size_t parts = node->children.size();
    m_s.graph.root()->addChild(std::move(node));
    if (wasEmpty) {
        if (m_s.lights.empty()) {
            for (const auto& s : m_s.studios)
                if (s.id == "product_softbox") applyStudio(s);
        }
        frameAll();
    }
    selectNode(uid); // applyStudio seçimi temizler; bu yüzden en sonda

    markDocumentChanged();
    setStatus("İçe aktarıldı: " + ui::fileName(path) + " (" + std::to_string(parts) + " parça)");
}

// ── Malzeme / ortam / stüdyo ─────────────────────────────────────────────

void Application::applyMaterialPreset(uint64_t nodeUid, const MaterialPreset& preset) {
    SceneNode* node = m_s.graph.findByUid(nodeUid);
    if (!node) {
        setStatus("Malzemeyi bir parçanın üzerine bırakın", true);
        return;
    }
    pushUndo();
    auto mat = MaterialLibrary::createMaterial(preset);
    int count = 0;
    forEachMesh(*node, [&](SceneNode& n) {
        n.material = mat;
        ++count;
    });
    selectNode(nodeUid);
    m_s.rightTab = 1;
    markDocumentChanged();
    setStatus(preset.name + " → " + node->name + (count > 1 ? " (" + std::to_string(count) + " parça)" : ""));
}

// HDRI dosyası önce arka planda önbelleğe yüklenir; sahne ancak sonra derlenir
// (aksi hâlde büyük bir HDR'nin okunması UI'yi bir saniye dondururdu).
void Application::applyEnvironment(const EnvAsset& env) {
    if (!env.path.empty()) {
        runAsync("Ortam yükleniyor: " + env.name, [this, env]() -> std::function<void()> {
            m_s.envCache.get(env.path);
            return [this, env] { applyEnvironmentNow(env); };
        });
        return;
    }
    applyEnvironmentNow(env);
}

void Application::applyEnvironmentNow(const EnvAsset& env) {
    pushUndo();
    m_s.environment.hdrPath = env.path;
    m_s.environment.zenith = env.zenith;
    m_s.environment.horizon = env.horizon;
    markDocumentChanged();
    setStatus("Ortam: " + env.name);
}

// Stüdyo: ortamı ve ışık düzenini birlikte uygular. Işıklar sahne sınırlarına
// göre yerleşir (document.cpp: placeStudioLights).
void Application::applyStudio(const StudioPreset& studio) {
    pushUndo();
    if (!studio.environment.empty()) {
        for (const auto& e : m_s.environments) {
            if (e.id == studio.environment || ui::fileName(e.path) == studio.environment) {
                m_s.environment.hdrPath = e.path;
                m_s.environment.zenith = e.zenith;
                m_s.environment.horizon = e.horizon;
            }
        }
    }
    m_s.environment.rotationDeg = studio.rotationDeg;
    m_s.environment.intensity = studio.intensity;
    if (studio.hasExposure) m_s.settings.exposure = studio.exposure;
    m_s.lights = placeStudioLights(studio, m_s.graph.worldBounds(), m_s.camera.phi * RAD_TO_DEG);
    clearSelection();
    markDocumentChanged();
    setStatus("Stüdyo: " + studio.name);
}

void Application::addLight(LightDesc::Type type) {
    pushUndo();
    const AABB box = m_s.graph.worldBounds();
    StudioPreset tmp;
    StudioPreset::Rig rig;
    rig.type = type;
    rig.azimuthDeg = 40.0f; // kameranın 40° yanından
    rig.elevationDeg = 45.0f;
    rig.distance = 2.2f;
    rig.size = 0.8f;
    rig.intensity = type == LightDesc::Type::Area ? 8.0f : type == LightDesc::Type::Directional ? 3.0f : 20.0f;
    rig.name = type == LightDesc::Type::Area ? "Alan ışığı" : type == LightDesc::Type::Directional ? "Güneş" : "Nokta ışık";
    tmp.lights.push_back(rig);
    LightDesc d = placeStudioLights(tmp, box, m_s.camera.phi * RAD_TO_DEG).front();
    if (type == LightDesc::Type::Point) {
        // Nokta ışık yoğunluğu mesafenin karesiyle düşer; sahne ölçeğine göre ayarla.
        const float r = std::max(1e-3f, (box.pMax - box.pMin).length() * 0.5f);
        d.intensity *= r * r;
    }
    m_s.lights.push_back(d);
    selectLight(static_cast<int>(m_s.lights.size()) - 1);
    markDocumentChanged();
}

// ── Düzenleme ────────────────────────────────────────────────────────────

void Application::deleteSelection() {
    if (m_s.selKind == SelectionKind::Node) {
        SceneNode* n = selectedNode();
        if (!n) return;
        pushUndo();
        const std::string name = n->name;
        m_s.graph.removeNode(n);
        clearSelection();
        markDocumentChanged();
        setStatus("Silindi: " + name);
    } else if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 &&
               m_s.selLight < static_cast<int>(m_s.lights.size())) {
        pushUndo();
        m_s.lights.erase(m_s.lights.begin() + m_s.selLight);
        clearSelection();
        markDocumentChanged();
    }
}

void Application::duplicateSelection() {
    SceneNode* n = selectedNode();
    if (!n || !n->parent) return;
    pushUndo();
    auto copy = SceneGraph::cloneTree(*n, false); // malzeme paylaşılır (KeyShot'taki bağlı kopya gibi)
    reassignUids(*copy);
    copy->name = n->name + " kopya";
    const AABB box = SceneGraph::nodeWorldBounds(*n);
    const float dx = box.pMin.x <= box.pMax.x ? (box.pMax.x - box.pMin.x) * 1.15f : 1.0f;
    copy->localTransform = Transform::translate(Vec3f(dx, 0, 0)) * copy->localTransform;
    const uint64_t uid = copy->uid;
    n->parent->addChild(std::move(copy));
    selectNode(uid);
    markDocumentChanged();
}

void Application::frameSelection() {
    AABB box = AABB::empty();
    if (SceneNode* n = selectedNode()) box = SceneGraph::nodeWorldBounds(*n);
    if (box.pMin.x > box.pMax.x) {
        frameAll();
        return;
    }
    const float aspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.6f;
    m_s.camera.frame(box, aspect);
}

void Application::frameAll() {
    AABB box = m_s.graph.worldBounds();
    if (box.pMin.x > box.pMax.x) return;
    const float aspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.6f;
    m_s.camera.frame(box, aspect);
}

void Application::placeSelectionOnGround() {
    SceneNode* n = selectedNode();
    if (!n) return;
    const AABB box = SceneGraph::nodeWorldBounds(*n);
    if (box.pMin.x > box.pMax.x || std::abs(box.pMin.y) < 1e-6f) return;
    pushUndo();
    n->localTransform = Transform::translate(Vec3f(0.0f, -box.pMin.y, 0.0f)) * n->localTransform;
    markDocumentChanged();
    setStatus("Zemine oturtuldu: " + n->name);
}

void Application::applyCameraPreset(const char* id) {
    auto& c = m_s.camera;
    const std::string s = id;
    if (s == "front") { c.theta = PI * 0.5f; c.phi = PI * 0.5f; }
    else if (s == "side") { c.theta = PI * 0.5f; c.phi = 0.0f; }
    else if (s == "top") { c.theta = 0.03f; c.phi = PI * 0.5f; }
    else if (s == "three_quarter") { c.theta = 1.15f; c.phi = 1.05f; }
    else if (s == "low") { c.theta = 1.45f; c.phi = 1.2f; }
    else return;
    frameAll();
}

CameraParams Application::cameraParams() const {
    const OrbitCamera& c = m_s.camera;
    CameraParams p;
    p.eye = c.position();
    p.target = c.targetVec();
    p.fovDeg = c.fov;
    p.orthographic = c.orthographic;
    p.orthoHeight = 2.0f * std::tan(c.fov * DEG_TO_RAD * 0.5f) * std::max(c.radius, 1e-3f);
    p.apertureRadius = c.aperture;
    p.focusDistance = effectiveFocusDistance(c);
    return p;
}

// Viewport koordinatı (u sağa, v aşağı, [0,1]) → kamera ışını → derlenmiş sahnede
// en yakın kesişim → düğüm kimliği. BVH kullanır, büyük modellerde de anlıktır.
uint64_t Application::pickAt(float u, float v, Vec3f* hitPoint) {
    if (!m_s.compiled) return 0;
    const float aspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.0f;
    CameraParams p = cameraParams();
    p.apertureRadius = 0.0f;
    auto cam = makeCamera(p, aspect);
    Ray ray = cam->generateRay(u, 1.0f - v, Vec2f(0.5f, 0.5f));
    SurfaceInteraction isect;
    if (!m_s.compiled->intersect(ray, isect)) return 0;
    if (hitPoint) *hitPoint = isect.point;
    return m_s.compiled->nodeUidOf(isect);
}

// ── Proje ────────────────────────────────────────────────────────────────

namespace {

json cameraJson(const OrbitCamera& c) {
    return {{"target", {c.target[0], c.target[1], c.target[2]}}, {"radius", c.radius}, {"theta", c.theta},
            {"phi", c.phi}, {"fov", c.fov}, {"focalLengthMm", c.focalLengthMm}, {"orthographic", c.orthographic},
            {"aperture", c.aperture}, {"fStop", c.fStop}, {"focusDistance", c.focusDistance},
            {"focusExplicit", c.focusExplicit}};
}

void readCamera(const json& j, OrbitCamera& c) {
    if (auto t = j.find("target"); t != j.end() && t->is_array() && t->size() == 3)
        for (int i = 0; i < 3; ++i) c.target[i] = (*t)[static_cast<size_t>(i)].get<float>();
    c.radius = j.value("radius", c.radius);
    c.theta = j.value("theta", c.theta);
    c.phi = j.value("phi", c.phi);
    c.fov = j.value("fov", c.fov);
    c.focalLengthMm = j.value("focalLengthMm", c.focalLengthMm);
    c.orthographic = j.value("orthographic", c.orthographic);
    c.aperture = j.value("aperture", c.aperture);
    c.fStop = j.value("fStop", c.fStop);
    c.focusDistance = j.value("focusDistance", c.focusDistance);
    c.focusExplicit = j.value("focusExplicit", c.focusExplicit);
}

} // namespace

void Application::saveProject() {
    if (m_s.projectPath.empty()) {
        saveProjectAs();
        return;
    }
    ProjectData d;
    d.camera = cameraJson(m_s.camera);
    d.render = {{"toneMap", static_cast<int>(m_s.settings.tmo)}, {"exposure", m_s.settings.exposure},
                {"maxBounces", m_s.settings.maxBounces}, {"denoise", m_s.denoise},
                {"output", {{"width", m_s.output.width}, {"height", m_s.output.height},
                            {"spp", m_s.output.spp}, {"format", m_s.output.format}}}};
    {
        json cams = json::array();
        for (const auto& sc : m_s.savedCameras) cams.push_back({{"name", sc.name}, {"camera", cameraJson(sc.cam)}});
        d.render["cameras"] = cams;
    }
    d.environment = m_s.environment;
    d.lights = m_s.lights;
    std::string err;
    if (!::photon::saveProject(m_s.projectPath, m_s.graph, d, &err)) {
        setStatus("Kaydedilemedi: " + err, true);
        return;
    }
    m_s.documentDirty = false;
    setStatus("Kaydedildi: " + ui::fileName(m_s.projectPath));
}

void Application::saveProjectAs() {
    std::string path = m_s.projectPath.empty() ? "urun.photon" : m_s.projectPath;
    FileDialogFilter f[] = {{"Photon projesi", "*.photon"}};
    if (!showFileDialog(path, FileDialogMode::Save, "Projeyi kaydet", f, 1)) return;
    m_s.projectPath = path;
    saveProject();
}

void Application::openProjectDialog() {
    std::string path = m_s.projectPath;
    FileDialogFilter f[] = {{"Photon projesi", "*.photon"}, {"Tüm dosyalar", "*.*"}};
    if (showFileDialog(path, FileDialogMode::Open, "Proje aç", f, 2)) openProject(path);
}

bool Application::openProject(const std::string& path) {
    ProjectData d;
    std::string err;
    SceneGraph loaded;
    if (!loadProject(path, loaded, d, &err)) {
        setStatus(err.empty() ? "Proje açılamadı" : err, true);
        return false;
    }
    m_s.graph.setRoot(SceneGraph::cloneTree(*loaded.root(), false));
    m_s.lights = d.lights;
    m_s.environment = d.environment;
    readCamera(d.camera, m_s.camera);
    m_s.settings.tmo = static_cast<ToneMapOperator>(d.render.value("toneMap", static_cast<int>(m_s.settings.tmo)));
    m_s.settings.exposure = d.render.value("exposure", m_s.settings.exposure);
    m_s.settings.maxBounces = d.render.value("maxBounces", m_s.settings.maxBounces);
    m_s.denoise = d.render.value("denoise", m_s.denoise);
    if (auto o = d.render.find("output"); o != d.render.end() && o->is_object()) {
        m_s.output.width = o->value("width", m_s.output.width);
        m_s.output.height = o->value("height", m_s.output.height);
        m_s.output.spp = o->value("spp", m_s.output.spp);
        m_s.output.format = o->value("format", m_s.output.format);
    }
    m_s.savedCameras.clear();
    for (const auto& cj : d.render.value("cameras", json::array())) {
        AppState::SavedCamera sc;
        sc.name = cj.value("name", std::string("Kamera"));
        sc.cam = m_s.camera;
        readCamera(cj.value("camera", json::object()), sc.cam);
        m_s.savedCameras.push_back(sc);
    }
    m_s.projectPath = path;
    m_s.undo.clear();
    clearSelection();
    markDocumentChanged();
    m_s.documentDirty = false;
    setStatus(err.empty() ? "Proje açıldı: " + ui::fileName(path) : "Proje açıldı, uyarılar: " + err, !err.empty());
    return true;
}

void Application::openModelDialog() {
    std::string path;
    FileDialogFilter f[] = {{"3B modeller (OBJ, glTF)", "*.obj;*.gltf;*.glb"}, {"Tüm dosyalar", "*.*"}};
    if (showFileDialog(path, FileDialogMode::Open, "Model içe aktar", f, 2)) importModel(path);
}

void Application::openHdrDialog() {
    std::string path;
    FileDialogFilter f[] = {{"HDR ortam haritası", "*.hdr;*.exr"}, {"Tüm dosyalar", "*.*"}};
    if (!showFileDialog(path, FileDialogMode::Open, "Ortam haritası aç", f, 2)) return;
    m_pendingDrops.push_back(path); // bırakma ile aynı yol: kütüphaneye ekle + uygula
}

void Application::exportViewport() {
    std::shared_ptr<const Image> hdr, alpha;
    m_s.viewport.lastImages(hdr, alpha);
    if (!hdr || hdr->width() <= 0) {
        setStatus("Henüz dışa aktarılacak görüntü yok", true);
        return;
    }
    std::string path = "goruntu.png";
    FileDialogFilter f[] = {{"PNG", "*.png"}, {"JPEG", "*.jpg"}, {"OpenEXR (HDR)", "*.exr"}};
    if (!showFileDialog(path, FileDialogMode::Save, "Viewport görüntüsünü kaydet", f, 3)) return;
    std::string ext = pathToUtf8(pathFromUtf8(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool ok = false;
    if (ext == ".exr") ok = saveImageEXR(*hdr, path);
    else if (ext == ".jpg" || ext == ".jpeg") ok = saveImageJPG(*hdr, path, m_s.settings.tmo, m_s.settings.exposure);
    else if (m_s.environment.background.mode == Background::Mode::Transparent && alpha)
        ok = saveImagePNGAlpha(*hdr, *alpha, path, m_s.settings.tmo, m_s.settings.exposure);
    else ok = saveImagePNG(*hdr, path, m_s.settings.tmo, m_s.settings.exposure);
    setStatus(ok ? "Kaydedildi: " + path : "Kaydedilemedi: " + path, !ok);
}

void Application::saveMaterialToLibrary(const std::string& name) {
    SceneNode* n = selectedNode();
    if (!n || !n->material) return;
    MaterialPreset p = MaterialLibrary::presetFromMaterial(*n->material, name);
    p.category = "Benim";
    if (m_s.materials.savePreset(p, pathToUtf8(pathFromUtf8(m_s.userDir) / "materials"))) {
        m_s.thumbs.requestMaterial("mat:" + p.id, p, true);
        setStatus("Kütüphaneye eklendi: " + name);
    } else {
        setStatus("Malzeme kaydedilemedi", true);
    }
}

} // namespace photon

namespace photon {

// Malzeme panosu: kopyalanan malzemenin bir KOPYASI saklanır (sonradan asıl malzeme
// düzenlense bile pano değişmez); yapıştırırken de yeni bir kopya atanır.
void Application::copyMaterial() {
    SceneNode* n = selectedNode();
    std::shared_ptr<Material> src;
    if (n) {
        std::function<void(SceneNode&)> find = [&](SceneNode& s) {
            if (!src && s.material) src = s.material;
            for (auto& c : s.children) find(*c);
        };
        find(*n);
    }
    if (!src) {
        setStatus("Kopyalanacak malzeme yok: önce bir parça seçin", true);
        return;
    }
    m_s.clipboardMaterial = cloneMaterial(*src);
    setStatus("Malzeme kopyalandı");
}

void Application::pasteMaterial() {
    SceneNode* n = selectedNode();
    if (!n || !m_s.clipboardMaterial) {
        setStatus(m_s.clipboardMaterial ? "Yapıştırmak için bir parça seçin" : "Panoda malzeme yok", true);
        return;
    }
    pushUndo();
    auto mat = cloneMaterial(*m_s.clipboardMaterial);
    forEachMesh(*n, [&](SceneNode& s) { s.material = mat; });
    markDocumentChanged();
    setStatus("Malzeme yapıştırıldı → " + n->name);
}

} // namespace photon
