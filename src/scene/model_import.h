// model_import.h — Model dosyasını (OBJ, glTF, FBX, Alembic) sahne ağacına bir grup düğümü
// olarak yükleme.
#pragma once

#include "scene/scene_node.h"
#include <memory>
#include <string>
#include <vector>

namespace photon {

/// İçe aktarma seçenekleri (içe aktarma penceresi; proje yeniden yüklemede varsayılanlar).
struct ModelImportOptions {
    /// Ek ölçek: dosyanın kendi birim dönüşümünün üstüne. 0.01 = "dosya santimetre", 0.001 = "mm".
    float scale = 1.0f;
    /// Z yukarı → Y yukarı (CAD ve bazı OBJ dışa aktarıcıları). FBX kendi eksenini bildirir.
    bool zUp = false;
    /// Alembic: okunacak zaman (saniye); < 0 → ilk kare.
    double alembicTime = -1.0;
};

/// Desteklenen model uzantısı mı (.obj, .gltf, .glb, .fbx, .abc)?
bool isSupportedModelFile(const std::string& path);

/// Dosya diyaloğu filtresi için desteklenen uzantılar ("*.obj;*.fbx;...").
std::string supportedModelPatterns();

/// Dosyayı okuyup bir grup düğümü döndürür. OBJ/glTF: her mesh bir çocuk ("Parça N").
/// FBX/Alembic: dosyadaki düğüm hiyerarşisi, adlar ve yerel dönüşümler korunur; aynı mesh'i
/// kullanan düğümler geometriyi paylaşır. Mesh düğümlerinin sourceIndex'i dosyadaki kararlı
/// sıradır (proje yeniden yüklemede eşleme). Malzemesi olmayan parçalar nötr gri plastik alır.
/// Başarısızsa nullptr ve @p error dolar; @p warnings (varsa) kayıpsız olmayan dönüşümleri sayar.
std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error = nullptr,
                                           const ModelImportOptions& options = {},
                                           std::vector<std::string>* warnings = nullptr);

} // namespace photon
