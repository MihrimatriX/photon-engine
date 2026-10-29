// environment_light.cpp — HDRI ortam ışığının örneklenmesi ve değerlendirilmesi.
#include "lights/environment_light.h"

#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include "core/math/frame.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace photon {

namespace {

std::atomic<bool> gMisCompensation{true};

struct CdfPick {
    int index = 0;
    float pdf = 0.0f;
    float coord = 0.0f;
};

// Ters CDF örnekleme: u ∈ [0,1) için cdf[i] <= u < cdf[i+1] olan i bulunur
// (ikili arama), sonra hücre içinde doğrusal konum hesaplanır. pdf = hücrenin
// olasılık kütlesi; coord = sürekli [0,1) koordinat.
CdfPick sampleCdf(const float* cdf, int n, float u) {
    const float* end = cdf + n + 1;
    const float* it = std::upper_bound(cdf, end, u);
    int i = static_cast<int>(it - cdf) - 1;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    float pdf = cdf[i + 1] - cdf[i];
    float du = pdf > 0.0f ? (u - cdf[i]) / pdf : 0.0f;
    du = std::clamp(du, 0.0f, 1.0f);
    return {i, pdf, (static_cast<float>(i) + du) / static_cast<float>(n)};
}

} // namespace

void EnvironmentLight::setMisCompensation(bool enabled) { gMisCompensation = enabled; }

Color3f sampleEquirect(const Image& img, float u, float v) {
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) return Color3f::black();
    u -= std::floor(u);
    v = std::clamp(v, 0.0f, 1.0f);
    const float x = u * static_cast<float>(w) - 0.5f;
    const float y = v * static_cast<float>(h) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    auto wrapX = [w](int i) { i %= w; return i < 0 ? i + w : i; };
    auto clampY = [h](int i) { return std::clamp(i, 0, h - 1); };
    const int xa = wrapX(x0), xb = wrapX(x0 + 1);
    const int ya = clampY(y0), yb = clampY(y0 + 1);
    const Color3f c0 = img.getPixel(xa, ya) * (1.0f - fx) + img.getPixel(xb, ya) * fx;
    const Color3f c1 = img.getPixel(xa, yb) * (1.0f - fx) + img.getPixel(xb, yb) * fx;
    return c0 * (1.0f - fy) + c1 * fy;
}

EnvironmentLight::EnvironmentLight(std::shared_ptr<const Image> envMap, float rotation, float intensity)
    : m_envMap(std::move(envMap)), m_rotation(rotation), m_intensity(intensity) {
    buildDistribution();
    m_meanLuminance = std::max(0.0f, power().luminance() / (4.0f * PI));
}

// Önem örnekleme dağılımı. Equirect görüntüde her satır θ enlemine karşılık
// gelir; kutuplara yakın satırlar küre üzerinde daha küçük alan kaplar. Bu
// yüzden fonksiyon f(x, y) = parlaklık · sin(θ) alınır. Önce her satırın kendi
// koşullu CDF'i (o satırda hangi sütun), sonra satır toplamlarından marjinal CDF
// (hangi satır) kurulur: 2B dağılım = p(satır) · p(sütun | satır).
//
// MIS telafisi (Karlík, Šik, Vévoda, Skřivan, Křivánek 2019): NEE ile BSDF örneklemesi MIS
// ile birleşince, ışık örneklemesinin "düzgün" kısmı boşa harcanır (BSDF onu zaten iyi
// örnekler). Radyanstan katı açı ağırlıklı ortalaması çıkarılır: f' = max(0, L − L̄)·sinθ
// (sinθ yalnız equirect parametrizasyonunun Jakobiyeni; ondan çıkarmak düzgün bir haritayı
// ekvatora yığardı). Tamamen düzgün haritada f' ≡ 0 olur; o zaman tam dağılıma dönülür.
// Not: telafili NEE tek başına yansız DEĞİLDİR (sönük yönlerde pdf = 0), yalnız BSDF
// örneklemesiyle MIS içinde yansızdır; MIS'siz kullanımlar sampleLiFull'u çağırır.
void EnvironmentLight::buildDistribution() {
    m_width = 0;
    m_height = 0;
    m_full = Distribution{};
    m_mis = Distribution{};
    m_misCompensated = false;
    if (!m_envMap || m_envMap->width() <= 0 || m_envMap->height() <= 0) return;

    const int w = m_envMap->width();
    const int h = m_envMap->height();
    m_width = w;
    m_height = h;
    m_full.func.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    std::vector<float> sinRow(static_cast<size_t>(h));
    double weighted = 0.0, weights = 0.0;
    for (int y = 0; y < h; ++y) {
        const float sinTheta = std::sin(PI * (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
        sinRow[static_cast<size_t>(y)] = sinTheta;
        for (int x = 0; x < w; ++x) {
            float L = m_envMap->getPixel(x, y).luminance();
            if (!(L > 0.0f)) L = 0.0f; // NaN ve negatifleri ele
            m_full.func[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = L * sinTheta;
            weighted += static_cast<double>(L) * sinTheta;
            weights += sinTheta;
        }
    }
    buildCdf(m_full);
    if (!m_full.valid()) return;

    const float meanL = weights > 0.0 ? static_cast<float>(weighted / weights) : 0.0f;
    m_mis.func.resize(m_full.func.size());
    double misTotal = 0.0, fullTotal = 0.0;
    for (int y = 0; y < h; ++y) {
        const float s = sinRow[static_cast<size_t>(y)];
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            float L = m_envMap->getPixel(x, y).luminance();
            if (!(L > 0.0f)) L = 0.0f;
            // Ortalamaya yuvarlama hatası kadar yakın pikseller düzgün sayılır.
            const float excess = L - meanL;
            m_mis.func[i] = excess > 1e-4f * meanL ? excess * s : 0.0f;
            misTotal += m_mis.func[i];
            fullTotal += m_full.func[i];
        }
    }
    // Ortalamanın üstünde kalan enerji ihmal edilebilirse (neredeyse düzgün harita) telafi
    // kazandırmaz; parlak bölgeyi kaçırma riskine girmeden tam dağılım kullanılır.
    m_misCompensated = gMisCompensation.load() && misTotal > 1e-3 * fullTotal;
    if (m_misCompensated) {
        buildCdf(m_mis);
        m_misCompensated = m_mis.valid();
    }
    if (!m_misCompensated) m_mis = m_full;
}

void EnvironmentLight::buildCdf(Distribution& d) const {
    const int w = m_width, h = m_height;
    d.marginalCdf.assign(static_cast<size_t>(h) + 1, 0.0f);
    d.condCdf.assign(static_cast<size_t>(h) * static_cast<size_t>(w + 1), 0.0f);
    for (int y = 0; y < h; ++y) {
        float* row = d.condCdf.data() + static_cast<size_t>(y) * static_cast<size_t>(w + 1);
        row[0] = 0.0f;
        for (int x = 0; x < w; ++x) row[x + 1] = row[x] + d.func[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        d.marginalCdf[static_cast<size_t>(y) + 1] = d.marginalCdf[static_cast<size_t>(y)] + row[w];
    }
    d.funcInt = d.marginalCdf[static_cast<size_t>(h)];
    if (!(d.funcInt > 0.0f)) {
        d.funcInt = 0.0f;
        d.marginalCdf.clear();
        return;
    }
    const float inv = 1.0f / d.funcInt;
    for (float& c : d.marginalCdf) c *= inv;
    for (int y = 0; y < h; ++y) {
        float* row = d.condCdf.data() + static_cast<size_t>(y) * static_cast<size_t>(w + 1);
        const float rowSum = row[w];
        if (rowSum <= 0.0f) continue;
        const float invRow = 1.0f / rowSum;
        for (int x = 0; x <= w; ++x) row[x] *= invRow;
    }
}

// Işık örnekleme: iki rastgele sayıyla önce satır, sonra sütun seçilir.
// Görüntü uzayındaki pdf (u,v başına) katı açıya çevrilirken Jakobiyen
// dω = 2π·π·sin(θ) du dv kullanılır: pdf_ω = pdf_uv / (2π² sin θ).
void EnvironmentLight::setSun(const Vec3f& direction, float cosMax, const Color3f& radiance) {
    m_sunDir = direction.normalized();
    m_sunCosMax = std::clamp(cosMax, -0.999f, 0.9999999f);
    m_sunRadiance = radiance * m_intensity;
    const float omega = TWO_PI * (1.0f - m_sunCosMax);
    const float sunPower = std::max(0.0f, m_sunRadiance.luminance()) * omega;
    const float skyPower = 4.0f * PI * m_meanLuminance;
    if (!(sunPower > 0.0f)) {
        m_sunPdfWeight = 0.0f;
        return;
    }
    // Güce orantılı, ama ikisi de aç kalmasın (gölgede gök, açıkta güneş önemli).
    m_sunPdfWeight = std::clamp(sunPower / (sunPower + skyPower), 0.15f, 0.85f);
    if (!m_full.valid()) m_sunPdfWeight = 1.0f;
}

float EnvironmentLight::importance(const Vec3f& /*p*/, const Vec3f& n) const {
    float imp = PI * m_meanLuminance;
    if (m_sunPdfWeight > 0.0f)
        imp += std::max(0.0f, m_sunRadiance.luminance()) * TWO_PI * (1.0f - m_sunCosMax) * receiverCosine(n, m_sunDir);
    return imp;
}

LightSample EnvironmentLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sampleIn) const {
    LightSample ls;
    ls.distance = INFINITY_F;
    Vec2f sample = sampleIn;
    if (m_sunPdfWeight > 0.0f) {
        if (sample.x < m_sunPdfWeight) {
            // Güneş konisinde düzgün yön (cosθ ∈ [cosMax, 1] düzgün).
            const float ux = std::min(sample.x / m_sunPdfWeight, 0.99999994f);
            const float cosT = 1.0f - ux * (1.0f - m_sunCosMax);
            const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
            const float phi = TWO_PI * sample.y;
            const Frame f(m_sunDir);
            ls.wi = f.toWorld(Vec3f(sinT * std::cos(phi), sinT * std::sin(phi), cosT)).normalized();
            ls.Li = eval(ls.wi);
            ls.pdf = pdfLi(ls.wi);
            return ls;
        }
        if (m_sunPdfWeight >= 1.0f) return ls;
        sample.x = std::min((sample.x - m_sunPdfWeight) / (1.0f - m_sunPdfWeight), 0.99999994f);
    }
    (void)si;

    LightSample img = sampleImage(m_mis, sample);
    if (m_sunPdfWeight > 0.0f && img.pdf > 0.0f) img.pdf = pdfLi(img.wi); // karışım pdf'i (güneş + görüntü)
    return img;
}

LightSample EnvironmentLight::sampleImage(const Distribution& d, const Vec2f& sample) const {
    LightSample ls;
    ls.distance = INFINITY_F;
    if (!d.valid() || m_width <= 0) {
        ls.wi = uniformSampleSphere(sample);
        ls.pdf = uniformSpherePdf();
        ls.Li = eval(ls.wi);
        return ls;
    }
    CdfPick row = sampleCdf(d.marginalCdf.data(), m_height, sample.y);
    const float* cond = d.condCdf.data() + static_cast<size_t>(row.index) * static_cast<size_t>(m_width + 1);
    CdfPick col = sampleCdf(cond, m_width, sample.x);

    float theta = row.coord * PI;
    float sinTheta = std::max(std::sin(theta), 1e-6f);
    ls.wi = equirectDirection(col.coord, row.coord, m_rotation).normalized();
    ls.pdf = (row.pdf * col.pdf) * static_cast<float>(m_width * m_height) / (TWO_PI * PI * sinTheta);
    ls.Li = eval(ls.wi);
    return ls;
}

LightSample EnvironmentLight::sampleLiFull(const Vec2f& sample) const {
    // Güneş + tam görüntü dağılımı. Güneş varsa önce koni seçimi (sampleLi ile aynı karışım).
    if (m_sunPdfWeight > 0.0f && sample.x < m_sunPdfWeight) {
        SurfaceInteraction dummy;
        return sampleLi(dummy, sample); // güneş dalı telafiden etkilenmez
    }
    Vec2f s = sample;
    if (m_sunPdfWeight > 0.0f) {
        if (m_sunPdfWeight >= 1.0f) return LightSample{};
        s.x = std::min((sample.x - m_sunPdfWeight) / (1.0f - m_sunPdfWeight), 0.99999994f);
    }
    LightSample ls = sampleImage(m_full, s);
    if (m_sunPdfWeight > 0.0f && ls.pdf > 0.0f) ls.pdf = pdfLiFull(ls.wi);
    return ls;
}

float EnvironmentLight::pdfLiFull(const Vec3f& wi) const {
    if (m_sunPdfWeight <= 0.0f) return solidAnglePdf(m_full, wi);
    float pdf = 0.0f;
    if (wi.normalized().dot(m_sunDir) >= m_sunCosMax) pdf += m_sunPdfWeight / (TWO_PI * (1.0f - m_sunCosMax));
    if (m_sunPdfWeight < 1.0f) pdf += (1.0f - m_sunPdfWeight) * solidAnglePdf(m_full, wi);
    return pdf;
}

float EnvironmentLight::solidAnglePdf(const Distribution& dist, const Vec3f& wi) const {
    if (!dist.valid() || m_width <= 0 || m_height <= 0) return uniformSpherePdf();

    Vec3f d = wi.normalized();
    Vec2f uv = directionToEquirect(d, m_rotation);
    int x = std::min(static_cast<int>(uv.x * static_cast<float>(m_width)), m_width - 1);
    int y = std::min(static_cast<int>(uv.y * static_cast<float>(m_height)), m_height - 1);
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - d.y * d.y));
    if (sinTheta < 1e-4f) return 0.0f;

    float f = dist.func[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    return (f / dist.funcInt) * static_cast<float>(m_width * m_height) / (TWO_PI * PI * sinTheta);
}

float EnvironmentLight::imagePdf(const Vec3f& wi) const {
    return solidAnglePdf(wi);
}

float EnvironmentLight::pdfLi(const Vec3f& wi) const {
    if (m_sunPdfWeight <= 0.0f) return solidAnglePdf(wi);
    float pdf = 0.0f;
    if (wi.normalized().dot(m_sunDir) >= m_sunCosMax) pdf += m_sunPdfWeight / (TWO_PI * (1.0f - m_sunCosMax));
    if (m_sunPdfWeight < 1.0f) pdf += (1.0f - m_sunPdfWeight) * solidAnglePdf(wi);
    return pdf;
}

float EnvironmentLight::pdfLi(const Vec3f& wi, const Vec3f&) const {
    return pdfLi(wi);
}

Color3f EnvironmentLight::eval(const Vec3f& direction) const {
    Color3f sun;
    if (m_sunPdfWeight > 0.0f && direction.normalized().dot(m_sunDir) >= m_sunCosMax) sun = m_sunRadiance;
    if (!m_envMap || m_envMap->width() <= 0) {
        return Color3f(0.5f) * m_intensity + sun;
    }
    Vec2f uv = directionToEquirect(direction, m_rotation);
    return sampleEquirect(*m_envMap, uv.x, uv.y) * m_intensity + sun;
}

Color3f EnvironmentLight::power() const {
    if (!m_envMap || m_width <= 0) {
        return Color3f(0.5f) * m_intensity * 4.0f * PI;
    }
    // Katı açıyla ağırlıklı ortalama radyans × 4π.
    Color3f sum = Color3f::black();
    float wsum = 0.0f;
    for (int y = 0; y < m_height; y += 4) {
        float s = std::sin(PI * (static_cast<float>(y) + 0.5f) / static_cast<float>(m_height));
        for (int x = 0; x < m_width; x += 4) {
            sum += m_envMap->getPixel(x, y) * s;
            wsum += s;
        }
    }
    return (wsum > 0.0f ? sum / wsum : sum) * m_intensity * 4.0f * PI;
}

} // namespace photon
