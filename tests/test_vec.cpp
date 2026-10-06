// test_vec.cpp — Vec3f temel testleri: kurulum, aritmetik, nokta/vektörel çarpım, uzunluk,
// normalize, indeksleme ve yazdırma. Tüm geometri bu işlemlere dayanır.
#include "gtest/gtest.h"
#include "core/math/vec.h"
#include <sstream>

using namespace photon;

TEST(VecTest, Vec3fConstruction) {
    Vec3f v1;
    EXPECT_FLOAT_EQ(v1.x, 0.0f);
    EXPECT_FLOAT_EQ(v1.y, 0.0f);
    EXPECT_FLOAT_EQ(v1.z, 0.0f);

    Vec3f v2(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v2.x, 1.0f);
    EXPECT_FLOAT_EQ(v2.y, 2.0f);
    EXPECT_FLOAT_EQ(v2.z, 3.0f);

    Vec3f v3(5.0f);
    EXPECT_FLOAT_EQ(v3.x, 5.0f);
    EXPECT_FLOAT_EQ(v3.y, 5.0f);
    EXPECT_FLOAT_EQ(v3.z, 5.0f);
}

TEST(VecTest, Vec3fArithmetic) {
    Vec3f a(1.0f, 2.0f, 3.0f);
    Vec3f b(4.0f, 5.0f, 6.0f);

    Vec3f c = a + b;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.0f);
    EXPECT_FLOAT_EQ(c.z, 9.0f);

    Vec3f d = a - b;
    EXPECT_FLOAT_EQ(d.x, -3.0f);
    EXPECT_FLOAT_EQ(d.y, -3.0f);
    EXPECT_FLOAT_EQ(d.z, -3.0f);

    Vec3f e = a * 2.0f;
    EXPECT_FLOAT_EQ(e.x, 2.0f);
    EXPECT_FLOAT_EQ(e.y, 4.0f);
    EXPECT_FLOAT_EQ(e.z, 6.0f);

    Vec3f f = b / 2.0f;
    EXPECT_FLOAT_EQ(f.x, 2.0f);
    EXPECT_FLOAT_EQ(f.y, 2.5f);
    EXPECT_FLOAT_EQ(f.z, 3.0f);
}

TEST(VecTest, Vec3fGeometric) {
    Vec3f a(1.0f, 0.0f, 0.0f);
    Vec3f b(0.0f, 1.0f, 0.0f);

    EXPECT_FLOAT_EQ(a.dot(b), 0.0f);
    EXPECT_FLOAT_EQ(dot(a, b), 0.0f);

    Vec3f c = a.cross(b);
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f);

    Vec3f v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.length(), 5.0f);
    EXPECT_FLOAT_EQ(v.lengthSquared(), 25.0f);

    Vec3f vNorm = v.normalized();
    EXPECT_FLOAT_EQ(vNorm.length(), 1.0f);
    EXPECT_FLOAT_EQ(vNorm.x, 3.0f / 5.0f);
    EXPECT_FLOAT_EQ(vNorm.y, 4.0f / 5.0f);
}

TEST(VecTest, Vec3fAccessors) {
    Vec3f v(10.0f, 20.0f, 30.0f);
    EXPECT_FLOAT_EQ(v[0], 10.0f);
    EXPECT_FLOAT_EQ(v[1], 20.0f);
    EXPECT_FLOAT_EQ(v[2], 30.0f);

    v[0] = 5.0f;
    EXPECT_FLOAT_EQ(v.x, 5.0f);
}

TEST(VecTest, Vec3fOstream) {
    Vec3f v(1.5f, 2.5f, 3.5f);
    std::stringstream ss;
    ss << v;
    EXPECT_EQ(ss.str(), "Vec3f(1.5, 2.5, 3.5)");
}
