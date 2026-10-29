// .photonpack ve ZIP katmanı testleri:
//   - ZIP: saklama ve DEFLATE girdileri birebir geri okunur; sıkışan veri DEFLATE ile yazılır;
//   - güvenlik: "../" içeren, mutlak, ters bölülü adlar reddedilir (yazarken ve okurken);
//     elle bozulmuş arşivdeki "zip slip" adı açılmaz; bozulmuş veri CRC ile yakalanır;
//   - ikili mesh: kodla/çöz birebir; bozuk boyut ve sınır dışı indeks reddedilir;
//   - paket kendi kendine yeter: dokular, IES, HDRI, FBX'ten gelen geometri, kamera izi pakete
//     girer; özgün doku ve IES dosyaları silinse de paket aynı sahneyi açar; paylaşılan mesh bir kez.
#include <gtest/gtest.h>
#include "core/image/image.h"
#include "core/image/image_io.h"
#include "core/io/zip.h"
#include "materials/disney.h"
#include "scene/mesh_binary.h"
#include "scene/model_import.h"
#include "scene/package.h"
#include "scene/primitives.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

using namespace photon;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeAll(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

int countNodes(const SceneNode& n) {
    int c = 1;
    for (const auto& ch : n.children) c += countNodes(*ch);
    return c;
}

void collectMeshes(const SceneNode& n, std::vector<const SceneNode*>& out) {
    if (n.type == SceneNodeType::Mesh) out.push_back(&n);
    for (const auto& ch : n.children) collectMeshes(*ch, out);
}

bool inside(const std::string& path, const fs::path& dir) {
    const auto rel = fs::weakly_canonical(fs::path(path)).lexically_relative(fs::weakly_canonical(dir));
    return !rel.empty() && *rel.begin() != "..";
}

} // namespace

TEST(Zip, StoreAndDeflateRoundTrip) {
    const fs::path dir = freshDir("photon_zip_rt");
    std::string text;
    for (int i = 0; i < 2000; ++i) text += "PhotonEngine paket testi satırı " + std::to_string(i % 7) + "\n";
    std::vector<uint8_t> noise(100000);
    std::mt19937 rng(7);
    for (auto& b : noise) b = static_cast<uint8_t>(rng());
    ZipWriter w;
    ASSERT_TRUE(w.open(dir / "a.zip"));
    ASSERT_TRUE(w.add("metin/notlar.txt", text.data(), text.size(), true));
    ASSERT_TRUE(w.add("ikili/gurultu.bin", noise.data(), noise.size(), true)); // sıkışmaz → saklanır
    ASSERT_TRUE(w.add("bos.dat", nullptr, 0, true));
    ASSERT_TRUE(w.close());

    ZipReader r;
    std::string err;
    ASSERT_TRUE(r.open(dir / "a.zip", &err)) << err;
    ASSERT_EQ(r.entries().size(), 3u);
    const auto* t = r.find("metin/notlar.txt");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->method, 8);
    EXPECT_LT(t->compressed, t->uncompressed / 5);
    std::vector<uint8_t> out;
    ASSERT_TRUE(r.read(*t, out, &err)) << err;
    EXPECT_EQ(std::string(out.begin(), out.end()), text);
    ASSERT_TRUE(r.read(*r.find("ikili/gurultu.bin"), out, &err)) << err;
    EXPECT_EQ(out, noise);
    ASSERT_TRUE(r.read(*r.find("bos.dat"), out, &err)) << err;
    EXPECT_TRUE(out.empty());
}

TEST(Zip, RejectsUnsafeNamesAndCorruptData) {
    for (const char* bad : {"../x", "a/../../x", "/etc/passwd", "C:/x", "a\\b", "a//b", "./a", "a/", ""})
        EXPECT_FALSE(isSafeZipEntryName(bad)) << bad;
    for (const char* good : {"project.photon", "assets/3_ahşap doku.png", "geometry/12.mesh"})
        EXPECT_TRUE(isSafeZipEntryName(good)) << good;

    const fs::path dir = freshDir("photon_zip_bad");
    ZipWriter w;
    ASSERT_TRUE(w.open(dir / "w.zip"));
    std::string err;
    EXPECT_FALSE(w.add("../kacak.txt", "x", 1, false, &err));
    const std::string payload = "zararsiz gorunen icerik";
    ASSERT_TRUE(w.add("abcdefghij/x.txt", payload.data(), payload.size(), false));
    ASSERT_TRUE(w.close());

    // Elle "zip slip": başlıklardaki adı aynı uzunlukta bir kaçış yoluyla değiştir.
    std::vector<uint8_t> bytes = readAll(dir / "w.zip");
    const std::string from = "abcdefghij/x.txt", to = "../../../evil.tx";
    ASSERT_EQ(from.size(), to.size());
    int patched = 0;
    for (size_t i = 0; i + from.size() <= bytes.size(); ++i)
        if (std::equal(from.begin(), from.end(), bytes.begin() + static_cast<long>(i))) {
            std::copy(to.begin(), to.end(), bytes.begin() + static_cast<long>(i));
            ++patched;
        }
    ASSERT_EQ(patched, 2); // yerel + merkezi başlık
    writeAll(dir / "slip.zip", bytes);
    ZipReader r;
    EXPECT_FALSE(r.open(dir / "slip.zip", &err));
    EXPECT_NE(err.find("Güvensiz"), std::string::npos) << err;
    EXPECT_FALSE(fs::exists(dir.parent_path().parent_path() / "evil.tx"));

    // Veri bozulması: CRC yakalar.
    std::vector<uint8_t> good = readAll(dir / "w.zip");
    for (size_t i = 0; i + payload.size() <= good.size(); ++i)
        if (std::equal(payload.begin(), payload.end(), good.begin() + static_cast<long>(i))) {
            good[i] ^= 0x20;
            break;
        }
    writeAll(dir / "crc.zip", good);
    ZipReader r2;
    ASSERT_TRUE(r2.open(dir / "crc.zip", &err)) << err;
    std::vector<uint8_t> out;
    EXPECT_FALSE(r2.read(r2.entries().front(), out, &err));
    EXPECT_NE(err.find("CRC"), std::string::npos) << err;
}

TEST(MeshBinary, RoundTripAndValidation) {
    auto cube = makePrimitiveMesh(PrimitiveKind::Cube);
    const std::vector<uint8_t> bytes = encodeMeshBinary(*cube);
    std::string err;
    auto back = decodeMeshBinary(bytes.data(), bytes.size(), nullptr, &err);
    ASSERT_NE(back, nullptr) << err;
    EXPECT_EQ(back->positions().size(), cube->positions().size());
    EXPECT_EQ(back->indices(), cube->indices());
    EXPECT_EQ(back->uvs().size(), cube->uvs().size());
    for (size_t i = 0; i < cube->positions().size(); ++i)
        EXPECT_EQ((back->positions()[i] - cube->positions()[i]).length(), 0.0f);

    std::vector<uint8_t> shortened(bytes.begin(), bytes.end() - 4);
    EXPECT_EQ(decodeMeshBinary(shortened.data(), shortened.size(), nullptr, &err), nullptr);
    std::vector<uint8_t> badIndex = bytes;
    const uint32_t huge = 0x7FFFFFFFu;
    std::memcpy(badIndex.data() + badIndex.size() - 4, &huge, 4);
    EXPECT_EQ(decodeMeshBinary(badIndex.data(), badIndex.size(), nullptr, &err), nullptr);
    EXPECT_NE(err.find("indeks"), std::string::npos);
}

TEST(Package, IsSelfContained) {
    const fs::path src = freshDir("photon_pack_src");
    const fs::path out = freshDir("photon_pack_out");
    // Kaynak dosyalar: doku (PNG) ve IES; paketledikten sonra silinecekler.
    Image tex(8, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) tex.setPixel(x, y, ((x + y) & 1) ? Color3f(0.9f, 0.2f, 0.1f) : Color3f(0.1f));
    const fs::path texPath = src / "dama doku.png";
    ASSERT_TRUE(saveImagePNG(tex, texPath.string()));
    const fs::path iesPath = src / "spot.ies";
    fs::copy_file(fs::path(PHOTON_ASSETS_DIR) / "ies" / "dar_spot.ies", iesPath);

    SceneGraph graph;
    auto mat = std::make_shared<DisneyMaterial>(Color3f(1.0f), 0.0f, 0.5f, 0.5f);
    mat->setAlbedoMap(texPath.string());
    auto mesh = makePrimitiveMesh(PrimitiveKind::Cube);
    auto group = std::make_unique<SceneNode>("Vitrin", SceneNodeType::Group);
    for (int i = 0; i < 2; ++i) { // aynı geometri iki kez (örnekleme): pakette bir kez
        auto n = std::make_unique<SceneNode>("Kutu " + std::to_string(i), SceneNodeType::Mesh);
        n->mesh = mesh;
        n->material = mat;
        n->localTransform = Transform::translate(Vec3f(2.0f * static_cast<float>(i), 0, 0));
        group->addChild(std::move(n));
    }
    graph.root()->addChild(std::move(group));
    std::string err;
    auto fbx = importModelFile(std::string(PHOTON_TEST_DATA) + "/fbx/dcc_camera_lights_7400_ascii.fbx", &err);
    ASSERT_NE(fbx, nullptr) << err;
    ASSERT_FALSE(fbx->sourcePath.empty());
    graph.root()->addChild(std::move(fbx));

    ProjectData data;
    data.environment.hdrPath = (fs::path(PHOTON_ASSETS_DIR) / "environments" / "studio_small_09_1k.hdr").string();
    LightDesc spot;
    spot.type = LightDesc::Type::Spot;
    spot.iesPath = iesPath.string();
    data.lights.push_back(spot);
    CameraTrack track;
    track.setKey(CameraKey{});
    CameraKey k2;
    k2.time = 2.0f;
    k2.eye = Vec3f(3, 1, 3);
    track.setKey(k2);
    data.cameraTracks.push_back(track);

    const fs::path pack = out / "sahne.photonpack";
    std::vector<std::string> warnings;
    ASSERT_TRUE(savePackage(pack.string(), graph, data, &err, &warnings)) << err;
    EXPECT_TRUE(warnings.empty());
    ASSERT_TRUE(isPackageFile(pack.string()));

    // Paket içeriği: proje, 2 geometri (paylaşılan kutu bir kez + FBX küpü), 3 dosya.
    {
        ZipReader r;
        ASSERT_TRUE(r.open(pack, &err)) << err;
        int geometry = 0, assets = 0;
        for (const auto& e : r.entries()) {
            geometry += e.name.rfind("geometry/", 0) == 0;
            assets += e.name.rfind("assets/", 0) == 0;
        }
        EXPECT_EQ(geometry, 2);
        EXPECT_EQ(assets, 3);
        EXPECT_NE(r.find("project.photon"), nullptr);
    }

    // Özgün dosyalar silinir: paket yine de açılır.
    fs::remove(texPath);
    fs::remove(iesPath);
    SceneGraph g2;
    ProjectData d2;
    const fs::path extract = out / "acilan";
    ASSERT_TRUE(loadPackage(pack.string(), g2, d2, &err, extract.string())) << err;
    EXPECT_EQ(countNodes(*g2.root()), countNodes(*graph.root()));
    std::vector<const SceneNode*> before, after;
    collectMeshes(*graph.root(), before);
    collectMeshes(*g2.root(), after);
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < after.size(); ++i) {
        ASSERT_TRUE(after[i]->mesh);
        EXPECT_EQ(after[i]->mesh->positions().size(), before[i]->mesh->positions().size());
        EXPECT_TRUE(after[i]->sourcePath.empty()); // kaynak dosyaya bağlı değil
    }
    const auto* dm = dynamic_cast<const DisneyMaterial*>(after[0]->material.get());
    ASSERT_NE(dm, nullptr);
    EXPECT_TRUE(fs::is_regular_file(dm->albedoMap()));
    EXPECT_TRUE(inside(dm->albedoMap(), extract));
    ASSERT_EQ(d2.lights.size(), 1u);
    EXPECT_TRUE(fs::is_regular_file(d2.lights[0].iesPath));
    EXPECT_TRUE(inside(d2.lights[0].iesPath, extract));
    EXPECT_TRUE(fs::is_regular_file(d2.environment.hdrPath));
    ASSERT_EQ(d2.cameraTracks.size(), 1u);
    EXPECT_EQ(d2.cameraTracks[0].keys.size(), 2u);
}
