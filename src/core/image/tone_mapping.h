// tone_mapping.h — HDR (sınırsız parlaklık) görüntüyü ekranın [0,1] aralığına sıkıştırma.
//
// Path tracer fiziksel radyans üretir: güneşli bir pencere 1000, gölge 0.01 olabilir.
// Ekran yalnız 0..1 gösterebildiği için bir "ton eşleme" eğrisi gerekir.
// Sıra: pozlama (EV, 2^EV ile çarpım) → eğri → sRGB kodlama (OETF) → 8 bit.
#pragma once

#include "core/color/spectrum.h"

namespace photon {

/// Desteklenen ton eşleme operatörleri. Sayısal değerler proje dosyalarında
/// saklanır; yeni operatörler yalnız SONA eklenir.
enum class ToneMapOperator {
    Reinhard,
    ReinhardExtended,
    ACES,
    Filmic,
    AgX,        ///< Blender 4'ün varsayılanı: parlak renkleri doğal biçimde beyaza doyurur.
    PBRNeutral, ///< Khronos PBR Neutral: ürün görseli için, taban renklerini korur.
    Linear,     ///< Eğri yok, yalnız kırpma: ölçüm ve karşılaştırma için.
};

/// Kullanıcıya gösterilen ad (Türkçe).
const char* toneMapName(ToneMapOperator op);

/// Reinhard: x / (1 + x)
Color3f toneMapReinhard(const Color3f& hdr);

/// Beyaz noktalı Reinhard: maxWhite değeri tam beyaza eşlenir.
Color3f toneMapReinhardExtended(const Color3f& hdr, float maxWhite);

/// ACES Filmic yaklaşımı (Narkowicz 2015).
Color3f toneMapACES(const Color3f& hdr);

/// Uncharted 2 Filmic (Hable).
Color3f toneMapFilmic(const Color3f& hdr);

/// AgX (Troy Sobotka), Benjamin Wrensch'in polinom yaklaşımı. Çıktı ekran-doğrusal.
Color3f toneMapAgX(const Color3f& hdr);

/// Khronos PBR Neutral (Emmett Lalish, 2024). Çıktı ekran-doğrusal.
Color3f toneMapPBRNeutral(const Color3f& hdr);

/// EV stop cinsinden pozlama: 2^exposureEV ile ölçekler (0 = değişmez, +1 = iki kat parlak).
Color3f toneMapExposure(const Color3f& hdr, float exposureEV);

/// Yalnız pozlama ve operatör eğrisi. Çıktı [0, 1] ekran-doğrusal, henüz sRGB
/// kodlanmamış. Negatif ve NaN girdiler 0'a eşlenir.
Color3f toneMapLinear(const Color3f& hdr, ToneMapOperator op, float exposureEV = 0.0f);

/// Tam ekran dönüşümü: pozlama → operatör → sRGB kodlama (OETF).
/// Çıktı 8-bit PNG'ye ya da viewport'a giden [0, 1] değeridir.
Color3f toneMap(const Color3f& hdr, ToneMapOperator op, float exposureEV = 0.0f);

} // namespace photon
