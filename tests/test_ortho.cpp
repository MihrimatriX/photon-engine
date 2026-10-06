// test_ortho.cpp — Ortografik kamera (paralel ışınlar, farklı başlangıç noktaları) ve
// odak uzaklığı (mm) ↔ dikey görüş açısı (FOV) dönüşümü testleri.
#include <gtest/gtest.h>
#include "camera/orthographic_camera.h"
#include <cmath>

using namespace photon;

TEST(OrthographicCamera, OriginsDifferDirectionsParallel) {
    OrthographicCamera cam(Vec3f(0, 0, 5), Vec3f(0, 0, 0), Vec3f(0, 1, 0), 2.0f, 1.0f);
    Ray a = cam.generateRay(0.25f, 0.25f, Vec2f(0.5f, 0.5f));
    Ray b = cam.generateRay(0.75f, 0.75f, Vec2f(0.5f, 0.5f));
    EXPECT_GT((a.origin - b.origin).length(), 0.1f);
    EXPECT_NEAR(a.direction.x, b.direction.x, 1e-5f);
    EXPECT_NEAR(a.direction.y, b.direction.y, 1e-5f);
    EXPECT_NEAR(a.direction.z, b.direction.z, 1e-5f);
    EXPECT_LT(a.direction.z, 0.0f);
}

TEST(FocalLength, SensorHeightSetsFov) {
    float fov = fovDegreesFromFocalMm(36.0f);
    // 36x24 full frame: 2 * atan(12 / 36) = 36.87 degrees vertical.
    EXPECT_NEAR(fov, 36.8699f, 0.05f);
    EXPECT_NEAR(focalMmFromFovDegrees(fov), 36.0f, 0.05f);
}
