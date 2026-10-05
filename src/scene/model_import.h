// model_import.h — OBJ/glTF dosyasını sahne ağacına bir grup düğümü olarak yükleme.
#pragma once

#include "scene/scene_node.h"
#include <memory>
#include <string>

namespace photon {

/// Desteklenen model uzantısı mı (.obj, .gltf, .glb)?
bool isSupportedModelFile(const std::string& path);

/// Dosyayı okuyup bir grup düğümü döndürür: her mesh bir çocuk düğüm olur
/// (sourcePath grubu, sourceIndex çocuğun dosyadaki sırasını tutar). Malzemesi
/// olmayan parçalar nötr gri plastik alır. Başarısızsa nullptr ve @p error dolar.
std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error = nullptr);

} // namespace photon
