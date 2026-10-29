// fbx_loader.cpp — FBX → ImportedModel (ufbx). Düğüm gezinmesi, malzeme dilimli üçgenleme,
// örnek paylaşımı, PBR malzeme eşlemesi ve doku yolu çözümü.
#include "io/fbx_loader.h"
#include "core/platform/path.h"
#include "materials/dielectric.h"
#include "materials/disney.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <utility>

namespace photon {

namespace {

namespace fs = std::filesystem;

std::string str(const ufbx_string& s) { return std::string(s.data, s.length); }

Transform toTransform(const ufbx_matrix& m) {
    return Transform(Mat4f(static_cast<float>(m.m00), static_cast<float>(m.m01), static_cast<float>(m.m02),
                           static_cast<float>(m.m03), static_cast<float>(m.m10), static_cast<float>(m.m11),
                           static_cast<float>(m.m12), static_cast<float>(m.m13), static_cast<float>(m.m20),
                           static_cast<float>(m.m21), static_cast<float>(m.m22), static_cast<float>(m.m23), 0.0f,
                           0.0f, 0.0f, 1.0f));
}

Color3f col(const ufbx_vec3& v) {
    return Color3f(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

float real(const ufbx_material_map& m, float def) { return m.has_value ? static_cast<float>(m.value_real) : def; }

struct Context {
    fs::path baseDir;
    double originalUnitMeters = 1.0;
    std::map<const ufbx_material*, std::shared_ptr<Material>> materials;
    std::map<std::pair<const ufbx_mesh*, uint32_t>, std::shared_ptr<TriangleMesh>> geometry;
    ImportedModel model;
    int unsupportedOpacity = 0;
    int missingTextures = 0;
};

/// Doku dosyasının yolu: dosyada yazan mutlak yol, FBX'e göreli yol, FBX'in yanındaki aynı adlı
/// dosya (dokular başka makinede kaydedilip birlikte taşındığında en sık işe yarayan) sırasıyla.
/// Hiçbiri yoksa ve FBX dokuyu gömmüşse önbelleğe çıkarılır.
std::string resolveTexture(const ufbx_texture* tex, Context& ctx) {
    if (!tex) return {};
    if (tex->type != UFBX_TEXTURE_FILE && tex->file_textures.count > 0) tex = tex->file_textures.data[0];
    std::error_code ec;
    std::vector<fs::path> candidates;
    for (const ufbx_string* s : {&tex->filename, &tex->absolute_filename, &tex->relative_filename}) {
        if (s->length == 0) continue;
        const fs::path p = pathFromUtf8(str(*s));
        candidates.push_back(p.is_absolute() ? p : ctx.baseDir / p);
        candidates.push_back(ctx.baseDir / p.filename());
        candidates.push_back(ctx.baseDir / "textures" / p.filename());
    }
    for (const fs::path& c : candidates)
        if (fs::is_regular_file(c, ec)) return pathToUtf8(c);
    if (tex->content.size > 0) {
        const std::string cached = cacheEmbeddedTexture(tex->content.data, tex->content.size);
        if (!cached.empty()) return cached;
    }
    if (!candidates.empty()) ++ctx.missingTextures;
    return {};
}

const ufbx_texture* mapTexture(const ufbx_material_map& m) {
    return (m.texture && m.texture_enabled) ? m.texture : nullptr;
}

/// ufbx'in ortak PBR görünümü → Principled ya da cam.
std::shared_ptr<Material> convertMaterial(const ufbx_material* fm, Context& ctx) {
    if (!fm) return nullptr;
    if (auto it = ctx.materials.find(fm); it != ctx.materials.end()) return it->second;
    const ufbx_material_pbr_maps& pbr = fm->pbr;
    const ufbx_material_features& feat = fm->features;

    Color3f base = pbr.base_color.has_value ? col(pbr.base_color.value_vec3) : Color3f(0.8f);
    base = (base * real(pbr.base_factor, 1.0f)).clamp(0.0f, 1.0f);
    float rough = std::clamp(real(pbr.roughness, 0.5f), 0.0f, 1.0f);
    if (feat.roughness_as_glossiness.enabled) rough = 1.0f - rough;
    const float metal = std::clamp(real(pbr.metalness, 0.0f), 0.0f, 1.0f);
    const float ior = std::clamp(real(pbr.specular_ior, 1.5f), 1.0f, 3.0f);

    std::shared_ptr<Material> result;
    const float transmission = feat.transmission.enabled ? real(pbr.transmission_factor, 0.0f) : 0.0f;
    if (transmission >= 0.5f && metal < 0.5f) {
        // Saydam malzeme (cam, sıvı, akrilik): kırıcı dielektrik.
        Color3f tint = pbr.transmission_color.has_value ? col(pbr.transmission_color.value_vec3) : Color3f(1.0f);
        float tr = pbr.transmission_roughness.has_value ? real(pbr.transmission_roughness, rough) : rough;
        if (feat.transmission_roughness_as_glossiness.enabled) tr = 1.0f - tr;
        auto glass = std::make_shared<Dielectric>(ior, tint.clamp(0.0f, 1.0f), tr <= 0.02f ? 0.0f : tr);
        if (feat.thin_walled.enabled) glass->setThinWalled(true);
        result = glass;
    } else {
        // Yalıtkan F0 = ((n−1)/(n+1))², specular_factor ile ölçekli; Disney specular = F0 / 0.08.
        const float f0 = std::pow((ior - 1.0f) / (ior + 1.0f), 2.0f) * real(pbr.specular_factor, 1.0f);
        auto d = std::make_shared<DisneyMaterial>(base, metal, std::max(rough, 0.001f), std::clamp(f0 / 0.08f, 0.0f, 2.0f));
        if (feat.diffuse_roughness.enabled) d->setDiffuseRoughness(real(pbr.diffuse_roughness, 0.0f));
        if (feat.coat.enabled && real(pbr.coat_factor, 0.0f) > 0.0f) {
            d->setClearCoat(real(pbr.coat_factor, 0.0f));
            float cr = real(pbr.coat_roughness, 0.03f);
            if (feat.coat_roughness_as_glossiness.enabled) cr = 1.0f - cr;
            d->setClearCoatRoughness(cr);
        }
        if (feat.sheen.enabled && real(pbr.sheen_factor, 0.0f) > 0.0f) {
            d->setSheen(real(pbr.sheen_factor, 0.0f));
            d->setSheenRoughness(real(pbr.sheen_roughness, 0.3f));
        }
        if (pbr.specular_anisotropy.has_value) d->setAnisotropy(std::abs(real(pbr.specular_anisotropy, 0.0f)));
        if (real(pbr.subsurface_factor, 0.0f) > 0.0f) {
            d->setSubsurface(real(pbr.subsurface_factor, 0.0f));
            if (pbr.subsurface_radius.has_value) d->setSubsurfaceRadius(col(pbr.subsurface_radius.value_vec3));
            // Ölçek dosyanın kendi biriminde: metreye.
            d->setSubsurfaceScale(real(pbr.subsurface_scale, 1.0f) * static_cast<float>(ctx.originalUnitMeters));
        }
        if (real(pbr.thin_film_factor, 0.0f) > 0.0f && pbr.thin_film_thickness.has_value)
            d->setThinFilm(real(pbr.thin_film_thickness, 0.0f), real(pbr.thin_film_ior, 1.4f));
        if (feat.emission.enabled) {
            const Color3f e = pbr.emission_color.has_value ? col(pbr.emission_color.value_vec3) : Color3f(0.0f);
            const float f = real(pbr.emission_factor, 1.0f);
            if (!(e * f).isBlack()) d->setEmission(e * f);
        }
        if (const std::string p = resolveTexture(mapTexture(pbr.base_color), ctx); !p.empty()) d->setAlbedoMap(p);
        if (const std::string p = resolveTexture(mapTexture(pbr.normal_map), ctx); !p.empty()) d->setNormalMap(p);
        if (!feat.roughness_as_glossiness.enabled)
            if (const std::string p = resolveTexture(mapTexture(pbr.roughness), ctx); !p.empty()) d->setRoughnessMap(p);
        if (const std::string p = resolveTexture(mapTexture(pbr.metalness), ctx); !p.empty()) d->setMetalnessMap(p);
        result = d;
    }
    if (feat.opacity.enabled && real(pbr.opacity, 1.0f) < 0.99f) ++ctx.unsupportedOpacity;
    ctx.materials[fm] = result;
    return result;
}

/// Bir mesh'in bir malzeme dilimini üçgenler; aynı (mesh, dilim) bir kez çevrilir.
std::shared_ptr<TriangleMesh> convertPart(const ufbx_mesh* mesh, uint32_t partIndex, Context& ctx) {
    const auto key = std::make_pair(mesh, partIndex);
    if (auto it = ctx.geometry.find(key); it != ctx.geometry.end()) return it->second;
    const ufbx_mesh_part& part = mesh->material_parts.data[partIndex];
    std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
    MeshBuilder builder;
    for (size_t k = 0; k < part.face_indices.count; ++k) {
        const uint32_t fi = part.face_indices.data[k];
        if (mesh->face_hole.count > fi && mesh->face_hole.data[fi]) continue;
        const ufbx_face face = mesh->faces.data[fi];
        if (face.num_indices < 3) continue;
        const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
        for (uint32_t c = 0; c < n * 3; ++c) {
            const uint32_t ix = tri[c];
            const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
            Vec3f nn(0.0f);
            if (mesh->vertex_normal.exists) {
                const ufbx_vec3 v = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                nn = Vec3f(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
                if (nn.lengthSquared() > 0.0f) nn = nn.normalized();
            }
            Vec2f uv(0.0f);
            if (mesh->vertex_uv.exists) {
                const ufbx_vec2 t = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                // FBX UV'si OpenGL kuralında (v = 0 altta); motorda dokunun (0, 0)'ı üst satır.
                uv = Vec2f(static_cast<float>(t.x), 1.0f - static_cast<float>(t.y));
            }
            builder.addCorner(Vec3f(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)), nn, uv);
        }
    }
    auto result = builder.build(nullptr);
    ctx.geometry[key] = result;
    return result;
}

/// Düğümü ve altını çevirir. Mesh'i olmayan alt ağaçlar (kemik, kamera, boş yardımcılar) atılır.
bool convertNode(const ufbx_node* node, ImportedNode& out, Context& ctx) {
    out.name = node->name.length > 0 ? str(node->name) : std::string(node->is_root ? "Kök" : "Düğüm");
    out.local = node->is_root ? Transform() : toTransform(node->node_to_parent);
    bool hasContent = false;
    if (const ufbx_mesh* mesh = node->mesh) {
        const ufbx_material_list& mats = node->materials.count > 0 ? node->materials : mesh->materials;
        for (uint32_t pi = 0; pi < mesh->material_parts.count; ++pi) {
            if (mesh->material_parts.data[pi].num_triangles == 0) continue;
            auto geom = convertPart(mesh, pi, ctx);
            if (!geom) continue;
            const ufbx_material* fm = pi < mats.count ? mats.data[pi] : nullptr;
            ImportedMesh im;
            im.mesh = geom;
            im.material = convertMaterial(fm, ctx);
            im.name = out.name;
            if (mesh->material_parts.count > 1 && fm && fm->name.length > 0) im.name += " · " + str(fm->name);
            out.meshes.push_back(static_cast<int>(ctx.model.meshes.size()));
            ctx.model.meshes.push_back(std::move(im));
            hasContent = true;
        }
    }
    for (size_t i = 0; i < node->children.count; ++i) {
        ImportedNode child;
        if (convertNode(node->children.data[i], child, ctx)) {
            out.children.push_back(std::move(child));
            hasContent = true;
        }
    }
    return hasContent;
}


/// Modelin dünya uzayı sınırları (kameraların hedef uzaklığı ve ışık şiddeti ölçeği için).
void accumulateBounds(const ImportedNode& n, const Transform& parent, const ImportedModel& model, AABB& box) {
    const Transform world = parent * n.local;
    for (int k : n.meshes) {
        const AABB b = model.meshes[static_cast<size_t>(k)].mesh->bounds();
        for (int c = 0; c < 8; ++c) {
            const Vec3f p((c & 1) ? b.pMax.x : b.pMin.x, (c & 2) ? b.pMax.y : b.pMin.y, (c & 4) ? b.pMax.z : b.pMin.z);
            box.merge(world.transformPoint(p));
        }
    }
    for (const ImportedNode& c : n.children) accumulateBounds(c, world, model, box);
}

Vec3f vec(const ufbx_vec3& v) {
    return Vec3f(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

/// Kamera düğümünün o anki görünümü. target_camera_axes ile kamera yerel −Z'ye bakar.
ImportedCameraKey cameraKey(const ufbx_node* node, const ufbx_camera* cam, const AABB& box, float time) {
    ImportedCameraKey k;
    k.time = time;
    const ufbx_matrix& m = node->node_to_world;
    k.eye = vec(ufbx_transform_position(&m, ufbx_vec3{0, 0, 0}));
    Vec3f fwd = vec(ufbx_transform_direction(&m, ufbx_vec3{0, 0, -1}));
    fwd = fwd.lengthSquared() > 1e-12f ? fwd.normalized() : Vec3f(0, 0, -1);
    // Hedef: modelin merkezinin bakış doğrultusundaki izdüşümü (yörünge merkezi olarak doğal);
    // model kameranın arkasındaysa sınır boyunun yarısı kadar ileri.
    const bool hasBox = box.pMin.x <= box.pMax.x;
    const float diag = hasBox ? (box.pMax - box.pMin).length() : 2.0f;
    float d = hasBox ? (box.centroid() - k.eye).dot(fwd) : 0.0f;
    if (!(d > 0.05f * diag)) d = std::max(0.5f * diag, 1e-3f);
    k.target = k.eye + fwd * d;
    float fov = static_cast<float>(cam->field_of_view_deg.y);
    if (!(fov > 0.1f)) {
        // Görüş açısı yazılmamış (film sığdırma kuralı yok): odak uzaklığı ve film yüksekliğinden.
        const double filmIn = cam->aperture_size_inch.y > 0.0 ? cam->aperture_size_inch.y : cam->film_size_inch.y;
        const double filmMm = filmIn * 25.4;
        fov = (cam->focal_length_mm > 0.0 && filmMm > 0.0)
                  ? static_cast<float>(2.0 * std::atan(filmMm / (2.0 * cam->focal_length_mm)) * 180.0 / 3.14159265358979)
                  : 35.0f;
    }
    k.fovDeg = std::clamp(fov, 1.0f, 170.0f);
    return k;
}

void readCameras(const ufbx_scene* scene, const AABB& box, ImportedModel& model) {
    const double fps = scene->settings.frames_per_second > 0.0 ? scene->settings.frames_per_second : 24.0;
    const ufbx_anim* anim = scene->anim;
    double t0 = anim ? anim->time_begin : 0.0, t1 = anim ? anim->time_end : 0.0;
    if (anim && !(t1 > t0) && scene->anim_curves.count > 0) {
        // Yığının zaman aralığı yazılmamış (bazı dışa aktarıcılar): eğrilerin anahtarlarından.
        t0 = scene->anim_curves.data[0]->min_time;
        t1 = scene->anim_curves.data[0]->max_time;
        for (size_t i = 1; i < scene->anim_curves.count; ++i) {
            t0 = std::min(t0, scene->anim_curves.data[i]->min_time);
            t1 = std::max(t1, scene->anim_curves.data[i]->max_time);
        }
    }
    std::vector<size_t> camNodes;
    for (size_t i = 0; i < scene->nodes.count; ++i)
        if (scene->nodes.data[i]->camera) camNodes.push_back(i);
    if (camNodes.empty()) return;
    for (size_t i : camNodes) {
        const ufbx_node* n = scene->nodes.data[i];
        ImportedCamera c;
        c.name = str(n->name).empty() ? "Kamera" : str(n->name);
        c.orthographic = n->camera->projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC;
        c.orthoHeight = static_cast<float>(n->camera->orthographic_size.y);
        c.fps = static_cast<float>(fps);
        c.keys.push_back(cameraKey(n, n->camera, box, 0.0f));
        model.cameras.push_back(std::move(c));
    }
    // Animasyon: her karede sahne değerlendirilir (ebeveyn zinciri, eksen ve birim dönüşümleri
    // ufbx'in kendi kurallarıyla). Kamera hiç kıpırdamıyorsa tek anahtar kalır.
    if (!anim || !(t1 > t0)) return;
    int frames = static_cast<int>(std::floor((t1 - t0) * fps + 0.5)) + 1;
    double step = 1.0 / fps;
    constexpr int kMaxFrames = 3000;
    if (frames > kMaxFrames) {
        step = (t1 - t0) / (kMaxFrames - 1);
        frames = kMaxFrames;
        model.warnings.push_back("Kamera animasyonu çok uzun: " + std::to_string(kMaxFrames) + " örneğe seyreltildi");
    }
    std::vector<std::vector<ImportedCameraKey>> sampled(camNodes.size());
    for (int f = 0; f < frames; ++f) {
        const double t = std::min(t1, t0 + step * f);
        ufbx_error err;
        ufbx_scene* ev = ufbx_evaluate_scene(scene, anim, t, nullptr, &err);
        if (!ev) break;
        for (size_t c = 0; c < camNodes.size(); ++c) {
            const ufbx_node* n = ev->nodes.data[camNodes[c]];
            if (n && n->camera) sampled[c].push_back(cameraKey(n, n->camera, box, static_cast<float>(t - t0)));
        }
        ufbx_free_scene(ev);
    }
    for (size_t c = 0; c < camNodes.size(); ++c) {
        auto& s = sampled[c];
        if (s.size() < 2) continue;
        bool moves = false;
        for (const ImportedCameraKey& k : s)
            moves = moves || (k.eye - s.front().eye).length() > 1e-5f || (k.target - s.front().target).length() > 1e-5f ||
                    std::abs(k.fovDeg - s.front().fovDeg) > 1e-3f;
        if (moves) model.cameras[c].keys = std::move(s);
    }
}

/// FBX ışık şiddetleri uygulamalar arasında standart değildir (Maya azalmasız "1", Max fotometrik,
/// Blender watt). Göreli oranlar korunur; mutlak düzey, tüm ışıkların modelin merkezinde verdiği
/// toplam ışınım (irradiance) ~3 olacak şekilde ölçeklenir (stüdyo ön ayarlarına yakın, pozlama 0'da
/// doğru aydınlık). Kullanıcı Işıklar sekmesinden ayarlar.
void readLights(const ufbx_scene* scene, const AABB& box, ImportedModel& model) {
    const bool hasBox = box.pMin.x <= box.pMax.x;
    const Vec3f center = hasBox ? box.centroid() : Vec3f(0.0f);
    const float diag = hasBox ? (box.pMax - box.pMin).length() : 2.0f;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        const ufbx_light* fl = n->light;
        if (!fl || !fl->cast_light) continue;
        ImportedLight l;
        l.name = str(n->name).empty() ? "Işık" : str(n->name);
        const ufbx_matrix& m = n->node_to_world;
        l.position = vec(ufbx_transform_position(&m, ufbx_vec3{0, 0, 0}));
        Vec3f dir = vec(ufbx_transform_direction(&m, ufbx_vec3{0, 0, -1}));
        l.direction = dir.lengthSquared() > 1e-12f ? dir.normalized() : Vec3f(0, -1, 0);
        l.color = col(fl->color);
        const float u = std::max(0.0f, static_cast<float>(fl->intensity));
        const float d = std::max(0.05f * diag, (center - l.position).length());
        l.aimDistance = d;
        constexpr float kTargetIrradiance = 1.0f; // önce birim başına 1; sonda toplam normalleştirilir
        switch (fl->type) {
            case UFBX_LIGHT_DIRECTIONAL:
                l.type = ImportedLight::Type::Directional;
                l.intensity = kTargetIrradiance * u;
                break;
            case UFBX_LIGHT_SPOT: {
                l.type = ImportedLight::Type::Spot;
                l.intensity = kTargetIrradiance * u * d * d;
                const float outer = std::clamp(static_cast<float>(fl->outer_angle), 1.0f, 179.0f);
                const float inner = std::clamp(static_cast<float>(fl->inner_angle), 0.0f, outer);
                l.coneAngleDeg = outer;
                l.coneBlend = std::clamp(1.0f - inner / outer, 0.0f, 1.0f);
                break;
            }
            case UFBX_LIGHT_AREA: {
                l.type = ImportedLight::Type::Area;
                // Maya alan ışığı yerel −1..1 karesidir; boyut düğüm ölçeğinden gelir.
                l.width = std::max(1e-3f, 2.0f * vec(ufbx_transform_direction(&m, ufbx_vec3{1, 0, 0})).length());
                l.height = std::max(1e-3f, 2.0f * vec(ufbx_transform_direction(&m, ufbx_vec3{0, 1, 0})).length());
                l.intensity = kTargetIrradiance * u * d * d / (l.width * l.height);
                break;
            }
            default: // nokta, hacim
                l.type = ImportedLight::Type::Point;
                l.intensity = kTargetIrradiance * u * d * d;
                break;
        }
        model.lights.push_back(std::move(l));
    }
    // Toplam ışınımı merkezde ~3'e getir (oranlar aynı kalır).
    double total = 0.0;
    for (const ImportedLight& l : model.lights) {
        const float d2 = std::max(1e-6f, l.aimDistance * l.aimDistance);
        switch (l.type) {
            case ImportedLight::Type::Directional: total += l.intensity; break;
            case ImportedLight::Type::Area: total += l.intensity * l.width * l.height / d2; break;
            default: total += l.intensity / d2; break;
        }
    }
    constexpr double kSceneIrradiance = 3.0;
    if (total > 0.0)
        for (ImportedLight& l : model.lights) l.intensity *= static_cast<float>(kSceneIrradiance / total);
    if (!model.lights.empty())
        model.warnings.push_back(std::to_string(model.lights.size()) +
                                 " ışık alındı; FBX şiddetleri uygulamalar arasında farklı olduğundan düzey "
                                 "ölçeklendi, oranlar korundu");
}

} // namespace

ImportedModel FbxLoader::load(const std::string& path, std::string* error) {
    ufbx_load_opts opts = {};
    // Motor: sağ el, Y yukarı, 1 birim = 1 metre. Geometri dönüştürülür (kök düğüme ölçek
    // konmaz), böylece hiyerarşideki dönüşümler kullanıcıya doğal birimlerde görünür.
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    opts.generate_missing_normals = true;
    // Kameralar yerel −Z'ye bakar, +Y yukarı (OpenGL kuralı); yönlü ışıklar da −Z'ye. FBX'in
    // kendi kuralı kamera için +X, ışık için −Y'dir.
    opts.target_camera_axes = ufbx_coordinate_axes{UFBX_COORDINATE_AXIS_POSITIVE_X, UFBX_COORDINATE_AXIS_POSITIVE_Y,
                                                   UFBX_COORDINATE_AXIS_POSITIVE_Z};
    opts.target_light_axes = opts.target_camera_axes;
    // Animasyon yüklenir (kamera animasyonu için); mesh'ler yine dosyadaki dinlenme pozundan okunur.
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        if (error) {
            char buf[512];
            ufbx_format_error(buf, sizeof(buf), &err);
            *error = "FBX okunamadı: " + std::string(buf);
        }
        return {};
    }
    Context ctx;
    ctx.baseDir = pathFromUtf8(path).parent_path();
    ctx.originalUnitMeters = scene->settings.original_unit_meters > 0.0 ? scene->settings.original_unit_meters : 1.0;
    convertNode(scene->root_node, ctx.model.root, ctx);
    ctx.model.root.name = pathToUtf8(pathFromUtf8(path).stem());
    AABB box = AABB::empty();
    accumulateBounds(ctx.model.root, Transform(), ctx.model, box);
    readCameras(scene, box, ctx.model);
    readLights(scene, box, ctx.model);
    if (ctx.unsupportedOpacity > 0)
        ctx.model.warnings.push_back(std::to_string(ctx.unsupportedOpacity) + " malzemede opaklık (alfa) yok sayıldı");
    if (ctx.missingTextures > 0)
        ctx.model.warnings.push_back(std::to_string(ctx.missingTextures) + " doku dosyası bulunamadı");
    ufbx_free_scene(scene);
    if (ctx.model.empty() && error) *error = "FBX dosyasında görünür mesh, kamera ya da ışık yok: " + path;
    return std::move(ctx.model);
}

} // namespace photon
