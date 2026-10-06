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
    pruneSelection();
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
    pruneSelection();
    markDocumentChanged();
    setStatus("Yinelendi");
}

// ── Seçim ────────────────────────────────────────────────────────────────

SceneNode* Application::selectedNode() {
    return m_s.selKind == SelectionKind::Node ? m_s.graph.findByUid(m_s.selUid) : nullptr;
}

std::vector<SceneNode*> Application::selectedNodes() {
    std::vector<SceneNode*> v;
    if (m_s.selKind != SelectionKind::Node) return v;
    for (uint64_t uid : m_s.selNodes)
        if (SceneNode* n = m_s.graph.findByUid(uid)) v.push_back(n);
    return m_s.graph.topmost(v);
}

bool Application::isNodeSelected(uint64_t uid) const {
    return m_s.selKind == SelectionKind::Node &&
           std::find(m_s.selNodes.begin(), m_s.selNodes.end(), uid) != m_s.selNodes.end();
}

void Application::selectNode(uint64_t uid) {
    if (uid == kGroundUid) {
        clearSelection();
        m_s.selKind = SelectionKind::Ground;
        m_s.rightTab = 2; // Ortam sekmesi zemin ayarlarını gösterir
        return;
    }
    if (!uid || !m_s.graph.findByUid(uid)) {
        clearSelection();
        return;
    }
    m_s.selKind = SelectionKind::Node;
    m_s.selUid = uid;
    m_s.selNodes = {uid};
    m_s.selAnchor = uid;
    m_s.selLight = -1;
    if (m_s.rightTab != 0 && m_s.rightTab != 1) m_s.rightTab = 1; // Malzeme
}

void Application::toggleNodeSelection(uint64_t uid) {
    if (!m_s.graph.findByUid(uid)) return;
    if (m_s.selKind != SelectionKind::Node) {
        selectNode(uid);
        return;
    }
    auto it = std::find(m_s.selNodes.begin(), m_s.selNodes.end(), uid);
    if (it != m_s.selNodes.end()) {
        m_s.selNodes.erase(it);
        if (m_s.selNodes.empty()) {
            clearSelection();
            return;
        }
        if (m_s.selUid == uid) m_s.selUid = m_s.selNodes.back();
    } else {
        m_s.selNodes.push_back(uid);
        m_s.selUid = uid;
    }
    m_s.selAnchor = uid;
}

void Application::selectNodeRange(uint64_t uid) {
    const auto& order = m_s.treeOrder;
    const auto a = std::find(order.begin(), order.end(), m_s.selAnchor);
    const auto b = std::find(order.begin(), order.end(), uid);
    if (m_s.selKind != SelectionKind::Node || a == order.end() || b == order.end()) {
        selectNode(uid);
        return;
    }
    const uint64_t anchor = m_s.selAnchor;
    m_s.selNodes.assign(std::min(a, b), std::max(a, b) + 1);
    m_s.selUid = uid;
    m_s.selAnchor = anchor; // çapa sabit: Shift ile aralık genişletilip daraltılabilir
}

void Application::selectAllNodes() {
    if (m_s.graph.empty()) return;
    m_s.selKind = SelectionKind::Node;
    m_s.selNodes.clear();
    for (const auto& c : m_s.graph.root()->children) m_s.selNodes.push_back(c->uid);
    m_s.selUid = m_s.selNodes.back();
    m_s.selAnchor = m_s.selNodes.front();
    m_s.selLight = -1;
}

void Application::selectLight(int index) {
    if (index < 0 || index >= static_cast<int>(m_s.lights.size())) {
        clearSelection();
        return;
    }
    clearSelection();
    m_s.selKind = SelectionKind::Light;
    m_s.selLight = index;
    m_s.rightTab = 3; // Işıklar
}

void Application::clearSelection() {
    m_s.selKind = SelectionKind::None;
    m_s.selUid = 0;
    m_s.selNodes.clear();
    m_s.selLight = -1;
}

void Application::pruneSelection() {
    if (m_s.selKind == SelectionKind::Node) {
        std::erase_if(m_s.selNodes, [&](uint64_t u) { return !m_s.graph.findByUid(u); });
        if (m_s.selNodes.empty()) clearSelection();
        else if (!m_s.graph.findByUid(m_s.selUid)) m_s.selUid = m_s.selNodes.back();
    } else if (m_s.selKind == SelectionKind::Light && m_s.selLight >= static_cast<int>(m_s.lights.size())) {
        clearSelection();
    }
    if (m_s.renamingUid && !m_s.graph.findByUid(m_s.renamingUid)) m_s.renamingUid = 0;
    if (m_s.renamingLight >= static_cast<int>(m_s.lights.size())) m_s.renamingLight = -1;
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
        const std::vector<SceneNode*> nodes = selectedNodes();
        if (nodes.empty()) return;
        pushUndo();
        const std::string what = nodes.size() == 1 ? nodes.front()->name : std::to_string(nodes.size()) + " nesne";
        for (SceneNode* n : nodes) m_s.graph.removeNode(n); // topmost: biri diğerinin içinde değil
        clearSelection();
        markDocumentChanged();
        setStatus("Silindi: " + what);
    } else if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 &&
               m_s.selLight < static_cast<int>(m_s.lights.size())) {
        pushUndo();
        const std::string name = m_s.lights[static_cast<size_t>(m_s.selLight)].name;
        m_s.lights.erase(m_s.lights.begin() + m_s.selLight);
        clearSelection();
        markDocumentChanged();
        setStatus("Silindi: " + name);
    }
}

// Kopyalar malzemeyi paylaşır (KeyShot'taki bağlı kopya gibi: birini düzenlemek
// hepsini değiştirir; ayırmak için Malzeme sekmesinde "Bağımsız yap").
// Birden çok nesne birlikte kopyalanır ve birlikte, seçimin genişliği kadar kayar.
void Application::duplicateSelection() {
    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 &&
        m_s.selLight < static_cast<int>(m_s.lights.size())) {
        pushUndo();
        LightDesc l = m_s.lights[static_cast<size_t>(m_s.selLight)];
        l.name += " kopya";
        // Hedef etrafında 25° yana döndür: kopya üst üste binmesin, yine ürüne baksın.
        const float a = 25.0f * DEG_TO_RAD;
        const Vec3f d = l.position - l.target;
        l.position = l.target + Vec3f(d.x * std::cos(a) - d.z * std::sin(a), d.y, d.x * std::sin(a) + d.z * std::cos(a));
        l.azimuthDeg += 25.0f;
        m_s.lights.push_back(l);
        selectLight(static_cast<int>(m_s.lights.size()) - 1);
        markDocumentChanged();
        setStatus("Işık çoğaltıldı");
        return;
    }
    const std::vector<SceneNode*> nodes = selectedNodes();
    if (nodes.empty()) return;
    pushUndo();
    AABB all = AABB::empty();
    for (SceneNode* n : nodes) all.merge(SceneGraph::nodeWorldBounds(*n));
    const float dx = all.pMin.x <= all.pMax.x ? (all.pMax.x - all.pMin.x) * 1.15f : 1.0f;
    std::vector<uint64_t> copies;
    for (SceneNode* n : nodes) {
        auto copy = SceneGraph::cloneTree(*n, false);
        reassignUids(*copy);
        SceneGraph::detachFromSource(*copy); // kaynak grubundaki parça kopyası: geometri projeye gömülür
        copy->name = n->name + " kopya";
        // Dünya X'inde kaydır: ebeveyn dönmüş/ölçeklenmiş olsa da kopya yana gider.
        const Mat4f world = Transform::translate(Vec3f(dx, 0, 0)).matrix() * n->worldTransform().matrix();
        copy->localTransform = Transform(n->parent->worldTransform().inverseMatrix() * world);
        copies.push_back(copy->uid);
        n->parent->addChild(std::move(copy));
    }
    m_s.selKind = SelectionKind::Node;
    m_s.selNodes = copies;
    m_s.selUid = copies.back();
    m_s.selAnchor = copies.front();
    markDocumentChanged();
    setStatus(copies.size() == 1 ? "Çoğaltıldı" : std::to_string(copies.size()) + " nesne çoğaltıldı");
}

void Application::frameSelection() {
    AABB box = AABB::empty();
    for (SceneNode* n : selectedNodes()) box.merge(SceneGraph::nodeWorldBounds(*n));
    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 && m_s.selLight < static_cast<int>(m_s.lights.size())) {
        const LightDesc& l = m_s.lights[static_cast<size_t>(m_s.selLight)];
        if (l.type != LightDesc::Type::Directional) {
            // Işık ve baktığı nokta birlikte kadraja girsin.
            const float pad = std::max({l.width, l.height, 0.1f}) * 0.6f;
            box.merge(l.position - Vec3f(pad));
            box.merge(l.position + Vec3f(pad));
            if (l.type == LightDesc::Type::Area) box.merge(l.target);
        }
    }
    if (box.pMin.x > box.pMax.x) {
        frameAll();
        return;
    }
    const float aspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.6f;
    m_s.camera.frame(box, aspect);
}

// H: seçimde görünen varsa hepsini gizle, hiçbiri görünmüyorsa hepsini göster
// (karışık seçimde tek basış hep aynı sonucu verir).
void Application::toggleSelectionVisibility() {
    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 && m_s.selLight < static_cast<int>(m_s.lights.size())) {
        pushUndo();
        LightDesc& l = m_s.lights[static_cast<size_t>(m_s.selLight)];
        l.enabled = !l.enabled;
        markDocumentChanged();
        return;
    }
    const std::vector<SceneNode*> nodes = selectedNodes();
    if (nodes.empty()) return;
    const bool anyVisible = std::any_of(nodes.begin(), nodes.end(), [](SceneNode* n) { return n->visible; });
    pushUndo();
    for (SceneNode* n : nodes) n->visible = !anyVisible;
    markDocumentChanged();
    setStatus(anyVisible ? "Gizlendi" : "Gösterildi");
}

// Yalnız seçili olanları göster: seçimin ataları görünür kalır (yoksa çocuk da
// gizlenirdi), seçimle ilgisi olmayan dallar gizlenir. Seçimin içindekilere
// dokunulmaz: kullanıcının daha önce gizlediği bir alt parça gizli kalır.
void Application::isolateSelection() {
    const std::vector<SceneNode*> nodes = selectedNodes();
    if (nodes.empty()) return;
    pushUndo();
    auto walk = [&](auto&& self, SceneNode& n) -> void {
        for (auto& c : n.children) {
            if (std::find(nodes.begin(), nodes.end(), c.get()) != nodes.end()) {
                c->visible = true;
            } else if (std::any_of(nodes.begin(), nodes.end(), [&](SceneNode* s) { return SceneGraph::isAncestor(*c, *s); })) {
                c->visible = true;
                self(self, *c);
            } else {
                c->visible = false;
            }
        }
    };
    walk(walk, *m_s.graph.root());
    markDocumentChanged();
    setStatus("Yalnız seçim gösteriliyor (Alt+H: hepsini göster)");
}

void Application::showAllNodes() {
    bool any = false;
    auto check = [&](auto&& self, const SceneNode& n) -> void {
        for (const auto& c : n.children) {
            any = any || !c->visible;
            self(self, *c);
        }
    };
    check(check, *m_s.graph.root());
    if (!any) return;
    pushUndo();
    auto walk = [&](auto&& self, SceneNode& n) -> void {
        for (auto& c : n.children) {
            c->visible = true;
            self(self, *c);
        }
    };
    walk(walk, *m_s.graph.root());
    markDocumentChanged();
    setStatus("Tüm nesneler gösteriliyor");
}

void Application::groupSelection() {
    const std::vector<SceneNode*> nodes = selectedNodes();
    if (nodes.empty()) return;
    pushUndo();
    SceneNode* g = m_s.graph.group(nodes, "Grup");
    if (!g) return;
    selectNode(g->uid);
    beginRename(); // yeni klasör gibi: ad kutusu hemen açılır, Enter "Grup" adını kabul eder
    markDocumentChanged();
    setStatus(std::to_string(nodes.size()) + " nesne gruplandı");
}

void Application::ungroupSelection() {
    std::vector<SceneNode*> groups;
    for (SceneNode* n : selectedNodes())
        if (n->type == SceneNodeType::Group && !n->children.empty()) groups.push_back(n);
    if (groups.empty()) {
        setStatus("Grubu çözmek için bir grup seçin", true);
        return;
    }
    pushUndo();
    std::vector<uint64_t> freed;
    for (SceneNode* g : groups)
        for (SceneNode* c : m_s.graph.ungroup(g)) freed.push_back(c->uid);
    m_s.selKind = SelectionKind::Node;
    m_s.selNodes = freed;
    m_s.selUid = freed.back();
    m_s.selAnchor = freed.front();
    markDocumentChanged();
    setStatus("Grup çözüldü");
}

void Application::moveNodes(const std::vector<uint64_t>& uids, uint64_t newParentUid, size_t index) {
    SceneNode* parent = newParentUid ? m_s.graph.findByUid(newParentUid) : m_s.graph.root();
    if (!parent) return;
    std::vector<SceneNode*> nodes;
    for (uint64_t u : uids)
        if (SceneNode* n = m_s.graph.findByUid(u)) nodes.push_back(n);
    nodes = m_s.graph.topmost(nodes);
    // Hedefin kendisi ya da atası taşınamaz (döngü); hepsi geçersizse geri al adımı da açma.
    std::erase_if(nodes, [&](SceneNode* n) { return n == parent || SceneGraph::isAncestor(*n, *parent); });
    if (nodes.empty()) return;
    pushUndo();
    m_s.graph.move(nodes, parent, index);
    markDocumentChanged();
    setStatus(nodes.size() == 1 ? "Taşındı: " + nodes.front()->name : std::to_string(nodes.size()) + " nesne taşındı");
}

void Application::beginRename() {
    if (SceneNode* n = selectedNode()) {
        m_s.renamingUid = n->uid;
        m_s.renameBuf = n->name;
        m_s.renameFocus = true;
    } else if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 &&
               m_s.selLight < static_cast<int>(m_s.lights.size())) {
        m_s.renamingLight = m_s.selLight;
        m_s.renameBuf = m_s.lights[static_cast<size_t>(m_s.selLight)].name;
        m_s.renameFocus = true;
    }
}

void Application::frameAll() {
    AABB box = m_s.graph.worldBounds();
    if (box.pMin.x > box.pMax.x) return;
    const float aspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.6f;
    m_s.camera.frame(box, aspect);
}

// Her nesne ayrı ayrı zemine iner (yığın değil): en alt noktası y = 0 olur.
// Dünya uzayındaki kayma, ebeveyn dönüşümünün tersiyle yerel uzaya çevrilir.
void Application::placeSelectionOnGround() {
    bool pushed = false;
    int count = 0;
    for (SceneNode* n : selectedNodes()) {
        const AABB box = SceneGraph::nodeWorldBounds(*n);
        if (box.pMin.x > box.pMax.x || std::abs(box.pMin.y) < 1e-6f) continue;
        if (!pushed) pushUndo();
        pushed = true;
        const Mat4f world = Transform::translate(Vec3f(0.0f, -box.pMin.y, 0.0f)).matrix() * n->worldTransform().matrix();
        n->localTransform = Transform(n->parent->worldTransform().inverseMatrix() * world);
        ++count;
    }
    if (!pushed) return;
    markDocumentChanged();
    setStatus(count == 1 ? "Zemine oturtuldu" : std::to_string(count) + " nesne zemine oturtuldu");
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
    const std::vector<SceneNode*> nodes = selectedNodes();
    if (nodes.empty() || !m_s.clipboardMaterial) {
        setStatus(m_s.clipboardMaterial ? "Yapıştırmak için bir parça seçin" : "Panoda malzeme yok", true);
        return;
    }
    pushUndo();
    // Tek kopya tüm seçime: yapıştırılan parçalar aynı malzemeyi paylaşır.
    auto mat = cloneMaterial(*m_s.clipboardMaterial);
    for (SceneNode* n : nodes) forEachMesh(*n, [&](SceneNode& s) { s.material = mat; });
    markDocumentChanged();
    setStatus(nodes.size() == 1 ? "Malzeme yapıştırıldı → " + nodes.front()->name
                                : "Malzeme yapıştırıldı → " + std::to_string(nodes.size()) + " nesne");
}

} // namespace photon
