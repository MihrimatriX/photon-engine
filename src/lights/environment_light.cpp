#include "lights/environment_light.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"

#include <algorithm>
#include <cmath>

namespace photon {
namespace {

Vec3f equirectToDirection(float u, float v, float rotation) {
    float theta = v * PI;
    float a = u * TWO_PI - PI - rotation;
    float st = std::sin(theta);
    return Vec3f(st * std::cos(a), std::cos(theta), -st * std::sin(a));
}

struct CdfPick {
    int index = 0;
    float pdf = 0.0f;
    float coord = 0.0f;
};

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

EnvironmentLight::EnvironmentLight(const Image* envMap, float rotation, float intensity)
    : m_envMap(envMap), m_rotation(rotation), m_intensity(intensity) {
    buildDistribution();
}

void EnvironmentLight::buildDistribution() {
    m_width = 0;
    m_height = 0;
    m_funcInt = 0.0f;
    m_func.clear();
    m_marginalCdf.clear();
    m_condCdf.clear();
    if (!m_envMap || m_envMap->width() <= 0 || m_envMap->height() <= 0) return;

    const int w = m_envMap->width();
    const int h = m_envMap->height();
    m_width = w;
    m_height = h;
    m_func.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    m_marginalCdf.assign(static_cast<size_t>(h) + 1, 0.0f);
    m_condCdf.assign(static_cast<size_t>(h) * static_cast<size_t>(w + 1), 0.0f);

    for (int y = 0; y < h; ++y) {
        float theta = PI * (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        float sinTheta = std::sin(theta);
        float* row = m_condCdf.data() + static_cast<size_t>(y) * static_cast<size_t>(w + 1);
        row[0] = 0.0f;
        for (int x = 0; x < w; ++x) {
            float f = m_envMap->getPixel(x, y).luminance() * sinTheta;
            m_func[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = f;
            row[x + 1] = row[x] + f;
        }
        m_marginalCdf[static_cast<size_t>(y) + 1] =
            m_marginalCdf[static_cast<size_t>(y)] + row[w];
    }

    m_funcInt = m_marginalCdf[static_cast<size_t>(h)];
    if (m_funcInt <= 0.0f) {
        m_funcInt = 0.0f;
        m_marginalCdf.clear();
        return;
    }

    float inv = 1.0f / m_funcInt;
    for (float& c : m_marginalCdf) c *= inv;
    for (int y = 0; y < h; ++y) {
        float* row = m_condCdf.data() + static_cast<size_t>(y) * static_cast<size_t>(w + 1);
        float rowSum = row[w];
        if (rowSum <= 0.0f) continue;
        float invRow = 1.0f / rowSum;
        for (int x = 0; x <= w; ++x) row[x] *= invRow;
    }
}

LightSample EnvironmentLight::sampleLi(const SurfaceInteraction&, const Vec2f& sample) const {
    LightSample ls;
    ls.distance = INFINITY_F;

    if (m_marginalCdf.empty() || m_funcInt <= 0.0f || m_width <= 0) {
        ls.wi = uniformSampleSphere(sample);
        ls.pdf = uniformSpherePdf();
        ls.Li = eval(ls.wi);
        return ls;
    }

    CdfPick row = sampleCdf(m_marginalCdf.data(), m_height, sample.y);
    const float* cond = m_condCdf.data() + static_cast<size_t>(row.index) * static_cast<size_t>(m_width + 1);
    CdfPick col = sampleCdf(cond, m_width, sample.x);

    float theta = row.coord * PI;
    float sinTheta = std::max(std::sin(theta), 1e-6f);
    ls.wi = equirectToDirection(col.coord, row.coord, m_rotation).normalized();
    ls.pdf = (row.pdf * col.pdf) * static_cast<float>(m_width * m_height) / (TWO_PI * PI * sinTheta);
    ls.Li = eval(ls.wi);
    return ls;
}

float EnvironmentLight::solidAnglePdf(const Vec3f& wi) const {
    if (m_funcInt <= 0.0f || m_width <= 0 || m_height <= 0) return uniformSpherePdf();

    Vec3f d = wi.normalized();
    Vec2f uv = directionToEquirect(d, m_rotation);
    int x = std::min(static_cast<int>(uv.x * static_cast<float>(m_width)), m_width - 1);
    int y = std::min(static_cast<int>(uv.y * static_cast<float>(m_height)), m_height - 1);
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    float sinTheta = std::sin(std::acos(std::clamp(d.y, -1.0f, 1.0f)));
    if (sinTheta < 1e-4f) return 0.0f;

    float f = m_func[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    return (f / m_funcInt) * static_cast<float>(m_width * m_height) / (TWO_PI * PI * sinTheta);
}

float EnvironmentLight::pdfLi(const Vec3f& wi) const {
    return solidAnglePdf(wi);
}

float EnvironmentLight::pdfLi(const Vec3f& wi, const Vec3f&) const {
    return solidAnglePdf(wi);
}

Color3f EnvironmentLight::eval(const Vec3f& direction) const {
    if (!m_envMap) {
        return Color3f(0.5f) * m_intensity;
    }

    Vec2f uv = directionToEquirect(direction, m_rotation);
    return m_envMap->sampleBilinear(uv.x, uv.y) * m_intensity;
}

Color3f EnvironmentLight::power() const {
    if (!m_envMap) {
        return Color3f(0.5f) * m_intensity * 4.0f * PI;
    }

    Color3f avgColor = Color3f::black();
    avgColor += m_envMap->getPixel(0, 0);
    avgColor += m_envMap->getPixel(m_envMap->width() / 2, m_envMap->height() / 2);
    avgColor += m_envMap->getPixel(m_envMap->width() - 1, m_envMap->height() - 1);
    avgColor = avgColor / 3.0f;

    return avgColor * m_intensity * 4.0f * PI;
}

} // namespace photon
