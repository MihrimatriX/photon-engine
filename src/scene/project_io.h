// project_io.h — .photon proje dosyası (JSON) okuma/yazma.
//
// Proje, sahnenin tam halini saklar: düğüm ağacı (dönüşüm, görünürlük, malzeme),
// içe aktarılan modellerin kaynak yolları, yerinde oluşturulmuş geometri, ışıklar,
// ortam ve uygulamanın kendi kamera/render ayarları (opak JSON olarak).
// Yollar mümkünse proje dosyasına göre göreli yazılır; böylece klasör taşınabilir.
#pragma once

#include "scene/scene_graph.h"
#include "scene/document.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace photon {

struct ProjectData {
    nlohmann::json camera = nlohmann::json::object();  ///< Uygulama tanımlı
    nlohmann::json render = nlohmann::json::object();  ///< Uygulama tanımlı
    EnvironmentDesc environment;
    std::vector<LightDesc> lights;
};

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
