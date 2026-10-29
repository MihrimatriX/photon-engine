// Cihaz ve oturum katmanı testleri (F9.7):
//   - örnekleyici (piksel, örnek indeksi)'ne göre deterministik: 8 örneklik bir parti sekiz
//     tek örneklik pass ile bit bit aynı film (viewport ve son render aynı görüntüye yakınsar);
//   - Final oturumu hedef örnekte, süre sınırında ve uyarlamalı yakınsamada durur;
//   - Etkileşimli oturum her adımda tek örnek ekler, hedef yükseltilince birikim korunur;
//   - cihaz listesinin ilk elemanı kullanılabilir CPU; bilinmeyen cihaz açık bir hatayla reddedilir;
//   - cihaz bir çıktıyı desteklemezse oturum onu kapatır ve uyarır (GPU arka uçlarına hazırlık).
#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "engine/render_session.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "materials/disney.h"
#include "materials/lambertian.h"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace photon;

namespace {

std::shared_ptr<Scene> makeScene() {
    static const Lambertian floorMat(Color3f(0.6f));
    static const DisneyMaterial plastic(Color3f(0.8f, 0.2f, 0.1f), 0.0f, 0.3f, 0.5f);
    auto scene = std::make_shared<Scene>();
    const Vec3f up(0, 1, 0);
    std::vector<Vec3f> fp = {{-3, 0, -3}, {3, 0, -3}, {3, 0, 3}, {-3, 0, 3}};
    scene->addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up}, std::vector<Vec2f>{},
                                                   std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
    scene->addShape(std::make_shared<Sphere>(Vec3f(0, 0.6f, 0), 0.6f, &plastic));
    scene->addLight(std::make_shared<AreaLight>(Vec3f(-1.0f, 2.5f, 1.0f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(6.0f)));
    scene->setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.3f));
    scene->buildAccelerator();
    return scene;
}

std::shared_ptr<const Camera> makeCam() {
    return std::make_shared<PerspectiveCamera>(Vec3f(0, 1.2f, 3.5f), Vec3f(0, 0.5f, 0), Vec3f(0, 1, 0), 45.0f, 1.0f);
}

RenderSettings smallSettings() {
    RenderSettings rs;
    rs.width = rs.height = 24;
    rs.maxBounces = 4;
    return rs;
}

/// Oturumu kullanmadan önce desteklenmeyen özellikleri bildiren sahte cihaz: CPU'ya devreder
/// ama AOV/geçiş/uyarlamalı desteği yokmuş gibi davranır (ilk GPU arka ucunun durumu).
class LimitedDevice final : public RenderDevice {
public:
    LimitedDevice() : m_cpu(createDevice("cpu")) { m_info = m_cpu->info(); m_info.name = "Sınırlı"; }
    const DeviceInfo& info() const override { return m_info; }
    DeviceFeatures features() const override { return DeviceFeatures{}; }
    void setScene(std::shared_ptr<const Scene> scene) override { m_cpu->setScene(std::move(scene)); }
    bool hasScene() const override { return m_cpu->hasScene(); }
    SampleBatchResult render(const Camera& c, const RenderSettings& s, RenderTargets& t, int first, int count,
                             const std::atomic<bool>* cancel) override {
        EXPECT_EQ(t.aovs, nullptr);
        EXPECT_EQ(t.passes, nullptr);
        EXPECT_FALSE(s.adaptiveSampling);
        return m_cpu->render(c, s, t, first, count, cancel);
    }

private:
    std::unique_ptr<RenderDevice> m_cpu;
    DeviceInfo m_info;
};

} // namespace

TEST(RenderDevice, CpuIsFirstAndUsableUnknownIdIsRejected) {
    const auto& devices = enumerateDevices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices.front().id, "cpu");
    EXPECT_EQ(devices.front().kind, DeviceKind::Cpu);
    EXPECT_TRUE(devices.front().usable);
    EXPECT_GT(devices.front().threads, 0);
    for (const DeviceInfo& d : devices)
        if (d.kind != DeviceKind::Cpu) EXPECT_FALSE(d.note.empty()) << d.id; // GPU: neden kullanılamadığı yazılı

    auto autoDev = createDevice("auto");
    ASSERT_NE(autoDev, nullptr);
    EXPECT_EQ(autoDev->info().id, "cpu");
    std::string err;
    EXPECT_EQ(createDevice("vulkan:7", &err), nullptr);
    EXPECT_NE(err.find("vulkan:7"), std::string::npos);
}

TEST(RenderDevice, BatchEqualsSinglePassesBitForBit) {
    auto scene = makeScene();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    const auto cam = makeCam();
    const RenderSettings rs = smallSettings();

    Film batch(rs.width, rs.height), single(rs.width, rs.height);
    RenderTargets tb{&batch, nullptr, nullptr};
    RenderTargets ts{&single, nullptr, nullptr};
    const SampleBatchResult r = dev->render(*cam, rs, tb, 0, 8, nullptr);
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.samplesTraced, 8u * rs.width * rs.height);
    for (int s = 0; s < 8; ++s) dev->render(*cam, rs, ts, s, 1, nullptr);

    const Image a = batch.resolve(), b = single.resolve();
    for (int y = 0; y < rs.height; ++y)
        for (int x = 0; x < rs.width; ++x)
            for (int c = 0; c < 3; ++c) ASSERT_EQ(a.getPixel(x, y)[c], b.getPixel(x, y)[c]) << x << "," << y;
}

TEST(RenderSession, FinalStopsAtTargetAndGrowsBatches) {
    auto scene = makeScene();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    RenderSession session(*dev);
    SessionConfig sc;
    sc.mode = RenderMode::Final;
    sc.targetSpp = 37; // parti sınırına denk gelmeyen hedef: aşılmamalı
    sc.batchSeconds = 10.0;
    session.begin(makeCam(), smallSettings(), sc);
    int batches = 0;
    while (session.advance()) ++batches;
    EXPECT_EQ(session.samples(), 37);
    EXPECT_TRUE(session.finished());
    EXPECT_LT(batches, 10) << "Final modda partiler büyümeli";
    EXPECT_NEAR(session.averageSamples(), 37.0, 1e-9);
    EXPECT_FALSE(session.advance()) << "bitmiş oturum örnek eklememeli";
}

TEST(RenderSession, InteractiveAddsOneSampleKeepsAccumulationWhenTargetRaised) {
    auto scene = makeScene();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    RenderSession session(*dev);
    SessionConfig sc;
    sc.mode = RenderMode::Interactive;
    sc.targetSpp = 3;
    session.begin(makeCam(), smallSettings(), sc);
    for (int i = 1; i <= 3; ++i) {
        ASSERT_TRUE(session.advance());
        EXPECT_EQ(session.samples(), i);
    }
    EXPECT_TRUE(session.finished());
    session.setTargetSpp(5);
    EXPECT_FALSE(session.finished());
    ASSERT_TRUE(session.advance());
    EXPECT_EQ(session.samples(), 4);
}

TEST(RenderSession, TimeLimitStopsWithoutOvershooting) {
    auto scene = makeScene();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    RenderSession session(*dev);
    SessionConfig sc;
    sc.targetSpp = 0; // sınırsız: yalnız süre durdurur
    sc.timeLimit = 0.3;
    sc.batchSeconds = 10.0; // parti boyu süre sınırına göre kısılmalı
    RenderSettings rs = smallSettings();
    rs.width = rs.height = 64;
    session.begin(makeCam(), rs, sc);
    while (session.advance()) {
    }
    EXPECT_TRUE(session.finished());
    EXPECT_GT(session.samples(), 0);
    EXPECT_LT(session.seconds(), 0.3 + 0.25) << "son parti süre sınırını belirgin aşmamalı";
}

TEST(RenderSession, AdaptiveConvergesBeforeTarget) {
    // Düz, sabit bir gök: her piksel birkaç örnekte eşiğin altına iner; oturum hedefi beklemeden biter.
    auto scene = std::make_shared<Scene>();
    scene->setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 1.0f));
    scene->buildAccelerator();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    RenderSession session(*dev);
    RenderSettings rs = smallSettings();
    rs.adaptiveSampling = true;
    rs.noiseThreshold = 0.05f;
    rs.minAdaptiveSamples = 8;
    SessionConfig sc;
    sc.targetSpp = 4096;
    session.begin(makeCam(), rs, sc);
    while (session.advance()) {
    }
    EXPECT_TRUE(session.converged());
    EXPECT_LT(session.samples(), 4096);
    EXPECT_LT(session.averageSamples(), 100.0);
}

TEST(RenderSession, UnsupportedOutputsAreDroppedWithWarning) {
    auto scene = makeScene();
    LimitedDevice dev;
    dev.setScene(scene);
    RenderSession session(dev);
    RenderSettings rs = smallSettings();
    rs.adaptiveSampling = true;
    SessionConfig sc;
    sc.targetSpp = 2;
    sc.aovs = true;
    sc.passes = true;
    session.begin(makeCam(), rs, sc);
    EXPECT_EQ(session.warnings().size(), 3u);
    EXPECT_EQ(session.aovs(), nullptr);
    EXPECT_EQ(session.passes(), nullptr);
    while (session.advance()) {
    }
    EXPECT_EQ(session.samples(), 2);
    const Image alpha = session.resolveAlpha(); // AOV yok: tam kapsama
    EXPECT_EQ(alpha.getPixel(3, 3).r, 1.0f);
}

TEST(RenderSession, CancelLeavesValidAverage) {
    auto scene = makeScene();
    auto dev = createDevice("cpu");
    dev->setScene(scene);
    RenderSession session(*dev);
    SessionConfig sc;
    sc.targetSpp = 4;
    session.begin(makeCam(), smallSettings(), sc);
    std::atomic<bool> cancel{true};
    EXPECT_FALSE(session.advance(&cancel));
    EXPECT_EQ(session.samples(), 0);
    cancel = false;
    while (session.advance(&cancel)) {
    }
    EXPECT_EQ(session.samples(), 4);
}
