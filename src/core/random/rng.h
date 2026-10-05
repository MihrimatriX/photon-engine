// PCG32 sözde-rastgele sayı üreteci (O'Neill 2014, XSH-RR çıkışı). Hızlı, küçük durumlu
// ve tohumlanabilir; örnekleyiciler ve testler bunu kullanır.
#pragma once

#include <cstdint>
#include "core/math/vec.h"

namespace photon {

class RNG {
public:
    explicit RNG(uint64_t seed = 0x853c49e6748fea9bULL) {
        m_state = 0;
        m_inc = (seed << 1u) | 1u;
        uniformUint32();
        m_state += 0x853c49e6748fea9bULL;
        uniformUint32();
    }

    uint32_t uniformUint32() {
        uint64_t oldState = m_state;
        m_state = oldState * 6364136223846793005ULL + m_inc;
        uint32_t xorShifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldState >> 59u);
        return (xorShifted >> rot) | (xorShifted << ((0u - rot) & 31));
    }

    /// [0, 1) aralığında düzgün float. Üstteki 24 biti alıp 2^-24 ile çarparız:
    /// sonuç k·2^-24 (k < 2^24) olur ve float'ta TAM temsil edilir, en büyük değer
    /// 1 - 2^-24 < 1. Eski u32 / 2^32 bölmesi yuvarlamayla 1.0 verebiliyordu
    /// (ör. 0xFFFFFFFF → 1.0f); bu da [0,1) varsayan örnekleyicileri bozar (core-11).
    float uniformFloat() {
        return static_cast<float>(uniformUint32() >> 8) * 0x1p-24f;
    }

    Vec2f uniformFloat2D() {
        return {uniformFloat(), uniformFloat()};
    }

private:
    uint64_t m_state;
    uint64_t m_inc;
};

} // namespace photon
