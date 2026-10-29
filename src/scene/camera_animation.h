// camera_animation.h — Kamera animasyonu: zamana bağlı anahtar kareler ve ara değerleme.
//
// Bir iz (track), kameranın göz noktası, baktığı nokta, görüş açısı ve odak değerlerini
// anahtar karelerde tutar. Aradaki kareler zamana göre parametrelenmiş kübik Hermite eğrisiyle
// bulunur. Teğetler Catmull-Rom kuralıyla komşu anahtarlardan gelir:
//   m_i = (p_{i+1} − p_{i−1}) / (t_{i+1} − t_{i−1})
// Bu, düzensiz aralıklı anahtarlarda bile hızı sürekli (C¹) tutar; ilk ve son anahtarda teğet
// sıfırdır, yani hareket yumuşak başlar ve yumuşak durur ("ease in/out"). Doğrusal mod da var
// (mimari yürüyüşlerde sabit hız için).
//
// Kaynaklar: uygulamada kullanıcının eklediği anahtarlar, kayıtlı kameralardan üretilen tur ve
// FBX'ten gelen kamera animasyonu (her kare örneklenir). Proje ve paket dosyasında saklanır.
#pragma once

#include "core/math/vec.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace photon {

struct CameraKey {
    float time = 0.0f;           ///< saniye
    Vec3f eye{0.0f, 1.0f, 5.0f};
    Vec3f target{0.0f, 0.5f, 0.0f};
    float fovDeg = 35.0f;        ///< dikey görüş açısı
    float focusDistance = 0.0f;  ///< 0 = hedefe uzaklık (göz–hedef)
    float fStop = 0.0f;          ///< 0 = alan derinliği kapalı
};

struct CameraTrack {
    std::string name = "Kamera animasyonu";
    std::vector<CameraKey> keys; ///< zamana göre sıralı
    bool smooth = true;          ///< false: doğrusal (sabit hız)
    float fps = 30.0f;           ///< render kare hızı

    float duration() const { return keys.empty() ? 0.0f : keys.back().time; }
    bool empty() const { return keys.empty(); }

    /// @p t anındaki kamera. İlk anahtardan önce ilk, sondan sonra son anahtar döner.
    CameraKey evaluate(float t) const;

    /// Anahtar ekler; aynı zamanda (±1 ms) bir anahtar varsa onu değiştirir. Sırayı korur.
    void setKey(const CameraKey& key);
    /// @p t'ye en yakın anahtarı siler (1 ms içinde değilse hiçbir şey yapmaz).
    bool removeKeyAt(float t);
    void sortKeys();
    /// Toplam sayıdaki kare: duration·fps + 1 (en az 1).
    int frameCount() const;
};

nlohmann::json cameraTrackToJson(const CameraTrack& track);
CameraTrack cameraTrackFromJson(const nlohmann::json& j);

} // namespace photon
