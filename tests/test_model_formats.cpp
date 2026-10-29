// Model biçimi testleri: FBX (gerçek Blender ve 3ds Max dışa aktarımları, ufbx test verisi) ve
// Alembic (arşiv testte yazılıp geri okunur). Hiyerarşi ve adların korunması, örneklemede
// geometrinin paylaşılması, çok malzemeli mesh'in parçalara bölünmesi, birim dönüşümü, sarma
// yönünün normallerle tutarlılığı ve proje dosyasında iç içe grupların kaybolmaması.

#include <gtest/gtest.h>
#include "io/imported_model.h"
#include "materials/disney.h"
#include "scene/model_import.h"
#include "scene/project_io.h"
#include "scene/scene_graph.h"
#include "core/platform/path.h"

#ifdef PHOTON_HAS_ALEMBIC
#include <Alembic/AbcCoreOgawa/All.h>
#include <Alembic/AbcGeom/All.h>
#endif

#include <cmath>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace photon;

namespace {

std::string fbx(const char* name) { return std::string(PHOTON_FBX_TESTDATA) + "/" + name; }

std::vector<const SceneNode*> meshNodes(const SceneNode& root) {
    std::vector<const SceneNode*> out;
    std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
        if (n.type == SceneNodeType::Mesh) out.push_back(&n);
        for (const auto& c : n.children) walk(*c);
    };
    walk(root);
    return out;
}

/// Üçgenlerin sarma yönü (geometrik normal) köşe normalleriyle aynı tarafta mı?
float windingAgreement(const TriangleMesh& m) {
    int agree = 0, total = 0;
    const auto& p = m.positions();
    const auto& n = m.normals();
    const auto& idx = m.indices();
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vec3f face = (p[idx[i + 1]] - p[idx[i]]).cross(p[idx[i + 2]] - p[idx[i]]);
        if (face.lengthSquared() == 0.0f) continue;
        ++total;
        if (face.dot(n[idx[i]] + n[idx[i + 1]] + n[idx[i + 2]]) > 0.0f) ++agree;
    }
    return total > 0 ? static_cast<float>(agree) / total : 0.0f;
}

std::string tempDir() {
    const auto d = std::filesystem::temp_directory_path() / "photon_model_formats";
    std::filesystem::create_directories(d);
    return pathToUtf8(d);
}

} // namespace

TEST(Fbx, BlenderDefaultCubeInMeters) {
    std::string err;
    auto g = importModelFile(fbx("blender_279_default_7400_binary.fbx"), &err);
    ASSERT_TRUE(g) << err;
    const auto meshes = meshNodes(*g);
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0]->name, "Cube");
    EXPECT_EQ(meshes[0]->mesh->numTriangles(), 12u);
    EXPECT_EQ(meshes[0]->mesh->positions().size(), 24u); // köşeler birleşti (36 değil)
    EXPECT_NE(dynamic_cast<const DisneyMaterial*>(meshes[0]->material.get()), nullptr);
    // Blender'ın varsayılan küpü 2 m: santimetre/metre karışıklığı olmadan.
    const AABB b = SceneGraph::nodeWorldBounds(*g);
    EXPECT_NEAR(b.pMax.x - b.pMin.x, 2.0f, 1e-3f);
    EXPECT_NEAR(b.pMax.y - b.pMin.y, 2.0f, 1e-3f);
    EXPECT_GT(windingAgreement(*meshes[0]->mesh), 0.99f);
}

TEST(Fbx, InstancesShareGeometry) {
    std::string err;
    auto g = importModelFile(fbx("blender_293_instancing_7400_binary.fbx"), &err);
    ASSERT_TRUE(g) << err;
    const auto meshes = meshNodes(*g);
    ASSERT_EQ(meshes.size(), 8u);
    std::set<const TriangleMesh*> geometry;
    std::set<int> sources;
    for (const SceneNode* m : meshes) {
        geometry.insert(m->mesh.get());
        sources.insert(m->sourceIndex);
    }
    EXPECT_EQ(geometry.size(), 1u) << "aynı mesh'i kullanan düğümler geometriyi paylaşmalı";
    EXPECT_EQ(sources.size(), 8u);
    const AABB b = SceneGraph::nodeWorldBounds(*g);
    EXPECT_NEAR(b.pMax.x, 3.95f, 0.02f);
    EXPECT_NEAR(b.pMin.z, -3.95f, 0.02f);
}

TEST(Fbx, MultiMaterialMeshSplitsIntoNamedParts) {
    std::string err;
    auto g = importModelFile(fbx("blender_suzanne_multimaterial_7400_binary.fbx"), &err);
    ASSERT_TRUE(g) << err;
    const auto meshes = meshNodes(*g);
    ASSERT_EQ(meshes.size(), 7u);
    std::set<std::string> names;
    std::set<const Material*> materials;
    for (const SceneNode* m : meshes) {
        names.insert(m->name);
        materials.insert(m->material.get());
    }
    EXPECT_TRUE(names.count("Suzanne · LeftEye"));
    EXPECT_TRUE(names.count("Suzanne · Monkey"));
    EXPECT_EQ(materials.size(), 7u);
    // Sol kulak malzemesi Blender'da (0.8, 0.01, 0.38) pembe.
    for (const SceneNode* m : meshes) {
        if (m->name != "Suzanne · LeftEar") continue;
        const auto* d = dynamic_cast<const DisneyMaterial*>(m->material.get());
        ASSERT_NE(d, nullptr);
        EXPECT_NEAR(d->baseColor().r, 0.8f, 0.02f);
        EXPECT_LT(d->baseColor().g, 0.05f);
    }
}

TEST(Fbx, AsciiFrom3dsMaxConvertsUnitsAndReportsMissingTexture) {
    std::string err;
    std::vector<std::string> warnings;
    auto g = importModelFile(fbx("max2009_cube_texture_6100_ascii.fbx"), &err, {}, &warnings);
    ASSERT_TRUE(g) << err;
    const AABB b = SceneGraph::nodeWorldBounds(*g);
    // 3ds Max kutusu 20 inç = 0.508 m; dosya Z-yukarı, motor Y-yukarı.
    EXPECT_NEAR(b.pMax.y - b.pMin.y, 0.508f, 2e-3f);
    EXPECT_NEAR(b.pMin.y, 0.0f, 2e-3f);
    // Doku dosyası test verisiyle gelmiyor: sessizce kaybolmaz, uyarı olarak bildirilir.
    bool reported = false;
    for (const auto& w : warnings) reported = reported || w.find("doku") != std::string::npos;
    EXPECT_TRUE(reported);
}

// Proje dosyası iç içe grupları korumalı: eskiden içe aktarılan modelin yalnız üst düzey
// çocukları sourceIndex ile eşleniyordu, alt gruplardaki parçalar yeniden açınca kayboluyordu.
TEST(Fbx, NestedHierarchySurvivesProjectRoundTrip) {
    std::string err;
    auto g = importModelFile(fbx("blender_279_nested_meshes_7400_binary.fbx"), &err);
    ASSERT_TRUE(g) << err;
    const size_t before = meshNodes(*g).size();
    ASSERT_EQ(before, 4u);
    const AABB b0 = SceneGraph::nodeWorldBounds(*g);

    SceneGraph graph;
    graph.root()->addChild(std::move(g));
    const std::string path = tempDir() + "/nested.photon";
    ASSERT_TRUE(saveProject(path, graph, ProjectData{}, &err)) << err;
    SceneGraph loaded;
    ProjectData data;
    ASSERT_TRUE(loadProject(path, loaded, data, &err)) << err;
    ASSERT_EQ(loaded.root()->children.size(), 1u);
    const SceneNode& model = *loaded.root()->children[0];
    EXPECT_EQ(meshNodes(model).size(), before);
    std::set<std::string> names;
    for (const SceneNode* m : meshNodes(model)) names.insert(m->name);
    EXPECT_TRUE(names.count("Deep Plane"));
    EXPECT_TRUE(names.count("Ico. .Sphere"));
    const AABB b1 = SceneGraph::nodeWorldBounds(model);
    EXPECT_NEAR(b1.pMin.x, b0.pMin.x, 1e-4f);
    EXPECT_NEAR(b1.pMax.y, b0.pMax.y, 1e-4f);
    EXPECT_NEAR(b1.pMax.z, b0.pMax.z, 1e-4f);
}

TEST(ImportedModel, WindingFollowsNormals) {
    // Saat yönünde (normallere ters) sarılmış bir üçgen: çevrilmeli.
    std::vector<Vec3f> p = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}};
    std::vector<Vec3f> n(3, Vec3f(0, 0, 1));
    std::vector<uint32_t> idx = {0, 1, 2};
    EXPECT_TRUE(orientWindingToNormals(p, n, idx));
    EXPECT_EQ(idx[1], 2u);
    EXPECT_FALSE(orientWindingToNormals(p, n, idx)); // artık tutarlı
}

TEST(ImportedModel, EmbeddedTextureIsCachedByContent) {
    setEmbeddedTextureCacheDir(tempDir() + "/texcache");
    const unsigned char png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    const std::string a = cacheEmbeddedTexture(png, sizeof(png));
    const std::string b = cacheEmbeddedTexture(png, sizeof(png));
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b);
    EXPECT_EQ(pathFromUtf8(a).extension(), ".png");
    const unsigned char junk[] = {1, 2, 3, 4};
    EXPECT_TRUE(cacheEmbeddedTexture(junk, sizeof(junk)).empty());
    setEmbeddedTextureCacheDir("");
}

TEST(ModelImport, OptionsScaleAndZUp) {
    std::string err;
    ModelImportOptions opt;
    opt.scale = 0.01f;
    opt.zUp = true;
    auto g = importModelFile(fbx("blender_279_default_7400_binary.fbx"), &err, opt);
    ASSERT_TRUE(g) << err;
    const AABB b = SceneGraph::nodeWorldBounds(*g);
    EXPECT_NEAR(b.pMax.x - b.pMin.x, 0.02f, 1e-4f);
}

#ifdef PHOTON_HAS_ALEMBIC

namespace {

namespace AG = Alembic::AbcGeom;

/// Birim küp, Alembic kuralında (yüzler dışarıdan bakınca SAAT YÖNÜNDE), yüz-köşe normalleri
/// ve UV'leriyle. 24 nokta (yüz başına 4).
void cubeSample(std::vector<Imath::V3f>& P, std::vector<int32_t>& idx, std::vector<int32_t>& counts,
                std::vector<Imath::V3f>& N, std::vector<Imath::V2f>& uv) {
    for (int axis = 0; axis < 3; ++axis) {
        for (float s : {1.0f, -1.0f}) {
            Imath::V3f n(0.0f), u(0.0f), v(0.0f);
            n[axis] = s;
            u[(axis + 1) % 3] = 1.0f;
            v[(axis + 2) % 3] = 1.0f;
            if (s < 0.0f) std::swap(u, v); // u × v = n
            const Imath::V3f c = n * 0.5f;
            const Imath::V3f ccw[4] = {c - u * 0.5f - v * 0.5f, c + u * 0.5f - v * 0.5f, c + u * 0.5f + v * 0.5f,
                                       c - u * 0.5f + v * 0.5f};
            counts.push_back(4);
            for (int k = 3; k >= 0; --k) { // saat yönü
                idx.push_back(static_cast<int32_t>(P.size()));
                P.push_back(ccw[k]);
                N.push_back(n);
                uv.emplace_back(k == 1 || k == 2 ? 1.0f : 0.0f, k >= 2 ? 1.0f : 0.0f);
            }
        }
    }
}

std::string writeArchive() {
    const std::string path = tempDir() + "/model.abc";
    AG::OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), path);
    const AG::TimeSampling ts(1.0 / 24.0, 0.0);
    const uint32_t tsIndex = archive.addTimeSampling(ts);

    AG::OXform body(AG::OObject(archive, AG::kTop), "Body", tsIndex);
    AG::XformSample xs;
    xs.setTranslation(Imath::V3d(1.0, 0.0, 0.0));
    body.getSchema().set(xs);
    xs.setTranslation(Imath::V3d(3.0, 0.0, 0.0)); // ikinci kare: x = 3
    body.getSchema().set(xs);

    std::vector<Imath::V3f> P, N;
    std::vector<int32_t> idx, counts;
    std::vector<Imath::V2f> uv;
    cubeSample(P, idx, counts, N, uv);
    AG::OPolyMesh mesh(body, "BodyShape");
    AG::OV2fGeomParam::Sample uvSample(AG::V2fArraySample(uv), AG::kFacevaryingScope);
    AG::ON3fGeomParam::Sample nSample(AG::N3fArraySample(N), AG::kFacevaryingScope);
    AG::OPolyMeshSchema::Sample sample(AG::V3fArraySample(P), AG::Int32ArraySample(idx),
                                       AG::Int32ArraySample(counts), uvSample, nSample);
    mesh.getSchema().set(sample);
    const std::vector<int32_t> red = {0, 1, 2}, blue = {3, 4, 5};
    AG::OFaceSet redSet = mesh.getSchema().createFaceSet("Red");
    redSet.getSchema().set(AG::OFaceSetSchema::Sample(AG::Int32ArraySample(red)));
    AG::OFaceSet blueSet = mesh.getSchema().createFaceSet("Blue");
    blueSet.getSchema().set(AG::OFaceSetSchema::Sample(AG::Int32ArraySample(blue)));

    AG::OXform lid(body, "Lid");
    AG::XformSample ls;
    ls.setTranslation(Imath::V3d(0.0, 1.0, 0.0));
    lid.getSchema().set(ls);
    AG::OPolyMesh lidMesh(lid, "LidShape");
    lidMesh.getSchema().set(AG::OPolyMeshSchema::Sample(AG::V3fArraySample(P), AG::Int32ArraySample(idx),
                                                        AG::Int32ArraySample(counts)));
    return path;
}

} // namespace

TEST(Alembic, HierarchyFaceSetsAndWinding) {
    const std::string path = writeArchive();
    std::string err;
    auto g = importModelFile(path, &err);
    ASSERT_TRUE(g) << err;
    const auto meshes = meshNodes(*g);
    ASSERT_EQ(meshes.size(), 3u); // Red, Blue (gövde) + kapak
    std::set<std::string> names;
    size_t tris = 0;
    for (const SceneNode* m : meshes) {
        names.insert(m->name);
        if (m->name.rfind("BodyShape", 0) == 0) tris += m->mesh->numTriangles();
    }
    EXPECT_TRUE(names.count("BodyShape · Red"));
    EXPECT_TRUE(names.count("BodyShape · Blue"));
    EXPECT_TRUE(names.count("LidShape"));
    EXPECT_EQ(tris, 12u);
    for (const SceneNode* m : meshes) {
        if (m->mesh->normals().empty()) continue;
        EXPECT_GT(windingAgreement(*m->mesh), 0.99f) << m->name;
    }
    // Kapak, gövdenin çocuğu: dünya konumu (1, 1, 0) + küp.
    const AABB b = SceneGraph::nodeWorldBounds(*g);
    EXPECT_NEAR(b.pMin.x, 0.5f, 1e-4f);
    EXPECT_NEAR(b.pMax.y, 1.5f, 1e-4f);
    // Normalsiz kapakta sarma yönü dışa (sağ el) dönmüş olmalı: üst yüzün geometrik normali +y.
    for (const SceneNode* m : meshes) {
        if (m->name != "LidShape") continue;
        const auto& p = m->mesh->positions();
        const auto& idx = m->mesh->indices();
        int outward = 0, total = 0;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vec3f c = (p[idx[i]] + p[idx[i + 1]] + p[idx[i + 2]]) / 3.0f;
            const Vec3f face = (p[idx[i + 1]] - p[idx[i]]).cross(p[idx[i + 2]] - p[idx[i]]);
            ++total;
            if (face.dot(c) > 0.0f) ++outward;
        }
        EXPECT_EQ(outward, total);
    }
}

TEST(Alembic, ReadsRequestedTime) {
    const std::string path = writeArchive();
    std::string err;
    ModelImportOptions opt;
    opt.alembicTime = 1.0 / 24.0;
    auto g = importModelFile(path, &err, opt);
    ASSERT_TRUE(g) << err;
    EXPECT_NEAR(SceneGraph::nodeWorldBounds(*g).pMin.x, 2.5f, 1e-4f);
}

#endif // PHOTON_HAS_ALEMBIC
