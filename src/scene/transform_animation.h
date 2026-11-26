// transform_animation.h — Nesne dönüşüm anahtar kareleri (TransformTrack) ve hareket bulanıklığı.
// Kuaterniyon SLERP ve TRS ayrıştırma core/math/quat.h içindedir.
#pragma once

#include "core/math/quat.h"

#include <vector>

namespace photon {

/// Nesne yerel dönüşümünün tek bir anahtar karesi.
struct TransformKey {
    float time = 0.0f;
    Vec3f pos{0.0f, 0.0f, 0.0f};
    Vec3f rotEulerDeg{0.0f, 0.0f, 0.0f};
    Vec3f scale{1.0f, 1.0f, 1.0f};
};

/// Bir düğümün zaman boyunca dönüşüm anahtarları (nesne hareket bulanıklığı ve animasyon).
struct TransformTrack {
    std::vector<TransformKey> keys;
    bool smooth = true; ///< true: konum/ölçekte Catmull-Rom, dönüşte SLERP; false: doğrusal + SLERP

    bool empty() const { return keys.empty(); }
    float duration() const { return keys.empty() ? 0.0f : keys.back().time; }

    /// Aynı zamandaki anahtarı günceller ya da zaman sırasıyla ekler.
    void setKey(const TransformKey& k);
    bool removeKeyAt(float time, float eps = 1e-3f);

    /// @p t saniyesindeki yerel dönüşüm. Anahtar yoksa birim dönüşüm.
    Transform evaluate(float t) const;
};

} // namespace photon
