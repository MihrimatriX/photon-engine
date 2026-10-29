// alembic_loader.h — Alembic (.abc, Ogawa) yükleyicisi: Maya, Houdini, Blender, Cinema 4D ve
// Marvelous Designer'ın "pişmiş geometri" (baked cache) biçimi. Animasyonlu bir arşivden seçilen
// zamandaki şekil okunur (varsayılan: ilk kare).
//
// Xform hiyerarşisi ve adlar korunur; PolyMesh ve SubD (kontrol kafesi) üçgenlenir; yüz kümeleri
// (face set — genelde DCC'deki malzeme atamaları) ayrı parçalara bölünür ki her birine
// kütüphaneden ayrı malzeme bırakılabilsin. Alembic'te malzeme ve birim bilgisi yoktur: birim,
// arşivi yazan uygulamadan tahmin edilir (Maya/C4D/3ds Max → santimetre) ve uyarı olarak bildirilir.
#pragma once

#include "io/imported_model.h"

#include <string>

namespace photon {

class AlembicLoader {
public:
    /// Derlemede Alembic desteği var mı (PHOTON_ENABLE_ALEMBIC ve bağımlılık bulundu)?
    static bool available();

    /// @p timeSeconds < 0 → arşivin başlangıç zamanı. @p unitMeters > 0 → 1 birim kaç metre
    /// (0 = yazan uygulamadan tahmin). Başarısızsa boş model ve @p error dolar.
    static ImportedModel load(const std::string& path, std::string* error = nullptr, double timeSeconds = -1.0,
                              float unitMeters = 0.0f);
};

} // namespace photon
