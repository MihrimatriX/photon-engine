// usd_loader.cpp — OpenUSD sahnesi → ImportedModel. Yalnız PHOTON_HAS_USD ile derlenir; yoksa
// model_import.cpp "desteklenmiyor" der.
//
// Uzay kuralları: USD satır vektörü kullanır (p' = p·M, öteleme son satırda); motor sütun vektörü.
// Sahne birimi ve yukarı ekseni yalnız kök düğümün dönüşümüne konur, hiyerarşideki dönüşümler
// dosyadaki birimde kalır. Kameralar, ışıklar ve ortam ise "kökün ebeveyni" uzayında (metre,
// Y yukarı) verilir; model_import.cpp onları grubun yerel uzayına taşır.
#include "io/usd_loader.h"

#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/platform/path.h"
#include "materials/disney.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primFlags.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/domeLight_1.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/meshLightAPI.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdShade/utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace photon {

namespace {

using namespace pxr;

constexpr size_t kMaxInstanceParts = 250000; ///< PointInstancer: sahne ağacına açılan parça sınırı
constexpr int kMaxCameraKeys = 20000;

// ─────────────────────────────── Temel dönüşümler ───────────────────────────────

/// USD satır vektörü matrisi → motorun sütun vektörü dönüşümü (Mat4f(satır, sütun) = M[sütun][satır]).
Transform toTransform(const GfMatrix4d& m) {
    auto f = [&](int r, int c) { return static_cast<float>(m[c][r]); };
    return Transform(Mat4f(f(0, 0), f(0, 1), f(0, 2), f(0, 3), f(1, 0), f(1, 1), f(1, 2), f(1, 3), f(2, 0), f(2, 1),
                           f(2, 2), f(2, 3), f(3, 0), f(3, 1), f(3, 2), f(3, 3)));
}

Vec3f vec(const GfVec3f& v) { return Vec3f(v[0], v[1], v[2]); }
Color3f col(const GfVec3f& v) { return Color3f(v[0], v[1], v[2]); }

float avgScale(const Transform& t) {
    return (t.transformVector(Vec3f(1, 0, 0)).length() + t.transformVector(Vec3f(0, 1, 0)).length() +
            t.transformVector(Vec3f(0, 0, 1)).length()) / 3.0f;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

/// 2B afin UV dönüşümü: (s, t) → (a·s + b·t + c, d·s + e·t + f).
struct Affine2 {
    float m[6] = {1, 0, 0, 0, 1, 0};
    bool identity() const {
        return std::abs(m[0] - 1) < 1e-6f && std::abs(m[1]) < 1e-6f && std::abs(m[2]) < 1e-6f &&
               std::abs(m[3]) < 1e-6f && std::abs(m[4] - 1) < 1e-6f && std::abs(m[5]) < 1e-6f;
    }
    Vec2f apply(const Vec2f& p) const { return Vec2f(m[0] * p.x + m[1] * p.y + m[2], m[3] * p.x + m[4] * p.y + m[5]); }
    /// this ∘ o: önce o, sonra this.
    Affine2 after(const Affine2& o) const {
        Affine2 r;
        r.m[0] = m[0] * o.m[0] + m[1] * o.m[3];
        r.m[1] = m[0] * o.m[1] + m[1] * o.m[4];
        r.m[2] = m[0] * o.m[2] + m[1] * o.m[5] + m[2];
        r.m[3] = m[3] * o.m[0] + m[4] * o.m[3];
        r.m[4] = m[3] * o.m[1] + m[4] * o.m[4];
        r.m[5] = m[3] * o.m[2] + m[4] * o.m[5] + m[5];
        return r;
    }
    static Affine2 scale(float sx, float sy) { Affine2 a; a.m[0] = sx; a.m[4] = sy; return a; }
    static Affine2 translate(float tx, float ty) { Affine2 a; a.m[2] = tx; a.m[5] = ty; return a; }
    static Affine2 rotate(float deg) {
        const float r = deg * DEG_TO_RAD, c = std::cos(r), s = std::sin(r);
        Affine2 a;
        a.m[0] = c; a.m[1] = -s; a.m[3] = s; a.m[4] = c;
        return a;
    }
    bool operator==(const Affine2& o) const { return std::memcmp(m, o.m, sizeof(m)) == 0; }
};

// ─────────────────────────────── Tanılama ───────────────────────────────

/// USD'nin uyarılarını (TF_WARN: çözülemeyen referans, eksik dosya) toplar; ilk birkaçı kullanıcıya
/// içe aktarma uyarısı olarak gösterilir.
class DiagnosticCollector : public TfDiagnosticMgr::Delegate {
public:
    void IssueError(const TfError& e) override { add(e.GetCommentary()); }
    void IssueFatalError(const TfCallContext&, const std::string& msg) override { add(msg); }
    void IssueStatus(const TfStatus&) override {}
    void IssueWarning(const TfWarning& w) override { add(w.GetCommentary()); }
    void add(const std::string& msg) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_messages.size() < 3) m_messages.push_back(msg);
        ++m_count;
    }
    std::vector<std::string> m_messages;
    int m_count = 0;

private:
    std::mutex m_mutex;
};

// ─────────────────────────────── Bağlam ───────────────────────────────

struct TexRef {
    bool valid = false;
    std::string resolved;   ///< çözülmüş yol (USDZ içindeyse paket göreli)
    int channel = -1;       ///< -1: rgb, 0..3: r, g, b, a
    std::array<float, 4> scale{1, 1, 1, 1};
    std::array<float, 4> bias{0, 0, 0, 0};
    enum class ColorSpace { Auto, Srgb, Raw } colorSpace = ColorSpace::Auto;
    std::string primvar = "st";
    Affine2 uv;
};

/// Girişin değeri: sabit, doku ya da yok (şemanın varsayılanı kullanılır).
struct Param {
    VtValue value;
    TexRef tex;
    bool hasValue() const { return !value.IsEmpty(); }
};

struct MaterialInfo {
    std::shared_ptr<Material> material;
    std::string primvar = "st";
    Affine2 uv;
    std::string name;
};

struct Ctx {
    UsdStageRefPtr stage;
    UsdTimeCode time = UsdTimeCode::EarliestTime();
    ImportedModel model;
    Transform rootFix;             ///< sahne uzayı → kökün ebeveyni (metre, Y yukarı)
    double metersPerUnit = 0.01;
    bool zUp = false;
    UsdGeomXformCache xfCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collectionCache;
    std::unordered_map<std::string, MaterialInfo> materials;        ///< malzeme yolu → çeviri
    std::unordered_map<std::string, std::vector<ImportedMesh>> meshCache; ///< prototip yolu+malzemeler → parçalar
    std::map<std::string, std::string> textureFiles;               ///< çözülmüş yol → yerel dosya
    int subdivMeshes = 0, unsupportedShaders = 0, unsupportedNodes = 0, missingTextures = 0, udimTextures = 0;
    int emissionTextures = 0, uvConflicts = 0, skippedInstances = 0, approxLights = 0, unsupportedDome = 0;
    int domeTilted = 0, colorSpecular = 0;
    std::unordered_set<std::string> unsupportedIds;
};

// ─────────────────────────────── Dokular ───────────────────────────────

/// Çözülmüş doku yolunu yerel bir dosyaya çevirir: USDZ içindekiler doku önbelleğine çıkarılır.
std::string localTextureFile(const std::string& resolved, Ctx& ctx) {
    if (resolved.empty()) return {};
    if (auto it = ctx.textureFiles.find(resolved); it != ctx.textureFiles.end()) return it->second;
    std::string out;
    if (ArIsPackageRelativePath(resolved)) {
        std::shared_ptr<ArAsset> asset = ArGetResolver().OpenAsset(ArResolvedPath(resolved));
        if (asset) {
            std::shared_ptr<const char> buf = asset->GetBuffer();
            if (buf) out = cacheEmbeddedTexture(buf.get(), asset->GetSize());
        }
    } else {
        std::error_code ec;
        if (std::filesystem::exists(pathFromUtf8(resolved), ec)) out = resolved;
    }
    if (out.empty()) ++ctx.missingTextures;
    ctx.textureFiles.emplace(resolved, out);
    return out;
}

bool isFloatImageFile(const std::string& path) {
    const std::string e = lower(pathToUtf8(pathFromUtf8(path).extension()));
    return e == ".exr" || e == ".hdr";
}

std::optional<Image> loadFloatImage(const std::string& path) {
    const std::string e = lower(pathToUtf8(pathFromUtf8(path).extension()));
    if (e == ".exr") return loadImageEXR(path);
    if (e == ".hdr") return loadImageHDR(path);
    return std::nullopt;
}

/// Kaynak pikselin istenen kanalı (doğrusal) ölçek ve kaydırmayla.
float channelValue(const float rgba[4], int ch, const TexRef& t) {
    const int c = ch < 0 ? 0 : ch;
    return rgba[c] * t.scale[static_cast<size_t>(c)] + t.bias[static_cast<size_t>(c)];
}

/// Veri haritası (pürüzlülük → G, metallik → B, opaklık → gri) doğrudan kullanılabilir mi, yoksa
/// kanal seçilip ölçeklenmiş bir kopya mı gerekir? Kopya gri 8 bit PNG olarak önbelleğe yazılır.
/// @p threshold > 0: opaklık eşiği (değer ≥ eşik → 1, değilse 0).
std::string dataMapFile(const TexRef& t, int targetChannel, Ctx& ctx, float threshold = 0.0f) {
    const std::string file = localTextureFile(t.resolved, ctx);
    if (file.empty()) return {};
    const int ch = t.channel < 0 ? 0 : t.channel;
    const bool trivial = t.scale[static_cast<size_t>(ch)] == 1.0f && t.bias[static_cast<size_t>(ch)] == 0.0f &&
                         threshold <= 0.0f;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::optional<Image> fimg;
    if (isFloatImageFile(file)) {
        fimg = loadFloatImage(file);
        if (!fimg) return {};
        w = fimg->width();
        h = fimg->height();
    } else if (!loadRGBA8(file, rgba, w, h)) {
        return {};
    }
    if (!fimg && trivial) {
        // Gri dosyada her kanal aynıdır; alfa istendiyse ve alfa doluysa opaklık okuyucusu onu alır.
        bool gray = true;
        bool alpha = false;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            if (rgba[i] != rgba[i + 1] || rgba[i] != rgba[i + 2]) gray = false;
            if (rgba[i + 3] != 255) alpha = true;
        }
        if (ch == targetChannel) return file;
        if (targetChannel == 3 && ch == 3 && alpha) return file;
        if (gray && ch < 3 && !(targetChannel == 3 && alpha)) return file;
    }
    std::vector<uint8_t> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            float px[4];
            if (fimg) {
                const Color3f c = fimg->getPixel(x, y);
                px[0] = c.r;
                px[1] = c.g;
                px[2] = c.b;
                px[3] = 1.0f;
            } else {
                for (int k = 0; k < 4; ++k) px[k] = rgba[i + static_cast<size_t>(k)] / 255.0f;
            }
            float v = channelValue(px, t.channel, t);
            if (threshold > 0.0f) v = v >= threshold ? 1.0f : 0.0f;
            const auto b = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            out[i] = out[i + 1] = out[i + 2] = b;
        }
    char key[96];
    std::snprintf(key, sizeof(key), "|ch%d|s%g|b%g|thr%g", t.channel, t.scale[static_cast<size_t>(ch)],
                  t.bias[static_cast<size_t>(ch)], threshold);
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(pathFromUtf8(file), ec).time_since_epoch().count();
    const std::string path = derivedTexturePath(file + key + std::to_string(mtime), ".png");
    if (path.empty()) return {};
    if (std::filesystem::exists(pathFromUtf8(path), ec)) return path;
    const std::string tmp = path + ".tmp.png";
    if (!saveRGBA8PNG(out.data(), w, h, tmp, false)) return {};
    std::filesystem::rename(pathFromUtf8(tmp), pathFromUtf8(path), ec);
    return ec ? std::string() : path;
}

/// Renk dokusu (taban rengi): doğrudan dosya ya da ölçek/kanal/renk uzayı işlenmiş doğrusal EXR.
/// @p tint ek çarpan (OpenPBR base_weight, Standard Surface base).
std::string colorMapFile(const TexRef& t, Ctx& ctx, float tint = 1.0f) {
    const std::string file = localTextureFile(t.resolved, ctx);
    if (file.empty()) return {};
    const bool floatFile = isFloatImageFile(file);
    // auto: 8 bit renk dokusu sRGB, kayan noktalı doğrusal (Hydra'nın kuralı).
    const bool srgb = !floatFile && t.colorSpace != TexRef::ColorSpace::Raw;
    const bool trivial = t.channel < 0 && tint == 1.0f && t.scale[0] == 1.0f && t.scale[1] == 1.0f &&
                         t.scale[2] == 1.0f && t.bias[0] == 0.0f && t.bias[1] == 0.0f && t.bias[2] == 0.0f &&
                         (floatFile || srgb);
    if (trivial) return file;
    std::optional<Image> src = floatFile ? loadFloatImage(file)
                                         : loadImageLDR(file, srgb ? TextureEncoding::SRGB : TextureEncoding::Linear);
    if (!src) return {};
    Image out(src->width(), src->height());
    for (int y = 0; y < src->height(); ++y)
        for (int x = 0; x < src->width(); ++x) {
            const Color3f c = src->getPixel(x, y);
            const float px[4] = {c.r, c.g, c.b, 1.0f};
            Color3f o;
            if (t.channel >= 0) {
                o = Color3f(channelValue(px, t.channel, t));
            } else {
                o = Color3f(px[0] * t.scale[0] + t.bias[0], px[1] * t.scale[1] + t.bias[1], px[2] * t.scale[2] + t.bias[2]);
            }
            out.setPixel(x, y, (o * tint).clamp(0.0f, 1e6f));
        }
    char key[128];
    std::snprintf(key, sizeof(key), "|col|ch%d|s%g,%g,%g|b%g,%g,%g|t%g|cs%d", t.channel, t.scale[0], t.scale[1],
                  t.scale[2], t.bias[0], t.bias[1], t.bias[2], tint, static_cast<int>(t.colorSpace));
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(pathFromUtf8(file), ec).time_since_epoch().count();
    return cacheDerivedImage(out, file + key + std::to_string(mtime));
}

/// Normal haritası: DirectX kuralı (yeşil ters, ölçek y < 0) ise yeşil çevrilmiş kopya.
std::string normalMapFile(const TexRef& t, Ctx& ctx) {
    const std::string file = localTextureFile(t.resolved, ctx);
    if (file.empty()) return {};
    if (t.scale[1] >= 0.0f) return file;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!loadRGBA8(file, rgba, w, h)) return file;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) rgba[i + 1] = static_cast<uint8_t>(255 - rgba[i + 1]);
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(pathFromUtf8(file), ec).time_since_epoch().count();
    const std::string path = derivedTexturePath(file + "|flipG" + std::to_string(mtime), ".png");
    if (path.empty()) return file;
    if (std::filesystem::exists(pathFromUtf8(path), ec)) return path;
    const std::string tmp = path + ".tmp.png";
    if (!saveRGBA8PNG(rgba.data(), w, h, tmp, false)) return file;
    std::filesystem::rename(pathFromUtf8(tmp), pathFromUtf8(path), ec);
    return ec ? file : path;
}

/// Emisyon dokusu desteklenmez (malzeme emisyonu sabittir): ortalama rengi kullanılır.
std::optional<Color3f> averageColor(const TexRef& t, Ctx& ctx) {
    const std::string file = localTextureFile(t.resolved, ctx);
    if (file.empty()) return std::nullopt;
    std::optional<Image> img = isFloatImageFile(file) ? loadFloatImage(file) : loadImageLDR(file, TextureEncoding::SRGB);
    if (!img || img->width() <= 0) return std::nullopt;
    Color3f sum(0.0f);
    for (int y = 0; y < img->height(); ++y)
        for (int x = 0; x < img->width(); ++x) sum = sum + img->getPixel(x, y);
    sum = sum / static_cast<float>(img->width() * img->height());
    return Color3f(sum.r * t.scale[0] + t.bias[0], sum.g * t.scale[1] + t.bias[1], sum.b * t.scale[2] + t.bias[2]);
}

// ─────────────────────────────── Gölgelendirici ağı ───────────────────────────────

std::string shaderId(const UsdShadeShader& s) {
    TfToken id;
    s.GetShaderId(&id);
    return id.GetString();
}

/// Girişe bağlı değeri üreten öznitelik (düğüm grafiği arayüzlerinden geçerek).
UsdAttribute producingAttr(const UsdShadeShader& s, const char* name) {
    UsdShadeInput in = s.GetInput(TfToken(name));
    if (!in) return {};
    UsdShadeAttributeVector attrs = in.GetValueProducingAttributes(false);
    if (attrs.empty()) return {};
    return attrs.front();
}

bool isOutput(const UsdAttribute& a) {
    return UsdShadeUtils::GetBaseNameAndType(a.GetName()).second == UsdShadeAttributeType::Output;
}

template <typename T>
std::optional<T> constInput(const UsdShadeShader& s, const char* name, UsdTimeCode t) {
    UsdAttribute a = producingAttr(s, name);
    if (!a || isOutput(a)) return std::nullopt;
    T v;
    if (a.Get(&v, t)) return v;
    return std::nullopt;
}

std::string stringInput(const UsdShadeShader& s, const char* name, UsdTimeCode t) {
    UsdAttribute a = producingAttr(s, name);
    if (!a || isOutput(a)) return {};
    VtValue v;
    if (!a.Get(&v, t)) return {};
    if (v.IsHolding<std::string>()) return v.UncheckedGet<std::string>();
    if (v.IsHolding<TfToken>()) return v.UncheckedGet<TfToken>().GetString();
    return {};
}

void parseUvChain(const UsdAttribute& out, TexRef& tex, Ctx& ctx, int depth);

/// UV kaynağı girişi (UsdUVTexture "st", MaterialX "texcoord"): birincil değişken okuyucu ve
/// UsdTransform2d / place2d zinciri.
void parseUvInput(const UsdShadeShader& s, const char* name, TexRef& tex, Ctx& ctx, int depth) {
    UsdAttribute a = producingAttr(s, name);
    if (a && isOutput(a)) parseUvChain(a, tex, ctx, depth + 1);
}

void parseUvChain(const UsdAttribute& out, TexRef& tex, Ctx& ctx, int depth) {
    if (depth > 8) return;
    UsdShadeShader s(out.GetPrim());
    const std::string id = shaderId(s);
    if (id == "UsdPrimvarReader_float2" || id == "ND_UsdPrimvarReader_vector2") {
        const std::string v = stringInput(s, "varname", ctx.time);
        if (!v.empty()) tex.primvar = v;
    } else if (startsWith(id, "ND_geompropvalue_vector2")) {
        const std::string v = stringInput(s, "geomprop", ctx.time);
        if (!v.empty()) tex.primvar = v;
    } else if (startsWith(id, "ND_texcoord_vector2")) {
        const int index = constInput<int>(s, "index", ctx.time).value_or(0);
        tex.primvar = index == 0 ? "st" : "st" + std::to_string(index);
    } else if (id == "UsdTransform2d" || id == "ND_UsdTransform2d") {
        parseUvInput(s, "in", tex, ctx, depth);
        const GfVec2f sc = constInput<GfVec2f>(s, "scale", ctx.time).value_or(GfVec2f(1, 1));
        const float rot = constInput<float>(s, "rotation", ctx.time).value_or(0.0f);
        const GfVec2f tr = constInput<GfVec2f>(s, "translation", ctx.time).value_or(GfVec2f(0, 0));
        // UsdTransform2d: önce ölçek, sonra saat yönü tersine dönüş (derece), sonra öteleme.
        tex.uv = Affine2::translate(tr[0], tr[1]).after(Affine2::rotate(rot)).after(Affine2::scale(sc[0], sc[1])).after(tex.uv);
    } else if (startsWith(id, "ND_place2d_vector2")) {
        parseUvInput(s, "texcoord", tex, ctx, depth);
        const GfVec2f pivot = constInput<GfVec2f>(s, "pivot", ctx.time).value_or(GfVec2f(0, 0));
        const GfVec2f sc = constInput<GfVec2f>(s, "scale", ctx.time).value_or(GfVec2f(1, 1));
        const float rot = constInput<float>(s, "rotate", ctx.time).value_or(0.0f);
        const GfVec2f off = constInput<GfVec2f>(s, "offset", ctx.time).value_or(GfVec2f(0, 0));
        // MaterialX place2d (SRT): (uv − pivot) / ölçek, dönüş, + pivot − kaydırma.
        const float sx = std::abs(sc[0]) > 1e-8f ? 1.0f / sc[0] : 1.0f, sy = std::abs(sc[1]) > 1e-8f ? 1.0f / sc[1] : 1.0f;
        tex.uv = Affine2::translate(pivot[0] - off[0], pivot[1] - off[1])
                     .after(Affine2::rotate(rot))
                     .after(Affine2::scale(sx, sy))
                     .after(Affine2::translate(-pivot[0], -pivot[1]))
                     .after(tex.uv);
    } else {
        ++ctx.unsupportedNodes;
    }
}

int channelFromOutput(const std::string& out) {
    if (out == "r" || out == "outr") return 0;
    if (out == "g" || out == "outg") return 1;
    if (out == "b" || out == "outb") return 2;
    if (out == "a" || out == "outa") return 3;
    return -1;
}

TexRef::ColorSpace colorSpaceOf(const std::string& cs) {
    const std::string l = lower(cs);
    if (l.empty() || l == "auto") return TexRef::ColorSpace::Auto;
    if (l.find("srgb") != std::string::npos) return TexRef::ColorSpace::Srgb;
    return TexRef::ColorSpace::Raw; // raw, lin_rec709, acescg, data
}

bool readFile(const UsdShadeShader& s, TexRef& tex, Ctx& ctx) {
    UsdAttribute a = producingAttr(s, "file");
    if (!a || isOutput(a)) return false;
    SdfAssetPath asset;
    if (!a.Get(&asset, ctx.time)) return false;
    std::string resolved = asset.GetResolvedPath();
    if (resolved.empty()) resolved = asset.GetAssetPath();
    if (resolved.find("<UDIM>") != std::string::npos || asset.GetAssetPath().find("<UDIM>") != std::string::npos) {
        ++ctx.udimTextures;
        return false;
    }
    if (resolved.empty()) return false;
    tex.resolved = resolved;
    if (a.HasColorSpace()) tex.colorSpace = colorSpaceOf(a.GetColorSpace().GetString());
    return true;
}

TexRef textureFromOutput(const UsdAttribute& out, Ctx& ctx, int depth = 0) {
    TexRef tex;
    if (depth > 8) return tex;
    UsdShadeShader s(out.GetPrim());
    const std::string id = shaderId(s);
    const std::string outName = UsdShadeUtils::GetBaseNameAndType(out.GetName()).first.GetString();
    if (id == "UsdUVTexture" || id == "ND_UsdUVTexture") {
        if (!readFile(s, tex, ctx)) return tex;
        tex.channel = channelFromOutput(outName);
        if (auto v = constInput<GfVec4f>(s, "scale", ctx.time)) tex.scale = {(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
        if (auto v = constInput<GfVec4f>(s, "bias", ctx.time)) tex.bias = {(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
        const std::string cs = stringInput(s, "sourceColorSpace", ctx.time);
        if (!cs.empty()) tex.colorSpace = colorSpaceOf(cs);
        parseUvInput(s, "st", tex, ctx, depth);
        tex.valid = true;
    } else if (startsWith(id, "ND_image_") || startsWith(id, "ND_tiledimage_") || startsWith(id, "ND_gltf_image_") ||
               startsWith(id, "ND_gltf_colorimage")) {
        if (!readFile(s, tex, ctx)) return tex;
        tex.channel = (id.find("_float") != std::string::npos) ? 0 : channelFromOutput(outName);
        parseUvInput(s, "texcoord", tex, ctx, depth);
        if (startsWith(id, "ND_tiledimage_")) {
            const GfVec2f tiling = constInput<GfVec2f>(s, "uvtiling", ctx.time).value_or(GfVec2f(1, 1));
            const GfVec2f off = constInput<GfVec2f>(s, "uvoffset", ctx.time).value_or(GfVec2f(0, 0));
            tex.uv = Affine2::translate(off[0], off[1]).after(Affine2::scale(tiling[0], tiling[1])).after(tex.uv);
        }
        tex.valid = true;
    } else if (startsWith(id, "ND_normalmap")) {
        UsdAttribute in = producingAttr(s, "in");
        if (in && isOutput(in)) tex = textureFromOutput(in, ctx, depth + 1);
    } else if (startsWith(id, "ND_extract_")) {
        UsdAttribute in = producingAttr(s, "in");
        if (in && isOutput(in)) {
            tex = textureFromOutput(in, ctx, depth + 1);
            tex.channel = std::clamp(constInput<int>(s, "index", ctx.time).value_or(0), 0, 3);
        }
    } else if (startsWith(id, "ND_separate3_") || startsWith(id, "ND_separate4_")) {
        UsdAttribute in = producingAttr(s, "in");
        if (in && isOutput(in)) {
            tex = textureFromOutput(in, ctx, depth + 1);
            tex.channel = channelFromOutput(outName);
            if (tex.channel < 0) tex.channel = outName == "outx" ? 0 : outName == "outy" ? 1 : outName == "outz" ? 2 : 3;
        }
    } else {
        ++ctx.unsupportedNodes;
    }
    return tex;
}

Param readParam(const UsdShadeShader& s, const char* name, Ctx& ctx) {
    Param p;
    UsdAttribute a = producingAttr(s, name);
    if (!a) return p;
    if (isOutput(a)) {
        p.tex = textureFromOutput(a, ctx);
        return p;
    }
    a.Get(&p.value, ctx.time);
    return p;
}

float asFloat(const Param& p, float def) {
    if (p.value.IsHolding<float>()) return p.value.UncheckedGet<float>();
    if (p.value.IsHolding<double>()) return static_cast<float>(p.value.UncheckedGet<double>());
    if (p.value.IsHolding<int>()) return static_cast<float>(p.value.UncheckedGet<int>());
    if (p.value.IsHolding<GfVec3f>()) return col(p.value.UncheckedGet<GfVec3f>()).luminance();
    return def;
}

Color3f asColor(const Param& p, const Color3f& def) {
    if (p.value.IsHolding<GfVec3f>()) return col(p.value.UncheckedGet<GfVec3f>());
    if (p.value.IsHolding<GfVec4f>()) {
        const GfVec4f v = p.value.UncheckedGet<GfVec4f>();
        return Color3f(v[0], v[1], v[2]);
    }
    if (p.value.IsHolding<float>()) return Color3f(p.value.UncheckedGet<float>());
    return def;
}

std::string asToken(const Param& p) {
    if (p.value.IsHolding<TfToken>()) return p.value.UncheckedGet<TfToken>().GetString();
    if (p.value.IsHolding<std::string>()) return p.value.UncheckedGet<std::string>();
    return {};
}

/// Dokulu girişlerin ortak UV kaynağı ve dönüşümü (motor malzeme başına tek UV kümesi kullanır).
struct UvCollector {
    bool set = false;
    std::string primvar = "st";
    Affine2 uv;
    void use(const TexRef& t, Ctx& ctx) {
        if (!t.valid) return;
        if (!set) {
            set = true;
            primvar = t.primvar;
            uv = t.uv;
        } else if (!(t.uv == uv) || t.primvar != primvar) {
            ++ctx.uvConflicts;
        }
    }
};

// ─── Malzeme eşlemeleri (hepsi birleşik OpenPBR / Principled'a) ───

struct Shading {
    DisneyMaterial* d;
    UvCollector uv;
    Ctx& ctx;
};

void applyBaseColor(Shading& sh, const Param& p, const Color3f& def, float weight = 1.0f) {
    if (p.tex.valid) {
        const std::string f = colorMapFile(p.tex, sh.ctx, weight);
        if (!f.empty()) {
            sh.d->setAlbedoMap(f);
            sh.uv.use(p.tex, sh.ctx);
            return;
        }
    }
    sh.d->setBaseColor(asColor(p, def) * weight);
}

void applyRoughness(Shading& sh, const Param& p, float def) {
    if (p.tex.valid) {
        const std::string f = dataMapFile(p.tex, 1, sh.ctx);
        if (!f.empty()) {
            sh.d->setRoughnessMap(f);
            sh.uv.use(p.tex, sh.ctx);
        }
    }
    sh.d->setRoughness(std::clamp(asFloat(p, def), 0.0f, 1.0f));
}

void applyMetallic(Shading& sh, const Param& p, float def) {
    if (p.tex.valid) {
        const std::string f = dataMapFile(p.tex, 2, sh.ctx);
        if (!f.empty()) {
            sh.d->setMetalnessMap(f);
            sh.uv.use(p.tex, sh.ctx);
        }
    }
    sh.d->setMetallic(std::clamp(asFloat(p, def), 0.0f, 1.0f));
}

void applyNormal(Shading& sh, const Param& p) {
    if (!p.tex.valid) return;
    const std::string f = normalMapFile(p.tex, sh.ctx);
    if (f.empty()) return;
    sh.d->setNormalMap(f);
    sh.uv.use(p.tex, sh.ctx);
}

/// Kesme (presence) opaklığı: sabit ya da doku; @p threshold > 0 ise eşiklenir.
void applyPresence(Shading& sh, const Param& p, float def, float threshold) {
    if (p.tex.valid) {
        const std::string f = dataMapFile(p.tex, 3, sh.ctx, threshold);
        if (!f.empty()) {
            sh.d->setOpacityMap(f);
            sh.uv.use(p.tex, sh.ctx);
            return;
        }
    }
    float o = std::clamp(asFloat(p, def), 0.0f, 1.0f);
    if (threshold > 0.0f) o = o >= threshold ? 1.0f : 0.0f;
    sh.d->setOpacity(o);
}

Color3f emissionOf(Shading& sh, const Param& p, const Color3f& def) {
    if (p.tex.valid) {
        ++sh.ctx.emissionTextures;
        if (auto c = averageColor(p.tex, sh.ctx)) return *c;
    }
    return asColor(p, def);
}

/// Motorun tek taban rengi hem difüzü hem geçirgenliği boyar; kaynak modellerde geçen ışığın rengi
/// ayrıdır (PreviewSurface: boyanmamış, OpenPBR/Standard Surface: transmission_color). Geçirgenlik
/// payıyla karıştırılır: tam cam geçiş rengini, opak yüzey taban rengini alır. Dokulu tabanda doku kalır.
void blendTransmissionColor(Shading& sh, const Param& base, const Color3f& baseDef, float baseWeight, float trans,
                            const Color3f& transColor) {
    if (trans <= 0.0f || base.tex.valid) return;
    sh.d->setBaseColor(lerp(asColor(base, baseDef) * baseWeight, transColor, std::clamp(trans, 0.0f, 1.0f)));
}

void previewSurface(Shading& sh, const UsdShadeShader& s) {
    Ctx& ctx = sh.ctx;
    DisneyMaterial& d = *sh.d;
    const Param diffuse = readParam(s, "diffuseColor", ctx);
    const bool specWorkflow = asFloat(readParam(s, "useSpecularWorkflow", ctx), 0.0f) > 0.5f;
    const float ior = std::max(1.0f, asFloat(readParam(s, "ior", ctx), 1.5f));
    applyRoughness(sh, readParam(s, "roughness", ctx), 0.5f);
    applyNormal(sh, readParam(s, "normal", ctx));
    d.setEmission(emissionOf(sh, readParam(s, "emissiveColor", ctx), Color3f(0.0f)));
    d.setClearCoat(asFloat(readParam(s, "clearcoat", ctx), 0.0f));
    d.setClearCoatRoughness(asFloat(readParam(s, "clearcoatRoughness", ctx), 0.01f));
    d.setSpecular(0.5f);
    d.setIor(ior);
    bool metalFromSpecular = false;
    if (specWorkflow) {
        // Speküler iş akışı: specularColor dik gelişteki yansıma (F0), kenarda beyaz. Yalıtkan için F0
        // kırılma indisine çevrilir (motorun yalıtkan F0'ı renksizdir). Taban rengi siyaha yakın ve F0
        // parlaksa yüzey metaldir (speküler akışla yazılmış altın, krom).
        const Param spec = readParam(s, "specularColor", ctx);
        const Color3f f0 = spec.tex.valid ? Color3f(0.04f) : asColor(spec, Color3f(0.0f));
        const Color3f base = diffuse.tex.valid ? Color3f(0.5f) : asColor(diffuse, Color3f(0.18f));
        const float f0l = std::max({f0.r, f0.g, f0.b});
        if (f0l > 0.25f && base.luminance() < 0.05f) {
            metalFromSpecular = true;
            d.setMetallic(1.0f);
            d.setBaseColor(f0);
        } else {
            if (std::max({f0.r, f0.g, f0.b}) - std::min({f0.r, f0.g, f0.b}) > 0.02f) ++ctx.colorSpecular;
            const float f = std::clamp(f0.luminance(), 0.0f, 0.99f);
            if (f <= 1e-6f) {
                // F0 = 0 ama spesifikasyona göre kenar rengi beyaz: sıyırma yansıması kalır (IOR 1).
                d.setIor(1.0f);
            } else {
                const float sq = std::sqrt(f);
                d.setIor(std::clamp((1.0f + sq) / (1.0f - sq), 1.0001f, 3.0f));
            }
        }
    } else {
        applyMetallic(sh, readParam(s, "metallic", ctx), 0.0f);
    }
    if (!metalFromSpecular) applyBaseColor(sh, diffuse, Color3f(0.18f));

    const Param opacity = readParam(s, "opacity", ctx);
    const float threshold = asFloat(readParam(s, "opacityThreshold", ctx), 0.0f);
    const std::string mode = asToken(readParam(s, "opacityMode", ctx));
    if (threshold > 0.0f || mode == "presence" || opacity.tex.valid) {
        // Kesme maskesi (yaprak, dantel). Dokulu opaklık "transparent" kipte de kesme olarak okunur:
        // Hydra Storm'un görüntüsüne en yakın sonuç.
        applyPresence(sh, opacity, 1.0f, threshold);
    } else {
        // transparent: opaklık yalnız difüzü azaltır, speküler kalır → kırılan cam (geçirgenlik).
        const float o = std::clamp(asFloat(opacity, 1.0f), 0.0f, 1.0f);
        if (o < 1.0f) {
            d.setSpecularTransmission(1.0f - o);
            if (specWorkflow) d.setIor(ior);
            if (!metalFromSpecular) blendTransmissionColor(sh, diffuse, Color3f(0.18f), 1.0f, 1.0f - o, Color3f(1.0f));
        }
    }
}

void openPbr(Shading& sh, const UsdShadeShader& s) {
    Ctx& ctx = sh.ctx;
    DisneyMaterial& d = *sh.d;
    const float baseWeight = asFloat(readParam(s, "base_weight", ctx), 1.0f);
    const Param baseColor = readParam(s, "base_color", ctx);
    applyBaseColor(sh, baseColor, Color3f(0.8f), baseWeight);
    applyMetallic(sh, readParam(s, "base_metalness", ctx), 0.0f);
    d.setDiffuseRoughness(asFloat(readParam(s, "base_diffuse_roughness", ctx), 0.0f));
    d.setSpecular(std::clamp(0.5f * asFloat(readParam(s, "specular_weight", ctx), 1.0f), 0.0f, 1.0f));
    d.setEdgeTint(asColor(readParam(s, "specular_color", ctx), Color3f(1.0f)));
    applyRoughness(sh, readParam(s, "specular_roughness", ctx), 0.3f);
    d.setIor(asFloat(readParam(s, "specular_ior", ctx), 1.5f));
    d.setAnisotropy(asFloat(readParam(s, "specular_roughness_anisotropy", ctx), 0.0f));
    const float transmission = asFloat(readParam(s, "transmission_weight", ctx), 0.0f);
    d.setSpecularTransmission(transmission);
    blendTransmissionColor(sh, baseColor, Color3f(0.8f), baseWeight, transmission,
                           asColor(readParam(s, "transmission_color", ctx), Color3f(1.0f)));
    const float sss = asFloat(readParam(s, "subsurface_weight", ctx), 0.0f);
    if (sss > 0.0f) {
        d.setSubsurface(sss);
        const float r = asFloat(readParam(s, "subsurface_radius", ctx), 1.0f);
        d.setSubsurfaceRadius(asColor(readParam(s, "subsurface_radius_scale", ctx), Color3f(1.0f, 0.5f, 0.25f)) * r);
        d.setSubsurfaceAnisotropy(asFloat(readParam(s, "subsurface_scatter_anisotropy", ctx), 0.0f));
    }
    const float fuzz = asFloat(readParam(s, "fuzz_weight", ctx), 0.0f);
    if (fuzz > 0.0f) {
        d.setSheen(fuzz);
        d.setSheenColor(asColor(readParam(s, "fuzz_color", ctx), Color3f(1.0f)));
        d.setSheenRoughness(asFloat(readParam(s, "fuzz_roughness", ctx), 0.5f));
    }
    const float coat = asFloat(readParam(s, "coat_weight", ctx), 0.0f);
    if (coat > 0.0f) {
        d.setClearCoat(coat);
        d.setClearCoatRoughness(asFloat(readParam(s, "coat_roughness", ctx), 0.0f));
        d.setCoatIor(asFloat(readParam(s, "coat_ior", ctx), 1.6f));
        d.setCoatTint(asColor(readParam(s, "coat_color", ctx), Color3f(1.0f)));
    }
    if (asFloat(readParam(s, "thin_film_weight", ctx), 0.0f) > 0.0f)
        d.setThinFilm(1000.0f * asFloat(readParam(s, "thin_film_thickness", ctx), 0.5f),
                      asFloat(readParam(s, "thin_film_ior", ctx), 1.4f));
    const float lum = asFloat(readParam(s, "emission_luminance", ctx), 0.0f);
    if (lum > 0.0f) d.setEmission(emissionOf(sh, readParam(s, "emission_color", ctx), Color3f(1.0f)) * lum);
    applyNormal(sh, readParam(s, "geometry_normal", ctx));
    applyPresence(sh, readParam(s, "geometry_opacity", ctx), 1.0f, 0.0f);
}

void standardSurface(Shading& sh, const UsdShadeShader& s) {
    Ctx& ctx = sh.ctx;
    DisneyMaterial& d = *sh.d;
    const float base = asFloat(readParam(s, "base", ctx), 0.8f);
    const Param baseColor = readParam(s, "base_color", ctx);
    applyBaseColor(sh, baseColor, Color3f(1.0f), base);
    applyMetallic(sh, readParam(s, "metalness", ctx), 0.0f);
    d.setDiffuseRoughness(asFloat(readParam(s, "diffuse_roughness", ctx), 0.0f));
    d.setSpecular(std::clamp(0.5f * asFloat(readParam(s, "specular", ctx), 1.0f), 0.0f, 1.0f));
    d.setEdgeTint(asColor(readParam(s, "specular_color", ctx), Color3f(1.0f)));
    applyRoughness(sh, readParam(s, "specular_roughness", ctx), 0.2f);
    d.setIor(asFloat(readParam(s, "specular_IOR", ctx), 1.5f));
    d.setAnisotropy(asFloat(readParam(s, "specular_anisotropy", ctx), 0.0f));
    const float transmission = asFloat(readParam(s, "transmission", ctx), 0.0f);
    d.setSpecularTransmission(transmission);
    blendTransmissionColor(sh, baseColor, Color3f(1.0f), base, transmission,
                           asColor(readParam(s, "transmission_color", ctx), Color3f(1.0f)));
    const float sss = asFloat(readParam(s, "subsurface", ctx), 0.0f);
    if (sss > 0.0f) {
        d.setSubsurface(sss);
        d.setSubsurfaceRadius(asColor(readParam(s, "subsurface_radius", ctx), Color3f(1.0f)));
        d.setSubsurfaceScale(asFloat(readParam(s, "subsurface_scale", ctx), 1.0f));
    }
    const float sheen = asFloat(readParam(s, "sheen", ctx), 0.0f);
    if (sheen > 0.0f) {
        d.setSheen(sheen);
        d.setSheenColor(asColor(readParam(s, "sheen_color", ctx), Color3f(1.0f)));
        d.setSheenRoughness(asFloat(readParam(s, "sheen_roughness", ctx), 0.3f));
    }
    const float coat = asFloat(readParam(s, "coat", ctx), 0.0f);
    if (coat > 0.0f) {
        d.setClearCoat(coat);
        d.setClearCoatRoughness(asFloat(readParam(s, "coat_roughness", ctx), 0.1f));
        d.setCoatIor(asFloat(readParam(s, "coat_IOR", ctx), 1.5f));
        d.setCoatTint(asColor(readParam(s, "coat_color", ctx), Color3f(1.0f)));
    }
    const float film = asFloat(readParam(s, "thin_film_thickness", ctx), 0.0f);
    if (film > 0.0f) d.setThinFilm(film, asFloat(readParam(s, "thin_film_IOR", ctx), 1.5f));
    const float em = asFloat(readParam(s, "emission", ctx), 0.0f);
    if (em > 0.0f) d.setEmission(emissionOf(sh, readParam(s, "emission_color", ctx), Color3f(1.0f)) * em);
    applyNormal(sh, readParam(s, "normal", ctx));
    applyPresence(sh, readParam(s, "opacity", ctx), 1.0f, 0.0f);
}

void gltfPbr(Shading& sh, const UsdShadeShader& s) {
    Ctx& ctx = sh.ctx;
    DisneyMaterial& d = *sh.d;
    applyBaseColor(sh, readParam(s, "base_color", ctx), Color3f(1.0f));
    applyMetallic(sh, readParam(s, "metallic", ctx), 1.0f);
    applyRoughness(sh, readParam(s, "roughness", ctx), 1.0f);
    applyNormal(sh, readParam(s, "normal", ctx));
    d.setIor(asFloat(readParam(s, "ior", ctx), 1.5f));
    d.setSpecular(std::clamp(0.5f * asFloat(readParam(s, "specular", ctx), 1.0f), 0.0f, 1.0f));
    d.setSpecularTransmission(asFloat(readParam(s, "transmission", ctx), 0.0f));
    const float cc = asFloat(readParam(s, "clearcoat", ctx), 0.0f);
    if (cc > 0.0f) {
        d.setClearCoat(cc);
        d.setClearCoatRoughness(asFloat(readParam(s, "clearcoat_roughness", ctx), 0.0f));
    }
    const Color3f sheen = asColor(readParam(s, "sheen_color", ctx), Color3f(0.0f));
    const float sw = std::max({sheen.r, sheen.g, sheen.b});
    if (sw > 0.0f) {
        d.setSheen(std::clamp(sw, 0.0f, 1.0f));
        d.setSheenColor(sheen / sw);
        d.setSheenRoughness(asFloat(readParam(s, "sheen_roughness", ctx), 0.0f));
    }
    const Color3f em = emissionOf(sh, readParam(s, "emissive", ctx), Color3f(0.0f));
    d.setEmission(em * asFloat(readParam(s, "emissive_strength", ctx), 1.0f));
    const int alphaMode = static_cast<int>(asFloat(readParam(s, "alpha_mode", ctx), 0.0f));
    if (alphaMode != 0)
        applyPresence(sh, readParam(s, "alpha", ctx), 1.0f,
                      alphaMode == 1 ? asFloat(readParam(s, "alpha_cutoff", ctx), 0.5f) : 0.0f);
}

const MaterialInfo* convertMaterial(const UsdShadeMaterial& mat, Ctx& ctx) {
    if (!mat) return nullptr;
    const std::string key = mat.GetPath().GetString();
    if (auto it = ctx.materials.find(key); it != ctx.materials.end()) return &it->second;
    MaterialInfo info;
    info.name = mat.GetPrim().GetName().GetString();
    UsdShadeShader surface = mat.ComputeSurfaceSource(TfTokenVector{TfToken("mtlx"), UsdShadeTokens->universalRenderContext});
    if (surface) {
        const std::string id = shaderId(surface);
        auto d = std::make_shared<DisneyMaterial>(Color3f(0.18f), 0.0f, 0.5f, 0.5f);
        Shading sh{d.get(), {}, ctx};
        bool ok = true;
        if (id == "UsdPreviewSurface" || id == "ND_UsdPreviewSurface_surfaceshader") previewSurface(sh, surface);
        else if (startsWith(id, "ND_open_pbr_surface")) openPbr(sh, surface);
        else if (startsWith(id, "ND_standard_surface")) standardSurface(sh, surface);
        else if (startsWith(id, "ND_gltf_pbr")) gltfPbr(sh, surface);
        else ok = false;
        if (ok) {
            info.material = d;
            info.primvar = sh.uv.primvar;
            info.uv = sh.uv.uv;
        } else {
            ++ctx.unsupportedShaders;
            ctx.unsupportedIds.insert(id.empty() ? surface.GetPrim().GetName().GetString() : id);
        }
    }
    return &ctx.materials.emplace(key, std::move(info)).first->second;
}

UsdShadeMaterial boundMaterial(const UsdPrim& prim, Ctx& ctx) {
    UsdShadeMaterialBindingAPI api(prim);
    return api.ComputeBoundMaterial(&ctx.bindingsCache, &ctx.collectionCache, UsdShadeTokens->full);
}

// ─────────────────────────────── Geometri ───────────────────────────────

template <typename T>
struct PrimvarData {
    VtArray<T> values;
    TfToken interp;
    bool valid() const { return !values.empty(); }
    /// Köşenin değeri: kapsamına göre nokta, yüz-köşe, yüz ya da sabit indeksi.
    const T* at(size_t point, size_t corner, size_t face) const {
        if (values.empty()) return nullptr;
        size_t i = point;
        if (interp == UsdGeomTokens->faceVarying) i = corner;
        else if (interp == UsdGeomTokens->uniform) i = face;
        else if (interp == UsdGeomTokens->constant) i = 0;
        return i < values.size() ? &values[i] : nullptr;
    }
};

template <typename T>
bool readPrimvar(const UsdGeomPrimvar& pv, PrimvarData<T>& out, UsdTimeCode t) {
    if (!pv || !pv.HasValue()) return false;
    if (!pv.ComputeFlattened(&out.values, t)) return false;
    out.interp = pv.GetInterpolation();
    return !out.values.empty();
}

/// UV kümesi: malzemenin istediği birincil değişken, yoksa "st", yoksa ilk doku koordinatı rolündeki.
PrimvarData<GfVec2f> readUvs(const UsdPrim& prim, const std::string& wanted, UsdTimeCode t) {
    PrimvarData<GfVec2f> uv;
    UsdGeomPrimvarsAPI api(prim);
    if (readPrimvar(api.GetPrimvar(TfToken(wanted)), uv, t)) return uv;
    if (wanted != "st" && readPrimvar(api.GetPrimvar(TfToken("st")), uv, t)) return uv;
    for (const UsdGeomPrimvar& pv : api.GetPrimvarsWithValues()) {
        const SdfValueTypeName type = pv.GetTypeName();
        if ((type == SdfValueTypeNames->TexCoord2fArray || type == SdfValueTypeNames->Float2Array) && readPrimvar(pv, uv, t))
            return uv;
    }
    return uv;
}

/// Newell normali (çokgenin alanıyla ağırlıklı, sarma yönüne göre).
Vec3f newell(const std::vector<Vec3f>& poly) {
    Vec3f n(0.0f);
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec3f& a = poly[i];
        const Vec3f& b = poly[(i + 1) % poly.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

/// Çokgeni üçgenler (yerel köşe indeksleri). Dışbükeyse yelpaze; değilse kulak kırpma (n ≤ 256).
void triangulate(const std::vector<Vec3f>& poly, std::vector<int>& tris) {
    tris.clear();
    const int n = static_cast<int>(poly.size());
    if (n < 3) return;
    auto fan = [&] {
        for (int k = 1; k + 1 < n; ++k) tris.insert(tris.end(), {0, k, k + 1});
    };
    if (n == 3) {
        fan();
        return;
    }
    const Vec3f nrm = newell(poly);
    if (nrm.lengthSquared() <= 0.0f || n > 256) {
        fan();
        return;
    }
    // Normalin baskın ekseni atılarak 2B'ye izdüşüm.
    const float ax = std::abs(nrm.x), ay = std::abs(nrm.y), az = std::abs(nrm.z);
    const int drop = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    const float sign = (drop == 0 ? nrm.x : drop == 1 ? nrm.y : nrm.z) >= 0.0f ? 1.0f : -1.0f;
    std::vector<Vec2f> p(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const Vec3f& v = poly[static_cast<size_t>(i)];
        p[static_cast<size_t>(i)] = drop == 0 ? Vec2f(v.y, v.z) : drop == 1 ? Vec2f(v.z, v.x) : Vec2f(v.x, v.y);
    }
    auto cross = [&](int a, int b, int c) {
        const Vec2f& A = p[static_cast<size_t>(a)];
        const Vec2f& B = p[static_cast<size_t>(b)];
        const Vec2f& C = p[static_cast<size_t>(c)];
        return sign * ((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x));
    };
    bool convex = true;
    for (int i = 0; i < n && convex; ++i)
        if (cross(i, (i + 1) % n, (i + 2) % n) < 0.0f) convex = false;
    if (convex) {
        fan();
        return;
    }
    std::vector<int> idx(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) idx[static_cast<size_t>(i)] = i;
    auto inside = [&](int a, int b, int c, int q) {
        const float c1 = cross(a, b, q), c2 = cross(b, c, q), c3 = cross(c, a, q);
        return c1 >= 0.0f && c2 >= 0.0f && c3 >= 0.0f;
    };
    int guard = n * n;
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const int m = static_cast<int>(idx.size());
        for (int i = 0; i < m; ++i) {
            const int a = idx[static_cast<size_t>((i + m - 1) % m)], b = idx[static_cast<size_t>(i)],
                      c = idx[static_cast<size_t>((i + 1) % m)];
            if (cross(a, b, c) <= 0.0f) continue;
            bool ear = true;
            for (int q : idx)
                if (q != a && q != b && q != c && inside(a, b, c, q)) {
                    ear = false;
                    break;
                }
            if (!ear) continue;
            tris.insert(tris.end(), {a, b, c});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) {
        tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
    } else {
        tris.clear(); // kendini kesen çokgen: yelpazeye dön
        fan();
    }
}

/// Mesh parçalarının malzemeleri (alt kümeler ve kümesiz yüzler).
struct PartSpec {
    std::string name;
    const MaterialInfo* mat = nullptr;
    std::vector<int> faces; ///< boşsa ve isRest: kalan yüzler
};

std::shared_ptr<Material> displayColorMaterial(const UsdPrim& prim, UsdTimeCode t) {
    PrimvarData<GfVec3f> dc;
    if (!readPrimvar(UsdGeomPrimvarsAPI(prim).GetPrimvar(UsdGeomTokens->primvarsDisplayColor), dc, t)) return nullptr;
    if (dc.interp != UsdGeomTokens->constant && dc.values.size() != 1) return nullptr;
    return std::make_shared<DisneyMaterial>(col(dc.values[0]), 0.0f, 0.5f, 0.5f);
}

std::string materialKey(const std::vector<PartSpec>& parts) {
    std::string k;
    for (const PartSpec& p : parts) k += "|" + (p.mat ? p.mat->name + std::to_string(reinterpret_cast<uintptr_t>(p.mat)) : "-");
    return k;
}

/// Önbellekteki parçaları düğüme ekler (örnekler aynı TriangleMesh'i ve malzemeyi paylaşır).
void addCachedParts(const std::vector<ImportedMesh>& parts, ImportedNode& node, Ctx& ctx) {
    for (const ImportedMesh& im : parts) {
        node.meshes.push_back(static_cast<int>(ctx.model.meshes.size()));
        ctx.model.meshes.push_back(im);
    }
}

std::string sourceKey(const UsdPrim& prim) {
    return (prim.IsInstanceProxy() ? prim.GetPrimInPrototype().GetPath() : prim.GetPath()).GetString();
}

void convertMesh(const UsdGeomMesh& mesh, ImportedNode& node, Ctx& ctx) {
    const UsdPrim prim = mesh.GetPrim();
    const std::string name = prim.GetName().GetString();
    // Malzeme dilimleri (GeomSubset, aile "materialBind").
    std::vector<PartSpec> parts;
    UsdShadeMaterialBindingAPI bindApi(prim);
    for (UsdGeomSubset& subset : bindApi.GetMaterialBindSubsets()) {
        PartSpec ps;
        ps.name = name + " · " + subset.GetPrim().GetName().GetString();
        ps.mat = convertMaterial(boundMaterial(subset.GetPrim(), ctx), ctx);
        VtIntArray idx;
        subset.GetIndicesAttr().Get(&idx, ctx.time);
        ps.faces.assign(idx.begin(), idx.end());
        parts.push_back(std::move(ps));
    }
    PartSpec rest;
    rest.name = name;
    rest.mat = convertMaterial(boundMaterial(prim, ctx), ctx);
    parts.push_back(rest);

    const std::string cacheKey = sourceKey(prim) + materialKey(parts);
    if (auto it = ctx.meshCache.find(cacheKey); it != ctx.meshCache.end()) {
        addCachedParts(it->second, node, ctx);
        return;
    }

    VtVec3fArray points;
    VtIntArray counts, indices, holes;
    mesh.GetPointsAttr().Get(&points, ctx.time);
    mesh.GetFaceVertexCountsAttr().Get(&counts, ctx.time);
    mesh.GetFaceVertexIndicesAttr().Get(&indices, ctx.time);
    mesh.GetHoleIndicesAttr().Get(&holes, ctx.time);
    if (points.empty() || counts.empty() || indices.empty()) return;
    TfToken orientation = UsdGeomTokens->rightHanded;
    mesh.GetOrientationAttr().Get(&orientation, ctx.time);
    const bool leftHanded = orientation == UsdGeomTokens->leftHanded;
    TfToken scheme = UsdGeomTokens->catmullClark;
    mesh.GetSubdivisionSchemeAttr().Get(&scheme, ctx.time);

    PrimvarData<GfVec3f> normals;
    if (!readPrimvar(UsdGeomPrimvarsAPI(prim).GetPrimvar(UsdGeomTokens->normals), normals, ctx.time)) {
        VtVec3fArray n;
        if (mesh.GetNormalsAttr().Get(&n, ctx.time) && !n.empty()) {
            normals.values = n;
            normals.interp = mesh.GetNormalsInterpolation();
        }
    }
    const bool subdiv = scheme != UsdGeomTokens->none;
    // Alt bölmeli yüzey (Catmull-Clark) kafesiyle okunur, normaller yumuşatılır. Normalsiz düz
    // yüzey (scheme none) Hydra'daki gibi yüzey başına (köşeli) normal alır.
    if (subdiv) {
        ++ctx.subdivMeshes;
        normals.values.clear();
    }
    const std::string wantedUv = rest.mat ? rest.mat->primvar : (parts.front().mat ? parts.front().mat->primvar : "st");
    const PrimvarData<GfVec2f> uvs = readUvs(prim, wantedUv, ctx.time);

    const size_t nFaces = counts.size();
    std::vector<int> faceToPart(nFaces, static_cast<int>(parts.size()) - 1);
    for (size_t pi = 0; pi + 1 < parts.size(); ++pi)
        for (int f : parts[pi].faces)
            if (f >= 0 && static_cast<size_t>(f) < nFaces && faceToPart[static_cast<size_t>(f)] == static_cast<int>(parts.size()) - 1)
                faceToPart[static_cast<size_t>(f)] = static_cast<int>(pi);
    for (int h : holes)
        if (h >= 0 && static_cast<size_t>(h) < nFaces) faceToPart[static_cast<size_t>(h)] = -1;

    std::vector<MeshBuilder> builders(parts.size());
    std::vector<Vec3f> poly;
    std::vector<size_t> corners;
    std::vector<int> tris;
    size_t corner = 0;
    for (size_t f = 0; f < nFaces; ++f) {
        const int cnt = counts[f];
        if (cnt < 0 || corner + static_cast<size_t>(cnt) > indices.size()) break;
        const size_t first = corner;
        corner += static_cast<size_t>(cnt);
        const int part = faceToPart[f];
        if (cnt < 3 || part < 0) continue;
        corners.clear();
        poly.clear();
        for (int k = 0; k < cnt; ++k) {
            // Sol el sarması: köşeler ters sırayla (motor sağ el kuralını kullanır).
            const size_t c = leftHanded ? first + static_cast<size_t>(cnt - 1 - k) : first + static_cast<size_t>(k);
            const int pi = indices[c];
            if (pi < 0 || static_cast<size_t>(pi) >= points.size()) {
                corners.clear();
                break;
            }
            corners.push_back(c);
            poly.push_back(vec(points[static_cast<size_t>(pi)]));
        }
        if (corners.size() < 3) continue;
        triangulate(poly, tris);
        Vec3f faceN(0.0f);
        if (!normals.valid() && !subdiv) {
            faceN = newell(poly);
            if (faceN.lengthSquared() > 0.0f) faceN = faceN.normalized();
        }
        const MaterialInfo* mi = parts[static_cast<size_t>(part)].mat;
        for (int local : tris) {
            const size_t c = corners[static_cast<size_t>(local)];
            const auto pi = static_cast<size_t>(indices[c]);
            Vec3f n = faceN;
            if (const GfVec3f* nv = normals.at(pi, c, f)) {
                n = vec(*nv);
                if (n.lengthSquared() > 0.0f) n = n.normalized();
            }
            Vec2f uv(0.0f);
            if (const GfVec2f* t = uvs.at(pi, c, f)) {
                Vec2f st((*t)[0], (*t)[1]);
                if (mi) st = mi->uv.apply(st);
                uv = Vec2f(st.x, 1.0f - st.y); // USD: t = 0 altta; motorun dokuları üstten başlar
            }
            builders[static_cast<size_t>(part)].addCorner(poly[static_cast<size_t>(local)], n, uv);
        }
    }

    std::vector<ImportedMesh> made;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        auto tm = builders[pi].build(nullptr);
        if (!tm) continue;
        ImportedMesh im;
        im.mesh = tm;
        im.material = parts[pi].mat ? parts[pi].mat->material : nullptr;
        if (!im.material) im.material = displayColorMaterial(prim, ctx.time);
        im.name = parts[pi].name;
        made.push_back(std::move(im));
    }
    ctx.meshCache.emplace(cacheKey, made);
    addCachedParts(made, node, ctx);
}

/// Basit şekil (sphere, cube, cylinder, cone, capsule, plane) → üçgen ağı. Eksen: "X", "Y", "Z".
struct ShapeMesh {
    std::vector<Vec3f> p, n;
    std::vector<Vec2f> uv;
    std::vector<uint32_t> idx;
};

/// Eksen kuralı: şekil Z ekseninde kurulur, sonra istenen eksene döndürülür.
Vec3f axisRemap(const Vec3f& v, char axis) {
    if (axis == 'X') return Vec3f(v.z, v.x, v.y);
    if (axis == 'Y') return Vec3f(v.y, v.z, v.x);
    return v;
}

/// Z ekseni etrafında dönen profil (r(t), z(t)) → yüzey (küre, silindir, koni, kapsül gövdesi).
void lathe(ShapeMesh& m, const std::vector<std::pair<float, float>>& profile, int segments, char axis) {
    const auto base = static_cast<uint32_t>(m.p.size());
    const int rows = static_cast<int>(profile.size());
    for (int i = 0; i < rows; ++i) {
        const float r = profile[static_cast<size_t>(i)].first, z = profile[static_cast<size_t>(i)].second;
        // Profil teğetinden normal (komşu noktalarla merkezi fark).
        const auto& pa = profile[static_cast<size_t>(std::max(0, i - 1))];
        const auto& pb = profile[static_cast<size_t>(std::min(rows - 1, i + 1))];
        // Profil yukarıdan aşağı gider: teğet (dr, dz) saat yönü tersine 90° → dışa bakan normal.
        const float dr = pb.first - pa.first, dz = pb.second - pa.second;
        float nr = -dz, nz = dr;
        const float len = std::sqrt(nr * nr + nz * nz);
        if (len > 0.0f) {
            nr /= len;
            nz /= len;
        }
        for (int j = 0; j <= segments; ++j) {
            const float a = TWO_PI * static_cast<float>(j) / static_cast<float>(segments);
            const float c = std::cos(a), s = std::sin(a);
            m.p.push_back(axisRemap(Vec3f(r * c, r * s, z), axis));
            m.n.push_back(axisRemap(Vec3f(nr * c, nr * s, nz), axis).normalized());
            m.uv.push_back(Vec2f(static_cast<float>(j) / static_cast<float>(segments),
                                 static_cast<float>(i) / static_cast<float>(rows - 1)));
        }
    }
    const auto stride = static_cast<uint32_t>(segments + 1);
    for (int i = 0; i + 1 < rows; ++i)
        for (int j = 0; j < segments; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j);
            const uint32_t b = a + stride;
            m.idx.insert(m.idx.end(), {a, b + 1, a + 1, a, b, b + 1});
        }
}

/// Düz disk kapağı (silindir, koni tabanı) z düzleminde, normal ±Z.
void cap(ShapeMesh& m, float r, float z, bool up, int segments, char axis) {
    const auto center = static_cast<uint32_t>(m.p.size());
    const Vec3f n = axisRemap(Vec3f(0, 0, up ? 1.0f : -1.0f), axis);
    m.p.push_back(axisRemap(Vec3f(0, 0, z), axis));
    m.n.push_back(n);
    m.uv.push_back(Vec2f(0.5f, 0.5f));
    for (int j = 0; j <= segments; ++j) {
        const float a = TWO_PI * static_cast<float>(j) / static_cast<float>(segments);
        m.p.push_back(axisRemap(Vec3f(r * std::cos(a), r * std::sin(a), z), axis));
        m.n.push_back(n);
        m.uv.push_back(Vec2f(0.5f + 0.5f * std::cos(a), 0.5f - 0.5f * std::sin(a)));
    }
    for (int j = 0; j < segments; ++j) {
        const uint32_t a = center + 1 + static_cast<uint32_t>(j);
        if (up) m.idx.insert(m.idx.end(), {center, a, a + 1});
        else m.idx.insert(m.idx.end(), {center, a + 1, a});
    }
}

char axisOf(const UsdAttribute& a, UsdTimeCode t) {
    TfToken axis = UsdGeomTokens->z;
    a.Get(&axis, t);
    return axis == UsdGeomTokens->x ? 'X' : axis == UsdGeomTokens->y ? 'Y' : 'Z';
}

template <typename T>
T attrOr(const UsdAttribute& a, T def, UsdTimeCode t) {
    T v;
    return a.Get(&v, t) ? v : def;
}

bool buildShape(const UsdPrim& prim, ShapeMesh& m, UsdTimeCode t) {
    constexpr int seg = 128;
    if (UsdGeomSphere s{prim}) {
        const auto r = static_cast<float>(attrOr<double>(s.GetRadiusAttr(), 1.0, t));
        std::vector<std::pair<float, float>> prof;
        for (int i = 0; i <= seg / 2; ++i) {
            const float th = PI * static_cast<float>(i) / static_cast<float>(seg / 2);
            prof.emplace_back(r * std::sin(th), r * std::cos(th));
        }
        lathe(m, prof, seg, 'Z');
        return true;
    }
    if (UsdGeomCube c{prim}) {
        const float h = 0.5f * static_cast<float>(attrOr<double>(c.GetSizeAttr(), 2.0, t));
        const Vec3f axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const Vec3f& n : axes) {
            const Vec3f u = std::abs(n.y) > 0.5f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0).cross(n).normalized();
            const Vec3f v = n.cross(u);
            const auto b = static_cast<uint32_t>(m.p.size());
            const Vec2f uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            const float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k) {
                m.p.push_back((n + u * su[k] + v * sv[k]) * h);
                m.n.push_back(n);
                m.uv.push_back(uvs[k]);
            }
            m.idx.insert(m.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        }
        return true;
    }
    if (UsdGeomCylinder c{prim}) {
        const auto r = static_cast<float>(attrOr<double>(c.GetRadiusAttr(), 1.0, t));
        const auto h = static_cast<float>(attrOr<double>(c.GetHeightAttr(), 2.0, t));
        const char ax = axisOf(c.GetAxisAttr(), t);
        // Yan yüzün normalleri radyal olsun diye kenarlar profilde ayrı satırlardır.
        lathe(m, {{r, 0.5f * h}, {r, 0.5f * h - 1e-4f * h}, {r, -0.5f * h + 1e-4f * h}, {r, -0.5f * h}}, seg, ax);
        cap(m, r, 0.5f * h, true, seg, ax);
        cap(m, r, -0.5f * h, false, seg, ax);
        return true;
    }
    if (UsdGeomCone c{prim}) {
        const auto r = static_cast<float>(attrOr<double>(c.GetRadiusAttr(), 1.0, t));
        const auto h = static_cast<float>(attrOr<double>(c.GetHeightAttr(), 2.0, t));
        const char ax = axisOf(c.GetAxisAttr(), t);
        std::vector<std::pair<float, float>> prof;
        for (int i = 0; i <= 8; ++i) {
            const float k = static_cast<float>(i) / 8.0f;
            prof.emplace_back(r * k, 0.5f * h - h * k);
        }
        lathe(m, prof, seg, ax);
        cap(m, r, -0.5f * h, false, seg, ax);
        return true;
    }
    if (UsdGeomCapsule c{prim}) {
        const auto r = static_cast<float>(attrOr<double>(c.GetRadiusAttr(), 0.5, t));
        const auto h = static_cast<float>(attrOr<double>(c.GetHeightAttr(), 1.0, t));
        const char ax = axisOf(c.GetAxisAttr(), t);
        std::vector<std::pair<float, float>> prof;
        for (int i = 0; i <= seg / 4; ++i) {
            const float th = 0.5f * PI * static_cast<float>(i) / static_cast<float>(seg / 4);
            prof.emplace_back(r * std::sin(th), 0.5f * h + r * std::cos(th));
        }
        for (int i = 0; i <= seg / 4; ++i) {
            const float th = 0.5f * PI + 0.5f * PI * static_cast<float>(i) / static_cast<float>(seg / 4);
            prof.emplace_back(r * std::sin(th), -0.5f * h + r * std::cos(th));
        }
        lathe(m, prof, seg, ax);
        return true;
    }
    if (UsdGeomPlane pl{prim}) {
        const auto w = static_cast<float>(attrOr<double>(pl.GetWidthAttr(), 2.0, t));
        const auto l = static_cast<float>(attrOr<double>(pl.GetLengthAttr(), 2.0, t));
        const char ax = axisOf(pl.GetAxisAttr(), t);
        // UsdGeomPlane: eksen normaldir. Genişlik Z ve Y ekseninde X'e, X ekseninde Z'ye; uzunluk Z ve
        // X ekseninde Y'ye, Y ekseninde Z'ye hizalanır. Köşeler u × v = n olacak sırada.
        Vec3f n(0, 0, 1), u(0.5f * w, 0, 0), v(0, 0.5f * l, 0);
        if (ax == 'Y') {
            n = Vec3f(0, 1, 0);
            u = Vec3f(0, 0, 0.5f * l);
            v = Vec3f(0.5f * w, 0, 0);
        } else if (ax == 'X') {
            n = Vec3f(1, 0, 0);
            u = Vec3f(0, 0.5f * l, 0);
            v = Vec3f(0, 0, 0.5f * w);
        }
        m.p = {-u - v, u - v, u + v, -u + v};
        m.n = {n, n, n, n};
        m.uv = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        m.idx = {0, 1, 2, 0, 2, 3};
        return true;
    }
    return false;
}

void convertShape(const UsdPrim& prim, ImportedNode& node, Ctx& ctx) {
    std::vector<PartSpec> parts{PartSpec{prim.GetName().GetString(), convertMaterial(boundMaterial(prim, ctx), ctx), {}}};
    const std::string cacheKey = sourceKey(prim) + materialKey(parts);
    if (auto it = ctx.meshCache.find(cacheKey); it != ctx.meshCache.end()) {
        addCachedParts(it->second, node, ctx);
        return;
    }
    ShapeMesh m;
    if (!buildShape(prim, m, ctx.time) || m.idx.empty()) return;
    const MaterialInfo* mi = parts[0].mat;
    if (mi && !mi->uv.identity())
        for (Vec2f& uv : m.uv) {
            const Vec2f st = mi->uv.apply(Vec2f(uv.x, 1.0f - uv.y));
            uv = Vec2f(st.x, 1.0f - st.y);
        }
    ImportedMesh im;
    im.mesh = std::make_shared<TriangleMesh>(m.p, m.n, m.uv, m.idx, nullptr);
    im.material = mi ? mi->material : nullptr;
    if (!im.material) im.material = displayColorMaterial(prim, ctx.time);
    im.name = parts[0].name;
    std::vector<ImportedMesh> made{im};
    ctx.meshCache.emplace(cacheKey, made);
    addCachedParts(made, node, ctx);
}

// ─────────────────────────────── Işıklar ───────────────────────────────

Color3f lightColor(const UsdLuxLightAPI& l, UsdTimeCode t) {
    Color3f c = col(attrOr<GfVec3f>(l.GetColorAttr(), GfVec3f(1.0f), t));
    if (attrOr<bool>(l.GetEnableColorTemperatureAttr(), false, t))
        c = c * col(UsdLuxBlackbodyTemperatureAsRgb(attrOr<float>(l.GetColorTemperatureAttr(), 6500.0f, t)));
    return c;
}

/// Taban parlaklık L = intensity · 2^exposure (nit).
float lightScalar(const UsdLuxLightAPI& l, UsdTimeCode t) {
    return attrOr<float>(l.GetIntensityAttr(), 1.0f, t) * std::exp2(attrOr<float>(l.GetExposureAttr(), 0.0f, t));
}

struct LightFrame {
    Vec3f origin, x, y, z; ///< motor (kökün ebeveyni) uzayında; x, y, z ölçekli eksenler (metre)
    Transform stageWorld;  ///< sahne uzayındaki dünya dönüşümü
};

LightFrame lightFrame(const UsdPrim& prim, Ctx& ctx) {
    const Transform w = toTransform(ctx.xfCache.GetLocalToWorldTransform(prim));
    const Transform p = ctx.rootFix * w;
    return {p.transformPoint(Vec3f(0.0f)), p.transformVector(Vec3f(1, 0, 0)), p.transformVector(Vec3f(0, 1, 0)),
            p.transformVector(Vec3f(0, 0, 1)), w};
}

/// Sahne birimiyle dünya uzayındaki eksen uzunluğu (normalize alanı için).
float stageLen(const Transform& w, const Vec3f& axis) { return w.transformVector(axis).length(); }

void addLight(const UsdPrim& prim, Ctx& ctx) {
    UsdLuxLightAPI api(prim);
    const UsdTimeCode t = ctx.time;
    const LightFrame f = lightFrame(prim, ctx);
    const bool normalize = attrOr<bool>(api.GetNormalizeAttr(), false, t);
    float L = lightScalar(api, t);
    ImportedLight l;
    l.name = prim.GetName().GetString();
    l.color = lightColor(api, t);
    l.position = f.origin;
    const Vec3f fwd = (-f.z).lengthSquared() > 0.0f ? (-f.z).normalized() : Vec3f(0, -1, 0);
    l.direction = fwd;
    l.aimDistance = 1.0f;
    UsdLuxShapingAPI shaping(prim);
    const float coneDeg = prim.HasAPI<UsdLuxShapingAPI>() ? attrOr<float>(shaping.GetShapingConeAngleAttr(), 90.0f, t) : 90.0f;
    const float softness = prim.HasAPI<UsdLuxShapingAPI>() ? attrOr<float>(shaping.GetShapingConeSoftnessAttr(), 0.0f, t) : 0.0f;

    if (UsdLuxRectLight r{prim}) {
        const float w = attrOr<float>(r.GetWidthAttr(), 1.0f, t), h = attrOr<float>(r.GetHeightAttr(), 1.0f, t);
        if (normalize) L /= std::max(1e-12f, w * stageLen(f.stageWorld, Vec3f(1, 0, 0)) * h * stageLen(f.stageWorld, Vec3f(0, 1, 0)));
        l.type = ImportedLight::Type::Area;
        l.width = w * f.x.length();
        l.height = h * f.y.length();
        // Motorun alan ışığı genişliği yataya koyar (up × n): USD'nin X ekseni dikeyse kenarlar yer değiştirir.
        Vec3f n = fwd;
        const Vec3f up = std::abs(n.y) > 0.95f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0);
        const Vec3f u = up.cross(n).normalized(), v = n.cross(u).normalized();
        const Vec3f xa = f.x.lengthSquared() > 0.0f ? f.x.normalized() : u;
        if (std::abs(xa.dot(v)) > std::abs(xa.dot(u))) std::swap(l.width, l.height);
        l.intensity = L;
    } else if (UsdLuxDiskLight d{prim}) {
        const float r = attrOr<float>(d.GetRadiusAttr(), 0.5f, t);
        if (normalize) L /= std::max(1e-12f, PI * r * r * stageLen(f.stageWorld, Vec3f(1, 0, 0)) * stageLen(f.stageWorld, Vec3f(0, 1, 0)));
        // Motorda disk alan ışığı yok: aynı alanlı kare (aynı güç ve yaklaşık aynı yumuşaklık).
        l.type = ImportedLight::Type::Area;
        const float side = std::sqrt(PI) * r;
        l.width = side * f.x.length();
        l.height = side * f.y.length();
        l.intensity = L;
        ++ctx.approxLights;
    } else if (UsdLuxSphereLight s{prim}) {
        const float r = attrOr<float>(s.GetRadiusAttr(), 0.5f, t);
        const float rs = r * avgScale(f.stageWorld);
        const float rm = r * (f.x.length() + f.y.length() + f.z.length()) / 3.0f;
        if (normalize && rs > 0.0f) L /= 4.0f * PI * rs * rs;
        // Küre ışığı motorda nokta ışık: uzaktan görülen yoğunluk I = L · π r² (W/sr).
        l.intensity = rm > 0.0f ? L * PI * rm * rm : (normalize ? L * 0.25f : L);
        if (rm > 0.0f && !attrOr<bool>(s.GetTreatAsPointAttr(), false, t)) ++ctx.approxLights;
        l.type = coneDeg < 89.9f ? ImportedLight::Type::Spot : ImportedLight::Type::Point;
    } else if (UsdLuxCylinderLight c{prim}) {
        const float len = attrOr<float>(c.GetLengthAttr(), 1.0f, t), r = attrOr<float>(c.GetRadiusAttr(), 0.5f, t);
        const float ls = len * stageLen(f.stageWorld, Vec3f(1, 0, 0)), rs = r * stageLen(f.stageWorld, Vec3f(0, 1, 0));
        if (normalize) L /= std::max(1e-12f, 2.0f * PI * rs * ls);
        // Yandan görülen izdüşüm alanı 2 r ℓ: nokta ışık yaklaşığı.
        l.type = ImportedLight::Type::Point;
        l.intensity = L * 2.0f * r * f.y.length() * len * f.x.length();
        ++ctx.approxLights;
    } else if (UsdLuxDistantLight dl{prim}) {
        const float angle = attrOr<float>(dl.GetAngleAttr(), 0.53f, t);
        const float thetaMax = std::clamp(0.5f * angle * DEG_TO_RAD, 0.0f, PI);
        // Işınım E: normalize açıkken intensity lüks cinsindendir; değilse L · güneş diskinin katı açısı.
        float size = 1.0f;
        if (thetaMax > 0.0f && !normalize) {
            const float s2 = std::sin(thetaMax) * std::sin(thetaMax);
            size = thetaMax <= 0.5f * PI ? s2 * PI : (2.0f - s2) * PI;
        }
        l.type = ImportedLight::Type::Directional;
        l.intensity = L * size;
    } else {
        return; // Dome ayrı; geometri ışığı (MeshLightAPI) malzemeye işlenir
    }
    if (l.type == ImportedLight::Type::Spot) {
        l.coneAngleDeg = std::clamp(2.0f * coneDeg, 1.0f, 179.0f);
        l.coneBlend = std::clamp(softness, 0.0f, 1.0f);
    }
    ctx.model.lights.push_back(l);
}

void addDome(const UsdPrim& prim, Ctx& ctx) {
    UsdLuxLightAPI api(prim);
    const UsdTimeCode t = ctx.time;
    const bool v1 = static_cast<bool>(UsdLuxDomeLight_1(prim));
    UsdAttribute fileAttr = v1 ? UsdLuxDomeLight_1(prim).GetTextureFileAttr() : UsdLuxDomeLight(prim).GetTextureFileAttr();
    UsdAttribute fmtAttr = v1 ? UsdLuxDomeLight_1(prim).GetTextureFormatAttr() : UsdLuxDomeLight(prim).GetTextureFormatAttr();
    const TfToken format = attrOr<TfToken>(fmtAttr, TfToken("automatic"), t);
    if (format != TfToken("automatic") && format != TfToken("latlong")) {
        ++ctx.unsupportedDome;
        return;
    }
    const Color3f tint = lightColor(api, t);
    std::optional<Image> src;
    std::string key, sourceFile;
    SdfAssetPath asset;
    if (fileAttr.Get(&asset, t) && !(asset.GetResolvedPath().empty() && asset.GetAssetPath().empty())) {
        const std::string resolved = asset.GetResolvedPath().empty() ? asset.GetAssetPath() : asset.GetResolvedPath();
        const std::string file = localTextureFile(resolved, ctx);
        if (file.empty()) return;
        sourceFile = file;
        src = isFloatImageFile(file) ? loadFloatImage(file) : loadImageLDR(file, TextureEncoding::SRGB);
        std::error_code ec;
        key = file + "|" + std::to_string(std::filesystem::file_size(pathFromUtf8(file), ec)) + "|" +
              std::to_string(std::filesystem::last_write_time(pathFromUtf8(file), ec).time_since_epoch().count());
    } else {
        src = Image(64, 32); // dokusuz kubbe: düz renk
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x) src->setPixel(x, y, Color3f(1.0f));
        key = "const";
    }
    if (!src || src->width() <= 0) {
        ++ctx.missingTextures;
        return;
    }
    Transform r = ctx.rootFix * toTransform(ctx.xfCache.GetLocalToWorldTransform(prim));
    if (v1) {
        TfToken pole = TfToken("scene");
        UsdLuxDomeLight_1(prim).GetPoleAxisAttr().Get(&pole, t);
        if (pole == TfToken("Z") || (pole == TfToken("scene") && ctx.zUp)) r = r * Transform::rotateX(0.5f * PI);
    }
    const Vec3f rx = r.transformVector(Vec3f(1, 0, 0)).normalized();
    const Vec3f ry = r.transformVector(Vec3f(0, 1, 0)).normalized();
    const Vec3f rz = r.transformVector(Vec3f(0, 0, 1)).normalized();
    if (ry.y < 0.999f) ++ctx.domeTilted;
    // USD kubbesi OpenEXR eşdikdörtgen kuralını izler (u = (π − atan2(x, z)) / 2π); motorunki
    // u = (atan2(−z, x) + π + dönüş) / 2π. İkisi birbirinin aynasıdır: görüntü yatayda çevrilir, kalan
    // fark Y ekseni etrafında bir dönüştür (dönüş = 90° − kubbenin sapma açısı). Kubbenin dönüşümü
    // kendisi aynalıysa (determinant < 0, ör. scale −1) iki ayna birbirini götürür: çevirme yapılmaz,
    // dönüş formülü aynı kalır (Ry(θ)·diag(−1, 1, 1) için de 90° − θ).
    const bool mirrored = rx.dot(ry.cross(rz)) < 0.0f;
    const bool flip = !mirrored;
    const float yaw = std::atan2(rz.x, rz.z) * RAD_TO_DEG;
    float intensity = lightScalar(api, t);
    // Gri ton şiddete katılır; renkli ton görüntüye işlenir.
    Color3f bake = tint;
    if (std::abs(tint.r - tint.g) < 1e-6f && std::abs(tint.g - tint.b) < 1e-6f) {
        intensity *= tint.r;
        bake = Color3f(1.0f);
    }
    const bool neutral = bake.r == 1.0f && bake.g == 1.0f && bake.b == 1.0f;
    std::string path;
    if (!flip && neutral && !sourceFile.empty() && isFloatImageFile(sourceFile)) {
        path = sourceFile; // dosya olduğu gibi kullanılır (önbelleğe kopya yok)
    } else {
        const int W = src->width(), H = src->height();
        Image out(W, H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) out.setPixel(x, y, src->getPixel(flip ? W - 1 - x : x, y) * bake);
        char tk[96];
        std::snprintf(tk, sizeof(tk), "|dome|%d|%g,%g,%g", flip ? 1 : 0, bake.r, bake.g, bake.b);
        path = cacheDerivedImage(out, key + tk);
    }
    if (path.empty()) return;

    ImportedEnvironment env;
    env.hdrPath = path;
    env.intensity = intensity;
    float rot = std::fmod(90.0f - yaw, 360.0f);
    if (rot < 0.0f) rot += 360.0f;
    env.rotationDeg = rot;
    ctx.model.environments.push_back(env);
}

/// Mesh'e uygulanmış ışık (MeshLightAPI): malzemenin emisyonu olur.
void applyMeshLight(const UsdPrim& prim, ImportedNode& node, size_t firstMesh, Ctx& ctx) {
    UsdLuxLightAPI api(prim);
    float L = lightScalar(api, ctx.time);
    const Color3f c = lightColor(api, ctx.time);
    if (attrOr<bool>(api.GetNormalizeAttr(), false, ctx.time)) {
        float area = 0.0f;
        const Transform w = toTransform(ctx.xfCache.GetLocalToWorldTransform(prim));
        for (size_t k = firstMesh; k < node.meshes.size(); ++k) {
            const TriangleMesh& m = *ctx.model.meshes[static_cast<size_t>(node.meshes[k])].mesh;
            for (size_t i = 0; i < m.numTriangles(); ++i) {
                Vec3f a, b, d;
                m.triangleVertices(i, a, b, d);
                area += 0.5f * (w.transformPoint(b) - w.transformPoint(a)).cross(w.transformPoint(d) - w.transformPoint(a)).length();
            }
        }
        if (area > 0.0f) L /= area;
    }
    for (size_t k = firstMesh; k < node.meshes.size(); ++k) {
        ImportedMesh& im = ctx.model.meshes[static_cast<size_t>(node.meshes[k])];
        auto d = std::make_shared<DisneyMaterial>(Color3f(0.0f), 0.0f, 1.0f, 0.0f);
        d->setEmission(c * L);
        im.material = d;
    }
}

// ─────────────────────────────── Kameralar ───────────────────────────────

void addCamera(const UsdPrim& prim, Ctx& ctx) {
    UsdGeomCamera cam(prim);
    ImportedCamera out;
    out.name = prim.GetName().GetString();
    const double tcps = ctx.stage->GetTimeCodesPerSecond() > 0 ? ctx.stage->GetTimeCodesPerSecond() : 24.0;
    out.fps = static_cast<float>(ctx.stage->GetFramesPerSecond() > 0 ? ctx.stage->GetFramesPerSecond() : tcps);
    // Kamera ya da atalarından biri zamanla değişiyorsa her kare örneklenir.
    bool animated = false;
    for (UsdPrim p = prim; p && !p.IsPseudoRoot(); p = p.GetParent())
        if (UsdGeomXformable(p).TransformMightBeTimeVarying()) animated = true;
    for (const UsdAttribute& a : {cam.GetFocalLengthAttr(), cam.GetVerticalApertureAttr(), cam.GetFocusDistanceAttr()})
        if (a.ValueMightBeTimeVarying()) animated = true;
    std::vector<double> times;
    const double start = ctx.stage->GetStartTimeCode(), end = ctx.stage->GetEndTimeCode();
    if (animated && ctx.stage->HasAuthoredTimeCodeRange() && end > start) {
        for (double tc = start; tc <= end + 1e-9 && static_cast<int>(times.size()) < kMaxCameraKeys; tc += 1.0)
            times.push_back(tc);
    } else {
        times.push_back(ctx.time.IsNumeric() ? ctx.time.GetValue() : start);
    }
    const float unit = avgScale(ctx.rootFix);
    for (double tc : times) {
        const GfCamera gc = cam.GetCamera(UsdTimeCode(tc));
        const Transform m = ctx.rootFix * toTransform(gc.GetTransform());
        ImportedCameraKey key;
        key.time = static_cast<float>((tc - times.front()) / tcps);
        key.eye = m.transformPoint(Vec3f(0.0f));
        Vec3f fwd = m.transformVector(Vec3f(0, 0, -1));
        fwd = fwd.lengthSquared() > 0.0f ? fwd.normalized() : Vec3f(0, 0, -1);
        const float focus = gc.GetFocusDistance() * unit;
        key.focusDistance = focus > 0.0f ? focus : 0.0f;
        key.fStop = gc.GetFStop() > 0.0f ? gc.GetFStop() : 0.0f;
        key.target = key.eye + fwd * (focus > 0.0f ? focus : 1.0f);
        key.fovDeg = gc.GetFieldOfView(GfCamera::FOVVertical);
        if (gc.GetProjection() == GfCamera::Orthographic) {
            out.orthographic = true;
            out.orthoHeight = gc.GetVerticalAperture() * static_cast<float>(GfCamera::APERTURE_UNIT) * unit;
        }
        out.keys.push_back(key);
    }
    ctx.model.cameras.push_back(std::move(out));
}

// ─────────────────────────────── Hiyerarşi ───────────────────────────────

/// Parçalarının kopyası (yeni indekslerle) ile alt ağaç: PointInstancer örnekleri için.
ImportedNode cloneTemplate(const ImportedNode& tmpl, const std::vector<ImportedMesh>& parts, Ctx& ctx) {
    ImportedNode n;
    n.name = tmpl.name;
    n.local = tmpl.local;
    for (int k : tmpl.meshes) {
        n.meshes.push_back(static_cast<int>(ctx.model.meshes.size()));
        ctx.model.meshes.push_back(parts[static_cast<size_t>(k)]);
    }
    for (const ImportedNode& c : tmpl.children) n.children.push_back(cloneTemplate(c, parts, ctx));
    return n;
}

size_t countParts(const ImportedNode& n) {
    size_t c = n.meshes.size();
    for (const ImportedNode& ch : n.children) c += countParts(ch);
    return c;
}

bool visitPrim(const UsdPrim& prim, ImportedNode& parent, const TfToken& parentPurpose, Ctx& ctx, bool identityLocal = false);

void convertInstancer(const UsdGeomPointInstancer& pi, ImportedNode& node, const TfToken& purpose, Ctx& ctx) {
    SdfPathVector protoPaths;
    pi.GetPrototypesRel().GetForwardedTargets(&protoPaths);
    VtIntArray protoIndices;
    pi.GetProtoIndicesAttr().Get(&protoIndices, ctx.time);
    VtArray<GfMatrix4d> xforms;
    if (protoPaths.empty() || protoIndices.empty() ||
        !pi.ComputeInstanceTransformsAtTime(&xforms, ctx.time, ctx.time))
        return;
    // Prototipler bir kez çevrilir; parçaları sahne listesinden çıkarılıp örnek başına kopyalanır.
    const size_t before = ctx.model.meshes.size();
    std::vector<ImportedNode> templates(protoPaths.size());
    for (size_t i = 0; i < protoPaths.size(); ++i) {
        const UsdPrim proto = ctx.stage->GetPrimAtPath(protoPaths[i]);
        if (proto) visitPrim(proto, templates[i], purpose, ctx, true);
    }
    std::vector<ImportedMesh> parts(ctx.model.meshes.begin() + static_cast<std::ptrdiff_t>(before), ctx.model.meshes.end());
    ctx.model.meshes.resize(before);
    std::function<void(ImportedNode&)> rebase = [&](ImportedNode& n) {
        for (int& k : n.meshes) k -= static_cast<int>(before);
        for (ImportedNode& c : n.children) rebase(c);
    };
    for (ImportedNode& t : templates) rebase(t);
    size_t made = 0;
    const std::string base = pi.GetPrim().GetName().GetString();
    for (size_t i = 0; i < protoIndices.size() && i < xforms.size(); ++i) {
        const int p = protoIndices[i];
        if (p < 0 || static_cast<size_t>(p) >= templates.size() || templates[static_cast<size_t>(p)].children.empty()) continue;
        const ImportedNode& tmpl = templates[static_cast<size_t>(p)].children.front();
        const size_t n = countParts(tmpl);
        if (made + n > kMaxInstanceParts) {
            ctx.skippedInstances += static_cast<int>(protoIndices.size() - i);
            break;
        }
        ImportedNode inst = cloneTemplate(tmpl, parts, ctx);
        inst.name = base + " " + std::to_string(i);
        inst.local = toTransform(xforms[i]);
        node.children.push_back(std::move(inst));
        made += n;
    }
}

/// Bir prim'i ve altını çevirir; içerik (mesh) varsa @p parent'a düğüm ekler ve true döner.
bool visitPrim(const UsdPrim& prim, ImportedNode& parent, const TfToken& parentPurpose, Ctx& ctx, bool identityLocal) {
    if (!prim.IsActive() || !prim.IsDefined() || prim.IsAbstract()) return false;
    UsdGeomImageable img(prim);
    TfToken purpose = parentPurpose;
    if (img) {
        TfToken vis;
        if (img.GetVisibilityAttr().Get(&vis, ctx.time) && vis == UsdGeomTokens->invisible) return false;
        TfToken p;
        if (img.GetPurposeAttr().HasAuthoredValue() && img.GetPurposeAttr().Get(&p, ctx.time)) purpose = p;
    }
    const bool renderable = purpose != UsdGeomTokens->guide && purpose != UsdGeomTokens->proxy;

    if (prim.IsA<UsdGeomCamera>()) {
        if (!identityLocal) addCamera(prim, ctx);
        return false;
    }
    if (prim.HasAPI<UsdLuxLightAPI>() && !prim.IsA<UsdGeomGprim>()) {
        if (!identityLocal) {
            if (prim.IsA<UsdLuxDomeLight>() || prim.IsA<UsdLuxDomeLight_1>()) addDome(prim, ctx);
            else addLight(prim, ctx);
        }
        return false;
    }

    ImportedNode node;
    node.name = prim.GetName().GetString();
    if (!identityLocal) {
        if (UsdGeomXformable xf{prim}) {
            GfMatrix4d m(1.0);
            bool resets = false;
            xf.GetLocalTransformation(&m, &resets, ctx.time);
            if (resets) {
                // Ebeveyn dönüşümünü yok sayan prim: dünya dönüşümü ebeveynin dünyasına göre ifade edilir.
                const GfMatrix4d parentWorld = ctx.xfCache.GetParentToWorldTransform(prim);
                m = ctx.xfCache.GetLocalToWorldTransform(prim) * parentWorld.GetInverse();
            }
            node.local = toTransform(m);
        }
    }
    bool content = false;
    if (renderable) {
        if (UsdGeomMesh mesh{prim}) {
            const size_t first = node.meshes.size();
            convertMesh(mesh, node, ctx);
            if (prim.HasAPI<UsdLuxMeshLightAPI>() || prim.HasAPI<UsdLuxLightAPI>()) applyMeshLight(prim, node, first, ctx);
        } else if (prim.IsA<UsdGeomGprim>()) {
            convertShape(prim, node, ctx);
        }
        content = !node.meshes.empty();
    }
    if (UsdGeomPointInstancer pi{prim}) {
        if (renderable) convertInstancer(pi, node, purpose, ctx);
    } else {
        for (const UsdPrim& child : prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimDefaultPredicate)))
            if (!child.IsA<UsdGeomSubset>()) visitPrim(child, node, purpose, ctx);
    }
    content = content || !node.children.empty();
    if (content) parent.children.push_back(std::move(node));
    return content;
}

void bounds(const ImportedNode& n, const Transform& parent, const ImportedModel& model, AABB& box) {
    const Transform w = parent * n.local;
    for (int k : n.meshes) {
        const TriangleMesh& m = *model.meshes[static_cast<size_t>(k)].mesh;
        const AABB b = m.bounds();
        for (int c = 0; c < 8; ++c)
            box.merge(w.transformPoint(Vec3f(c & 1 ? b.pMax.x : b.pMin.x, c & 2 ? b.pMax.y : b.pMin.y,
                                              c & 4 ? b.pMax.z : b.pMin.z)));
    }
    for (const ImportedNode& c : n.children) bounds(c, w, model, box);
}

std::once_flag gPluginsOnce;

/// Paketlenmiş uygulamada USD eklenti tanımları (plugInfo.json) exe'nin yanındaki usd/ klasöründedir;
/// geliştirme derlemesinde kitaplığın kendi kurulum yolundan bulunur.
void registerPlugins() {
    std::call_once(gPluginsOnce, [] {
        std::vector<std::string> paths;
        for (const char* sub : {"usd", "plugin/usd"}) {
            const std::filesystem::path p = executableDir() / sub;
            std::error_code ec;
            if (std::filesystem::exists(p / "plugInfo.json", ec)) paths.push_back(pathToUtf8(p));
        }
        if (!paths.empty()) PlugRegistry::GetInstance().RegisterPlugins(paths);
    });
}

void summarize(Ctx& ctx) {
    auto& w = ctx.model.warnings;
    if (ctx.subdivMeshes) w.push_back(std::to_string(ctx.subdivMeshes) + " alt bölmeli (Catmull-Clark) mesh kafesiyle okundu");
    if (ctx.unsupportedShaders) {
        std::string ids;
        for (const std::string& s : ctx.unsupportedIds) ids += (ids.empty() ? "" : ", ") + s;
        w.push_back(std::to_string(ctx.unsupportedShaders) + " malzemenin gölgelendiricisi tanınmadı (" + ids + "), gri kullanıldı");
    }
    if (ctx.unsupportedNodes) w.push_back(std::to_string(ctx.unsupportedNodes) + " doku düğümü desteklenmiyor (sabit değer kullanıldı)");
    if (ctx.missingTextures) w.push_back(std::to_string(ctx.missingTextures) + " doku dosyası bulunamadı");
    if (ctx.udimTextures) w.push_back(std::to_string(ctx.udimTextures) + " UDIM dokusu henüz desteklenmiyor");
    if (ctx.emissionTextures) w.push_back(std::to_string(ctx.emissionTextures) + " emisyon dokusu ortalama renge indirildi");
    if (ctx.uvConflicts) w.push_back(std::to_string(ctx.uvConflicts) + " dokunun UV dönüşümü malzemenin ilk dokusununkiyle değiştirildi");
    if (ctx.colorSpecular) w.push_back(std::to_string(ctx.colorSpecular) + " malzemenin renkli speküleri griye indirildi");
    if (ctx.approxLights) w.push_back(std::to_string(ctx.approxLights) + " ışık en yakın motor ışığıyla yaklaşıklandı (disk → kare, küre/silindir → nokta)");
    if (ctx.unsupportedDome) w.push_back("Kubbe ışığı biçimi desteklenmiyor (yalnız eşdikdörtgen)");
    if (ctx.domeTilted) w.push_back("Kubbe ışığının eğimi yok sayıldı (yalnız Y etrafında dönüş)");
    if (ctx.skippedInstances) w.push_back(std::to_string(ctx.skippedInstances) + " PointInstancer örneği sınırı aştığı için atlandı");
}

} // namespace

bool UsdLoader::available() { return true; }

ImportedModel UsdLoader::load(const std::string& path, std::string* error, float unitMeters) {
    registerPlugins();
    DiagnosticCollector diag;
    TfDiagnosticMgr::GetInstance().AddDelegate(&diag);
    TfErrorMark mark;
    Ctx ctx;
    ctx.stage = UsdStage::Open(path, UsdStage::LoadAll);
    auto finish = [&] {
        for (auto it = mark.GetBegin(); it != mark.GetEnd(); ++it) diag.add(it->GetCommentary());
        mark.Clear();
        TfDiagnosticMgr::GetInstance().RemoveDelegate(&diag);
    };
    if (!ctx.stage) {
        finish();
        if (error) *error = "USD sahnesi açılamadı: " + path + (diag.m_messages.empty() ? "" : " (" + diag.m_messages.front() + ")");
        return {};
    }
    const bool hasRange = ctx.stage->HasAuthoredTimeCodeRange();
    ctx.time = hasRange ? UsdTimeCode(ctx.stage->GetStartTimeCode()) : UsdTimeCode::EarliestTime();
    ctx.xfCache.SetTime(ctx.time);
    ctx.zUp = UsdGeomGetStageUpAxis(ctx.stage) == UsdGeomTokens->z;
    if (UsdGeomStageHasAuthoredMetersPerUnit(ctx.stage)) {
        ctx.metersPerUnit = UsdGeomGetStageMetersPerUnit(ctx.stage);
    } else if (unitMeters > 0.0f) {
        ctx.metersPerUnit = unitMeters;
    } else {
        ctx.metersPerUnit = 0.01;
        ctx.model.warnings.push_back("Sahne birimi yazılmamış: USD varsayılanı santimetre kullanıldı");
    }
    if (!(ctx.metersPerUnit > 0.0)) ctx.metersPerUnit = 1.0;
    ctx.rootFix = Transform::scale(Vec3f(static_cast<float>(ctx.metersPerUnit)));
    if (ctx.zUp) ctx.rootFix = Transform::rotateX(-0.5f * PI) * ctx.rootFix;

    try {
        ImportedNode top;
        for (const UsdPrim& child :
             ctx.stage->GetPseudoRoot().GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimDefaultPredicate)))
            visitPrim(child, top, UsdGeomTokens->default_, ctx);
        ctx.model.root.children = std::move(top.children);
    } catch (const std::exception& e) {
        finish();
        if (error) *error = std::string("USD okunamadı: ") + e.what();
        return {};
    }
    ctx.model.root.name = pathToUtf8(pathFromUtf8(path).stem());
    ctx.model.root.local = ctx.rootFix;

    // Odak uzaklığı yazılmamış kameraların hedefi sahnenin ortasına (yörünge kamerası için).
    AABB box = AABB::empty();
    bounds(ctx.model.root, Transform(), ctx.model, box);
    if (box.pMin.x <= box.pMax.x)
        for (ImportedCamera& c : ctx.model.cameras)
            for (ImportedCameraKey& k : c.keys)
                if (k.focusDistance <= 0.0f) {
                    const Vec3f fwd = (k.target - k.eye).normalized();
                    const float d = std::max(0.1f * box.diagonal().length(), (box.centroid() - k.eye).dot(fwd));
                    k.target = k.eye + fwd * d;
                }
    summarize(ctx);
    finish();
    if (diag.m_count > 0)
        ctx.model.warnings.push_back("USD: " + std::to_string(diag.m_count) + " uyarı (ilki: " + diag.m_messages.front() + ")");
    if (ctx.model.empty() && error) *error = "USD sahnesinde görünür mesh, kamera ya da ışık yok: " + path;
    return std::move(ctx.model);
}

} // namespace photon
