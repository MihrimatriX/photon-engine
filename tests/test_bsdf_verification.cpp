// BSDF doğrulama paketi (F2.6, F2.7): her malzeme × pürüzlülük × geliş açısı için
//  1) Chi-kare testi: sample() ile üretilen yönlerin histogramı pdf()'in hücre integralleriyle
//     istatistiksel olarak uyumlu mu? (Mitsuba / PBRT-v3 yöntemi.) Örnekleyici ile pdf
//     arasındaki her uyumsuzluk MIS ağırlıklarını bozar ve görüntüde sessiz yanlılık yapar.
//  2) Tutarlılık: sampleFull'un döndürdüğü f ve pdf, eval()/pdf() ile aynı mı?
//  3) Fırın testi: yönlü albedo ρ(wo) = E[f·|cos|/pdf] ≤ 1 (enerji yaratılmaz); kayıpsız
//     malzemelerde (beyaz Lambert, beyaz metal, soğurmasız cam) ≈ 1.

#include <gtest/gtest.h>

#include "core/math/constants.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "samplers/independent_sampler.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace photon;

namespace {

// ── İstatistik ──────────────────────────────────────────────────────────────

/// Düzenlenmiş üst eksik gama fonksiyonu Q(a, x) = Γ(a, x)/Γ(a) (Numerical Recipes 6.2).
/// Ki-kare dağılımının kuyruğu: p = Q(dof/2, χ²/2).
double gammaQ(double a, double x) {
    if (x <= 0.0) return 1.0;
    const double lnPrefix = -x + a * std::log(x) - std::lgamma(a);
    if (x < a + 1.0) {
        double ap = a, sum = 1.0 / a, del = sum;
        for (int n = 0; n < 10000; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::abs(del) < std::abs(sum) * 1e-15) break;
        }
        return std::max(0.0, 1.0 - sum * std::exp(lnPrefix));
    }
    constexpr double tiny = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / tiny, d = 1.0 / b, h = d;
    for (int i = 1; i < 10000; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::abs(d) < tiny) d = tiny;
        c = b + an / c;
        if (std::abs(c) < tiny) c = tiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < 1e-15) break;
    }
    return std::exp(lnPrefix) * h;
}

// ── Test durumları ──────────────────────────────────────────────────────────

struct Case {
    std::string name;
    std::shared_ptr<Material> material;
    SurfaceInteraction si;
    Vec3f wo;
    /// Kırılan yönlerde radyans ölçeklemesini (1/η²) geri alan çarpan; fırın testi için.
    float etaSqForTransmission = 1.0f;
};

SurfaceInteraction frontSi() {
    SurfaceInteraction si;
    si.point = Vec3f(0.37f, 0.0f, -0.21f);
    si.normal = si.ng = Vec3f(0, 0, 1);
    si.tangent = Vec3f(1, 0, 0);
    si.frontFace = true;
    return si;
}

/// Camın içinden gelen ışın: gölgeleme normali ışına (−z), geometrik normal dışa (+z).
SurfaceInteraction insideSi() {
    SurfaceInteraction si = frontSi();
    si.normal = Vec3f(0, 0, -1);
    si.frontFace = false;
    return si;
}

Vec3f dirFromCos(float c, float phi = 0.3f) {
    const float s = std::sqrt(std::max(0.0f, 1.0f - c * c));
    return Vec3f(s * std::cos(phi), s * std::sin(phi), c);
}

std::shared_ptr<DisneyMaterial> disney(const Color3f& base, float metal, float rough,
                                       const std::function<void(DisneyMaterial&)>& extra = {}) {
    auto m = std::make_shared<DisneyMaterial>(base, metal, rough, 0.5f);
    if (extra) extra(*m);
    return m;
}

std::string fmt(float v) {
    std::ostringstream s;
    s.precision(2);
    s << v;
    return s.str();
}

/// Yansıtıcı (tek yarıküre) ve geçirgen malzemelerin chi-kare durumları.
std::vector<Case> chi2Cases() {
    std::vector<Case> cases;
    const float cosines[] = {0.95f, 0.6f, 0.2f};
    auto add = [&](const std::string& name, std::shared_ptr<Material> m, const SurfaceInteraction& si,
                   bool flipWo = false) {
        for (float c : cosines) {
            Vec3f wo = dirFromCos(c);
            if (flipWo) wo.z = -wo.z;
            cases.push_back({name + " cos=" + fmt(c), m, si, wo});
        }
    };
    add("lambert", std::make_shared<Lambertian>(Color3f(0.8f)), frontSi());
    for (float r : {0.3f, 0.6f, 1.0f}) {
        add("plastic r=" + fmt(r), disney(Color3f(0.7f, 0.5f, 0.3f), 0.0f, r), frontSi());
        add("metal r=" + fmt(r), disney(Color3f(0.9f, 0.6f, 0.4f), 1.0f, r), frontSi());
    }
    add("half-metal r=0.4", disney(Color3f(0.6f), 0.5f, 0.4f), frontSi());
    add("aniso metal", disney(Color3f(0.9f), 1.0f, 0.5f, [](DisneyMaterial& m) { m.setAnisotropy(0.8f); }),
        frontSi());
    add("clearcoat", disney(Color3f(0.6f, 0.1f, 0.1f), 0.0f, 0.5f, [](DisneyMaterial& m) {
            m.setClearCoat(1.0f);
            m.setClearCoatRoughness(0.35f);
        }), frontSi());
    add("sheen", disney(Color3f(0.3f, 0.3f, 0.8f), 0.0f, 0.8f, [](DisneyMaterial& m) { m.setSheen(1.0f); }),
        frontSi());
    add("rough diffuse", disney(Color3f(0.6f), 0.0f, 0.7f, [](DisneyMaterial& m) { m.setDiffuseRoughness(0.8f); }),
        frontSi());
    add("diffuse transmission",
        disney(Color3f(0.7f), 0.0f, 0.5f, [](DisneyMaterial& m) { m.setDiffuseTransmission(0.6f); }), frontSi());
    add("thin film metal", disney(Color3f(0.8f), 1.0f, 0.4f, [](DisneyMaterial& m) { m.setThinFilm(400.0f, 1.5f); }),
        frontSi());
    for (float r : {0.3f, 0.6f}) {
        add("rough glass out r=" + fmt(r), std::make_shared<Dielectric>(1.5f, Color3f(1.0f), r), frontSi());
        add("rough glass in r=" + fmt(r), std::make_shared<Dielectric>(1.5f, Color3f(1.0f), r), insideSi(), true);
    }
    return cases;
}

// ── Chi-kare ────────────────────────────────────────────────────────────────

constexpr int kCosBins = 24; // cosθ ∈ [−1, 1], eşit katı açılı hücreler
constexpr int kPhiBins = 24;

int cellOf(const Vec3f& w) {
    const float c = std::clamp(w.z, -1.0f, 1.0f);
    float phi = std::atan2(w.y, w.x);
    if (phi < 0.0f) phi += TWO_PI;
    const int ic = std::min(kCosBins - 1, static_cast<int>((c + 1.0f) * 0.5f * kCosBins));
    const int ip = std::min(kPhiBins - 1, static_cast<int>(phi / TWO_PI * kPhiBins));
    return ic * kPhiBins + ip;
}

/// Bir hücrede ∫pdf dω, orta nokta kuralıyla, (cosθ, φ) uzayında (dω = dcosθ dφ).
double integrateCell(const Material& m, const Case& c, int ic, int ip, int k) {
    const double c0 = -1.0 + 2.0 * ic / kCosBins, dc = 2.0 / kCosBins / k;
    const double p0 = TWO_PI * ip / kPhiBins, dp = TWO_PI / kPhiBins / k;
    double sum = 0.0;
    for (int i = 0; i < k; ++i) {
        const double cz = c0 + (i + 0.5) * dc;
        const double s = std::sqrt(std::max(0.0, 1.0 - cz * cz));
        for (int j = 0; j < k; ++j) {
            const double phi = p0 + (j + 0.5) * dp;
            const Vec3f wi(static_cast<float>(s * std::cos(phi)), static_cast<float>(s * std::sin(phi)),
                           static_cast<float>(cz));
            sum += m.pdf(c.wo, wi, c.si);
        }
    }
    return sum * dc * dp;
}

/// Uyarlamalı: çözünürlük ikiye katlanınca sonuç binde birden az değişene dek incelt.
double expectedMass(const Material& m, const Case& c, int ic, int ip) {
    double prev = integrateCell(m, c, ic, ip, 6);
    for (int k = 12; k <= 96; k *= 2) {
        const double cur = integrateCell(m, c, ic, ip, k);
        if (std::abs(cur - prev) <= 1e-3 * std::max(cur, 1e-7)) return cur;
        prev = cur;
    }
    return prev;
}

struct Chi2Outcome {
    double pValue = 0.0;
    double chi2 = 0.0;
    int dof = 0;
    double pdfMass = 0.0;   ///< ∫pdf dω (başarılı örnek olasılığına eşit olmalı)
    double sampledFrac = 0.0;
    int zeroPdfHits = 0;    ///< pdf'in 0 olduğu hücreye düşen örnek (kesin hata)
    float maxPdfMismatch = 0.0f;
    float maxFMismatch = 0.0f;
};

Chi2Outcome runChi2(const Case& c, int n, uint64_t seed) {
    const Material& m = *c.material;
    std::vector<double> observed(kCosBins * kPhiBins, 0.0), expected(kCosBins * kPhiBins, 0.0);
    IndependentSampler rng(seed);
    Chi2Outcome out;
    int ok = 0;
    for (int i = 0; i < n; ++i) {
        BSDFSample bs;
        const float uc = rng.get1D();
        const Vec2f u = rng.get2D();
        if (!m.sampleFull(c.wo, c.si, uc, u, bs) || !(bs.pdf > 0.0f) || bs.subsurface) continue;
        ++ok;
        observed[static_cast<size_t>(cellOf(bs.wi))] += 1.0;
        if ((i & 63) == 0) {
            const float p = m.pdf(c.wo, bs.wi, c.si);
            const Color3f f = m.eval(c.wo, bs.wi, c.si);
            out.maxPdfMismatch = std::max(out.maxPdfMismatch, std::abs(p - bs.pdf) / std::max(bs.pdf, 1e-3f));
            const float fl = std::max({bs.f.r, bs.f.g, bs.f.b, 1e-3f});
            out.maxFMismatch = std::max(out.maxFMismatch, std::max({std::abs(f.r - bs.f.r), std::abs(f.g - bs.f.g),
                                                                    std::abs(f.b - bs.f.b)}) / fl);
        }
    }
    out.sampledFrac = static_cast<double>(ok) / n;
    for (int ic = 0; ic < kCosBins; ++ic)
        for (int ip = 0; ip < kPhiBins; ++ip) {
            const double mass = expectedMass(m, c, ic, ip);
            out.pdfMass += mass;
            expected[static_cast<size_t>(ic * kPhiBins + ip)] = mass * n;
        }

    // Beklenen sayısı küçük hücreler birleştirilir (Pearson testinin geçerlilik koşulu).
    std::vector<size_t> order(expected.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return expected[a] < expected[b]; });
    double pooledObs = 0.0, pooledExp = 0.0, chi2 = 0.0;
    int cells = 0;
    for (size_t idx : order) {
        if (expected[idx] <= 0.0) {
            if (observed[idx] > 0.0) out.zeroPdfHits += static_cast<int>(observed[idx]);
            continue;
        }
        if (expected[idx] < 5.0 || pooledExp > 0.0 && pooledExp < 5.0) {
            pooledObs += observed[idx];
            pooledExp += expected[idx];
            continue;
        }
        const double d = observed[idx] - expected[idx];
        chi2 += d * d / expected[idx];
        ++cells;
    }
    if (pooledExp > 0.0) {
        const double d = pooledObs - pooledExp;
        chi2 += d * d / pooledExp;
        ++cells;
    }
    out.chi2 = chi2;
    out.dof = std::max(1, cells - 1);
    out.pValue = gammaQ(0.5 * out.dof, 0.5 * chi2);
    return out;
}

// ── Fırın ───────────────────────────────────────────────────────────────────

/// ρ(wo) = E[f·|cos|/pdf] (kırılan yönler isteğe bağlı η² ile).
Color3f directionalAlbedo(const Case& c, int n, uint64_t seed) {
    IndependentSampler rng(seed);
    Color3f sum(0.0f);
    for (int i = 0; i < n; ++i) {
        BSDFSample bs;
        if (!c.material->sampleFull(c.wo, c.si, rng.get1D(), rng.get2D(), bs) || !(bs.pdf > 0.0f)) continue;
        if (bs.subsurface) {
            sum += bs.weight;
            continue;
        }
        Color3f w = bs.f * (std::abs(bs.wi.dot(c.si.normal)) / bs.pdf);
        if (bs.wi.dot(c.si.ng) * c.wo.dot(c.si.ng) < 0.0f) w *= c.etaSqForTransmission;
        sum += w;
    }
    return sum / static_cast<float>(n);
}

float maxComp(const Color3f& c) { return std::max({c.r, c.g, c.b}); }

} // namespace

TEST(BsdfVerification, Chi2SamplingMatchesPdf) {
    const std::vector<Case> cases = chi2Cases();
    // Bonferroni: tüm durumlar birlikte %1 yanlış alarm olasılığı.
    const double alpha = 0.01 / static_cast<double>(cases.size());
    constexpr int kSamples = 250000;
    uint64_t seed = 7;
    for (const Case& c : cases) {
        const Chi2Outcome r = runChi2(c, kSamples, seed++);
        EXPECT_EQ(r.zeroPdfHits, 0) << c.name << ": samples landed where pdf() == 0";
        EXPECT_GT(r.pValue, alpha) << c.name << ": chi2 " << r.chi2 << " dof " << r.dof << " p " << r.pValue
                                   << " (pdf mass " << r.pdfMass << ", sampled " << r.sampledFrac << ")";
        // ∫pdf = örneklemenin başarı olasılığı (reddedilen örnekler pdf'te de yok sayılır).
        EXPECT_NEAR(r.pdfMass, r.sampledFrac, 0.01) << c.name;
        EXPECT_LT(r.maxPdfMismatch, 1e-3f) << c.name << ": sampleFull pdf != pdf()";
        EXPECT_LT(r.maxFMismatch, 1e-3f) << c.name << ": sampleFull f != eval()";
    }
}

// Enerji korunumu: hiçbir malzeme gelen ışıktan fazlasını yansıtamaz.
TEST(BsdfVerification, FurnaceNeverGainsEnergy) {
    std::vector<Case> cases;
    for (float r : {0.05f, 0.3f, 0.6f, 1.0f}) {
        for (float c : {1.0f, 0.5f, 0.15f, 0.03f}) {
            const std::string tag = " r=" + fmt(r) + " cos=" + fmt(c);
            cases.push_back({"white plastic" + tag, disney(Color3f(1.0f), 0.0f, r), frontSi(), dirFromCos(c)});
            cases.push_back({"white metal" + tag, disney(Color3f(1.0f), 1.0f, r), frontSi(), dirFromCos(c)});
            cases.push_back({"coated white metal" + tag, disney(Color3f(1.0f), 1.0f, r, [](DisneyMaterial& m) {
                                 m.setClearCoat(1.0f);
                                 m.setClearCoatRoughness(0.1f);
                             }), frontSi(), dirFromCos(c)});
            cases.push_back({"white sheen" + tag, disney(Color3f(1.0f), 0.0f, 0.5f, [r](DisneyMaterial& m) {
                                 m.setSheen(1.0f);
                                 m.setSheenRoughness(r);
                             }), frontSi(), dirFromCos(c)});
            cases.push_back({"white velvet" + tag, disney(Color3f(1.0f), 0.0f, r, [](DisneyMaterial& m) {
                                 m.setSheen(1.0f);
                                 m.setDiffuseRoughness(1.0f);
                                 m.setClearCoat(0.5f);
                             }), frontSi(), dirFromCos(c)});
            cases.push_back({"white translucent" + tag, disney(Color3f(1.0f), 0.0f, r, [](DisneyMaterial& m) {
                                 m.setDiffuseTransmission(1.0f);
                             }), frontSi(), dirFromCos(c)});
            Case g{"rough glass" + tag, std::make_shared<Dielectric>(1.5f, Color3f(1.0f), r), frontSi(), dirFromCos(c)};
            g.etaSqForTransmission = 1.5f * 1.5f;
            cases.push_back(g);
        }
    }
    uint64_t seed = 101;
    for (const Case& c : cases) {
        const Color3f a = directionalAlbedo(c, 40000, seed++);
        EXPECT_LE(maxComp(a), 1.02f) << c.name << " albedo " << a.r << " " << a.g << " " << a.b;
    }
}

// Kayıpsız malzemeler her açıda ışığın tamamını geri verir: beyaz Lambert, beyaz metal (Turquin
// telafisi), beyaz plastik (yalıtkan speküler + difüz, Kulla–Conty katmanlaması), pürüzlü difüz
// (EON) ve beyaz ince geçirgen yaprak. Burley difüzünde beyaz plastik yatayda 1.58 veriyordu.
TEST(BsdfVerification, FurnaceLosslessMaterialsReturnEverything) {
    uint64_t seed = 301;
    for (float c : {1.0f, 0.5f, 0.15f, 0.04f}) {
        Case lam{"lambert", std::make_shared<Lambertian>(Color3f(1.0f)), frontSi(), dirFromCos(c)};
        EXPECT_NEAR(directionalAlbedo(lam, 40000, seed++).g, 1.0f, 0.01f) << "cos " << c;
        for (float r : {0.05f, 0.2f, 0.6f, 1.0f}) {
            const std::string tag = " r=" + fmt(r) + " cos=" + fmt(c);
            Case metal{"metal", disney(Color3f(1.0f), 1.0f, r), frontSi(), dirFromCos(c)};
            EXPECT_NEAR(directionalAlbedo(metal, 60000, seed++).g, 1.0f, 0.03f) << "metal" << tag;
            Case plastic{"plastic", disney(Color3f(1.0f), 0.0f, r), frontSi(), dirFromCos(c)};
            EXPECT_NEAR(directionalAlbedo(plastic, 60000, seed++).g, 1.0f, 0.025f) << "plastic" << tag;
            Case leaf{"translucent", disney(Color3f(1.0f), 0.0f, r, [](DisneyMaterial& m) {
                          m.setDiffuseTransmission(0.5f);
                      }), frontSi(), dirFromCos(c)};
            EXPECT_NEAR(directionalAlbedo(leaf, 60000, seed++).g, 1.0f, 0.025f) << "translucent" << tag;
            Case clay{"rough diffuse", disney(Color3f(1.0f), 0.0f, 0.5f, [r](DisneyMaterial& m) {
                          m.setDiffuseRoughness(r);
                      }), frontSi(), dirFromCos(c)};
            EXPECT_NEAR(directionalAlbedo(clay, 60000, seed++).g, 1.0f, 0.03f) << "EON" << tag;
        }
    }
}
