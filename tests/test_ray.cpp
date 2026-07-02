#include "gtest/gtest.h"
#include "core/math/ray.h"

using namespace photon;

TEST(RayTest, Construction) {
    Vec3f orig(1.0f, 2.0f, 3.0f);
    Vec3f dir(0.0f, 0.0f, 1.0f);
    Ray r(orig, dir);

    EXPECT_FLOAT_EQ(r.origin.x, 1.0f);
    EXPECT_FLOAT_EQ(r.origin.y, 2.0f);
    EXPECT_FLOAT_EQ(r.origin.z, 3.0f);

    EXPECT_FLOAT_EQ(r.direction.x, 0.0f);
    EXPECT_FLOAT_EQ(r.direction.y, 0.0f);
    EXPECT_FLOAT_EQ(r.direction.z, 1.0f);

    EXPECT_FLOAT_EQ(r.tMin, 1e-4f); // Default tMin value
}

TEST(RayTest, Evaluation) {
    Vec3f orig(0.0f, 0.0f, 0.0f);
    Vec3f dir(1.0f, 2.0f, 3.0f);
    Ray r(orig, dir);

    Vec3f p = r.at(2.5f);
    EXPECT_FLOAT_EQ(p.x, 2.5f);
    EXPECT_FLOAT_EQ(p.y, 5.0f);
    EXPECT_FLOAT_EQ(p.z, 7.5f);
}
