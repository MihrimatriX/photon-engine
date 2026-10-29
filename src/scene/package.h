// package.h — PhotonEngine'in kendi kendine yeten proje paketi (.photonpack).
//
// .photon proje dosyası JSON'dur ve dokuları, HDRI'ları, IES profillerini ve içe aktarılan model
// dosyalarını yolla gösterir: başka bir makineye taşınınca kopyalanmayan her dosya kaybolur.
// Paket tek bir ZIP dosyasında her şeyi taşır (KeyShot .ksp, Marmoset .tbscene gibi):
//
//   project.photon        proje (JSON); yollar paket içi adlar
//   geometry/N.mesh       her mesh ikili (mesh_binary.h); paylaşılan geometri bir kez
//   assets/K_ad.uzantı    dokular, HDRI, IES (aynı dosya bir kez)
//
// Paketteki geometri kaynak modelden (FBX, OBJ, Alembic) bağımsızdır: paket o dosyalar ve
// içe aktarıcının sürümü olmadan da aynı sahneyi açar. Kamera animasyonları, ışıklar, ortam ve
// render ayarları proje JSON'undadır.
//
// Açma: paket kullanıcı veri klasöründeki önbelleğe (packages/<karma>) çıkarılır ve oradan
// yüklenir; aynı paket ikinci kez açılınca yeniden çıkarılmaz. Okuma güvenilmeyen dosyaya
// karşı denetlidir (zip.h: güvensiz yol, CRC, boyut sınırları).
#pragma once

#include "scene/project_io.h"

#include <string>
#include <vector>

namespace photon {

/// .photonpack uzantılı mı?
bool isPackageFile(const std::string& path);

/// Sahneyi pakete yazar (geçici dosya → yeniden adlandır). Bulunamayan dosyalar @p warnings'e
/// yazılır ve pakete alınmaz.
bool savePackage(const std::string& path, const SceneGraph& graph, const ProjectData& data,
                 std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);

/// Paketi açar ve yükler. @p extractDir boşsa kullanıcı veri klasöründeki paket önbelleği.
/// @p error, başarıda da uyarılarla dolabilir (loadProject gibi).
bool loadPackage(const std::string& path, SceneGraph& graph, ProjectData& data, std::string* error = nullptr,
                 const std::string& extractDir = {});

/// Paketin önbellekteki çıkarma klasörü (yol + boyut + değişme zamanı karması).
std::string packageCacheDir(const std::string& path);

} // namespace photon
