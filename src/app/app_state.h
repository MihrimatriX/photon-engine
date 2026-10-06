// app_state.h — Uygulamanın tüm durumu: belge (sahne, ışıklar, ortam), görünüm
// (kamera, render ayarları), seçim, kütüphaneler ve arayüz bayrakları.
//
// "Belge" kaydedilen ve geri alınabilen kısımdır (DocSnapshot). Görünüm ve arayüz
// durumu geri al geçmişine girmez (kamera hariç tutulur: KeyShot'ta da kamera
// hareketi geri alınmaz, kamera kayıtları ayrıdır).
#pragma once

#include "app/render_controller.h"
#include "app/raster_view.h"
#include "app/thumbnails.h"
#include "scene/scene_graph.h"
#include "scene/material_library.h"
#include "scene/document.h"
#include "scene/undo_stack.h"
#include "ui/orbit_camera.h"
#include "engine/render_settings.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace photon {

/// Geri al için belgenin tam kopyası.
struct DocSnapshot {
    std::unique_ptr<SceneNode> root;
    std::vector<LightDesc> lights;
    EnvironmentDesc environment;
};

enum class SelectionKind { None, Node, Light, Ground };

/// Kütüphanedeki bir ortam (HDRI dosyası ya da prosedürel gökyüzü).
struct EnvAsset {
    std::string id;
    std::string name;
    std::string path;  ///< Boş = prosedürel
    Color3f zenith{0.75f, 0.8f, 0.95f};
    Color3f horizon{0.55f, 0.55f, 0.58f};
};

/// Son render (dosyaya) ayarları.
struct OutputSettings {
    int width = 1920;
    int height = 1080;
    int spp = 256;
    float maxSeconds = 0.0f;       ///< > 0 ise süre sınırı (örnek sayısından önce dolarsa durur)
    int format = 0;                ///< 0 PNG, 1 JPEG, 2 EXR
    std::string path;
    bool denoise = true;
    int turntableFrames = 36;
    int turntableSpp = 32;
};

/// Arka planda çalışan son render / turntable işi.
struct FinalRenderJob {
    std::atomic<bool> active{false};
    std::atomic<bool> cancel{false};
    std::atomic<int> spp{0};
    std::atomic<int> targetSpp{0};
    std::atomic<int> frame{0};
    std::atomic<int> frames{0};
    std::mutex mutex;              ///< Aşağıdaki alanları korur
    std::shared_ptr<const Image> preview;
    bool previewNew = false;
    std::string status;
    std::string outputPath;
    double seconds = 0.0;
    bool succeeded = false;
    std::thread thread;
};

struct AppState {
    // ── Belge ──
    SceneGraph graph;
    std::vector<LightDesc> lights;
    EnvironmentDesc environment;
    std::string projectPath;
    bool documentDirty = false;
    UndoStack<DocSnapshot> undo;

    // ── Görünüm ──
    OrbitCamera camera;
    RenderSettings settings;
    float resolutionScale = 1.0f;  ///< Viewport render çözünürlüğü / ekran pikseli
    int previewSpp = 0;            ///< 0 = sınırsız
    bool denoise = true;
    bool showGrid = false;
    ViewMode viewMode = ViewMode::Render; ///< Render/Kil: ışın izleme; Katı/Tel kafes/Normaller: GPU
    bool wireOverlay = false;             ///< Render ve Kil'in üstüne tel kafes bindir

    // ── Kayıtlı kameralar (projeyle saklanır) ──
    struct SavedCamera {
        std::string name;
        OrbitCamera cam;
    };
    std::vector<SavedCamera> savedCameras;

    // ── Seçim ──
    // Çoklu seçim: selNodes tüm seçili düğümler; selUid bunlardan "birincil" olanı
    // (son tıklanan). Özellikler paneli ve malzeme düzenleme birincili gösterir;
    // sil/çoğalt/gizle/grupla/taşı hepsine uygulanır.
    SelectionKind selKind = SelectionKind::None;
    uint64_t selUid = 0;
    std::vector<uint64_t> selNodes;
    uint64_t selAnchor = 0;                ///< Shift ile aralık seçiminin başlangıcı
    int selLight = -1;
    uint64_t hoverUid = 0;                 ///< İmlecin altındaki parça (vurgulama)
    std::shared_ptr<Material> clipboardMaterial; ///< Malzeme kopyala / yapıştır

    // ── Sahne paneli ──
    std::string sceneFilter;               ///< Ada göre süzme
    uint64_t renamingUid = 0;              ///< Yeniden adlandırılan düğüm (0 = yok)
    int renamingLight = -1;                ///< Yeniden adlandırılan ışık (-1 = yok)
    std::string renameBuf;
    bool renameFocus = false;              ///< Ad kutusuna yalnız ilk karede odak ver
    std::vector<uint64_t> treeOrder;       ///< Son karede görünen satırlar (Shift aralığı, ok tuşları)

    // ── Kütüphaneler ──
    std::string assetsRoot;
    std::string userDir;           ///< %APPDATA%/PhotonEngine
    MaterialLibrary materials;
    std::vector<StudioPreset> studios;
    std::vector<EnvAsset> environments;
    std::vector<std::string> textures;
    std::vector<std::string> models;
    EnvironmentCache envCache;
    ThumbnailBaker thumbs;

    // ── Render ──
    RenderController viewport;
    std::shared_ptr<Scene> compiled;  ///< Son derlenen sahne (seçim ışınları için)
    bool sceneDirty = true;           ///< Bir sonraki karede yeniden derle
    bool sceneDirtyInteractive = false;
    OutputSettings output;
    FinalRenderJob finalJob;

    // ── Arayüz ──
    bool showRenderDialog = false;
    bool showAbout = false;
    bool showShortcuts = false;
    int leftTab = 0;
    int rightTab = 0;
    int gizmoOp = 0;               ///< 0 yok, 1 taşı, 2 döndür, 3 ölçekle
    bool gizmoLocal = false;       ///< Tutamaç eksenleri: nesnenin kendi eksenleri / dünya
    bool snap = false;             ///< Adımlı taşı/döndür/ölçekle (Ctrl basılıyken tersine döner)
    std::string librarySearch;
    std::string statusText;
    float statusTimer = 0.0f;
    bool statusError = false;
    float fps = 0.0f;
    float dpiScale = 1.0f;
    int viewportPxW = 0;
    int viewportPxH = 0;
    bool pickFocus = false;        ///< Viewport'ta tıklayınca odak mesafesini ayarla
};

} // namespace photon
