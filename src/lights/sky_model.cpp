// sky_model.cpp — Nishita tek saçılmalı gökyüzü ve NOAA güneş konumu.
#include "lights/sky_model.h"
#include "lights/environment_light.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace photon {

namespace {

// Metre cinsinden: yer yarıçapı, atmosfer üst sınırı, ölçek yükseklikleri.
constexpr double kEarth = 6360e3;
constexpr double kAtmosphere = 6420e3;
constexpr double kHr = 7994.0;
constexpr double kHm = 1200.0;
// Deniz seviyesinde saçılma katsayıları (1/m), RGB ≈ 680/550/440 nm.
constexpr double kBetaR[3] = {5.802e-6, 13.558e-6, 33.1e-6};
constexpr double kBetaMBase = 3.996e-6; // turbidity 2.5'te; sönüm = saçılma / 0.9
constexpr double kBetaO[3] = {0.650e-6, 1.881e-6, 0.085e-6};
constexpr double kMieG = 0.76;

struct Atmos {
    double betaM = kBetaMBase;
    double ozone = 1.0;
};

/// Orijin merkezli kürenin r yarıçapıyla ışının çıkış (uzak) kesişimi; yoksa −1.
double sphereExit(const double o[3], const double d[3], double r) {
    const double b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    const double c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - r * r;
    const double disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + std::sqrt(disc);
}

/// Işın yer küresine çarpıyorsa yakın kesişim, yoksa −1.
double earthHit(const double o[3], const double d[3]) {
    const double b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    const double c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - kEarth * kEarth;
    const double disc = b * b - c;
    if (disc < 0.0) return -1.0;
    const double t = -b - std::sqrt(disc);
    return t > 0.0 ? t : -1.0;
}

void densities(const double p[3], double& rho_r, double& rho_m, double& rho_o) {
    const double h = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - kEarth;
    rho_r = std::exp(-h / kHr);
    rho_m = std::exp(-h / kHm);
    rho_o = std::max(0.0, 1.0 - std::abs(h - 25000.0) / 15000.0); // 10–40 km çadır profili
}

/// p'den güneşe optik derinlik (Rayleigh, Mie, ozon yoğunluk integralleri).
/// Güneş yer tarafından örtülüyorsa false.
bool lightDepth(const double p[3], const double s[3], double od[3]) {
    if (earthHit(p, s) > 0.0) return false;
    const double t = sphereExit(p, s, kAtmosphere);
    od[0] = od[1] = od[2] = 0.0;
    if (t <= 0.0) return true;
    constexpr int kSteps = 8;
    const double ds = t / kSteps;
    for (int i = 0; i < kSteps; ++i) {
        const double tt = (i + 0.5) * ds;
        const double q[3] = {p[0] + s[0] * tt, p[1] + s[1] * tt, p[2] + s[2] * tt};
        double r, m, o;
        densities(q, r, m, o);
        od[0] += r * ds;
        od[1] += m * ds;
        od[2] += o * ds;
    }
    return true;
}

void transmittance(const Atmos& a, const double od[3], double T[3]) {
    for (int c = 0; c < 3; ++c)
        T[c] = std::exp(-(kBetaR[c] * od[0] + a.betaM / 0.9 * od[1] + a.ozone * kBetaO[c] * od[2]));
}

/// Gözden d yönünde atmosferin saçtığı radyans (güneş ışınımı 1 için).
void inScatter(const Atmos& a, const double eye[3], const double d[3], const double s[3], double L[3],
               double* groundT = nullptr) {
    L[0] = L[1] = L[2] = 0.0;
    double tEnd = sphereExit(eye, d, kAtmosphere);
    const double tGround = earthHit(eye, d);
    if (tGround > 0.0) tEnd = tGround;
    if (tEnd <= 0.0) return;
    const int steps = d[1] < 0.15 ? 32 : 16; // ufka yakın uzun yollar daha ince adım ister
    const double ds = tEnd / steps;
    double odView[3] = {0, 0, 0};
    double sumR[3] = {0, 0, 0}, sumM[3] = {0, 0, 0};
    for (int i = 0; i < steps; ++i) {
        const double tt = (i + 0.5) * ds;
        const double p[3] = {eye[0] + d[0] * tt, eye[1] + d[1] * tt, eye[2] + d[2] * tt};
        double r, m, o;
        densities(p, r, m, o);
        odView[0] += r * ds;
        odView[1] += m * ds;
        odView[2] += o * ds;
        double odL[3];
        if (!lightDepth(p, s, odL)) continue;
        const double od[3] = {odView[0] + odL[0], odView[1] + odL[1], odView[2] + odL[2]};
        double T[3];
        transmittance(a, od, T);
        for (int c = 0; c < 3; ++c) {
            sumR[c] += T[c] * r * ds;
            sumM[c] += T[c] * m * ds;
        }
    }
    const double mu = d[0] * s[0] + d[1] * s[1] + d[2] * s[2];
    const double phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    const double g = kMieG;
    const double phaseM = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                          ((2.0 + g * g) * std::pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    for (int c = 0; c < 3; ++c) L[c] = sumR[c] * kBetaR[c] * phaseR + sumM[c] * a.betaM * phaseM;
    if (groundT) {
        double T[3];
        transmittance(a, odView, T);
        for (int c = 0; c < 3; ++c) groundT[c] = tGround > 0.0 ? T[c] : 0.0;
    }
}

} // namespace

SkyResult buildPhysicalSky(const SkyParams& params) {
    SkyResult out;
    Atmos a;
    a.betaM = kBetaMBase * std::max(0.2f, params.turbidity) / 2.5;
    a.ozone = std::max(0.0f, params.ozone);

    const float el = params.sunElevationDeg * DEG_TO_RAD;
    const float az = params.sunAzimuthDeg * DEG_TO_RAD;
    out.sunDirection = Vec3f(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)).normalized();
    const double s[3] = {out.sunDirection.x, out.sunDirection.y, out.sunDirection.z};
    const double eye[3] = {0.0, kEarth + 2.0, 0.0};
    const float E = std::max(0.0f, params.sunStrength);

    // Güneş diski: gözden güneşe geçirgenlik × ışınım / disk katı açısı.
    const float halfAngle = std::clamp(params.sunSizeDeg, 0.05f, 20.0f) * 0.5f * DEG_TO_RAD;
    out.sunCosMax = std::cos(halfAngle);
    const float omegaSun = TWO_PI * (1.0f - out.sunCosMax);
    double odSun[3];
    if (lightDepth(eye, s, odSun)) {
        double T[3];
        transmittance(a, odSun, T);
        out.sunRadiance = Color3f(static_cast<float>(T[0]), static_cast<float>(T[1]), static_cast<float>(T[2])) *
                          (E / omegaSun);
    }

    // Yerin aydınlığı (ufkun altı): güneşin yatay ışınımı + zenit gök radyansı × π.
    double zen[3];
    const double up[3] = {0.0, 1.0, 0.0};
    inScatter(a, eye, up, s, zen);
    const Color3f sunAtGround = out.sunRadiance * omegaSun;
    Color3f groundE = sunAtGround * std::max(0.0f, out.sunDirection.y);
    for (int c = 0; c < 3; ++c) groundE[c] += static_cast<float>(PI * zen[c] * E);
    const Color3f groundL = groundE * (std::clamp(params.groundAlbedo, 0.0f, 1.0f) * INV_PI);

    const int W = std::max(16, params.width), H = std::max(8, W / 2);
    auto img = std::make_shared<Image>(W, H);
    // Kendi iş parçacıkları: gök, render havuzunun bir görevinin içinden de kurulabilir
    // (sahne derlemesi); aynı havuza iç içe iş göndermek kilitlenme riski taşır.
    auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {
                const Vec3f d = equirectDirection((static_cast<float>(x) + 0.5f) / W, (static_cast<float>(y) + 0.5f) / H, 0.0f);
                const double dd[3] = {d.x, d.y, d.z};
                double L[3], Tg[3] = {0, 0, 0};
                inScatter(a, eye, dd, s, L, Tg);
                Color3f c(static_cast<float>(L[0]), static_cast<float>(L[1]), static_cast<float>(L[2]));
                c *= E;
                if (d.y < 0.0f) {
                    c += Color3f(groundL.r * static_cast<float>(Tg[0]), groundL.g * static_cast<float>(Tg[1]),
                                 groundL.b * static_cast<float>(Tg[2]));
                }
                img->setPixel(x, y, c);
            }
        }
    };
    const int nThreads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    std::vector<std::thread> workers;
    for (int t = 0; t < nThreads; ++t) {
        const int y0 = H * t / nThreads, y1 = H * (t + 1) / nThreads;
        workers.emplace_back(rows, y0, y1);
    }
    for (auto& w : workers) w.join();
    out.image = std::move(img);
    return out;
}

void solarPosition(float latitudeDeg, float longitudeDeg, int dayOfYear, float localHours, float utcOffsetHours,
                   float& elevationDeg, float& compassAzimuthDeg) {
    // NOAA "General Solar Position Calculations": kesirli yıl γ, denklem zamanı, deklinasyon.
    const double gamma = 2.0 * PI / 365.0 * (dayOfYear - 1 + (localHours - 12.0) / 24.0);
    const double eqTime = 229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                                    0.014615 * std::cos(2 * gamma) - 0.040849 * std::sin(2 * gamma));
    const double decl = 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
                        0.006758 * std::cos(2 * gamma) + 0.000907 * std::sin(2 * gamma) -
                        0.002697 * std::cos(3 * gamma) + 0.00148 * std::sin(3 * gamma);
    const double timeOffset = eqTime + 4.0 * longitudeDeg - 60.0 * utcOffsetHours; // dakika
    const double tst = localHours * 60.0 + timeOffset;                              // gerçek güneş zamanı
    const double ha = (tst / 4.0 - 180.0) * DEG_TO_RAD;                              // saat açısı
    const double lat = latitudeDeg * DEG_TO_RAD;
    double cosZen = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(ha);
    cosZen = std::clamp(cosZen, -1.0, 1.0);
    const double zen = std::acos(cosZen);
    elevationDeg = static_cast<float>(90.0 - zen * RAD_TO_DEG);
    // Azimut (kuzeyden saat yönünde).
    const double sinZen = std::sin(zen);
    double az = 0.0;
    if (sinZen > 1e-6) {
        double cosAz = (std::sin(decl) - std::sin(lat) * cosZen) / (std::cos(lat) * sinZen);
        cosAz = std::clamp(cosAz, -1.0, 1.0);
        az = std::acos(cosAz) * RAD_TO_DEG;
        if (ha > 0.0) az = 360.0 - az; // öğleden sonra batıda
    }
    compassAzimuthDeg = static_cast<float>(az);
}

} // namespace photon
