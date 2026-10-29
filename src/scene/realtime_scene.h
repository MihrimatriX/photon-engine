// realtime_scene.h — Gerçek zamanlı (oyun motoru tarzı) viewport modunun CPU tarafı.
//
// Gerçek zamanlı mod ışın izlemez: geometriyi GPU'da rasterleştirir, her pikseli bir kez
// gölgelendirir (forward PBR). Işın izleyiciyle aynı sahneyi göstermesi için girdileri aynı
// kaynaktan alır; bu dosyadaki hazırlık GL'den bağımsızdır ve testlerle doğrulanır:
//   - Ortam: aynı EnvironmentLight'tan (HDRI, prosedürel stüdyo, fiziksel gök; döndürme ve
//     yoğunluk dahil) dünya yönlerinde equirect olarak pişirilir. Güneş diski haritaya girmez,
//     ayrı ve gölgeli bir yönlü ışık olur (0.5°'lik disk mip zincirinde bulanık bir lekeye dönerdi).
//     Güneşi görüntünün içinde olan HDRI'lerde (açık gök) güneş lekesi ayıklanır ve aynı şekilde
//     gölgeli yönlü ışık olur (ışın izleyici onu önem örneklemesiyle bulup keskin gölge düşürür).
//     Zemin açıksa alt yarıküre zeminin kendisiyle değiştirilir (aydınlanmış zemin rengi): krom
//     yansımalarında ve alttan gelen dolaylı ışıkta ışın izleyicideki gibi zemin görünür.
//   - Difüz ortam ışığı: ışınım (irradiance) 9 katsayılı küresel harmoniklerle (Ramamoorthi &
//     Hanrahan 2001). Parlak/pürüzlü yansımalar haritanın mip zincirinden, "split-sum" (Karis 2013).
//   - Işıklar: LightDesc'lerin GPU listesi, aynı radyometrik anlam (nokta/spot: şiddet, alan:
//     radyans × alan, yönlü: ışınım) ve Kelvin rengi. Sahneye en çok ışık veren en çok 4 ışık
//     gölge haritası alır (stüdyonun ana, dolgu ve kontur softbox'larının hepsi gölge düşürür).
//   - Ton eşleme: viewport'un operatörü (AgX, ACES, PBR Neutral…) 3B LUT'a pişirilir; GPU aynı
//     eğriyi birebir uygular (shader'da yeniden yazılan eğri zamanla ayrışırdı).
#pragma once

#include "core/color/spectrum.h"
#include "core/image/tone_mapping.h"
#include "core/math/aabb.h"
#include "core/math/mat.h"
#include "core/math/vec.h"
#include "scene/document.h"

#include <cstdint>
#include <vector>

namespace photon {

class EnvironmentLight;

struct RealtimeEnvironment {
    int width = 0, height = 0;
    std::vector<float> rgb;       ///< doğrusal radyans, satır 0 = zenit (v = 0); u, directionToEquirect(·, 0)
    float sh[9][3] = {};          ///< ışınım SH9 (konvolüsyon dahil): E(n) = Σ sh[i]·Y_i(n)
    bool hasSun = false;
    Vec3f sunDirection{0, 1, 0};  ///< güneşe doğru
    Color3f sunIrradiance;        ///< güneşe dik yüzeye düşen ışınım (disk radyansı × katı açı)
    float sunAngularRadius = 0.0f; ///< diskin açısal yarıçapı (radyan): gölgenin yumuşaklığı
};

/// Zemin açıkken alt yarıküre: @p groundAlbedo rengi, üst yarıkürenin (ve güneşin) zemine
/// verdiği ışınımla aydınlanmış difüz yüzey olarak görünür. groundAlbedo < 0 → zemin yok.
RealtimeEnvironment bakeRealtimeEnvironment(const EnvironmentLight& env, int width = 1024,
                                            const Color3f& groundAlbedo = Color3f(-1.0f));

/// HDRI'deki güneş lekesini ayıklar (yalnız @p env'de güneş yokken): en parlak teksel ortalamanın
/// en az 300 katıysa ve çevresindeki parlak bölge küçükse (< ~3.5°, stüdyo softbox'ları değil),
/// bölgenin enerjisi yönlü ışığa taşınır, teksellerin yerine güneşin çevresindeki gök konur.
/// Ayıklandıysa true. (bakeRealtimeEnvironment çağırır; testler için ayrı.)
bool extractSunFromMap(RealtimeEnvironment& env);

/// SH9 ışınım katsayıları (equirect, satır 0 = zenit) — E(n) için.
void projectIrradianceSH(const std::vector<float>& rgb, int width, int height, float out[9][3]);
/// n normalli yüzeye düşen ışınım; difüz radyans = albedo · E / π.
Color3f evalIrradianceSH(const float sh[9][3], const Vec3f& n);

struct RealtimeLight {
    enum class Type { Point = 0, Spot = 1, Area = 2, Directional = 3 };
    Type type = Type::Point;
    Vec3f position;
    /// Spot/alan: ışığın baktığı yön; yönlü: ışığın GİTTİĞİ yön.
    Vec3f direction{0, -1, 0};
    /// Nokta/spot: şiddet I (W/sr). Alan: L·A (normal yönündeki şiddet; I(θ) = L·A·|cosθ|,
    /// iki yüzlü). Yönlü: ışınım E.
    Color3f intensity;
    float cosInner = -2.0f, cosOuter = -2.0f; ///< spot konisi (−2: koni yok)
    /// Konumlu: kaynağın eşdeğer küre yarıçapı (parlamanın genişliği, yumuşak gölge). Yönlü: açısal
    /// yarıçap (radyan; güneş ~0.0047, elle eklenen yönlü ışık 0 = keskin gölge, ışın izleyicideki gibi).
    float radius = 0.0f;
    bool castsShadow = false;
};

/// Etkin ışıklar (+ fiziksel gök güneşi), sahne merkezine verdikleri ışınıma göre sıralı. Gölgeyi en
/// güçlü @p maxShadows ışık alır; en güçlünün %5'inden sönükler (etkisi görünmez) ve kutunun içindeki
/// ışıklar almaz (gölge haritası ışığın önündeki koniyi görmeli).
std::vector<RealtimeLight> packRealtimeLights(const std::vector<LightDesc>& descs, const RealtimeEnvironment& env,
                                              const AABB& sceneBounds, size_t maxLights = 16, size_t maxShadows = 4);

/// @p p noktasında @p n normalli yüzeye ışıkların verdiği ışınım (gölgesiz). Zeminin ışıklardan
/// aldığı payı (alt yarıküre radyansı) hesaplamak için.
Color3f realtimeIrradiance(const std::vector<RealtimeLight>& lights, const Vec3f& p, const Vec3f& n);

/// Gölge haritasının izdüşümü. Kapsanan bölge: sahne kutusu + kutunun zemine (kutunun tabanı) düşen
/// gölge izi (alçak ışıkta gölgeler uzun; kapsanmasa kadraj sınırında sert bir çizgiyle kesilirdi).
/// Yönlü ışık: bu bölgenin sınır küresine oturan ortografik; konumlu: ışıktan küreye bakan perspektif.
struct ShadowProjection {
    Mat4f viewProj;          ///< bölge tamamen NDC [−1, 1]³ içinde
    bool ortho = false;
    float zNear = 0.0f, zFar = 1.0f;
    /// Harita uv'si (0..1) / dünya birimi: ortografikte sabit (1/genişlik); perspektifte alıcının
    /// ışığa uzaklığına bölünür (1/(2·tan(fov/2))). PCSS yarı gölge genişliğini uv'ye çevirir.
    float uvPerWorld = 1.0f;
};
ShadowProjection shadowProjection(const RealtimeLight& light, const AABB& sceneBounds);
/// Gölgenin düşebileceği bölge (yukarıdaki kutu + zemindeki gölge izi).
AABB shadowReceiverBounds(const RealtimeLight& light, const AABB& sceneBounds);

/// Ton eşleme LUT'u: size³ RGB8, ekran (sRGB kodlu) değerleri. Giriş: pozlanmış HDR, kanal başına
/// log2 alanında [minLog2, maxLog2] → [0, 1] (shaper); dizin r + g·size + b·size².
struct ToneLut {
    int size = 0;
    float minLog2 = -14.0f, maxLog2 = 8.0f;
    std::vector<uint8_t> rgb;
};
/// 64³: kanallar arası min/max kullanan operatörlerde (PBR Neutral) bile sapma ≤ ~4/255.
ToneLut buildToneLut(ToneMapOperator op, int size = 64);
/// GPU'nun üçdoğrusal örneklemesinin CPU karşılığı (testler için).
Color3f sampleToneLut(const ToneLut& lut, const Color3f& exposedHdr);

} // namespace photon
