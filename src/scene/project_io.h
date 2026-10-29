// project_io.h — .photon proje dosyası (JSON) okuma/yazma.
//
// Proje, sahnenin tam halini saklar: düğüm ağacı (dönüşüm, görünürlük, malzeme),
// içe aktarılan modellerin kaynak yolları, yerinde oluşturulmuş geometri, ışıklar,
// ortam ve uygulamanın kendi kamera/render ayarları (opak JSON olarak).
// Yollar mümkünse proje dosyasına göre göreli yazılır; böylece klasör taşınabilir.
#pragma once

#include "scene/scene_graph.h"
#include "scene/document.h"
#include "scene/camera_animation.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>

namespace photon {

struct ProjectData {
    nlohmann::json camera = nlohmann::json::object();  ///< Uygulama tanımlı
    nlohmann::json render = nlohmann::json::object();  ///< Uygulama tanımlı
    EnvironmentDesc environment;
    std::vector<LightDesc> lights;
    std::vector<CameraTrack> cameraTracks; ///< Kamera animasyonları (anahtar kareler)
};

/// Paketleme (package.cpp) için yazım kancaları. mapAsset: doku/HDRI/IES yolunu paket içi ada
/// çevirir (dosyayı pakete almayı üstlenir). storeMesh: doluysa her mesh ikili olarak saklanır,
/// adı ("geometry/N.mesh") kaydedilir ve kaynak dosya bağı yazılmaz (paket kendi kendine yeter).
struct ProjectWriteHooks {
    std::function<std::string(const std::string& path)> mapAsset;
    std::function<std::string(const TriangleMesh& mesh)> storeMesh;
};

/// Proje JSON metni. @p baseDir göreli yolların kökü; @p hooks paketleme için (normalde nullptr).
std::string projectJsonText(const SceneGraph& graph, const ProjectData& data, const std::string& baseDir,
                            const ProjectWriteHooks* hooks = nullptr);

/// Projeyi yaz. Önce geçici dosyaya yazar, sonra yerine taşır (yarım dosya kalmaz).
bool saveProject(const std::string& path, const SceneGraph& graph, const ProjectData& data,
                 std::string* error = nullptr);

/// Projeyi oku; @p graph temizlenip yeniden kurulur.
bool loadProject(const std::string& path, SceneGraph& graph, ProjectData& data,
                 std::string* error = nullptr);

/// Bellekteki JSON metninden yükle (testler için). @p baseDir göreli yolların kökü.
bool loadProjectJson(const std::string& text, const std::string& baseDir, SceneGraph& graph,
                     ProjectData& data, std::string* error = nullptr);

} // namespace photon
