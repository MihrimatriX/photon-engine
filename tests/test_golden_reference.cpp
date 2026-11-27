// Altın görüntü regresyonu (F2.8), referans bağımsız bir motordan: Mitsuba 3.
//
// tests/data/golden/<sahne>.photon küçük çözünürlüklü referans sahneleridir (bench/reference/
// refscenes.py ile aynı tanım) ve <sahne>_mitsuba.exr aynı sahnenin Mitsuba 3 ile çok yüksek
// örnekle (gürültüsüz sayılacak kadar) render'ıdır. Üreten betik: bench/reference/make_golden.py.
//
// Test, sahneyi uygulamanın son render'ıyla aynı yoldan (loadProject → buildRenderScene → CPU
// RenderDevice → RenderSession) sabit örnekle render eder ve iki şeyi kontrol eder:
//   1. kanal başına görüntü ortalaması referansın ±%1.5'i içinde (toplam enerji: yanlı bir
//      tahminci, kayıp bir lob, yanlış bir pdf ya da yanlış ışık birimi bunu bozar);
//   2. 8×8 blok ortalamalarının göreli farkı: medyan ve %95'lik eşik (yerel sapma: yanlış yöne
//      kayan kostik, aynalanmış HDRI, eksik gölge...).
// Render deterministik olduğu için (örnekleyici piksel ve örnek indeksine bağlı) eşikler
// sabittir; değerler gürültü tabanının üstünde ama gerçek bir sapmayı yakalayacak kadar dar.
#include <gtest/gtest.h>

#include "camera/perspective_camera.h"
#include "core/image/image_io.h"
#include "engine/render_session.h"
#include "scene/document.h"
#include "scene/project_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace photon;

namespace {

struct GoldenCase {
    const char* name;
    int spp;
    double meanTol;     ///< kanal ortalaması göreli toleransı
    double blockMedian; ///< 8×8 blok |göreli fark| medyanı üst sınırı
    double blockP95;    ///< 8×8 blok |göreli fark| %95'lik üst sınırı
    double expectedMean = 1.0; ///< beklenen Photon/Mitsuba ortalama oranı (bilinen model farkı)
};

std::shared_ptr<const Camera> orbitCamera(const nlohmann::json& c, float aspect) {
    // photon_render ile aynı orbit → bakış dönüşümü (main.cpp cameraFromJson, perspektif kolu).
    const auto& t = c["target"];
    const Vec3f target(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
    const float r = c.value("radius", 5.0f), th = c.value("theta", 1.15f), ph = c.value("phi", 0.45f);
    const Vec3f eye = target + Vec3f(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)) * r;
    return std::make_shared<PerspectiveCamera>(eye, target, Vec3f(0, 1, 0), c.value("fov", 35.0f), aspect);
}

double luminanceMean(const Image& img, int x0, int y0, int w, int h) {
    double s = 0.0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const Color3f p = img.getPixel(x, y);
            s += (p.r + p.g + p.b) / 3.0;
        }
    return s / (w * h);
}

void runGolden(const GoldenCase& gc) {
    const std::string dir = std::string(PHOTON_TEST_DATA) + "/golden/";
    SceneGraph graph;
    ProjectData data;
    std::string err;
    ASSERT_TRUE(loadProject(dir + gc.name + ".photon", graph, data, &err)) << err;
    const std::optional<Image> ref = loadImageEXR(dir + gc.name + "_mitsuba.exr");
    ASSERT_TRUE(ref.has_value()) << "referans okunamadı";

    EnvironmentCache cache;
    auto scene = buildRenderScene(graph, data.lights, data.environment, cache, false);
    RenderSettings rs;
    rs.width = ref->width();
    rs.height = ref->height();
    rs.maxBounces = 64;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
    rs.denoiseEnabled = false;

    auto dev = createDevice("cpu");
    ASSERT_TRUE(dev);
    dev->setScene(scene);
    RenderSession session(*dev);
    SessionConfig sc;
    sc.mode = RenderMode::Final;
    sc.targetSpp = gc.spp;
    sc.batchSeconds = 10.0;
    session.begin(orbitCamera(data.camera, static_cast<float>(rs.width) / rs.height), rs, sc);
    while (session.advance()) {
    }
    const Image img = session.resolve();
    ASSERT_EQ(img.width(), ref->width());
    ASSERT_EQ(img.height(), ref->height());

    // 1. Kanal ortalamaları.
    double sa[3] = {0, 0, 0}, sr[3] = {0, 0, 0};
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            for (int c = 0; c < 3; ++c) {
                sa[c] += img.getPixel(x, y)[c];
                sr[c] += ref->getPixel(x, y)[c];
            }
    for (int c = 0; c < 3; ++c) {
        ASSERT_GT(sr[c], 0.0);
        EXPECT_NEAR(sa[c] / sr[c], gc.expectedMean, gc.meanTol) << gc.name << " kanal " << c;
    }

    // 2. 8×8 blok göreli farkı (neredeyse siyah bloklar hariç).
    constexpr int B = 8;
    std::vector<double> rel;
    double maxRef = 0.0;
    for (int by = 0; by + B <= img.height(); by += B)
        for (int bx = 0; bx + B <= img.width(); bx += B) maxRef = std::max(maxRef, luminanceMean(*ref, bx, by, B, B));
    for (int by = 0; by + B <= img.height(); by += B)
        for (int bx = 0; bx + B <= img.width(); bx += B) {
            const double r = luminanceMean(*ref, bx, by, B, B);
            if (r < 0.02 * maxRef) continue;
            rel.push_back(std::abs(luminanceMean(img, bx, by, B, B) - r) / r);
        }
    ASSERT_FALSE(rel.empty());
    std::sort(rel.begin(), rel.end());
    const double median = rel[rel.size() / 2];
    const double p95 = rel[std::min(rel.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(rel.size())))];
    EXPECT_LT(median, gc.blockMedian) << gc.name;
    EXPECT_LT(p95, gc.blockP95) << gc.name;
    std::printf("[golden] %s: ortalama oranı %.4f %.4f %.4f, blok medyan %.4f, p95 %.4f\n", gc.name, sa[0] / sr[0],
                sa[1] / sr[1], sa[2] / sr[2], median, p95);
}

} // namespace

// Eşikler, ilk ölçümün (GCC 13, x86-64) yaklaşık 2–3 katı; render deterministik olduğu için
// aynı derleyicide sonuç hep aynıdır, pay derleyici/komut kümesi farkları içindir.
// Ölçüm (2026-11-27): difüz Cornell oran 0.9998, medyan 0.0016, p95 0.0073; kostikli Cornell
// 0.9995, 0.0147, 0.0739; HDRI malzemeleri 1.0155, 0.0102, 0.0673.
TEST(GoldenReference, CornellDiffuseMatchesMitsuba) { runGolden({"cornell_diffuse", 256, 0.005, 0.005, 0.02}); }
TEST(GoldenReference, CornellSpecularMatchesMitsuba) { runGolden({"cornell_specular", 256, 0.01, 0.035, 0.16}); }
// HDRI sahnesinde beklenen oran 1 değil: pürüzlü beyaz metaller Photon'da Kulla–Conty enerji
// telafisi ve yüksekliğe bağlı Smith G2 ile Mitsuba'nın tek saçılmalı, ayrık G1·G1 GGX'inden
// parlaktır (r = 0.5 küresi ≈ +%12, bkz. bench/reference/REPORT.md). Difüz, ayna ve cam bölgeleri
// birebir. Oran 1'e düşerse telafi bozulmuş demektir; yükselirse enerji fazlası vardır.
TEST(GoldenReference, HdriMaterialsMatchesMitsuba) { runGolden({"hdri_materials", 256, 0.006, 0.025, 0.12, 1.0155}); }
