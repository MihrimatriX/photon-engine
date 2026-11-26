// test_motion_blur.cpp — Kuaterniyon SLERP / TransformTrack, Embree 4 BLAS/TLAS instancing,
// nesne hareket bulanıklığı ve kamera hareket bulanıklığı testleri.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "core/math/constants.h"
#include "core/math/quat.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "lights/directional_light.h"
#include "materials/lambertian.h"
#include "scene/primitives.h"
#include "scene/scene_graph.h"
#include "scene/transform_animation.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

std::shared_ptr<TriangleMesh> makeUnitQuad(const Material* mat) {
    std::vector<Vec3f> pos = {
        Vec3f(-0.35f, -0.35f, 0.0f),
        Vec3f( 0.35f, -0.35f, 0.0f),
        Vec3f( 0.35f,  0.35f, 0.0f),
        Vec3f(-0.35f,  0.35f, 0.0f),
    };
    std::vector<Vec3f> nrm = {
        Vec3f(0, 0, 1), Vec3f(0, 0, 1), Vec3f(0, 0, 1), Vec3f(0, 0, 1)
    };
    std::vector<Vec2f> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return std::make_shared<TriangleMesh>(pos, nrm, uv, idx, mat);
}

} // namespace

TEST(TransformAnimation, QuatSlerpAndTrackInterpolation) {
    // 0° → 90° Y ekseni dönmesi: t = 0.5'te tam 45° olmalı
    const Quatf q0 = Quatf::fromEulerDeg(Vec3f(0.0f, 0.0f, 0.0f));
    const Quatf q1 = Quatf::fromEulerDeg(Vec3f(0.0f, 90.0f, 0.0f));
    EXPECT_NEAR(q1.y, std::sin(PI_OVER_4), 1e-4f);
    EXPECT_NEAR(q1.w, std::cos(PI_OVER_4), 1e-4f);
    const Quatf qMid = Quatf::slerp(q0, q1, 0.5f);
    EXPECT_NEAR(qMid.y, std::sin(PI_OVER_4 * 0.5f), 1e-4f);
    EXPECT_NEAR(qMid.w, std::cos(PI_OVER_4 * 0.5f), 1e-4f);
    const Vec3f euler = qMid.toEulerDeg();
    EXPECT_NEAR(euler.y, 45.0f, 1e-3f);
    EXPECT_NEAR(euler.x, 0.0f, 1e-3f);
    EXPECT_NEAR(euler.z, 0.0f, 1e-3f);

    // TRS ayrıştırma + birleştirme gidiş-dönüşü (ayna negatif ölçek dahil)
    const Transform orig = composeTRS(Vec3f(1.5f, -2.0f, 3.25f), Vec3f(15.0f, 40.0f, -25.0f), Vec3f(2.0f, 1.5f, -1.0f));
    Vec3f pos, scale;
    Quatf rot;
    decomposeTRS(orig, pos, rot, scale);
    const Transform rebuilt = composeTRS(pos, rot, scale);
    const Vec3f probe(0.7f, -0.3f, 1.1f);
    const Vec3f a = orig.transformPoint(probe);
    const Vec3f b = rebuilt.transformPoint(probe);
    EXPECT_NEAR(a.x, b.x, 1e-4f);
    EXPECT_NEAR(a.y, b.y, 1e-4f);
    EXPECT_NEAR(a.z, b.z, 1e-4f);

    // TransformTrack değerlendirmesi
    TransformTrack tr;
    tr.smooth = false;
    tr.setKey(TransformKey{0.0f, Vec3f(0, 0, 0), Vec3f(0, 0, 0), Vec3f(1, 1, 1)});
    tr.setKey(TransformKey{2.0f, Vec3f(4, 2, 0), Vec3f(0, 180, 0), Vec3f(2, 2, 2)});
    const Transform mid = tr.evaluate(1.0f);
    const Vec3f pMid = mid.transformPoint(Vec3f(0, 0, 0));
    EXPECT_NEAR(pMid.x, 2.0f, 1e-4f);
    EXPECT_NEAR(pMid.y, 1.0f, 1e-4f);
}

TEST(MotionBlur, MovingObjectStreaksAcrossShutter) {
    for (bool ownBvh : {false, true}) {
        Scene::setPreferOwnBvh(ownBvh);
        auto renderQuad = [](bool moving) {
            Scene scene;
            auto emitMat = std::make_shared<Lambertian>(Color3f::black(), Color3f(1.0f));
            scene.retainMaterial(emitMat);
            auto quad = makeUnitQuad(emitMat.get());
            auto inst = std::make_shared<MeshInstance>(quad, Transform::translate(Vec3f(-1.0f, 0.0f, 0.0f)),
                                                       emitMat.get());
            if (moving) inst->setMotionTransform(Transform::translate(Vec3f(1.0f, 0.0f, 0.0f)));
            scene.addShape(inst);
            scene.buildAccelerator();

            PerspectiveCamera cam(Vec3f(0.0f, 0.0f, 4.0f), Vec3f(0.0f, 0.0f, 0.0f),
                                  Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
            RenderSettings rs;
            rs.width = 64;
            rs.height = 32;
            rs.samplesPerPixel = 64;
            rs.maxBounces = 1;
            rs.numThreads = 1;
            rs.motionBlur = true;
            rs.filter.type = PixelFilterType::Box;
            rs.filter.width = 1.0f;

            Renderer renderer;
            return renderer.render(scene, cam, rs);
        };

        Image stillImg = renderQuad(false);
        Image blurImg = renderQuad(true);

        // Hareketli karede yatay şerit boyunca lit piksel sayısı statikten çok daha geniş olmalı,
        // ve orta piksel kısmi (0 < L < 1) parlaklık almalı; toplam akı korunmalı!
        int stillLit = 0, blurLit = 0;
        double stillSum = 0.0, blurSum = 0.0;
        const int midY = 16;
        for (int x = 0; x < 64; ++x) {
            const float s = stillImg.getPixel(x, midY).luminance();
            const float b = blurImg.getPixel(x, midY).luminance();
            if (s > 0.05f) ++stillLit;
            if (b > 0.05f) ++blurLit;
            stillSum += s;
            blurSum += b;
        }
        EXPECT_GT(blurLit, stillLit * 2) << "ownBvh=" << ownBvh;
        const float centerLum = blurImg.getPixel(32, midY).luminance();
        EXPECT_GT(centerLum, 0.1f) << "ownBvh=" << ownBvh;
        EXPECT_LT(centerLum, 0.85f) << "ownBvh=" << ownBvh;
        // Toplam satır enerjisi hareket boyunca korunur (aynı alan, zaman ortalaması)
        EXPECT_NEAR(blurSum, stillSum, stillSum * 0.2f) << "ownBvh=" << ownBvh;
    }
    Scene::setPreferOwnBvh(false);
}

TEST(MotionBlur, CameraShutterInterpolation) {
    Scene scene;
    auto emitMat = std::make_shared<Lambertian>(Color3f::black(), Color3f(1.0f));
    scene.retainMaterial(emitMat);
    auto quad = makeUnitQuad(emitMat.get());
    auto inst = std::make_shared<MeshInstance>(quad, Transform{}, emitMat.get());
    scene.addShape(inst);
    scene.buildAccelerator();

    PerspectiveCamera cam(Vec3f(-0.8f, 0.0f, 4.0f), Vec3f(-0.8f, 0.0f, 0.0f),
                          Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
    PerspectiveCamera camEnd(Vec3f(0.8f, 0.0f, 4.0f), Vec3f(0.8f, 0.0f, 0.0f),
                             Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
    cam.setShutterEnd(camEnd.describeGpu());
    EXPECT_TRUE(cam.hasShutterEnd());

    RenderSettings rs;
    rs.width = 64;
    rs.height = 32;
    rs.samplesPerPixel = 64;
    rs.maxBounces = 1;
    rs.numThreads = 1;
    rs.filter.type = PixelFilterType::Box;
    rs.filter.width = 1.0f;

    rs.motionBlur = false;
    Renderer renderer;
    Image sharp = renderer.render(scene, cam, rs);

    rs.motionBlur = true;
    Image blurred = renderer.render(scene, cam, rs);

    int sharpMid = 0, blurMid = 0;
    for (int x = 0; x < 64; ++x) {
        const float s = sharp.getPixel(x, 16).luminance();
        const float b = blurred.getPixel(x, 16).luminance();
        if (s > 0.1f && s < 0.9f) ++sharpMid;
        if (b > 0.1f && b < 0.9f) ++blurMid;
    }
    EXPECT_GT(blurMid, sharpMid + 10) << "Kamera hareket bulanıklığı keskin kenarı yumuşak geçişe yaymalı";
}

TEST(Instancing, BlasTlasEmbreeMatchesOwnBvhAndMirroredTransform) {
    auto renderScene = [](bool ownBvh) {
        Scene::setPreferOwnBvh(ownBvh);
        SceneGraph graph;
        auto mesh = makePrimitiveMesh(PrimitiveKind::Cube);
        auto mat = std::make_shared<Lambertian>(Color3f(0.75f, 0.5f, 0.3f));

        // 5 örnek tek TriangleMesh'i paylaşır (biri negatif determinantlı ayna ölçek!)
        for (int i = 0; i < 5; ++i) {
            auto node = std::make_unique<SceneNode>("Kutu_" + std::to_string(i), SceneNodeType::Mesh);
            node->mesh = mesh;
            node->material = mat;
            const float x = -2.0f + static_cast<float>(i) * 1.0f;
            const Vec3f sc = (i == 2) ? Vec3f(-1.0f, 1.0f, 1.0f) : Vec3f(1.0f, 1.0f, 1.0f);
            node->localTransform = composeTRS(Vec3f(x, 0.3f, 0.0f), Vec3f(0.0f, i * 25.0f, 0.0f), sc);
            graph.root()->addChild(std::move(node));
        }

        Scene scene;
        auto sun = std::make_shared<DirectionalLight>(Vec3f(-0.3f, -1.0f, -0.5f).normalized(), Color3f(3.0f));
        graph.compile(scene, {sun}, nullptr, false);

        PerspectiveCamera cam(Vec3f(0.0f, 1.8f, 5.2f), Vec3f(0.0f, 0.3f, 0.0f),
                              Vec3f(0.0f, 1.0f, 0.0f), 42.0f, 1.5f);
        RenderSettings rs;
        rs.width = 60;
        rs.height = 40;
        rs.samplesPerPixel = 4;
        rs.maxBounces = 2;
        rs.numThreads = 1;
        rs.filter.type = PixelFilterType::Box;
        rs.filter.width = 1.0f;

        Renderer renderer;
        return renderer.render(scene, cam, rs);
    };

    Image embreeImg = renderScene(false);
    Image ownBvhImg = renderScene(true);
    Scene::setPreferOwnBvh(false);

    double maxDiff = 0.0;
    double meanDiff = 0.0;
    for (int y = 0; y < embreeImg.height(); ++y) {
        for (int x = 0; x < embreeImg.width(); ++x) {
            const Color3f a = embreeImg.getPixel(x, y);
            const Color3f b = ownBvhImg.getPixel(x, y);
            const double d = std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
            maxDiff = std::max(maxDiff, d);
            meanDiff += d;
        }
    }
    meanDiff /= static_cast<double>(embreeImg.width() * embreeImg.height());
    EXPECT_LT(meanDiff, 1e-3) << "Embree BLAS/TLAS sonucu yerleşik BVH ile birebir örtüşmeli";
    EXPECT_LT(maxDiff, 0.05);
}
