// dcc_convert.h — 3ds Max (.max), Maya (.ma/.mb) ve Blender (.blend) dosyalarını doğrudan açma.
//
// Bu biçimler ya kapalıdır (.max, .mb: yalnız Autodesk'in kendi yazılımı okuyabilir) ya da
// uygulamanın iç veri yapısıdır (.blend). Güvenilir tek yol, dosyayı yazan uygulamanın kendisidir:
// makinede kuruluysa onun komut satırı aracı arka planda (pencere açmadan) dosyayı açar ve FBX'e
// aktarır; PhotonEngine o FBX'i okur. Kullanıcı açısından sonuç "doğrudan açmak"tır:
//   Blender  : blender -b dosya.blend --factory-startup --python betik.py -- çıktı.fbx
//   Maya     : mayapy betik.py dosya.ma çıktı.fbx
//   3ds Max  : 3dsmaxbatch betik.ms -sceneFile dosya.max -mxsString out:çıktı.fbx
// Dışa aktarma ayarları DCC eklentileriyle aynıdır (dokular gömülü, kameralar, ışıklar, pişirilmiş
// animasyon, Y yukarı). Sonuç kullanıcı veri klasöründeki dcc_cache'te tutulur (yol + boyut +
// değişme zamanı karması): aynı dosya ikinci kez açılınca DCC yeniden çalıştırılmaz.
//
// Araç yeri: önce ortam değişkeni (PHOTON_BLENDER, PHOTON_MAYAPY, PHOTON_3DSMAXBATCH), sonra
// standart kurulum klasörlerinin en yeni sürümü (Windows: Program Files; macOS: /Applications;
// Linux: PATH ve /usr/autodesk).
#pragma once

#include <optional>
#include <string>

namespace photon {

enum class DccApp { Blender, Maya, Max };
const char* dccAppName(DccApp app);

/// .blend → Blender, .ma/.mb → Maya, .max → 3ds Max.
std::optional<DccApp> dccForFile(const std::string& path);
bool isDccNativeFile(const std::string& path);

/// Aracın yürütülebiliri; bulunamazsa boş.
std::string findDccExecutable(DccApp app);

/// Yerel dosyayı FBX'e çevirir (önbellekli). Başarısızsa boş döner ve @p error nedenini yazar
/// (araç yok, zaman aşımı, DCC'nin kendi hata çıktısının sonu).
std::string convertDccToFbx(const std::string& path, std::string* error = nullptr, int timeoutSeconds = 900);

} // namespace photon
