// Kamera animasyonu testleri: anahtarlardan tam geçer, uçlarda sabit kalır, iç anahtarlarda hız
// süreklidir (C¹, düzensiz aralıklarda bile), uçlarda yumuşak durur, doğrusal mod sabit hızlıdır,
// JSON ve proje dosyasında kayıpsız saklanır.
#include <gtest/gtest.h>
#include "scene/camera_animation.h"
#include "scene/project_io.h"

#include <filesystem>

using namespace photon;

namespace {

CameraTrack threeKeys() {
    CameraTrack t;
    CameraKey a, b, c;
    a.time = 0.0f;
    a.eye = Vec3f(4, 1, 0);
    a.fovDeg = 40.0f;
    b.time = 1.0f;
    b.eye = Vec3f(0, 1.5f, 4);
    b.fovDeg = 30.0f;
    c.time = 3.5f; // düzensiz aralık
    c.eye = Vec3f(-4, 2, 0);
    c.target = Vec3f(0, 0.2f, 0);
    c.fovDeg = 25.0f;
    t.setKey(c); // sırasız ekleme: setKey sıralar
    t.setKey(a);
    t.setKey(b);
    return t;
}

float dist(const Vec3f& a, const Vec3f& b) { return (a - b).length(); }

} // namespace

TEST(CameraAnimation, PassesThroughKeysAndClampsOutside) {
    const CameraTrack t = threeKeys();
    ASSERT_EQ(t.keys.size(), 3u);
    EXPECT_FLOAT_EQ(t.duration(), 3.5f);
    for (const CameraKey& k : t.keys) {
        const CameraKey e = t.evaluate(k.time);
        EXPECT_LT(dist(e.eye, k.eye), 1e-5f);
        EXPECT_LT(dist(e.target, k.target), 1e-5f);
        EXPECT_NEAR(e.fovDeg, k.fovDeg, 1e-4f);
    }
    EXPECT_LT(dist(t.evaluate(-1.0f).eye, t.keys.front().eye), 1e-6f);
    EXPECT_LT(dist(t.evaluate(99.0f).eye, t.keys.back().eye), 1e-6f);
}

TEST(CameraAnimation, VelocityIsContinuousAndEasesAtEnds) {
    const CameraTrack t = threeKeys();
    const float h = 1e-3f;
    auto vel = [&](float time) { return (t.evaluate(time + h).eye - t.evaluate(time - h).eye) * (0.5f / h); };
    // İç anahtar (t = 1): soldan ve sağdan hız aynı (kırılma yok).
    const Vec3f left = (t.evaluate(1.0f).eye - t.evaluate(1.0f - h).eye) * (1.0f / h);
    const Vec3f right = (t.evaluate(1.0f + h).eye - t.evaluate(1.0f).eye) * (1.0f / h);
    EXPECT_LT(dist(left, right), 0.02f * std::max(1.0f, left.length()));
    // Uçlarda hız sıfıra iner (yumuşak başla / yumuşak dur).
    EXPECT_LT(vel(0.0f + 2 * h).length(), 0.1f);
    EXPECT_LT(vel(3.5f - 2 * h).length(), 0.1f);
    EXPECT_GT(vel(2.0f).length(), 0.5f);
}

TEST(CameraAnimation, LinearModeHasConstantSpeedBetweenKeys) {
    CameraTrack t = threeKeys();
    t.smooth = false;
    const Vec3f mid = t.evaluate(0.5f).eye;
    EXPECT_LT(dist(mid, (t.keys[0].eye + t.keys[1].eye) * 0.5f), 1e-5f);
}

TEST(CameraAnimation, SetKeyReplacesSameTimeAndRemoveWorks) {
    CameraTrack t = threeKeys();
    CameraKey k = t.keys[1];
    k.eye = Vec3f(9, 9, 9);
    k.time += 0.0004f; // 1 ms içinde: aynı anahtar
    t.setKey(k);
    EXPECT_EQ(t.keys.size(), 3u);
    EXPECT_LT(dist(t.keys[1].eye, Vec3f(9, 9, 9)), 1e-6f);
    EXPECT_TRUE(t.removeKeyAt(1.0f));
    EXPECT_FALSE(t.removeKeyAt(1.0f));
    EXPECT_EQ(t.keys.size(), 2u);
    t.fps = 24.0f;
    EXPECT_EQ(t.frameCount(), 85); // 3.5 s · 24 + 1
}

TEST(CameraAnimation, SurvivesJsonAndProjectRoundTrip) {
    CameraTrack t = threeKeys();
    t.name = "Ürün turu";
    t.fps = 25.0f;
    t.keys[1].fStop = 2.8f;
    t.keys[1].focusDistance = 3.2f;
    const CameraTrack back = cameraTrackFromJson(cameraTrackToJson(t));
    ASSERT_EQ(back.keys.size(), 3u);
    EXPECT_EQ(back.name, "Ürün turu");
    EXPECT_FLOAT_EQ(back.fps, 25.0f);
    EXPECT_FLOAT_EQ(back.keys[1].fStop, 2.8f);
    EXPECT_LT(dist(back.evaluate(2.2f).eye, t.evaluate(2.2f).eye), 1e-5f);

    SceneGraph graph;
    ProjectData data;
    data.cameraTracks.push_back(t);
    const std::string path = (std::filesystem::temp_directory_path() / "photon_anim_roundtrip.photon").string();
    ASSERT_TRUE(saveProject(path, graph, data));
    SceneGraph g2;
    ProjectData d2;
    ASSERT_TRUE(loadProject(path, g2, d2));
    ASSERT_EQ(d2.cameraTracks.size(), 1u);
    EXPECT_EQ(d2.cameraTracks[0].keys.size(), 3u);
    EXPECT_LT(dist(d2.cameraTracks[0].evaluate(1.7f).eye, t.evaluate(1.7f).eye), 1e-5f);
}
