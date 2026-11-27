// model_import.h — Model dosyasını (OBJ, glTF, FBX, Alembic, OpenUSD) sahne ağacına bir grup
// düğümü olarak yükleme.
#pragma once

#include "scene/camera_animation.h"
#include "scene/document.h"
#include "scene/scene_node.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace photon {

/// İçe aktarma seçenekleri (içe aktarma penceresi; proje yeniden yüklemede varsayılanlar).
struct ModelImportOptions {
    /// Dosyadaki 1 birimin metre karşılığı; 0 = otomatik. Yalnız birim bilgisi taşımayan
    /// biçimlerde kullanılır: OBJ (otomatik = metre), Alembic (otomatik = yazan uygulamadan
    /// tahmin) ve metersPerUnit yazılmamış USD (otomatik = santimetre, USD'nin varsayılanı).
    /// FBX, glTF ve birimi yazılmış USD kendi birimlerini bildirir.
    float unitMeters = 0.0f;
    /// Ek ölçek (her biçimde, birim dönüşümünün üstüne).
    float scale = 1.0f;
    /// Z yukarı → Y yukarı (CAD ve bazı OBJ dışa aktarıcıları). FBX kendi eksenini bildirir.
    bool zUp = false;
    /// Alembic: okunacak zaman (saniye); < 0 → ilk kare.
    double alembicTime = -1.0;
};

/// Model dosyasından gelen kameralar, ışıklar ve ortam (FBX: 3ds Max, Maya, Blender; USD).
/// İçe aktarılan grubun YEREL uzayındadır: uygulama modeli yerleştirdikten (zemine oturtma,
/// bırakılan nokta) sonra grubun dünya dönüşümüyle transformImportExtras() çağırır; böylece
/// DCC'deki kadraj ve ışık düzeni modelle birlikte taşınır.
struct ImportExtras {
    std::vector<LightDesc> lights;
    /// Her kamera bir iz: tek anahtarlı = sabit kamera (kayıtlı kamera olur), çok anahtarlı =
    /// her karede örneklenmiş animasyon.
    std::vector<CameraTrack> cameras;
    /// USD kubbe ışığı (DomeLight): HDRI, şiddet ve dönüş. Yalnız Image türü alanları anlamlıdır.
    std::optional<EnvironmentDesc> environment;
    bool empty() const { return lights.empty() && cameras.empty() && !environment; }
};

/// Grubun dünya dönüşümünü kameralara ve ışıklara uygular. Nokta/spot şiddeti ölçeğin karesiyle
/// çarpılır (aynı uzaklık oranında aynı aydınlık); alan ışığı boyutları ölçeklenir. Ortamın dönüşüne
/// grubun Y ekseni etrafındaki dönüşü eklenir.
void transformImportExtras(ImportExtras& extras, const Transform& toWorld);

/// Desteklenen model uzantısı mı (.obj, .gltf, .glb, .fbx, .abc, .usd, .usda, .usdc, .usdz)?
bool isSupportedModelFile(const std::string& path);

/// Dosya diyaloğu filtresi için desteklenen uzantılar ("*.obj;*.fbx;...").
std::string supportedModelPatterns();

/// Dosyayı okuyup bir grup düğümü döndürür. OBJ/glTF: her mesh bir çocuk ("Parça N").
/// FBX/Alembic: dosyadaki düğüm hiyerarşisi, adlar ve yerel dönüşümler korunur; aynı mesh'i
/// kullanan düğümler geometriyi paylaşır. Mesh düğümlerinin sourceIndex'i dosyadaki kararlı
/// sıradır (proje yeniden yüklemede eşleme). Malzemesi olmayan parçalar nötr gri plastik alır.
/// Başarısızsa nullptr ve @p error dolar; @p warnings (varsa) kayıpsız olmayan dönüşümleri sayar.
/// @p extras doluysa dosyadaki kameralar ve ışıklar (FBX, USD) ve kubbe ortamı (USD) da okunur.
std::unique_ptr<SceneNode> importModelFile(const std::string& path, std::string* error = nullptr,
                                           const ModelImportOptions& options = {},
                                           std::vector<std::string>* warnings = nullptr,
                                           ImportExtras* extras = nullptr);

} // namespace photon
