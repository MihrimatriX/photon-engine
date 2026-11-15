// Opaklık (alfa kesikleri) testleri. Kart siyah ve speküler değil: kendisi hiç ışık yansıtmaz,
// görünen her şey kartın ARKASINDAN geçmiştir. Beyaz fırında (L = 1) bir ışının kartı geçme
// olasılığı 1 − α olduğundan beklenen parlaklık tam olarak 1 − α'dır; elle hesaplanabilir.
//
// Yakaladığı hatalar: geçişte throughput'un ayrıca (1 − α) ile çarpılması (0.75 yerine 0.5625),
// gölge ışınının opaklığı görmemesi, ışığın iki kez sayılması, sRGB eğrisinin veri haritasına
// uygulanması, opak modda doku alfasının kullanılması, doku tekrarının unutulması.

#include <gtest/gtest.h>
#include "core/image/image_io.h"
#include "core/platform/path.h"
#include "engine/embree_accel.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "integrators/path_tracer.h"
#include "io/gltf_loader.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "samplers/independent_sampler.h"
#include "scene/material_library.h"
#include "scene/model_import.h"
#include "scene/project_io.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

using namespace photon;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<EnvironmentLight> whiteFurnace() {
    return std::make_shared<EnvironmentLight>(nullptr, 0.0f, 2.0f); // görüntüsüz: 0.5 · yoğunluk
}

std::shared_ptr<DisneyMaterial> blackCard(float opacity) {
    auto m = std::make_shared<DisneyMaterial>(Color3f(0.0f), 0.0f, 1.0f, 0.0f);
    m->setOpacity(opacity);
    return m;
}

/// z = 0 düzleminde [-2, 2]² kart; UV [0, 1]² (u = 0 solda).
std::shared_ptr<TriangleMesh> makeCard(const Material* m) {
    return std::make_shared<TriangleMesh>(
        std::vector<Vec3f>{{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}}, std::vector<Vec3f>{},
        std::vector<Vec2f>{{0, 1}, {1, 1}, {1, 0}, {0, 0}}, std::vector<uint32_t>{0, 1, 2, 0, 2, 3}, m);
}

Color3f throughCard(const std::shared_ptr<DisneyMaterial>& card, float x = 0.1f) {
    Scene scene;
    scene.retainMaterial(card);
    scene.addShape(makeCard(card.get()));
    scene.setEnvironment(whiteFurnace());
    scene.buildAccelerator();
    PathTracer pt(4);
    IndependentSampler sampler(5);
    Color3f sum(0.0f);
    const int n = 20000;
    for (int i = 0; i < n; ++i) sum += pt.Li(Ray(Vec3f(x, 0.1f, 5.0f), Vec3f(0, 0, -1)), scene, sampler);
    return sum / static_cast<float>(n);
}

/// Dört piksellik 1 satır doku: sol iki piksel @p left, sağ ikisi @p right.
std::shared_ptr<Image> halfImage(float left, float right) {
    auto img = std::make_shared<Image>(4, 1);
    for (int x = 0; x < 4; ++x) img->setPixel(x, 0, Color3f(x < 2 ? left : right));
    return img;
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

SurfaceInteraction atUv(float u) {
    SurfaceInteraction si;
    si.uv = Vec2f(u, 0.5f);
    return si;
}

/// Tek üçgenli glTF yazar ve yükler. @p materialJson glTF "materials" dizisinin tek elemanı.
GltfLoadResult loadGltfWith(const fs::path& dir, const std::string& materialJson, const std::string& extra = "") {
    const fs::path bin = dir / "buf.bin", gltf = dir / "m.gltf";
    std::vector<unsigned char> bytes(42, 0);
    const float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(bytes.data(), verts, sizeof(verts));
    const uint16_t idx[3] = {0, 1, 2};
    std::memcpy(bytes.data() + 36, idx, sizeof(idx));
    {
        std::ofstream out(bin, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    {
        std::ofstream out(gltf);
        out << R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
  "buffers": [{"uri": "buf.bin", "byteLength": 42}],
  "materials": [)" << materialJson << "]" << extra << "}";
    }
    return GltfLoader::load(gltf.string(), nullptr);
}

const DisneyMaterial* firstDisney(const GltfLoadResult& r) {
    return r.meshes.empty() ? nullptr : dynamic_cast<const DisneyMaterial*>(r.meshes[0]->material());
}

} // namespace

TEST(Opacity, CameraRaySeesThroughInProportion) {
    EXPECT_NEAR(throughCard(blackCard(1.0f)).g, 0.0f, 1e-4f);
    EXPECT_NEAR(throughCard(blackCard(0.0f)).g, 1.0f, 1e-4f);
    EXPECT_NEAR(throughCard(blackCard(0.25f)).g, 0.75f, 0.015f);
    EXPECT_NEAR(throughCard(blackCard(0.6f)).g, 0.4f, 0.015f);
}

// Işık ile difüz zemin arasında %50 opak kart: zemin tam yarı parlaklıkta kalır. Gölge ışını
// geçirgenliği görmezse 1, yalnız o görürse (BSDF ışını kartta durursa) MIS payı kadar az,
// (ışık büyük: küçük ışıkta BSDF payı testin hata payından küçük kalır ve bu hata gizlenir)
// iki strateji tutarsızsa fazla çıkar. İki kesişim arka ucu da aynı sonucu vermeli.
TEST(Opacity, ShadowIsProportionalWithoutDoubleCounting) {
    auto shade = [](bool withCard) {
        const Lambertian floorMat(Color3f(0.7f));
        auto card = blackCard(0.5f);
        Scene scene;
        const Vec3f up(0, 1, 0);
        scene.addShape(std::make_shared<TriangleMesh>(
            std::vector<Vec3f>{{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5}}, std::vector<Vec3f>{up, up, up, up},
            std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
        if (withCard) {
            scene.retainMaterial(card);
            scene.addShape(std::make_shared<TriangleMesh>(
                std::vector<Vec3f>{{-3, 1, -3}, {3, 1, -3}, {3, 1, 3}, {-3, 1, 3}}, std::vector<Vec3f>{},
                std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, card.get()));
        }
        scene.addLight(std::make_shared<AreaLight>(Vec3f(-4, 2.5f, -4), Vec3f(8, 0, 0), Vec3f(0, 0, 8), Color3f(2.0f)));
        scene.buildAccelerator();
        PathTracer pt(3, 64);
        IndependentSampler sampler(11);
        Color3f sum(0.0f);
        const int n = 40000;
        for (int i = 0; i < n; ++i) sum += pt.Li(Ray(Vec3f(0.2f, 0.5f, 0.3f), Vec3f(0, -1, 0)), scene, sampler);
        return sum.g / static_cast<float>(n);
    };
    for (const bool own : {true, false}) {
        if (!own && !EmbreeAccel::available()) continue;
        SCOPED_TRACE(own ? "yerleşik BVH" : "Embree");
        Scene::setPreferOwnBvh(own);
        const float open = shade(false);
        ASSERT_GT(open, 0.05f);
        EXPECT_NEAR(shade(true), 0.5f * open, 0.03f * open);
    }
    Scene::setPreferOwnBvh(false);
}

TEST(Opacity, MapCutsTheCard) {
    auto card = blackCard(1.0f);
    card->setOpacityImage(halfImage(0.0f, 1.0f));
    EXPECT_NEAR(throughCard(card, -1.5f).g, 1.0f, 1e-4f); // u = 0.125: kesik
    EXPECT_NEAR(throughCard(card, 1.5f).g, 0.0f, 1e-4f);  // u = 0.875: dolu
}

TEST(Opacity, FactorMultipliesTheMap) {
    auto card = blackCard(0.4f);
    card->setOpacityImage(halfImage(0.0f, 1.0f));
    EXPECT_NEAR(card->opacity(atUv(0.875f)), 0.4f, 1e-5f);
    EXPECT_NEAR(card->opacity(atUv(0.125f)), 0.0f, 1e-5f);
}

// Doku tekrarı (texScale) diğer haritalarla aynı koordinatı kullanmalı: 0.5 ile u = 0.625,
// 0.3125'e iner ve dokunun kesik yarısına düşer; ölçek unutulursa dolu yarıda kalırdı.
TEST(Opacity, MapFollowsMaterialTextureScale) {
    auto card = blackCard(1.0f);
    card->setOpacityImage(halfImage(0.0f, 1.0f));
    EXPECT_NEAR(card->opacity(atUv(0.625f)), 1.0f, 1e-5f);
    card->setTextureScale(0.5f);
    EXPECT_NEAR(card->opacity(atUv(0.625f)), 0.0f, 1e-5f);
}

TEST(Opacity, OpaqueMaterialKeepsFastShadowPath) {
    EXPECT_FALSE(blackCard(1.0f)->transmitsShadows());
    EXPECT_TRUE(blackCard(0.5f)->transmitsShadows());
    auto mapped = blackCard(1.0f);
    mapped->setOpacityImage(halfImage(0.0f, 1.0f));
    EXPECT_TRUE(mapped->transmitsShadows());
}

// Şeffaf fonda alfa kanalı: örneklerin yalnız kartta duran payı kapsama sayılır (Renderer
// primary.hit'e bakar). Kesik bölgeden geçen örnek "boşluk"tur, kompozitte arkası görünür.
TEST(Opacity, TransparentBackgroundCoverageMatchesOpacity) {
    auto card = blackCard(0.25f);
    Scene scene;
    scene.retainMaterial(card);
    scene.addShape(makeCard(card.get()));
    scene.setEnvironment(whiteFurnace());
    Background bg;
    bg.mode = Background::Mode::Transparent;
    scene.setBackground(bg);
    scene.buildAccelerator();
    PathTracer pt(4);
    IndependentSampler sampler(9);
    int hits = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        PathTracer::PrimaryHit ph;
        pt.Li(Ray(Vec3f(0.1f, 0.1f, 5.0f), Vec3f(0, 0, -1)), scene, sampler, &ph);
        if (ph.hit) ++hits;
    }
    EXPECT_NEAR(static_cast<float>(hits) / n, 0.25f, 0.015f);
}

// Dosya alfası varsa o, yoksa gri değer maske olur; veri haritasıdır, sRGB eğrisi uygulanmaz
// (64/255 = 0.251; sRGB çözülseydi 0.051 olurdu).
TEST(OpacityMap, AlphaChannelWinsOverGreyAndStaysLinear) {
    const fs::path dir = scratch("photon_opacity_map");
    const uint8_t withAlpha[] = {255, 255, 255, 0, 255, 255, 255, 255};
    const fs::path a = dir / "alpha.png";
    ASSERT_TRUE(saveRGBA8PNG(withAlpha, 2, 1, pathToUtf8(a), false));
    auto m = blackCard(1.0f);
    m->setOpacityMap(pathToUtf8(a));
    EXPECT_NEAR(m->opacity(atUv(0.25f)), 0.0f, 1e-5f);
    EXPECT_NEAR(m->opacity(atUv(0.75f)), 1.0f, 1e-5f);

    const uint8_t grey[] = {64, 64, 64, 255, 64, 64, 64, 255};
    const fs::path g = dir / "grey.png";
    ASSERT_TRUE(saveRGBA8PNG(grey, 2, 1, pathToUtf8(g), false));
    m->setOpacityMap(pathToUtf8(g));
    EXPECT_NEAR(m->opacity(atUv(0.25f)), 64.0f / 255.0f, 1e-5f);
    fs::remove_all(dir);
}

// Proje başka klasörde yaşasa da (ör. otomatik kayıt %APPDATA% altında) doku yolu bozulmaz.
TEST(OpacityMap, SurvivesProjectRoundTripInAnotherFolder) {
    const fs::path dir = scratch("photon_opacity_project");
    fs::create_directories(dir / "autosave");
    const uint8_t px[] = {255, 255, 255, 0, 255, 255, 255, 255};
    const fs::path tex = dir / "yaprak.png";
    ASSERT_TRUE(saveRGBA8PNG(px, 2, 1, pathToUtf8(tex), false));

    SceneGraph graph;
    auto mat = blackCard(0.3f);
    mat->setOpacityMap(pathToUtf8(tex));
    auto node = std::make_unique<SceneNode>("Kart", SceneNodeType::Mesh);
    node->mesh = makeCard(mat.get());
    node->material = mat;
    graph.root()->addChild(std::move(node));

    const fs::path file = dir / "autosave" / "recovery.photon";
    ASSERT_TRUE(saveProject(pathToUtf8(file), graph, ProjectData{}));
    SceneGraph back;
    ProjectData data;
    ASSERT_TRUE(loadProject(pathToUtf8(file), back, data));
    ASSERT_EQ(back.root()->children.size(), 1u);
    const auto* d = dynamic_cast<const DisneyMaterial*>(back.root()->children[0]->material.get());
    ASSERT_NE(d, nullptr);
    EXPECT_NEAR(d->baseOpacity(), 0.3f, 1e-6f);
    EXPECT_EQ(pathFromUtf8(d->opacityMap()).lexically_normal(), tex.lexically_normal());
    EXPECT_NEAR(d->opacity(atUv(0.75f)), 0.3f, 1e-5f); // doku gerçekten yüklendi
    fs::remove_all(dir);
}

TEST(OpacityMap, PresetKeepsOpacity) {
    auto mat = blackCard(0.3f);
    auto copy = MaterialLibrary::createMaterial(MaterialLibrary::presetFromMaterial(*mat, "kart"));
    const auto* d = dynamic_cast<const DisneyMaterial*>(copy.get());
    ASSERT_NE(d, nullptr);
    EXPECT_NEAR(d->baseOpacity(), 0.3f, 1e-6f);
}

TEST(GltfOpacity, AlphaModesFollowTheSpec) {
    const fs::path dir = scratch("photon_opacity_gltf");
    struct Case {
        const char* mode;
        float alpha;
        float want;
    };
    // Spec: OPAQUE alfayı yok sayar; BLEND alfayı kullanır; MASK alpha ≥ cutoff (0.5) ise tam dolu.
    for (const Case c : {Case{"OPAQUE", 0.4f, 1.0f}, Case{"BLEND", 0.4f, 0.4f}, Case{"MASK", 0.4f, 0.0f}, Case{"MASK", 0.6f, 1.0f}}) {
        SCOPED_TRACE(c.mode);
        std::ostringstream m;
        m << R"({"alphaMode": ")" << c.mode << R"(", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, )" << c.alpha << "]}}";
        const GltfLoadResult r = loadGltfWith(dir, m.str());
        const DisneyMaterial* d = firstDisney(r);
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->opacity(atUv(0.5f)), c.want, 1e-5f);
    }
    fs::remove_all(dir);
}

TEST(GltfOpacity, BaseColorAlphaBecomesTheMap) {
    const fs::path dir = scratch("photon_opacity_gltf_tex");
    // Sol piksel alfa 102 (0.4), sağ piksel alfa 153 (0.6).
    const uint8_t px[] = {200, 100, 50, 102, 200, 100, 50, 153};
    ASSERT_TRUE(saveRGBA8PNG(px, 2, 1, pathToUtf8(dir / "tex.png"), false));
    const std::string images = R"(, "images": [{"uri": "tex.png"}], "textures": [{"source": 0}])";
    auto material = [](const char* mode) {
        return std::string(R"({"alphaMode": ")") + mode + R"(", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})";
    };

    const GltfLoadResult blend = loadGltfWith(dir, material("BLEND"), images);
    ASSERT_NE(firstDisney(blend), nullptr);
    EXPECT_NEAR(firstDisney(blend)->opacity(atUv(0.25f)), 0.4f, 1e-5f);

    const GltfLoadResult mask = loadGltfWith(dir, material("MASK"), images);
    ASSERT_NE(firstDisney(mask), nullptr);
    EXPECT_NEAR(firstDisney(mask)->opacity(atUv(0.25f)), 0.0f, 1e-5f);
    EXPECT_NEAR(firstDisney(mask)->opacity(atUv(0.75f)), 1.0f, 1e-5f);

    // Çoğu PNG'nin alfası vardır ama OPAQUE malzemede anlamı yoktur: gölge yolu hızlı kalmalı.
    const GltfLoadResult opaque = loadGltfWith(dir, material("OPAQUE"), images);
    ASSERT_NE(firstDisney(opaque), nullptr);
    EXPECT_FALSE(firstDisney(opaque)->transmitsShadows());
    fs::remove_all(dir);
}

// Klasik Max/Maya FBX'inde kesik doku TransparentColor'a bağlanır; ufbx bunu "transmission"
// sayar. Eskiden malzeme cama dönüyordu: yaprak kartı kırıcı bir cam levha oluyordu.
TEST(FbxOpacity, LegacyTransparentColorTextureIsACutoutNotGlass) {
    const fs::path dir = scratch("photon_opacity_fbx");
    const uint8_t px[] = {255, 255, 255, 0, 255, 255, 255, 255};
    ASSERT_TRUE(saveRGBA8PNG(px, 2, 1, pathToUtf8(dir / "yaprak.png"), false));
    {
        std::ofstream out(dir / "kart.fbx");
        out << R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
Objects:  {
    Geometry: 1, "Geometry::g", "Mesh" {
        Vertices: *12 { a: 0,0,0,1,0,0,1,1,0,0,1,0 }
        PolygonVertexIndex: *4 { a: 0,1,2,-4 }
        GeometryVersion: 124
    }
    Model: 2, "Model::Kart", "Mesh" {
        Version: 232
    }
    Material: 3, "Material::yaprak", "" {
        Version: 102
        ShadingModel: "phong"
        Properties70:  {
            P: "DiffuseColor", "Color", "", "A",0.2,0.6,0.2
            P: "TransparencyFactor", "Number", "", "A",1
        }
    }
    Texture: 4, "Texture::t", "" {
        Type: "TextureVideoClip"
        FileName: "yaprak.png"
        RelativeFilename: "yaprak.png"
    }
}
Connections:  {
    C: "OO",2,0
    C: "OO",1,2
    C: "OO",3,2
    C: "OP",4,3, "TransparentColor"
}
)";
    }
    std::string err;
    auto group = importModelFile(pathToUtf8(dir / "kart.fbx"), &err);
    ASSERT_TRUE(group) << err;
    const SceneNode* part = group.get();
    while (part && !part->mesh) part = part->children.empty() ? nullptr : part->children[0].get();
    ASSERT_NE(part, nullptr);
    const auto* d = dynamic_cast<const DisneyMaterial*>(part->material.get());
    ASSERT_NE(d, nullptr) << "cam olarak içe aktarıldı";
    EXPECT_EQ(pathFromUtf8(d->opacityMap()).filename(), fs::path("yaprak.png"));
    EXPECT_NEAR(d->opacity(atUv(0.25f)), 0.0f, 1e-5f);
    EXPECT_NEAR(d->opacity(atUv(0.75f)), 1.0f, 1e-5f);
    group.reset();
    fs::remove_all(dir);
}
