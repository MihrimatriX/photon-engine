// ltc_lut.cpp — Heitz 2016 GGX LTC1/LTC2 64×64 tablo üretici ve analitik dikdörtgen alan
// ışığı integratörü (ufuk kırpma + Baum 1989 vektör form faktörü).
#include "scene/ltc_lut.h"
#include "core/math/constants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photon {

namespace {

// ── GGX VNDF örnekleme & Smith yükseklik-ilintili (height-correlated) G2 ──

float ggxLambda(float cosT, float a) {
    const float c = std::clamp(cosT, 1e-5f, 1.0f);
    const float tan2 = (1.0f - c * c) / (c * c);
    return 0.5f * (-1.0f + std::sqrt(1.0f + a * a * tan2));
}

float ggxD(float NoH, float a) {
    const float a2 = a * a;
    const float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(PI * d * d, 1e-12f);
}

// V_Smith = G2 / (4 * NoV * NoL)
float ggxVSmith(float NoV, float NoL, float a) {
    const float a2 = a * a;
    const float gv = NoL * std::sqrt(NoV * NoV * (1.0f - a2) + a2);
    const float gl = NoV * std::sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / std::max(gv + gl, 1e-7f);
}

// Heitz 2018: GGX görünür normaller dağılımından (VNDF) yarı vektör örneklemesi.
Vec3f sampleGgxVndf(const Vec3f& V, float a, float u1, float u2) {
    const Vec3f Vh = Vec3f(a * V.x, a * V.y, V.z).normalized();
    const float lenSq = Vh.x * Vh.x + Vh.y * Vh.y;
    const Vec3f T1 = lenSq > 1e-10f ? Vec3f(-Vh.y, Vh.x, 0.0f) / std::sqrt(lenSq) : Vec3f(1.0f, 0.0f, 0.0f);
    const Vec3f T2 = Vh.cross(T1);

    const float r = std::sqrt(std::max(0.0f, u1));
    const float phi = TWO_PI * u2;
    const float t1 = r * std::cos(phi);
    float t2 = r * std::sin(phi);
    const float s = 0.5f * (1.0f + Vh.z);
    t2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - t1 * t1)) + s * t2;

    const Vec3f Nh = T1 * t1 + T2 * t2 + Vh * std::sqrt(std::max(0.0f, 1.0f - t1 * t1 - t2 * t2));
    return Vec3f(a * Nh.x, a * Nh.y, std::max(0.0f, Nh.z)).normalized();
}

// R2 düşük sapmalı 2B dizi (Roberts 2018)
inline void r2Sample(int i, float& u1, float& u2) {
    constexpr double g = 1.32471795724474602596;
    constexpr double a1 = 1.0 / g;
    constexpr double a2 = 1.0 / (g * g);
    const double n = static_cast<double>(i) + 0.5;
    u1 = static_cast<float>(std::fmod(0.5 + a1 * n, 1.0));
    u2 = static_cast<float>(std::fmod(0.5 + a2 * n, 1.0));
}

// Parametrik M(mx, my, tilt, skew) → 3×3 matris ve tersi.
// V = (+sinθ, 0, +cosθ) için speküler lob −X yönüne (R = (−sinθ, 0, cosθ)) eğilir.
struct LtcMat {
    float m[3][3];
    float inv[3][3];
    float detInv;
};

LtcMat makeLtcMat(float mx, float my, float tilt, float skew) {
    mx = std::max(mx, 1e-4f);
    my = std::max(my, 1e-4f);
    tilt = std::clamp(tilt, -1.55f, 1.55f);
    skew = std::clamp(skew, -3.0f, 3.0f);

    const float c = std::cos(tilt);
    const float s = std::sin(tilt);
    // M = Ry(-tilt) * [[mx, 0, skew], [0, my, 0], [0, 0, 1]]
    // Ry(-tilt) sütun 0: (c, 0, s), sütun 1: (0, 1, 0), sütun 2: (-s, 0, c)
    // Böylece tilt > 0 iken M*(0,0,1) = (skew*c - s, 0, skew*s + c) → −X, +Z bölgesinde.
    LtcMat out{};
    out.m[0][0] = c * mx;  out.m[0][1] = 0.0f; out.m[0][2] = c * skew - s;
    out.m[1][0] = 0.0f;    out.m[1][1] = my;   out.m[1][2] = 0.0f;
    out.m[2][0] = s * mx;  out.m[2][1] = 0.0f; out.m[2][2] = s * skew + c;

    // M⁻¹ = [[1/mx, 0, -skew/mx], [0, 1/my, 0], [0, 0, 1]] * Ry(+tilt)
    // Ry(+tilt) = [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    const float imx = 1.0f / mx;
    const float imy = 1.0f / my;
    out.inv[0][0] = (c + skew * s) * imx; out.inv[0][1] = 0.0f; out.inv[0][2] = (s - skew * c) * imx;
    out.inv[1][0] = 0.0f;                 out.inv[1][1] = imy;  out.inv[1][2] = 0.0f;
    out.inv[2][0] = -s;                   out.inv[2][1] = 0.0f; out.inv[2][2] = c;
    out.detInv = imx * imy;
    return out;
}

inline Vec3f mul3(const float m[3][3], const Vec3f& v) {
    return Vec3f(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                 m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                 m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}

float evalLtcPdf(const LtcMat& mat, const Vec3f& L) {
    const Vec3f lo = mul3(mat.inv, L);
    const float len2 = lo.lengthSquared();
    if (len2 < 1e-14f) return 0.0f;
    const float len = std::sqrt(len2);
    const float cos0 = lo.z / len;
    if (cos0 <= 0.0f) return 0.0f;
    return (cos0 * INV_PI) * (mat.detInv / (len2 * len));
}

Vec3f sampleCosHemisphere(float u1, float u2) {
    const float r = std::sqrt(std::max(0.0f, u1));
    const float phi = TWO_PI * u2;
    return Vec3f(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1)));
}

// Hedef normalize GGX küresel yoğunluğu: f_base(V, L) * max(0, L.z) / norm
float evalGgxTargetPdf(const Vec3f& V, const Vec3f& L, float a, float norm) {
    if (L.z <= 1e-6f || V.z <= 1e-6f || norm <= 1e-8f) return 0.0f;
    const Vec3f H = V + L;
    const float hl2 = H.lengthSquared();
    if (hl2 < 1e-12f) return 0.0f;
    const Vec3f hn = H / std::sqrt(hl2);
    if (hn.z <= 0.0f) return 0.0f;
    const float f = ggxD(hn.z, a) * ggxVSmith(V.z, L.z, a) * L.z;
    return f / norm;
}

// 4B Nelder-Mead simpleks optimizasyonu (Heitz 2016 L3 hatası için)
template <typename EvalFn>
std::array<float, 4> nelderMead4(const std::array<float, 4>& x0, const std::array<float, 4>& step,
                                 EvalFn&& eval, int maxIter) {
    constexpr int D = 4;
    std::array<std::array<float, D>, D + 1> p{};
    std::array<float, D + 1> f{};
    p[0] = x0;
    f[0] = eval(p[0]);
    for (int i = 0; i < D; ++i) {
        p[i + 1] = x0;
        p[i + 1][static_cast<size_t>(i)] += step[static_cast<size_t>(i)];
        f[i + 1] = eval(p[i + 1]);
    }

    for (int it = 0; it < maxIter; ++it) {
        // En iyi, ikinci en kötü ve en kötü köşeleri bul
        int best = 0, worst = 0, secondWorst = 0;
        for (int i = 1; i <= D; ++i) {
            if (f[i] < f[best]) best = i;
            if (f[i] > f[worst]) worst = i;
        }
        secondWorst = best;
        for (int i = 0; i <= D; ++i) {
            if (i != worst && f[i] > f[secondWorst]) secondWorst = i;
        }
        if (f[worst] - f[best] < 1e-7f) break;

        std::array<float, D> c{};
        for (int i = 0; i <= D; ++i) {
            if (i == worst) continue;
            for (int d = 0; d < D; ++d) c[d] += p[i][d];
        }
        for (int d = 0; d < D; ++d) c[d] /= static_cast<float>(D);

        // Yansıma
        std::array<float, D> xr{};
        for (int d = 0; d < D; ++d) xr[d] = c[d] + (c[d] - p[worst][d]);
        const float fr = eval(xr);
        if (fr < f[best]) {
            // Genişletme
            std::array<float, D> xe{};
            for (int d = 0; d < D; ++d) xe[d] = c[d] + 2.0f * (xr[d] - c[d]);
            const float fe = eval(xe);
            if (fe < fr) { p[worst] = xe; f[worst] = fe; }
            else         { p[worst] = xr; f[worst] = fr; }
        } else if (fr < f[secondWorst]) {
            p[worst] = xr; f[worst] = fr;
        } else {
            // Daraltma
            std::array<float, D> xc{};
            const bool outside = fr < f[worst];
            for (int d = 0; d < D; ++d) xc[d] = c[d] + 0.5f * ((outside ? xr[d] : p[worst][d]) - c[d]);
            const float fc = eval(xc);
            if (fc < std::min(fr, f[worst])) {
                p[worst] = xc; f[worst] = fc;
            } else {
                // Küçültme
                for (int i = 0; i <= D; ++i) {
                    if (i == best) continue;
                    for (int d = 0; d < D; ++d) p[i][d] = p[best][d] + 0.5f * (p[i][d] - p[best][d]);
                    f[i] = eval(p[i]);
                }
            }
        }
    }

    int best = 0;
    for (int i = 1; i <= D; ++i)
        if (f[i] < f[best]) best = i;
    return p[best];
}

// Bir (roughness, cosTheta) hücresi için exact (norm, fresnel) + ortalama yön hesabı.
void computeGgxStats(float roughness, float cosTheta, float& outNorm, float& outFres, Vec3f& outAvgDir) {
    const float a = std::max(roughness * roughness, 1e-4f);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const Vec3f V(sinTheta, 0.0f, cosTheta);
    const float lamV = ggxLambda(cosTheta, a);

    constexpr int kN = 192;
    double sumNorm = 0.0;
    double sumFres = 0.0;
    Vec3f avg(0.0f);
    for (int i = 0; i < kN; ++i) {
        float u1, u2;
        r2Sample(i, u1, u2);
        const Vec3f H = sampleGgxVndf(V, a, u1, u2);
        const float VoH = std::max(0.0f, V.dot(H));
        const Vec3f L = H * (2.0f * VoH) - V;
        if (L.z <= 1e-6f) continue;
        const float lamL = ggxLambda(L.z, a);
        const float w = (1.0f + lamV) / (1.0f + lamV + lamL);
        const float f5 = std::pow(1.0f - VoH, 5.0f);
        sumNorm += w;
        sumFres += w * f5;
        avg += L * w;
    }
    outNorm = static_cast<float>(sumNorm / kN);
    outFres = static_cast<float>(sumFres / kN);
    avg.y = 0.0f;
    outAvgDir = avg.lengthSquared() > 1e-12f ? avg.normalized() : Vec3f(-sinTheta, 0.0f, cosTheta);
}

// Bir (roughness, cosTheta) hücresi için 4B LTC parametrelerini uydurur.
std::array<float, 4> fitLtcCell(float roughness, float cosTheta, float norm, const Vec3f& avgDir,
                                const std::array<float, 4>& guess) {
    const float a = std::max(roughness * roughness, 1e-4f);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const Vec3f V(sinTheta, 0.0f, cosTheta);

    // Önceden GGX VNDF örneklerini hazırla (64 çift yarı-rastgele örnek)
    constexpr int kHalf = 48;
    struct SampleDir { Vec3f L; float ggxPdf; };
    std::array<SampleDir, kHalf> ggxSamples{};
    std::array<Vec3f, kHalf> cosSamples{};
    for (int i = 0; i < kHalf; ++i) {
        float u1, u2;
        r2Sample(i, u1, u2);
        const Vec3f H = sampleGgxVndf(V, a, u1, u2);
        const float VoH = std::max(0.0f, V.dot(H));
        const Vec3f L = H * (2.0f * VoH) - V;
        ggxSamples[static_cast<size_t>(i)] = {L, evalGgxTargetPdf(V, L, a, norm)};
        cosSamples[static_cast<size_t>(i)] = sampleCosHemisphere(u1, u2);
    }

    const float initTilt = std::atan2(-avgDir.x, std::max(avgDir.z, 1e-4f));
    std::array<float, 4> start = guess;
    start[2] = 0.5f * (guess[2] + initTilt);

    auto loss = [&](const std::array<float, 4>& x) -> float {
        if (x[0] <= 1e-3f || x[1] <= 1e-3f || x[0] > 4.0f || x[1] > 4.0f ||
            x[2] < -0.1f || x[2] > 1.54f || std::abs(x[3]) > 2.5f) {
            return 1e9f;
        }
        const LtcMat mat = makeLtcMat(x[0], x[1], x[2], x[3]);
        double err = 0.0;
        for (int i = 0; i < kHalf; ++i) {
            // 1. GGX VNDF örnekleri
            const auto& gs = ggxSamples[static_cast<size_t>(i)];
            if (gs.ggxPdf > 1e-8f) {
                const float ltcPdf = evalLtcPdf(mat, gs.L);
                const float diff = std::abs(ltcPdf - gs.ggxPdf);
                err += (diff * diff * diff) / (0.5f * gs.ggxPdf + 0.5f * ltcPdf + 1e-6f);
            }
            // 2. LTC örnekleri
            const Vec3f lm = mul3(mat.m, cosSamples[static_cast<size_t>(i)]);
            const float ll = lm.length();
            if (ll > 1e-7f) {
                const Vec3f L = lm / ll;
                const float ltcPdf = evalLtcPdf(mat, L);
                const float ggxPdf = evalGgxTargetPdf(V, L, a, norm);
                const float diff = std::abs(ltcPdf - ggxPdf);
                err += (diff * diff * diff) / (0.5f * ggxPdf + 0.5f * ltcPdf + 1e-6f);
            }
        }
        return static_cast<float>(err / (2.0 * kHalf));
    };

    const std::array<float, 4> step = {
        std::max(0.04f, start[0] * 0.2f),
        std::max(0.04f, start[1] * 0.2f),
        0.08f,
        0.08f
    };
    return nelderMead4(start, step, loss, 55);
}

// 16×16 kaba ızgarayı uydurup 64×64 pürüzsüz GPU dokusuna çift doğrusal enterpolasyonla çıkarır.
LtcTables buildLtcTables() {
    constexpr int kCoarse = 16;
    constexpr int N = LtcTables::kSize;
    std::array<std::array<float, 4>, kCoarse * kCoarse> coarseInv{};
    std::array<float, kCoarse * kCoarse> coarseNorm{};
    std::array<float, kCoarse * kCoarse> coarseFres{};

    for (int iv = 0; iv < kCoarse; ++iv) {
        const float v = static_cast<float>(iv) / static_cast<float>(kCoarse - 1);
        const float cosTheta = std::max(0.015f, 1.0f - v * v);
        std::array<float, 4> prev = {0.06f, 0.06f, std::acos(cosTheta), 0.0f};
        for (int iu = 0; iu < kCoarse; ++iu) {
            const float u = static_cast<float>(iu) / static_cast<float>(kCoarse - 1);
            const float rough = std::max(0.03f, u);
            float norm = 0.0f, fres = 0.0f;
            Vec3f avgDir;
            computeGgxStats(rough, cosTheta, norm, fres, avgDir);
            if (iu == 0) {
                const float a = rough * rough;
                prev = {std::max(0.02f, a), std::max(0.02f, a), std::atan2(-avgDir.x, std::max(avgDir.z, 1e-4f)), 0.0f};
            }
            const std::array<float, 4> fitted = fitLtcCell(rough, cosTheta, norm, avgDir, prev);
            prev = fitted;

            const LtcMat mat = makeLtcMat(fitted[0], fitted[1], fitted[2], fitted[3]);
            // m22 = 1 olacak şekilde normalize et
            const float invM22 = 1.0f / std::max(std::abs(mat.inv[2][2]), 1e-4f);
            const size_t idx = static_cast<size_t>(iv * kCoarse + iu);
            coarseInv[idx] = {
                mat.inv[0][0] * invM22,
                mat.inv[0][2] * invM22,
                mat.inv[1][1] * invM22,
                mat.inv[2][0] * invM22
            };
            coarseNorm[idx] = norm;
            coarseFres[idx] = fres;
        }
    }

    LtcTables out;
    out.ltc1.resize(static_cast<size_t>(N) * N * 4);
    out.ltc2.resize(static_cast<size_t>(N) * N * 4);

    for (int y = 0; y < N; ++y) {
        const float fy = static_cast<float>(y) / static_cast<float>(N - 1) * static_cast<float>(kCoarse - 1);
        const int y0 = std::min(static_cast<int>(fy), kCoarse - 2);
        const int y1 = y0 + 1;
        const float ty = fy - static_cast<float>(y0);
        for (int x = 0; x < N; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(N - 1) * static_cast<float>(kCoarse - 1);
            const int x0 = std::min(static_cast<int>(fx), kCoarse - 2);
            const int x1 = x0 + 1;
            const float tx = fx - static_cast<float>(x0);

            const size_t i00 = static_cast<size_t>(y0 * kCoarse + x0);
            const size_t i10 = static_cast<size_t>(y0 * kCoarse + x1);
            const size_t i01 = static_cast<size_t>(y1 * kCoarse + x0);
            const size_t i11 = static_cast<size_t>(y1 * kCoarse + x1);
            auto bilerp = [&](float v00, float v10, float v01, float v11) {
                const float a = v00 * (1.0f - tx) + v10 * tx;
                const float b = v01 * (1.0f - tx) + v11 * tx;
                return a * (1.0f - ty) + b * ty;
            };

            const size_t dst = (static_cast<size_t>(y) * N + static_cast<size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) {
                out.ltc1[dst + static_cast<size_t>(c)] =
                    bilerp(coarseInv[i00][static_cast<size_t>(c)], coarseInv[i10][static_cast<size_t>(c)],
                           coarseInv[i01][static_cast<size_t>(c)], coarseInv[i11][static_cast<size_t>(c)]);
            }
            out.ltc2[dst + 0] = bilerp(coarseNorm[i00], coarseNorm[i10], coarseNorm[i01], coarseNorm[i11]);
            out.ltc2[dst + 1] = bilerp(coarseFres[i00], coarseFres[i10], coarseFres[i01], coarseFres[i11]);
            out.ltc2[dst + 2] = 0.0f;
            out.ltc2[dst + 3] = 1.0f;
        }
    }
    return out;
}

// ── Ufuk (z >= 0) kırpma ve Baum 1989 kenar vektör integrali ──

int clipPolyToHorizon(const Vec3f* in, int nIn, Vec3f* out) {
    int n = 0;
    for (int i = 0; i < nIn; ++i) {
        const Vec3f& a = in[i];
        const Vec3f& b = in[(i + 1) % nIn];
        const bool aIn = a.z >= 0.0f;
        const bool bIn = b.z >= 0.0f;
        if (aIn) out[n++] = a;
        if (aIn != bIn) {
            const float t = a.z / (a.z - b.z);
            Vec3f p = a + (b - a) * t;
            p.z = 0.0f;
            out[n++] = p;
        }
    }
    return n;
}

Vec3f integrateEdgeVec(const Vec3f& v1, const Vec3f& v2) {
    const float x = std::clamp(v1.dot(v2), -1.0f, 1.0f);
    float y = 1.0f;
    if (x < 0.9999f) {
        const float theta = std::acos(x);
        const float s = std::sqrt(std::max(1.0f - x * x, 1e-12f));
        y = theta / s;
    }
    return v1.cross(v2) * y;
}

float integrateLtcQuad(const Vec3f L[4], const float mInv[4], bool twoSided) {
    // 1) Önce yüzeyin geometrik ufkuna (z >= 0) kırp (yüzeyin altından ışık sızmasın).
    Vec3f geomClip[5];
    const int nGeom = clipPolyToHorizon(L, 4, geomClip);
    if (nGeom < 3) return 0.0f;

    // 2) M⁻¹ ile kosinüs uzayına taşı ve kosinüs ufkuna (z >= 0) kırp.
    Vec3f trans[5];
    for (int i = 0; i < nGeom; ++i) {
        trans[i] = Vec3f(mInv[0] * geomClip[i].x + mInv[1] * geomClip[i].z,
                         mInv[2] * geomClip[i].y,
                         mInv[3] * geomClip[i].x + geomClip[i].z);
    }
    Vec3f clipped[6];
    const int n = clipPolyToHorizon(trans, nGeom, clipped);
    if (n < 3) return 0.0f;

    for (int i = 0; i < n; ++i) {
        const float len = clipped[i].length();
        if (len < 1e-10f) return 0.0f;
        clipped[i] /= len;
    }

    Vec3f sum(0.0f);
    for (int i = 0; i < n; ++i) {
        sum += integrateEdgeVec(clipped[i], clipped[(i + 1) % n]);
    }
    const float form = sum.z * INV_TWO_PI;
    return twoSided ? std::abs(form) : std::max(0.0f, form);
}

} // namespace

const LtcTables& getLtcTables() {
    static const LtcTables kTables = buildLtcTables();
    return kTables;
}

void sampleLtcTables(float roughness, float cosTheta, float outMInv[4], float& outNorm, float& outFres) {
    const LtcTables& tbl = getLtcTables();
    constexpr int N = LtcTables::kSize;
    const float u = std::clamp(roughness, 0.0f, 1.0f) * static_cast<float>(N - 1);
    const float v = std::sqrt(std::clamp(1.0f - std::clamp(cosTheta, 0.0f, 1.0f), 0.0f, 1.0f)) * static_cast<float>(N - 1);
    const int x0 = std::min(static_cast<int>(u), N - 2);
    const int y0 = std::min(static_cast<int>(v), N - 2);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    auto sample4 = [&](const std::vector<float>& buf, int ch) {
        const size_t i00 = (static_cast<size_t>(y0) * N + static_cast<size_t>(x0)) * 4 + static_cast<size_t>(ch);
        const size_t i10 = (static_cast<size_t>(y0) * N + static_cast<size_t>(x1)) * 4 + static_cast<size_t>(ch);
        const size_t i01 = (static_cast<size_t>(y1) * N + static_cast<size_t>(x0)) * 4 + static_cast<size_t>(ch);
        const size_t i11 = (static_cast<size_t>(y1) * N + static_cast<size_t>(x1)) * 4 + static_cast<size_t>(ch);
        const float a = buf[i00] * (1.0f - tx) + buf[i10] * tx;
        const float b = buf[i01] * (1.0f - tx) + buf[i11] * tx;
        return a * (1.0f - ty) + b * ty;
    };

    for (int c = 0; c < 4; ++c) outMInv[c] = sample4(tbl.ltc1, c);
    outNorm = sample4(tbl.ltc2, 0);
    outFres = sample4(tbl.ltc2, 1);
}

Color3f evalLtcRectArea(const Vec3f& P, const Vec3f& N, const Vec3f& V,
                        float roughness, const Color3f& f0, const Color3f& diffuseAlbedo,
                        const Vec3f& lightCenter, const Vec3f& halfU, const Vec3f& halfV,
                        const Color3f& radiance, bool twoSided) {
    const float NoV = std::clamp(N.dot(V), 1e-4f, 1.0f);
    Vec3f T1 = V - N * N.dot(V);
    if (T1.lengthSquared() < 1e-8f) {
        const Vec3f up = std::abs(N.y) > 0.9f ? Vec3f(1.0f, 0.0f, 0.0f) : Vec3f(0.0f, 1.0f, 0.0f);
        T1 = up.cross(N);
    }
    T1 = T1.normalized();
    const Vec3f T2 = N.cross(T1);

    const Vec3f worldCorners[4] = {
        lightCenter - halfU - halfV,
        lightCenter + halfU - halfV,
        lightCenter + halfU + halfV,
        lightCenter - halfU + halfV
    };
    Vec3f localCorners[4];
    for (int i = 0; i < 4; ++i) {
        const Vec3f d = worldCorners[i] - P;
        localCorners[i] = Vec3f(d.dot(T1), d.dot(T2), d.dot(N));
    }

    float mInv[4];
    float norm = 0.0f, fres = 0.0f;
    sampleLtcTables(roughness, NoV, mInv, norm, fres);

    const float specForm = integrateLtcQuad(localCorners, mInv, twoSided);
    const Color3f specScale = f0 * norm + (Color3f(1.0f) - f0) * fres;
    const Color3f spec = specScale * specForm;

    Color3f diff(0.0f);
    if (diffuseAlbedo.r > 0.0f || diffuseAlbedo.g > 0.0f || diffuseAlbedo.b > 0.0f) {
        constexpr float kIdent[4] = {1.0f, 0.0f, 1.0f, 0.0f};
        const float diffForm = integrateLtcQuad(localCorners, kIdent, twoSided);
        diff = diffuseAlbedo * diffForm;
    }

    return (spec + diff) * radiance;
}

} // namespace photon
