// alembic_loader.cpp — Alembic arşivi → ImportedModel. Yalnız PHOTON_HAS_ALEMBIC ile derlenir;
// yoksa model_import.cpp "desteklenmiyor" der.
#include "io/alembic_loader.h"
#include "core/platform/path.h"

#include <Alembic/AbcCoreFactory/All.h>
#include <Alembic/AbcGeom/All.h>

#include <algorithm>
#include <cfloat>
#include <fstream>
#include <optional>

namespace photon {

namespace {

namespace AG = Alembic::AbcGeom;

/// Imath satır-vektör kuralı (p' = p·M, öteleme son satırda) → motorun sütun-vektör matrisi.
Transform toTransform(const Imath::M44d& m) {
    return Transform(Mat4f(static_cast<float>(m[0][0]), static_cast<float>(m[1][0]), static_cast<float>(m[2][0]),
                           static_cast<float>(m[3][0]), static_cast<float>(m[0][1]), static_cast<float>(m[1][1]),
                           static_cast<float>(m[2][1]), static_cast<float>(m[3][1]), static_cast<float>(m[0][2]),
                           static_cast<float>(m[1][2]), static_cast<float>(m[2][2]), static_cast<float>(m[3][2]), 0.0f,
                           0.0f, 0.0f, 1.0f));
}

/// Genişletilmiş (indekssiz) bir geometri parametresinin değerine erişim: kapsamına göre
/// köşe (point), yüz-köşe (corner), yüz ya da sabit.
template <typename SampleT>
struct Attr {
    SampleT sample;
    bool valid = false;

    template <typename V>
    std::optional<V> at(size_t point, size_t corner, size_t face) const {
        if (!valid || !sample.getVals()) return std::nullopt;
        const auto& vals = *sample.getVals();
        size_t i;
        switch (sample.getScope()) {
            case AG::kFacevaryingScope: i = corner; break;
            case AG::kVertexScope:
            case AG::kVaryingScope: i = point; break;
            case AG::kUniformScope: i = face; break;
            case AG::kConstantScope: i = 0; break;
            default: return std::nullopt;
        }
        if (i >= vals.size()) return std::nullopt;
        return V(vals[i]);
    }
};

struct Context {
    AG::ISampleSelector sel;
    ImportedModel model;
    int skippedTopologyChanges = 0;
};

/// PolyMesh ya da SubD şemasının ortak çevirisi: yüz kümelerine göre parçalar.
template <typename Schema>
void convertMesh(const std::string& name, Schema& schema, ImportedNode& owner, Context& ctx) {
    typename Schema::Sample samp;
    schema.get(samp, ctx.sel);
    const auto P = samp.getPositions();
    const auto indices = samp.getFaceIndices();
    const auto counts = samp.getFaceCounts();
    if (!P || !indices || !counts || P->size() == 0) return;

    Attr<AG::IN3fGeomParam::Sample> normals;
    if constexpr (std::is_same_v<Schema, AG::IPolyMeshSchema>) {
        AG::IN3fGeomParam np = schema.getNormalsParam();
        if (np.valid()) {
            normals.sample = np.getExpandedValue(ctx.sel);
            normals.valid = true;
        }
    }
    Attr<AG::IV2fGeomParam::Sample> uvs;
    if (AG::IV2fGeomParam up = schema.getUVsParam(); up.valid()) {
        uvs.sample = up.getExpandedValue(ctx.sel);
        uvs.valid = true;
    }

    // Yüz → küme (yüz kümesi yoksa hepsi 0). Birden çok kümedeki yüz ilk kümede kalır.
    const size_t nFaces = counts->size();
    std::vector<int> faceSet(nFaces, -1);
    std::vector<std::string> setNames;
    schema.getFaceSetNames(setNames);
    for (size_t s = 0; s < setNames.size(); ++s) {
        AG::IFaceSet fs = schema.getFaceSet(setNames[s]);
        AG::IFaceSetSchema::Sample fss;
        fs.getSchema().get(fss, ctx.sel);
        if (!fss.getFaces()) continue;
        const auto& faces = *fss.getFaces();
        for (size_t i = 0; i < faces.size(); ++i) {
            const int32_t f = faces[i];
            if (f >= 0 && static_cast<size_t>(f) < nFaces && faceSet[static_cast<size_t>(f)] < 0)
                faceSet[static_cast<size_t>(f)] = static_cast<int>(s);
        }
    }
    const int nParts = static_cast<int>(setNames.size()) + 1; // son = kümesiz yüzler

    std::vector<MeshBuilder> builders(static_cast<size_t>(nParts));
    size_t corner = 0;
    std::vector<size_t> polyCorners;
    for (size_t f = 0; f < nFaces; ++f) {
        const int32_t cnt = (*counts)[f];
        if (cnt < 0 || corner + static_cast<size_t>(cnt) > indices->size()) break;
        const int part = faceSet[f] >= 0 ? faceSet[f] : nParts - 1;
        // Alembic çokgenleri saat yönündedir: köşeler ters sırayla alınır (Blender'ın içe
        // aktarıcısı da böyle yapar); yanlışsa orientWindingToNormals normallere göre düzeltir.
        polyCorners.clear();
        for (int32_t k = cnt - 1; k >= 0; --k) polyCorners.push_back(corner + static_cast<size_t>(k));
        corner += static_cast<size_t>(cnt);
        if (cnt < 3) continue;
        auto emit = [&](size_t c) {
            const int32_t pi = (*indices)[c];
            if (pi < 0 || static_cast<size_t>(pi) >= P->size()) return;
            const Imath::V3f p = (*P)[static_cast<size_t>(pi)];
            Vec3f n(0.0f);
            if (auto v = normals.template at<Imath::V3f>(static_cast<size_t>(pi), c, f)) {
                n = Vec3f(v->x, v->y, v->z);
                if (n.lengthSquared() > 0.0f) n = n.normalized();
            }
            Vec2f uv(0.0f);
            if (auto t = uvs.template at<Imath::V2f>(static_cast<size_t>(pi), c, f)) uv = Vec2f(t->x, 1.0f - t->y);
            builders[static_cast<size_t>(part)].addCorner(Vec3f(p.x, p.y, p.z), n, uv);
        };
        for (size_t k = 1; k + 1 < polyCorners.size(); ++k) {
            emit(polyCorners[0]);
            emit(polyCorners[k]);
            emit(polyCorners[k + 1]);
        }
    }
    for (int s = 0; s < nParts; ++s) {
        auto mesh = builders[static_cast<size_t>(s)].build(nullptr);
        if (!mesh) continue;
        ImportedMesh im;
        im.mesh = mesh;
        im.name = s < static_cast<int>(setNames.size()) ? name + " · " + setNames[static_cast<size_t>(s)] : name;
        owner.meshes.push_back(static_cast<int>(ctx.model.meshes.size()));
        ctx.model.meshes.push_back(std::move(im));
    }
}

/// Çocukları gezer. Şekiller (mesh) ebeveyn Xform'un düğümüne eklenir (Alembic'te şekil, kendi
/// dönüşümü olmayan bir yapraktır). Mesh içermeyen dallar atılır.
bool visit(const Alembic::Abc::IObject& obj, ImportedNode& node, Context& ctx) {
    bool content = false;
    for (size_t i = 0; i < obj.getNumChildren(); ++i) {
        Alembic::Abc::IObject child = obj.getChild(i);
        const Alembic::Abc::ObjectHeader& h = child.getHeader();
        if (AG::IXform::matches(h)) {
            AG::IXform xf(child, Alembic::Abc::kWrapExisting);
            AG::XformSample xs;
            xf.getSchema().get(xs, ctx.sel);
            ImportedNode n;
            n.name = child.getName();
            n.local = toTransform(xs.getMatrix());
            if (visit(child, n, ctx)) {
                node.children.push_back(std::move(n));
                content = true;
            }
        } else if (AG::IPolyMesh::matches(h)) {
            AG::IPolyMesh mesh(child, Alembic::Abc::kWrapExisting);
            const size_t before = ctx.model.meshes.size();
            convertMesh(child.getName(), mesh.getSchema(), node, ctx);
            content = content || ctx.model.meshes.size() > before;
            content = visit(child, node, ctx) || content;
        } else if (AG::ISubD::matches(h)) {
            AG::ISubD subd(child, Alembic::Abc::kWrapExisting);
            const size_t before = ctx.model.meshes.size();
            convertMesh(child.getName(), subd.getSchema(), node, ctx);
            content = content || ctx.model.meshes.size() > before;
            content = visit(child, node, ctx) || content;
        } else {
            content = visit(child, node, ctx) || content; // kamera, nokta bulutu, eğri: içinden geç
        }
    }
    return content;
}

} // namespace

bool AlembicLoader::available() { return true; }

ImportedModel AlembicLoader::load(const std::string& path, std::string* error, double timeSeconds) {
    // Akışla açılır: Windows'ta Türkçe karakterli yollar dar (char) dosya API'sinde bozulur.
    std::ifstream stream(pathFromUtf8(path), std::ios::binary);
    if (!stream) {
        if (error) *error = "Alembic dosyası açılamadı: " + path;
        return {};
    }
    Context ctx;
    try {
        Alembic::AbcCoreFactory::IFactory factory;
        Alembic::AbcCoreFactory::IFactory::CoreType type;
        std::vector<std::istream*> streams{&stream};
        Alembic::Abc::IArchive archive = factory.getArchive(streams, type);
        if (!archive.valid()) {
            if (error) *error = "Geçerli bir Alembic (Ogawa) arşivi değil (eski HDF5 arşivleri desteklenmez): " + path;
            return {};
        }
        double start = 0.0, end = 0.0;
        Alembic::Abc::GetArchiveStartAndEndTime(archive, start, end);
        if (start == DBL_MAX) start = 0.0;
        ctx.sel = AG::ISampleSelector(timeSeconds >= 0.0 ? timeSeconds : start);

        visit(archive.getTop(), ctx.model.root, ctx);
        ctx.model.root.name = pathToUtf8(pathFromUtf8(path).stem());

        // Birim: Alembic birimsizdir; yazan uygulamanın sahne birimi varsayılır.
        std::string app, abcVersion, date, description;
        Alembic::Util::uint32_t apiVersion = 0;
        Alembic::Abc::GetArchiveInfo(archive, app, abcVersion, apiVersion, date, description);
        std::string lower = app;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool centimeters = lower.find("maya") != std::string::npos || lower.find("cinema") != std::string::npos ||
                                 lower.find("3ds max") != std::string::npos || lower.find("3dsmax") != std::string::npos;
        if (centimeters) {
            ctx.model.root.local = Transform::scale(Vec3f(0.01f));
            ctx.model.warnings.push_back("Birim santimetre varsayıldı (" + app + ")");
        }
    } catch (const std::exception& e) {
        if (error) *error = std::string("Alembic okunamadı: ") + e.what();
        return {};
    }
    if (ctx.model.meshes.empty() && error) *error = "Alembic arşivinde mesh yok: " + path;
    return std::move(ctx.model);
}

} // namespace photon
