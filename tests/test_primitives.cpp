// test_primitives.cpp — Temel şekiller: 1 birimlik kutuya sığmalı, tabanı y = 0'da
// olmalı ve her üçgen dışa bakmalı (geometrik normal köşe normalleriyle aynı yönde).
// Ters örülmüş bir küre cam malzemede içi dışına çıkmış gibi kırılırdı.
#include <gtest/gtest.h>
#include "scene/primitives.h"
#include <cmath>

using namespace photon;

TEST(Primitives, FitUnitBoxAndFaceOutward) {
    for (int k = 0; k < kPrimitiveCount; ++k) {
        const auto kind = static_cast<PrimitiveKind>(k);
        SCOPED_TRACE(primitiveName(kind));
        auto mesh = makePrimitiveMesh(kind);
        ASSERT_TRUE(mesh);
        ASSERT_GT(mesh->numTriangles(), 0u);
        const AABB b = mesh->bounds();
        EXPECT_NEAR(b.pMin.y, 0.0f, 1e-5f);
        EXPECT_LE(b.pMax.y, 1.0f + 1e-5f);
        EXPECT_NEAR(b.pMax.x - b.pMin.x, 1.0f, 1e-3f);
        EXPECT_NEAR(b.pMax.z - b.pMin.z, 1.0f, 1e-3f);
        const auto& p = mesh->positions();
        const auto& n = mesh->normals();
        const auto& idx = mesh->indices();
        ASSERT_EQ(p.size(), n.size());
        int wrong = 0;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const Vec3f g = (p[idx[t + 1]] - p[idx[t]]).cross(p[idx[t + 2]] - p[idx[t]]);
            if (g.lengthSquared() < 1e-14f) continue; // kutuptaki dejenere üçgen
            const Vec3f avg = n[idx[t]] + n[idx[t + 1]] + n[idx[t + 2]];
            if (g.dot(avg) <= 0.0f) ++wrong;
        }
        EXPECT_EQ(wrong, 0);
        for (const Vec3f& v : n) EXPECT_NEAR(v.length(), 1.0f, 1e-4f);
    }
}
