// OpenUSD içe aktarıcısı (io/usd_loader): birimler ve eksen, çokgen ve GeomSubset, sarma yönü,
// örnekleme (yerel instancing + PointInstancer), malzeme eşlemeleri (UsdPreviewSurface, OpenPBR,
// Standard Surface), dokular ve UV dönüşümü, UsdLux ışık birimleri, kamera ve kubbe ışığının
// yönü. Her test küçük bir .usda dosyasını kendisi yazar. Render eşitliği (aynı sahne USD'den ve
// Mitsuba'dan) test_golden_reference.cpp içindedir.
#include <gtest/gtest.h>

#ifdef PHOTON_HAS_USD

#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "io/imported_model.h"
#include "io/usd_loader.h"
#include "lights/environment_light.h"
#include "materials/disney.h"
#include "scene/model_import.h"
#include "scene/scene_node.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>

using namespace photon;
namespace fs = std::filesystem;

namespace {

fs::path testDir() {
    static const fs::path dir = [] {
        fs::path d = fs::temp_directory_path() / "photon_usd_tests";
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d);
        setEmbeddedTextureCacheDir((d / "cache").string());
        return d;
    }();
    return dir;
}

std::string writeUsda(const std::string& name, const std::string& body, const std::string& layerMeta = "") {
    const fs::path p = testDir() / (name + ".usda");
    std::ofstream f(p, std::ios::binary);
    f << "#usda 1.0\n(\n" << (layerMeta.empty() ? "    metersPerUnit = 1\n    upAxis = \"Y\"\n" : layerMeta) << ")\n\n" << body;
    return p.string();
}

ImportedModel loadOk(const std::string& path, float unitMeters = 0.0f) {
    std::string err;
    ImportedModel m = UsdLoader::load(path, &err, unitMeters);
    EXPECT_FALSE(m.empty()) << err;
    return m;
}

/// Grubun dünya uzayındaki köşe sınırları.
void worldBounds(const SceneNode& n, Vec3f& lo, Vec3f& hi) {
    if (n.mesh) {
        const Transform w = n.worldTransform();
        for (const Vec3f& p : n.mesh->positions()) {
            const Vec3f q = w.transformPoint(p);
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], q[k]);
                hi[k] = std::max(hi[k], q[k]);
            }
        }
    }
    for (const auto& c : n.children) worldBounds(*c, lo, hi);
}

void collectMeshNodes(const SceneNode& n, std::vector<const SceneNode*>& out) {
    if (n.mesh) out.push_back(&n);
    for (const auto& c : n.children) collectMeshNodes(*c, out);
}

const DisneyMaterial* disneyOf(const ImportedModel& m, const std::string& partName) {
    for (const ImportedMesh& im : m.meshes)
        if (im.name == partName) return dynamic_cast<const DisneyMaterial*>(im.material.get());
    return nullptr;
}

const char* kQuad = R"(
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)]
        uniform token subdivisionScheme = "none"
)";

std::string mesh(const std::string& name, const std::string& extra = "", const std::string& schemas = "") {
    std::string s = "def Mesh \"" + name + "\"";
    if (!schemas.empty()) s += " (\n    prepend apiSchemas = [" + schemas + "]\n)";
    return s + "\n{" + kQuad + extra + "}\n";
}

std::string previewMaterial(const std::string& path, const std::string& inputs) {
    const std::string name = path.substr(path.rfind('/') + 1);
    return "def Material \"" + name + "\"\n{\n    token outputs:surface.connect = <" + path +
           "/S.outputs:surface>\n    def Shader \"S\"\n    {\n        uniform token info:id = \"UsdPreviewSurface\"\n" + inputs +
           "        token outputs:surface\n    }\n}\n";
}

std::string mtlxMaterial(const std::string& path, const std::string& nodeId, const std::string& inputs) {
    const std::string name = path.substr(path.rfind('/') + 1);
    return "def Material \"" + name + "\"\n{\n    token outputs:mtlx:surface.connect = <" + path +
           "/S.outputs:out>\n    def Shader \"S\"\n    {\n        uniform token info:id = \"" + nodeId + "\"\n" + inputs +
           "        token outputs:out\n    }\n}\n";
}

} // namespace

// ─────────────────────────────── birimler ve eksen ───────────────────────────────

TEST(UsdImport, ZUpCentimetreStageBecomesYUpMetres) {
    // Z yukarı, santimetre: 100 birimlik küp tabanı z = 0'da. Motor uzayında 1 m'lik, zemine oturan küp.
    const std::string path = writeUsda("zup_cm", R"(
def Cube "Box"
{
    double size = 100
    double3 xformOp:translate = (0, 0, 50)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)", "    metersPerUnit = 0.01\n    upAxis = \"Z\"\n");
    std::string err;
    auto g = importModelFile(path, &err);
    ASSERT_NE(g, nullptr) << err;
    Vec3f lo(1e9f), hi(-1e9f);
    worldBounds(*g, lo, hi);
    EXPECT_NEAR(lo.y, 0.0f, 1e-5f);
    EXPECT_NEAR(hi.y, 1.0f, 1e-5f);
    EXPECT_NEAR(lo.x, -0.5f, 1e-5f);
    EXPECT_NEAR(hi.x, 0.5f, 1e-5f);
    EXPECT_NEAR(lo.z, -0.5f, 1e-5f);
    EXPECT_NEAR(hi.z, 0.5f, 1e-5f);
}

TEST(UsdImport, UnauthoredUnitDefaultsToCentimetresUnlessOverridden) {
    const std::string path = writeUsda("no_units", R"(
def Cube "Box"
{
    double size = 100
}
)", "    upAxis = \"Y\"\n");
    std::string err;
    auto g = importModelFile(path, &err);
    ASSERT_NE(g, nullptr) << err;
    Vec3f lo(1e9f), hi(-1e9f);
    worldBounds(*g, lo, hi);
    EXPECT_NEAR(hi.x - lo.x, 1.0f, 1e-5f); // USD varsayılanı: 1 birim = 1 cm

    ModelImportOptions opt;
    opt.unitMeters = 0.001f; // kullanıcı: milimetre
    g = importModelFile(path, &err, opt);
    ASSERT_NE(g, nullptr) << err;
    lo = Vec3f(1e9f);
    hi = Vec3f(-1e9f);
    worldBounds(*g, lo, hi);
    EXPECT_NEAR(hi.x - lo.x, 0.1f, 1e-6f);
}

// ─────────────────────────────── geometri ───────────────────────────────

TEST(UsdImport, PolygonsTriangulateAndSubsetsSplitMaterials) {
    // Bir dörtgen ve bir içbükey beşgen; iki GeomSubset iki malzemeye bağlı.
    const std::string path = writeUsda("subsets", R"(
def Scope "Looks"
{
)" + previewMaterial("/Looks/Red", "        color3f inputs:diffuseColor = (0.8, 0.1, 0.1)\n") +
                                                      previewMaterial("/Looks/Blue", "        color3f inputs:diffuseColor = (0.1, 0.1, 0.8)\n") + R"(
}
def Mesh "Part" (
    prepend apiSchemas = ["MaterialBindingAPI"]
)
{
    int[] faceVertexCounts = [4, 5]
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7, 8]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
                        (2, 0, 0), (4, 0, 0), (4, 2, 0), (3, 0.5, 0), (2, 2, 0)]
    uniform token subdivisionScheme = "none"
    uniform token subsetFamily:materialBind:familyType = "partition"
    def GeomSubset "Kirmizi" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [0]
        rel material:binding = </Looks/Red>
    }
    def GeomSubset "Mavi" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        uniform token elementType = "face"
        uniform token familyName = "materialBind"
        int[] indices = [1]
        rel material:binding = </Looks/Blue>
    }
}
)");
    const ImportedModel m = loadOk(path);
    ASSERT_EQ(m.meshes.size(), 2u);
    const DisneyMaterial* red = disneyOf(m, "Part · Kirmizi");
    const DisneyMaterial* blue = disneyOf(m, "Part · Mavi");
    ASSERT_NE(red, nullptr);
    ASSERT_NE(blue, nullptr);
    EXPECT_NEAR(red->baseColor().r, 0.8f, 1e-6f);
    EXPECT_NEAR(blue->baseColor().b, 0.8f, 1e-6f);
    size_t quadTris = 0, pentTris = 0;
    double pentArea = 0.0;
    for (const ImportedMesh& im : m.meshes) {
        if (im.name == "Part · Kirmizi") quadTris = im.mesh->numTriangles();
        if (im.name == "Part · Mavi") {
            pentTris = im.mesh->numTriangles();
            const auto& p = im.mesh->positions();
            const auto& ix = im.mesh->indices();
            for (size_t t = 0; t + 2 < ix.size(); t += 3)
                pentArea += 0.5 * (p[ix[t + 1]] - p[ix[t]]).cross(p[ix[t + 2]] - p[ix[t]]).length();
        }
    }
    EXPECT_EQ(quadTris, 2u);
    EXPECT_EQ(pentTris, 3u);
    // İçbükey beşgenin alanı (ayakkabı bağı formülü): 2.5. Yelpaze üçgenleme içbükey köşede taşar.
    EXPECT_NEAR(pentArea, 2.5, 1e-5);
}

TEST(UsdImport, LeftHandedOrientationFlipsWinding) {
    // Aynı üçgen iki kez: sağ el (saat yönü tersi ön yüz) ve sol el kuralıyla. Normal +Z istenir.
    const std::string body = R"(
def Mesh "R"
{
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    uniform token subdivisionScheme = "none"
}
def Mesh "L"
{
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(0, 0, 0), (0, 1, 0), (1, 0, 0)]
    uniform token orientation = "leftHanded"
    uniform token subdivisionScheme = "none"
}
)";
    const ImportedModel m = loadOk(writeUsda("winding", body));
    ASSERT_EQ(m.meshes.size(), 2u);
    for (const ImportedMesh& im : m.meshes) {
        const auto& p = im.mesh->positions();
        const auto& ix = im.mesh->indices();
        ASSERT_EQ(ix.size(), 3u);
        const Vec3f ng = (p[ix[1]] - p[ix[0]]).cross(p[ix[2]] - p[ix[0]]);
        EXPECT_GT(ng.z, 0.0f) << im.name;
        EXPECT_GT(im.mesh->normals()[ix[0]].z, 0.9f) << im.name;
    }
}

TEST(UsdImport, NativeInstancesShareGeometry) {
    const std::string path = writeUsda("instancing", R"(
class "Proto"
{
    )" + mesh("Plate") + R"(
}
def Xform "A" (
    instanceable = true
    prepend inherits = </Proto>
)
{
    double3 xformOp:translate = (-2, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
def Xform "B" (
    instanceable = true
    prepend inherits = </Proto>
)
{
    double3 xformOp:translate = (2, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)");
    std::string err;
    auto g = importModelFile(path, &err);
    ASSERT_NE(g, nullptr) << err;
    std::vector<const SceneNode*> nodes;
    collectMeshNodes(*g, nodes);
    ASSERT_EQ(nodes.size(), 2u);
    EXPECT_EQ(nodes[0]->mesh.get(), nodes[1]->mesh.get()); // tek geometri, iki örnek
    const float x0 = nodes[0]->worldTransform().transformPoint(Vec3f(0.0f)).x;
    const float x1 = nodes[1]->worldTransform().transformPoint(Vec3f(0.0f)).x;
    EXPECT_NEAR(std::min(x0, x1), -2.0f, 1e-5f);
    EXPECT_NEAR(std::max(x0, x1), 2.0f, 1e-5f);
}

TEST(UsdImport, PointInstancerClonesPrototypes) {
    const std::string path = writeUsda("point_instancer", R"(
def PointInstancer "Field"
{
    rel prototypes = [</Field/Protos/Plate>]
    int[] protoIndices = [0, 0, 0]
    point3f[] positions = [(0, 0, 0), (3, 0, 0), (0, 0, 5)]
    quath[] orientations = [(1, 0, 0, 0), (0.7071068, 0, 0.7071068, 0), (1, 0, 0, 0)]
    float3[] scales = [(1, 1, 1), (1, 1, 1), (2, 2, 2)]
    def Scope "Protos"
    {
        )" + mesh("Plate") + R"(
    }
}
)");
    std::string err;
    auto g = importModelFile(path, &err);
    ASSERT_NE(g, nullptr) << err;
    std::vector<const SceneNode*> nodes;
    collectMeshNodes(*g, nodes);
    ASSERT_EQ(nodes.size(), 3u) << "prototip kendisi ayrıca çizilmemeli";
    std::set<const TriangleMesh*> meshes;
    for (const SceneNode* n : nodes) meshes.insert(n->mesh.get());
    EXPECT_EQ(meshes.size(), 1u);
    Vec3f lo(1e9f), hi(-1e9f);
    worldBounds(*g, lo, hi);
    EXPECT_NEAR(lo.x, -2.0f, 1e-4f); // 3. örnek: 2× ölçek, x ∈ [−2, 2]
    EXPECT_NEAR(hi.x, 4.0f, 1e-4f);  // 2. örnek: 90° dönmüş, x ∈ [2, 4]
    EXPECT_NEAR(hi.z, 7.0f, 1e-4f);  // 3. örnek z ∈ [3, 7]
}

// ─────────────────────────────── malzemeler ───────────────────────────────

TEST(UsdImport, PreviewSurfaceMapsToPrincipled) {
    const std::string path = writeUsda("preview", R"(
def Scope "Looks"
{
)" + previewMaterial("/Looks/Plastic", "        color3f inputs:diffuseColor = (0.2, 0.4, 0.6)\n"
                                        "        float inputs:metallic = 0.25\n        float inputs:roughness = 0.35\n"
                                        "        float inputs:ior = 1.33\n        float inputs:clearcoat = 0.5\n"
                                        "        float inputs:clearcoatRoughness = 0.2\n"
                                        "        color3f inputs:emissiveColor = (0.5, 0.25, 0)\n") +
                                                     previewMaterial("/Looks/Glass", "        float inputs:opacity = 0\n"
                                                                                     "        float inputs:ior = 1.5\n"
                                                                                     "        float inputs:roughness = 0.1\n") +
                                                     previewMaterial("/Looks/Gold", "        int inputs:useSpecularWorkflow = 1\n"
                                                                                    "        color3f inputs:diffuseColor = (0, 0, 0)\n"
                                                                                    "        color3f inputs:specularColor = (1, 0.77, 0.34)\n") +
                                                     previewMaterial("/Looks/Ceramic", "        int inputs:useSpecularWorkflow = 1\n"
                                                                                       "        color3f inputs:diffuseColor = (0.9, 0.9, 0.9)\n"
                                                                                       "        color3f inputs:specularColor = (0.04, 0.04, 0.04)\n") +
                                                     previewMaterial("/Looks/Leaf", "        float inputs:opacity = 0.2\n"
                                                                                    "        float inputs:opacityThreshold = 0.5\n") +
                                                     R"(
}
)" + mesh("Plastic", "    rel material:binding = </Looks/Plastic>\n", "\"MaterialBindingAPI\"") +
                                                     mesh("Glass", "    rel material:binding = </Looks/Glass>\n", "\"MaterialBindingAPI\"") +
                                                     mesh("Gold", "    rel material:binding = </Looks/Gold>\n", "\"MaterialBindingAPI\"") +
                                                     mesh("Ceramic", "    rel material:binding = </Looks/Ceramic>\n", "\"MaterialBindingAPI\"") +
                                                     mesh("Leaf", "    rel material:binding = </Looks/Leaf>\n", "\"MaterialBindingAPI\""));
    const ImportedModel m = loadOk(path);
    const DisneyMaterial* plastic = disneyOf(m, "Plastic");
    ASSERT_NE(plastic, nullptr);
    EXPECT_NEAR(plastic->baseColor().g, 0.4f, 1e-6f);
    EXPECT_NEAR(plastic->metallic(), 0.25f, 1e-6f);
    EXPECT_NEAR(plastic->roughness(), 0.35f, 1e-6f);
    EXPECT_NEAR(plastic->ior(), 1.33f, 1e-5f);
    EXPECT_NEAR(plastic->specular(), 0.5f, 1e-6f); // F0 kırılma indisinden
    EXPECT_NEAR(plastic->clearCoat(), 0.5f, 1e-6f);
    EXPECT_NEAR(plastic->clearCoatRoughness(), 0.2f, 1e-6f);
    EXPECT_NEAR(plastic->emitted(SurfaceInteraction{}).r, 0.5f, 1e-6f);

    // opacityMode transparent (varsayılan): opaklık 0 → kırılan cam, geçen ışık boyanmaz (taban beyaz).
    const DisneyMaterial* glass = disneyOf(m, "Glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_NEAR(glass->specularTransmission(), 1.0f, 1e-6f);
    EXPECT_NEAR(glass->baseColor().r, 1.0f, 1e-6f);
    EXPECT_NEAR(glass->ior(), 1.5f, 1e-6f);
    EXPECT_NEAR(glass->baseOpacity(), 1.0f, 1e-6f);

    // Speküler akış: siyah taban + parlak renkli F0 → metal; gri F0 0.04 → IOR 1.5 yalıtkan.
    const DisneyMaterial* gold = disneyOf(m, "Gold");
    ASSERT_NE(gold, nullptr);
    EXPECT_NEAR(gold->metallic(), 1.0f, 1e-6f);
    EXPECT_NEAR(gold->baseColor().g, 0.77f, 1e-6f);
    const DisneyMaterial* ceramic = disneyOf(m, "Ceramic");
    ASSERT_NE(ceramic, nullptr);
    EXPECT_NEAR(ceramic->metallic(), 0.0f, 1e-6f);
    EXPECT_NEAR(ceramic->ior(), 1.5f, 1e-3f);

    // Eşikli opaklık: kesme maskesi (0.2 < 0.5 → tamamen kesik), geçirgenlik yok.
    const DisneyMaterial* leaf = disneyOf(m, "Leaf");
    ASSERT_NE(leaf, nullptr);
    EXPECT_NEAR(leaf->baseOpacity(), 0.0f, 1e-6f);
    EXPECT_NEAR(leaf->specularTransmission(), 0.0f, 1e-6f);
}

TEST(UsdImport, MaterialXOpenPbrAndStandardSurfaceMap) {
    const std::string path = writeUsda("mtlx", R"(
def Scope "Looks"
{
)" + mtlxMaterial("/Looks/Matte", "ND_open_pbr_surface_surfaceshader",
                  "        color3f inputs:base_color = (0.5, 0.6, 0.7)\n        float inputs:specular_weight = 0\n"
                  "        float inputs:base_diffuse_roughness = 0.3\n") +
                                                  mtlxMaterial("/Looks/Coated", "ND_open_pbr_surface_surfaceshader",
                                                               "        float inputs:base_metalness = 1\n"
                                                               "        float inputs:specular_roughness = 0.4\n"
                                                               "        float inputs:coat_weight = 1\n        float inputs:coat_roughness = 0.05\n"
                                                               "        float inputs:coat_ior = 1.5\n") +
                                                  mtlxMaterial("/Looks/Tinted", "ND_open_pbr_surface_surfaceshader",
                                                               "        color3f inputs:base_color = (0.1, 0.1, 0.1)\n"
                                                               "        float inputs:transmission_weight = 1\n"
                                                               "        color3f inputs:transmission_color = (0.2, 0.9, 0.3)\n"
                                                               "        float inputs:specular_ior = 1.7\n") +
                                                  mtlxMaterial("/Looks/Std", "ND_standard_surface_surfaceshader",
                                                               "        float inputs:base = 1\n        color3f inputs:base_color = (0.3, 0.3, 0.3)\n"
                                                               "        float inputs:specular_roughness = 0.6\n        float inputs:sheen = 0.5\n") +
                                                  R"(
}
)" + mesh("Matte", "    rel material:binding = </Looks/Matte>\n", "\"MaterialBindingAPI\"") +
                                                  mesh("Coated", "    rel material:binding = </Looks/Coated>\n", "\"MaterialBindingAPI\"") +
                                                  mesh("Tinted", "    rel material:binding = </Looks/Tinted>\n", "\"MaterialBindingAPI\"") +
                                                  mesh("Std", "    rel material:binding = </Looks/Std>\n", "\"MaterialBindingAPI\""));
    const ImportedModel m = loadOk(path);
    const DisneyMaterial* matte = disneyOf(m, "Matte");
    ASSERT_NE(matte, nullptr);
    EXPECT_NEAR(matte->specular(), 0.0f, 1e-6f); // specular_weight 0 → speküler yok (saf difüz)
    EXPECT_NEAR(matte->diffuseRoughness(), 0.3f, 1e-6f);
    EXPECT_NEAR(matte->baseColor().b, 0.7f, 1e-6f);
    const DisneyMaterial* coated = disneyOf(m, "Coated");
    ASSERT_NE(coated, nullptr);
    EXPECT_NEAR(coated->metallic(), 1.0f, 1e-6f);
    EXPECT_NEAR(coated->roughness(), 0.4f, 1e-6f);
    EXPECT_NEAR(coated->clearCoat(), 1.0f, 1e-6f);
    EXPECT_NEAR(coated->clearCoatRoughness(), 0.05f, 1e-6f);
    const DisneyMaterial* tinted = disneyOf(m, "Tinted");
    ASSERT_NE(tinted, nullptr);
    EXPECT_NEAR(tinted->specularTransmission(), 1.0f, 1e-6f);
    EXPECT_NEAR(tinted->baseColor().g, 0.9f, 1e-6f); // geçen ışık transmission_color ile boyanır
    EXPECT_NEAR(tinted->ior(), 1.7f, 1e-6f);
    const DisneyMaterial* stdSurf = disneyOf(m, "Std");
    ASSERT_NE(stdSurf, nullptr);
    EXPECT_NEAR(stdSurf->baseColor().r, 0.3f, 1e-6f);
    EXPECT_NEAR(stdSurf->roughness(), 0.6f, 1e-6f);
    EXPECT_NEAR(stdSurf->sheen(), 0.5f, 1e-6f);
}

TEST(UsdImport, TexturesAndUvTransform) {
    // 2×2 sRGB doku; UsdTransform2d ölçek 2 ve kaydırma → mesh UV'lerine işlenir.
    Image tex(2, 2);
    tex.setPixel(0, 0, Color3f(1, 0, 0));
    tex.setPixel(1, 0, Color3f(0, 1, 0));
    tex.setPixel(0, 1, Color3f(0, 0, 1));
    tex.setPixel(1, 1, Color3f(1, 1, 1));
    const fs::path png = testDir() / "checker.png";
    ASSERT_TRUE(saveImagePNG(tex, png.string(), ToneMapOperator::Linear, 0.0f, false));
    const std::string path = writeUsda("textured", R"(
def Material "Mat"
{
    token outputs:surface.connect = </Mat/S.outputs:surface>
    def Shader "S"
    {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </Mat/Tex.outputs:rgb>
        token outputs:surface
    }
    def Shader "Tex"
    {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @checker.png@
        token inputs:sourceColorSpace = "sRGB"
        float2 inputs:st.connect = </Mat/Xf.outputs:result>
        float3 outputs:rgb
    }
    def Shader "Xf"
    {
        uniform token info:id = "UsdTransform2d"
        float2 inputs:in.connect = </Mat/Reader.outputs:result>
        float2 inputs:scale = (2, 2)
        float2 inputs:translation = (0.5, 0)
        float2 outputs:result
    }
    def Shader "Reader"
    {
        uniform token info:id = "UsdPrimvarReader_float2"
        string inputs:varname = "uv"
        float2 outputs:result
    }
}
def Mesh "Plane" (
    prepend apiSchemas = ["MaterialBindingAPI"]
)
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    texCoord2f[] primvars:uv = [(0, 0), (1, 0), (1, 1), (0, 1)] (
        interpolation = "vertex"
    )
    uniform token subdivisionScheme = "none"
    rel material:binding = </Mat>
}
)");
    const ImportedModel m = loadOk(path);
    ASSERT_EQ(m.meshes.size(), 1u);
    const auto* d = dynamic_cast<const DisneyMaterial*>(m.meshes[0].material.get());
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(d->albedoMap().empty());
    EXPECT_TRUE(fs::exists(fs::path(d->albedoMap())));
    // Köşe (1, 1) → st' = 2·(1, 1) + (0.5, 0) = (2.5, 2); motor v = 1 − t.
    const auto& p = m.meshes[0].mesh->positions();
    const auto& uv = m.meshes[0].mesh->uvs();
    ASSERT_EQ(uv.size(), p.size());
    bool found = false;
    for (size_t i = 0; i < p.size(); ++i)
        if ((p[i] - Vec3f(1, 1, 0)).length() < 1e-6f) {
            found = true;
            EXPECT_NEAR(uv[i].x, 2.5f, 1e-5f);
            EXPECT_NEAR(uv[i].y, 1.0f - 2.0f, 1e-5f);
        }
    EXPECT_TRUE(found);
}

// ─────────────────────────────── ışıklar ve kamera ───────────────────────────────

TEST(UsdImport, LuxLightUnits) {
    const std::string path = writeUsda("lights", R"(
def RectLight "Box"
{
    float inputs:width = 2
    float inputs:height = 0.5
    float inputs:intensity = 5
    float inputs:exposure = 1
    color3f inputs:color = (1, 0.5, 0.25)
    double3 xformOp:translate = (0, 3, 0)
    float xformOp:rotateX = -90
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateX"]
}
def RectLight "Normalized"
{
    float inputs:width = 2
    float inputs:height = 0.5
    float inputs:intensity = 8
    bool inputs:normalize = true
}
def SphereLight "Bulb"
{
    float inputs:radius = 0.1
    float inputs:intensity = 100
    double3 xformOp:translate = (1, 2, 3)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
def DistantLight "Sun"
{
    float inputs:angle = 0
    float inputs:intensity = 3
    float xformOp:rotateX = -90
    uniform token[] xformOpOrder = ["xformOp:rotateX"]
}
def SphereLight "Warm"
{
    bool inputs:enableColorTemperature = true
    float inputs:colorTemperature = 2700
    bool inputs:treatAsPoint = true
}
)");
    ImportExtras ex;
    std::string err;
    auto g = importModelFile(path, &err, {}, nullptr, &ex);
    ASSERT_NE(g, nullptr) << err;
    transformImportExtras(ex, g->localTransform);
    ASSERT_EQ(ex.lights.size(), 5u);
    auto find = [&](const char* n) -> const LightDesc* {
        for (const LightDesc& l : ex.lights)
            if (l.name == n) return &l;
        return nullptr;
    };
    const LightDesc* box = find("Box");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->type, LightDesc::Type::Area);
    EXPECT_NEAR(box->intensity, 10.0f, 1e-4f); // 5 · 2^1, radyans
    EXPECT_NEAR(box->color.g, 0.5f, 1e-6f);
    EXPECT_NEAR(box->position.y, 3.0f, 1e-5f);
    EXPECT_LT((box->target - box->position).normalized().y, -0.999f); // −Z yerel → aşağı
    EXPECT_NEAR(box->width * box->height, 1.0f, 1e-5f);
    const LightDesc* norm = find("Normalized");
    ASSERT_NE(norm, nullptr);
    EXPECT_NEAR(norm->intensity, 8.0f, 1e-4f); // normalize: 8 / alan 1
    const LightDesc* bulb = find("Bulb");
    ASSERT_NE(bulb, nullptr);
    EXPECT_EQ(bulb->type, LightDesc::Type::Point);
    EXPECT_NEAR(bulb->intensity, 100.0f * PI * 0.01f, 1e-3f); // I = L · π r²
    EXPECT_LT((bulb->position - Vec3f(1, 2, 3)).length(), 1e-5f);
    const LightDesc* sun = find("Sun");
    ASSERT_NE(sun, nullptr);
    EXPECT_EQ(sun->type, LightDesc::Type::Directional);
    EXPECT_NEAR(sun->intensity, 3.0f, 1e-5f); // açı 0: delta, ışınım = intensity
    EXPECT_NEAR(sun->elevationDeg, 90.0f, 1e-3f);
    const LightDesc* warm = find("Warm");
    ASSERT_NE(warm, nullptr);
    EXPECT_GT(warm->color.r, warm->color.b * 1.5f); // 2700 K: sıcak
}

TEST(UsdImport, LightUnitsSurviveCentimetreStage) {
    // Santimetre sahnesi: konum ve boyut metreye, nokta yoğunluğu (W/sr) metre uzayında aynı kalır.
    const std::string path = writeUsda("lights_cm", R"(
def SphereLight "Bulb"
{
    float inputs:radius = 10
    float inputs:intensity = 100
    double3 xformOp:translate = (0, 200, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
def RectLight "Panel"
{
    float inputs:width = 100
    float inputs:height = 50
    float inputs:intensity = 4
    double3 xformOp:translate = (0, 300, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)", "    metersPerUnit = 0.01\n    upAxis = \"Y\"\n");
    ImportExtras ex;
    std::string err;
    auto g = importModelFile(path, &err, {}, nullptr, &ex);
    ASSERT_NE(g, nullptr) << err;
    transformImportExtras(ex, g->localTransform);
    ASSERT_EQ(ex.lights.size(), 2u);
    for (const LightDesc& l : ex.lights) {
        if (l.name == "Bulb") {
            EXPECT_NEAR(l.intensity, 100.0f * PI * 0.1f * 0.1f, 1e-3f); // L · π r², r = 0.1 m
            EXPECT_NEAR(l.position.y, 2.0f, 1e-5f);
        } else {
            EXPECT_NEAR(l.intensity, 4.0f, 1e-5f); // radyans birimden bağımsız
            EXPECT_NEAR(l.width * l.height, 0.5f, 1e-5f);
            EXPECT_NEAR(l.position.y, 3.0f, 1e-5f);
        }
    }
}

TEST(UsdImport, CameraFieldOfViewAndPose) {
    const std::string path = writeUsda("camera", R"(
def Camera "Shot"
{
    float focalLength = 50
    float verticalAperture = 24
    float horizontalAperture = 36
    float fStop = 2.8
    float focusDistance = 400
    double3 xformOp:translate = (0, 150, 500)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
def Cube "Target"
{
    double size = 100
    double3 xformOp:translate = (0, 150, 100)
    uniform token[] xformOpOrder = ["xformOp:translate"]
}
)", "    metersPerUnit = 0.01\n    upAxis = \"Y\"\n");
    ImportExtras ex;
    std::string err;
    auto g = importModelFile(path, &err, {}, nullptr, &ex);
    ASSERT_NE(g, nullptr) << err;
    transformImportExtras(ex, g->localTransform);
    ASSERT_EQ(ex.cameras.size(), 1u);
    ASSERT_EQ(ex.cameras[0].keys.size(), 1u);
    const CameraKey& k = ex.cameras[0].keys[0];
    EXPECT_NEAR(k.fovDeg, 2.0f * std::atan(12.0f / 50.0f) * RAD_TO_DEG, 1e-3f);
    EXPECT_LT((k.eye - Vec3f(0, 1.5f, 5.0f)).length(), 1e-4f);
    EXPECT_LT((k.target - k.eye).normalized().z, -0.9999f);
    EXPECT_NEAR(k.focusDistance, 4.0f, 1e-4f); // santimetre → metre
    EXPECT_NEAR(k.fStop, 2.8f, 1e-6f);
}

// ─────────────────────────────── kubbe ışığı ───────────────────────────────

namespace {

/// USD/OpenEXR eşdikdörtgen kuralında (u, v) texel merkezinin kubbe-yerel yönü:
/// u = (π − atan2(x, z)) / 2π, v = θ / π (θ +Y'den).
Vec3f usdLatlongDirection(float u, float v) {
    const float phi = PI - u * TWO_PI; // atan2(x, z)
    const float th = v * PI;
    return Vec3f(std::sin(th) * std::sin(phi), std::cos(th), std::sin(th) * std::cos(phi));
}

struct DomeProbe {
    std::shared_ptr<EnvironmentLight> light;
    std::string hdrPath;
};

DomeProbe importDome(const std::string& name, const std::string& xform) {
    // 16×8 siyah görüntü, tek parlak texel (sütun 3, satır 2).
    const fs::path exr = testDir() / "dome_marker.exr";
    if (!fs::exists(exr)) {
        Image img(16, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x) img.setPixel(x, y, Color3f(0.0f));
        img.setPixel(3, 2, Color3f(100.0f, 50.0f, 25.0f));
        EXPECT_TRUE(saveImageEXR(img, exr.string()));
    }
    const std::string path = writeUsda(name, "def DomeLight \"Env\"\n{\n    asset inputs:texture:file = @dome_marker.exr@\n"
                                             "    float inputs:intensity = 2\n" + xform + "}\n");
    ImportExtras ex;
    std::string err;
    auto g = importModelFile(path, &err, {}, nullptr, &ex);
    EXPECT_NE(g, nullptr) << err;
    DomeProbe out;
    if (!g || !ex.environment) {
        ADD_FAILURE() << "kubbe okunmadı";
        return out;
    }
    transformImportExtras(ex, g->localTransform);
    EXPECT_NEAR(ex.environment->intensity, 2.0f, 1e-6f);
    out.hdrPath = ex.environment->hdrPath;
    auto img = loadImageEXR(out.hdrPath);
    if (!img) {
        ADD_FAILURE() << "ortam görüntüsü okunamadı: " << out.hdrPath;
        return out;
    }
    out.light = std::make_shared<EnvironmentLight>(std::make_shared<const Image>(std::move(*img)),
                                                   ex.environment->rotationDeg * DEG_TO_RAD, 1.0f);
    return out;
}

/// İşaretli texel'in yönü (dünyada) parlak, aynası ve komşu yönler karanlık mı?
void expectMarkerAt(const DomeProbe& d, const std::function<Vec3f(const Vec3f&)>& localToWorld) {
    ASSERT_TRUE(d.light);
    const Vec3f local = usdLatlongDirection((3 + 0.5f) / 16.0f, (2 + 0.5f) / 8.0f);
    const Vec3f w = localToWorld(local).normalized();
    const Color3f c = d.light->eval(w);
    EXPECT_NEAR(c.r, 100.0f, 1.0f);
    EXPECT_NEAR(c.g, 50.0f, 0.5f);
    // Ayna yönü (x → −x yerelde) karanlık olmalı: yanlış kural görüntüyü aynalardı.
    const Vec3f mirror = localToWorld(Vec3f(-local.x, local.y, local.z)).normalized();
    EXPECT_LT(d.light->eval(mirror).r, 1.0f);
}

} // namespace

TEST(UsdImport, DomeLightFollowsUsdLatlongConvention) {
    const DomeProbe d = importDome("dome_plain", "");
    expectMarkerAt(d, [](const Vec3f& v) { return v; });
}

TEST(UsdImport, DomeLightRotationIsApplied) {
    const DomeProbe d = importDome("dome_rot", "    float xformOp:rotateY = 60\n    uniform token[] xformOpOrder = [\"xformOp:rotateY\"]\n");
    const Transform r = Transform::rotateY(60.0f * DEG_TO_RAD);
    expectMarkerAt(d, [&](const Vec3f& v) { return r.transformVector(v); });
}

TEST(UsdImport, MirroredDomeUsesSourceImageDirectly) {
    // Aynalı dönüşüm (scale −1): çevirme gerekmez, kaynak dosya önbelleğe kopyalanmadan kullanılır.
    const DomeProbe d = importDome("dome_mirror", "    float xformOp:rotateY = 90\n    float3 xformOp:scale = (-1, 1, 1)\n"
                                                  "    uniform token[] xformOpOrder = [\"xformOp:rotateY\", \"xformOp:scale\"]\n");
    EXPECT_EQ(fs::path(d.hdrPath).filename().string(), "dome_marker.exr");
    const Transform r = Transform::rotateY(90.0f * DEG_TO_RAD);
    expectMarkerAt(d, [&](const Vec3f& v) { return r.transformVector(Vec3f(-v.x, v.y, v.z)); });
}

TEST(UsdImport, MissingFileAndBadStageReportErrors) {
    std::string err;
    EXPECT_EQ(importModelFile((testDir() / "yok.usda").string(), &err), nullptr);
    EXPECT_FALSE(err.empty());
    const fs::path bad = testDir() / "bozuk.usda";
    std::ofstream(bad) << "#usda 1.0\ndef Mesh \"X\" { int[] faceVertexCounts = [3 }\n";
    err.clear();
    EXPECT_EQ(importModelFile(bad.string(), &err), nullptr);
    EXPECT_FALSE(err.empty());
}

#endif // PHOTON_HAS_USD
