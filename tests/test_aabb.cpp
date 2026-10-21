// test_aabb.cpp — Eksen hizalı sınır kutusu (AABB) testleri: merkez, köşegen, yüzey alanı,
// birleştirme ve slab yöntemiyle ışın kesişimi. BVH bunlara dayanır: yüzey alanı yanlışsa SAH
// kötü ağaç kurar, kesişim yanlışsa nesneler görüntüden kaybolur.
#include "gtest/gtest.h"
#include "core/math/aabb.h"

using namespace photon;

TEST(AABBTest, Construction) {
    AABB box;
    EXPECT_TRUE(box.pMin.x > box.pMax.x); // Uninitialized / empty box is invalid

    AABB box2(Vec3f(-1.0f, -2.0f, -3.0f), Vec3f(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(box2.pMin.x, -1.0f);
    EXPECT_FLOAT_EQ(box2.pMax.z, 3.0f);
}

TEST(AABBTest, CentroidAndDiagonal) {
    AABB box(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(10.0f, 20.0f, 30.0f));
    
    Vec3f c = box.centroid();
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 10.0f);
    EXPECT_FLOAT_EQ(c.z, 15.0f);

    Vec3f diag = box.diagonal();
    EXPECT_FLOAT_EQ(diag.x, 10.0f);
    EXPECT_FLOAT_EQ(diag.y, 20.0f);
    EXPECT_FLOAT_EQ(diag.z, 30.0f);

    EXPECT_FLOAT_EQ(box.surfaceArea(), 2.0f * (10.0f * 20.0f + 20.0f * 30.0f + 30.0f * 10.0f));
}

TEST(AABBTest, Merge) {
    AABB b1(Vec3f(0, 0, 0), Vec3f(2, 2, 2));
    AABB b2(Vec3f(1, 1, 1), Vec3f(3, 3, 3));
    
    AABB merged = b1.merged(b2);
    EXPECT_FLOAT_EQ(merged.pMin.x, 0.0f);
    EXPECT_FLOAT_EQ(merged.pMax.x, 3.0f);

    AABB mergedPoint = b1;
    mergedPoint.merge(Vec3f(-1.0f, 5.0f, 1.0f));
    EXPECT_FLOAT_EQ(mergedPoint.pMin.x, -1.0f);
    EXPECT_FLOAT_EQ(mergedPoint.pMax.y, 5.0f);
}

TEST(AABBTest, Intersection) {
    AABB box(Vec3f(-1.0f, -1.0f, -1.0f), Vec3f(1.0f, 1.0f, 1.0f));
    
    // Ray hitting from front
    Ray r1(Vec3f(0, 0, -5), Vec3f(0, 0, 1));
    float tNear, tFar;
    EXPECT_TRUE(box.intersect(r1, tNear, tFar));
    EXPECT_FLOAT_EQ(tNear, 4.0f);
    EXPECT_FLOAT_EQ(tFar, 6.0f);

    // Ray missing
    Ray r2(Vec3f(2, 2, -5), Vec3f(0, 0, 1));
    EXPECT_FALSE(box.intersect(r2, tNear, tFar));
}
