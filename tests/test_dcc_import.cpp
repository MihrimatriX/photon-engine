// DCC sahnesinden kamera ve ışık alma testleri. tests/data/fbx/dcc_camera_lights_7400_ascii.fbx
// elle yazılmış bilinen bir sahnedir: 1 m küp; 1 saniyede x = 0 → 3 kayan 35 mm kamera (30 fps,
// 36×24 film); renkli nokta ışık (2,3,1); aşağı bakan spot (iç 20°, dış 40°); X'te 45° eğik güneş.
//   - kamera her karede örneklenir, −Z'ye bakar, hedef modelin merkez doğrultusundadır;
//   - dikey görüş açısı film yüksekliği ve odak uzaklığından gelir (2·atan(12/35) = 37.85°);
//   - ışık türleri, renkleri, yönleri ve koni açıları korunur; şiddet oranları korunur;
//   - grubun dünya dönüşümü (yerleştirme, birim) kameralara ve ışıklara uygulanır, nokta ışık
//     şiddeti ölçeğin karesiyle artar (aynı oranda uzaklıkta aynı aydınlık).
#include <gtest/gtest.h>
#include "io/fbx_loader.h"
#include "scene/model_import.h"

#include <cmath>
#include <string>

using namespace photon;

namespace {

const std::string kFile = std::string(PHOTON_TEST_DATA) + "/fbx/dcc_camera_lights_7400_ascii.fbx";

float dist(const Vec3f& a, const Vec3f& b) { return (a - b).length(); }

const ImportedLight* findLight(const ImportedModel& m, const std::string& name) {
    for (const ImportedLight& l : m.lights)
        if (l.name == name) return &l;
    return nullptr;
}

} // namespace

TEST(DccImport, FbxAnimatedCameraIsSampledEveryFrame) {
    std::string err;
    const ImportedModel m = FbxLoader::load(kFile, &err);
    ASSERT_FALSE(m.meshes.empty()) << err;
    ASSERT_EQ(m.cameras.size(), 1u);
    const ImportedCamera& c = m.cameras[0];
    EXPECT_EQ(c.name, "Cekim");
    EXPECT_FLOAT_EQ(c.fps, 30.0f);
    ASSERT_EQ(c.keys.size(), 31u);
    EXPECT_LT(dist(c.keys.front().eye, Vec3f(0, 1, 5)), 1e-3f);
    EXPECT_LT(dist(c.keys.back().eye, Vec3f(3, 1, 5)), 1e-3f);
    EXPECT_NEAR(c.keys.back().time, 1.0f, 1e-4f);
    EXPECT_LT(dist(c.keys[15].eye, Vec3f(1.5f, 1, 5)), 1e-2f); // doğrusal anahtarlar: ortada yarı yol
    // Bakış −Z, hedef küpün merkez doğrultusunda (göz yüksekliğinde, z = 0).
    EXPECT_LT(dist(c.keys.front().target, Vec3f(0, 1, 0)), 1e-3f);
    EXPECT_LT(dist(c.keys.back().target, Vec3f(3, 1, 0)), 1e-3f);
    EXPECT_NEAR(c.keys.front().fovDeg, 2.0f * std::atan(12.0f / 35.0f) * 180.0f / 3.14159265f, 0.2f);
}

TEST(DccImport, FbxLightsKeepTypeColorDirectionAndRatios) {
    const ImportedModel m = FbxLoader::load(kFile);
    ASSERT_EQ(m.lights.size(), 3u);
    const ImportedLight* lamp = findLight(m, "Lamba");
    const ImportedLight* spot = findLight(m, "Spot");
    const ImportedLight* sun = findLight(m, "Gunes");
    ASSERT_TRUE(lamp && spot && sun);

    EXPECT_EQ(lamp->type, ImportedLight::Type::Point);
    EXPECT_LT(dist(lamp->position, Vec3f(2, 3, 1)), 1e-3f);
    EXPECT_NEAR(lamp->color.g, 0.5f, 1e-4f);
    EXPECT_NEAR(lamp->color.b, 0.25f, 1e-4f);

    EXPECT_EQ(spot->type, ImportedLight::Type::Spot);
    EXPECT_LT(dist(spot->direction, Vec3f(0, -1, 0)), 1e-3f); // FBX ışığı varsayılan olarak −Y'ye bakar
    EXPECT_NEAR(spot->coneAngleDeg, 40.0f, 1e-3f);
    EXPECT_NEAR(spot->coneBlend, 0.5f, 1e-3f);

    EXPECT_EQ(sun->type, ImportedLight::Type::Directional);
    EXPECT_LT(dist(sun->direction, Vec3f(0, -std::sqrt(0.5f), -std::sqrt(0.5f))), 1e-3f);

    // Oranlar: modelin merkezindeki ışınım, FBX şiddetiyle orantılı (spot 200, lamba 100).
    const float eLamp = lamp->intensity / (lamp->position.lengthSquared());
    const float eSpot = spot->intensity / (spot->position.lengthSquared());
    EXPECT_NEAR(eSpot / eLamp, 2.0f, 1e-3f);
    EXPECT_NEAR(sun->intensity, eLamp, 1e-3f); // aynı FBX şiddeti → aynı ışınım
    // Mutlak düzey: modelin merkezindeki toplam ışınım ~3 (stüdyo ön ayarlarına yakın).
    EXPECT_NEAR(eLamp + eSpot + sun->intensity, 3.0f, 1e-3f);
}

TEST(DccImport, ExtrasFollowTheGroupTransform) {
    std::string err;
    std::vector<std::string> warnings;
    ImportExtras extras;
    auto group = importModelFile(kFile, &err, {}, &warnings, &extras);
    ASSERT_NE(group, nullptr) << err;
    ASSERT_EQ(extras.cameras.size(), 1u);
    ASSERT_EQ(extras.lights.size(), 3u);
    EXPECT_EQ(extras.cameras[0].keys.size(), 31u);

    // Güneş: geldiği yön (0, .707, .707) → yükseklik 45°, azimut 90°.
    for (const LightDesc& d : extras.lights)
        if (d.type == LightDesc::Type::Directional) {
            EXPECT_NEAR(d.elevationDeg, 45.0f, 1e-2f);
            EXPECT_NEAR(d.azimuthDeg, 90.0f, 1e-2f);
        }

    // Uygulama modeli yerleştirdikten sonra: 2× ölçek + kaydırma.
    const Transform xf = Transform::translate(Vec3f(10, 0, 0)) * Transform::scale(Vec3f(2.0f));
    ImportExtras moved = extras;
    transformImportExtras(moved, xf);
    EXPECT_LT(dist(moved.cameras[0].keys.front().eye, Vec3f(10, 2, 10)), 1e-3f);
    for (size_t i = 0; i < extras.lights.size(); ++i) {
        const LightDesc& a = extras.lights[i];
        const LightDesc& b = moved.lights[i];
        if (a.type == LightDesc::Type::Point) {
            EXPECT_NEAR(b.intensity, 4.0f * a.intensity, 1e-3f * a.intensity);
            EXPECT_LT(dist(b.position, Vec3f(14, 6, 2)), 1e-3f);
        }
        if (a.type == LightDesc::Type::Directional) {
            EXPECT_NEAR(b.intensity, a.intensity, 1e-6f);
            EXPECT_NEAR(b.elevationDeg, a.elevationDeg, 1e-3f);
        }
    }
}
