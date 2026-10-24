// Basitleştirilmiş Disney Principled BRDF (Burley 2012/2015): Burley difüz, GGX
// speküler (anizotrop olabilir), sheen, ince-yüzey difüz geçirgenlik ve enerjiyi
// koruyan bir clearcoat katmanı. Örnekleme Heitz 2018 görünür normalleri (VNDF) ile.
// Üretim eklemeleri: metal kenar tonu (F82), çoklu saçılma enerji telafisi (Turquin
// 2019), ince film girişimi, metalik boya pulları ve rastgele yürüyüş SSS lobu.

#include "materials/disney.h"
#include "materials/microfacet.h"
#include "materials/fresnel.h"
#include "core/image/image_io.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

namespace {

/// Clearcoat katmanı sabit IOR 1.5'tir (Burley): F0 = ((1.5-1)/(1.5+1))² = 0.04.
constexpr float COAT_F0 = 0.04f;

float schlick(float f0, float cosTheta) {
    return f0 + (1.0f - f0) * pow5(1.0f - std::clamp(cosTheta, 0.0f, 1.0f));
}

/// Burley'nin anizotropi eşlemesi: aspect = sqrt(1 - 0.9·aniso),
/// ax = r²/aspect, ay = r²·aspect. aniso = 0 → ax = ay = r² (izotrop GGX).
/// Alt sınır (1e-4) microfacet.h içinde uygulanır.
void anisoAlpha(float roughness, float anisotropy, float& ax, float& ay) {
    float aspect = std::sqrt(std::max(0.1f, 1.0f - 0.9f * std::clamp(anisotropy, 0.0f, 1.0f)));
    float a = roughness * roughness;
    ax = a / aspect;
    ay = a * aspect;
}

Frame shadingFrame(const Vec3f& nIn, const Vec3f& tangentIn) {
    Vec3f n = nIn.lengthSquared() > 0.0f ? nIn.normalized() : Vec3f(0, 1, 0);
    Vec3f t = tangentIn - n * tangentIn.dot(n);
    if (t.lengthSquared() < 1e-8f) return Frame(n);
    t = t.normalized();
    Vec3f b = n.cross(t);
    if (b.lengthSquared() < 1e-8f) return Frame(n);
    b = b.normalized();
    t = b.cross(n).normalized();
    return Frame(t, b, n);
}

/// Pulların mikro-yüzey pürüzlülüğü: her pul neredeyse ayna gibi parlak bir levhadır.
constexpr float FLAKE_ALPHA = 0.06f;

/// Taban speküler lobunun Fresnel'i (yalıtkan ve metal paylarının karışımı).
///  - İnce film yoksa: yalıtkan Schlick(0.08·specular), metal F82-tint(baseColor, edgeTint).
///  - İnce film varsa: Airy toplamı; yalıtkan taban n = (1+√F0)/(1−√F0), metal taban
///    Gulbrandsen'den karmaşık n + ik.
Color3f specularFresnel(const DisneyMaterial::ShadingParams& p, float cosD) {
    const float f0d = 0.08f * p.specular;
    if (p.thinFilmThickness <= 0.0f) {
        const Color3f fd(schlickF(f0d, cosD));
        if (p.metallic <= 0.0f) return fd;
        return lerp(fd, fresnelF82Tint(p.baseColor, p.edgeTint, cosD), p.metallic);
    }
    const float sd = std::sqrt(std::clamp(f0d, 0.0f, 0.99f));
    const cfloat nd((1.0f + sd) / (1.0f - sd), 0.0f);
    const cfloat dielectric[3] = {nd, nd, nd};
    Color3f fd = thinFilmRGB(cosD, p.thinFilmIor, dielectric, p.thinFilmThickness);
    if (p.metallic <= 0.0f) return fd;
    cfloat metal[3];
    for (int c = 0; c < 3; ++c) metal[c] = gulbrandsenIor(p.baseColor[c], p.edgeTint[c]);
    return lerp(fd, thinFilmRGB(cosD, p.thinFilmIor, metal, p.thinFilmThickness), p.metallic);
}

/// Çoklu saçılma telafisi (Turquin 2019): tek saçılmalı GGX, mikro-yüzeyler arasında
/// birden çok seken ışığı hesaba katmaz; pürüzlü metal olması gerekenden koyu çıkar.
/// Kaybolan oran 1 − E(μo); F0 kadarı geri eklenir:  f_ms = f_ss · (1 + F0·(1/E − 1)).
/// pdf değişmez (örnekleme yine VNDF), yalnız değer büyür; beyaz pürüzlü metal ≈ 1 yansıtır.
Color3f multiScatterFactor(const DisneyMaterial::ShadingParams& p, float cosO, float ax, float ay) {
    const float E = ggxDirectionalAlbedo(cosO, std::sqrt(ax * ay));
    const Color3f f0 = lerp(Color3f(0.08f * p.specular), p.baseColor, p.metallic);
    return Color3f(1.0f) + f0 * (1.0f / E - 1.0f);
}

/// Pul lobu: pulun kendi normali etrafında dar GGX, renkli Schlick (metal pul).
/// Kosinüsler pul çerçevesinde ölçülür (normal haritası gibi gölgelendirme normali).
Color3f evalFlake(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    const Frame ff(p.flakeNormal);
    const Vec3f o = ff.toLocal(wo), i = ff.toLocal(wi);
    if (o.z <= 0.0f || i.z <= 0.0f) return Color3f::black();
    const Vec3f h = (o + i).normalized();
    const float D = ggxD(h, FLAKE_ALPHA, FLAKE_ALPHA);
    const float G = ggxG(o, i, FLAKE_ALPHA, FLAKE_ALPHA);
    const float c = o.dot(h);
    Color3f F;
    for (int k = 0; k < 3; ++k) F[k] = schlickF(p.flakeColor[k], c);
    return F * (D * G / (4.0f * o.z * i.z));
}

float pdfFlake(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    const Frame ff(p.flakeNormal);
    const Vec3f o = ff.toLocal(wo), i = ff.toLocal(wi);
    if (o.z <= 0.0f || i.z <= 0.0f) return 0.0f;
    const Vec3f h = (o + i).normalized();
    const float c = o.dot(h);
    if (c <= 0.0f) return 0.0f;
    return ggxVisiblePdf(o, h, FLAKE_ALPHA, FLAKE_ALPHA) / (4.0f * c);
}

/// Tüm lobların toplamı f(wo, wi), yerel çerçevede.
///
/// Clearcoat (bulgu materials-10): kaplama, alttaki tabanın ÜSTÜNDE ince bir 1.5 IOR
/// katmanıdır. Işığın Fc kadarı kaplamadan yansır; tabana yalnızca (1 - Fc) girer ve
/// geri çıkarken yine (1 - Fc) ile zayıflar. Bu yüzden taban
///   (1 - cc·Fc(cosθo)) · (1 - cc·Fc(cosθi))
/// ile çarpılır. Eskiden kaplama lobu tabanın üstüne zayıflatmadan EKLENİYORDU ve
/// yatay açılarda (Fc → 1) toplam yansıma 1'i aşıyordu (enerji yaratıyordu).
Color3f evalLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f) return Color3f::black();

    const float cosThetaO = woLocal.z;
    const float coatO = 1.0f - p.clearCoat * schlick(COAT_F0, cosThetaO);

    float trans = p.inFlake ? 0.0f : std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    if (wiLocal.z <= 0.0f) {
        if (trans <= 0.0f) return Color3f::black();
        // İnce yaprak: alttan gelen ışık kaplamayı yalnızca wo tarafında bir kez geçer.
        return p.baseColor * (trans * INV_PI * coatO);
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    const float cosThetaI = wiLocal.z;
    const float cosThetaD = woLocal.dot(hLocal);

    Color3f base;
    if (p.inFlake) {
        // Pulun üstünde taban görünmez: ışık pul levhasından yansır.
        base = evalFlake(wo, wi, p);
    } else {

    // Burley difüz: F_D90 = 0.5 + 2·r·cos²θd ile kenarlarda geri-saçılma.
    float F90 = 0.5f + 2.0f * p.roughness * cosThetaD * cosThetaD;
    float Fo = 1.0f + (F90 - 1.0f) * pow5(1.0f - cosThetaO);
    float Fi = 1.0f + (F90 - 1.0f) * pow5(1.0f - cosThetaI);
    // Yüzey altı payı difüzden düşülür: o ışık giriş noktasında değil, yürüyüşün çıktığı
    // noktada (integratörde) görünür.
    Color3f fDiffuse = p.baseColor * INV_PI * Fo * Fi * (1.0f - p.metallic) * (1.0f - trans) * (1.0f - p.subsurface);

    // GGX speküler. F0: dielektrikte 0.08·specular (Burley 2012; specular = 0.5 →
    // F0 = 0.04, yani IOR 1.5). Metalde baseColor. (materials-9: eskiden 0.04·specular.)
    float ax, ay;
    anisoAlpha(p.roughness, p.anisotropy, ax, ay);
    float D = ggxD(hLocal, ax, ay);
    float G = ggxG(woLocal, wiLocal, ax, ay);
    Color3f F = specularFresnel(p, cosThetaD);
    Color3f fSpecular = (F * D * G) / (4.0f * cosThetaO * cosThetaI) * multiScatterFactor(p, cosThetaO, ax, ay);

    Color3f fSheen(0.0f);
    if (p.sheen > 0.0f) {
        // ponytail: sheen rides the diffuse sample; a dedicated sheen lobe if cloth noise matters
        float fh = pow5(std::max(0.0f, 1.0f - cosThetaD));
        float lum = p.baseColor.luminance();
        Color3f tint = lum > 1e-6f ? p.baseColor * (1.0f / lum) : Color3f(1.0f);
        Color3f csheen = Color3f(1.0f) * 0.5f + tint * 0.5f;
        fSheen = csheen * (p.sheen * fh * (1.0f - p.metallic));
    }

    base = fDiffuse + fSpecular + fSheen;
    }
    if (p.clearCoat <= 0.0f) return base;

    float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
    float Dc = ggxD(hLocal, alphaC, alphaC);
    float Gc = ggxG(woLocal, wiLocal, alphaC, alphaC);
    float Fc = schlick(COAT_F0, cosThetaD);
    Color3f fCoat(p.clearCoat * Fc * Dc * Gc / (4.0f * cosThetaO * cosThetaI));
    float coatI = 1.0f - p.clearCoat * schlick(COAT_F0, cosThetaI);
    return base * (coatO * coatI) + fCoat;
}

void lobeWeights(const DisneyMaterial::ShadingParams& p, float& pCoat, float& pSpec,
                 float& pDiffRefl, float& pTrans, float* pSssOut = nullptr) {
    pCoat = 0.25f * p.clearCoat;
    if (p.inFlake) {
        // Pulda yalnız kaplama ve pul lobu var; pSpec pulu temsil eder.
        pSpec = 1.0f - pCoat;
        pDiffRefl = pTrans = 0.0f;
        if (pSssOut) *pSssOut = 0.0f;
        return;
    }
    pSpec = (1.0f - pCoat) * lerp(0.5f, 1.0f, p.metallic);
    float pDiff = 1.0f - pCoat - pSpec;
    float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    pTrans = pDiff * trans;
    pDiffRefl = pDiff - pTrans;
    // Yüzey altı lobu yansıyan difüz payından ayrılır (metalde ve tam geçirgende 0).
    const float pSss = p.metallic < 1.0f ? pDiffRefl * p.subsurface : 0.0f;
    pDiffRefl -= pSss;
    if (pSssOut) *pSssOut = pSss;
}

/// sample() ile birebir aynı karışım pdf'i: Σ (lob seçme olasılığı × lob pdf'i).
/// GGX loblar VNDF ile örneklendiği için yansıyan wi'nin pdf'i
///   D_wo(h) / (4 wo·h) = G1(wo)·D(h) / (4 cosθo)      (Heitz 2018, denklem 17)
float pdfLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f) return 0.0f;

    float pCoat, pSpec, pDiffRefl, pTrans;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans);

    if (wiLocal.z <= 0.0f) {
        if (pTrans <= 0.0f) return 0.0f;
        return pTrans * cosineHemispherePdf(-wiLocal.z);
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    float cosThetaD = woLocal.dot(hLocal);
    if (cosThetaD <= 0.0f) return 0.0f;

    float pdfDiffuse = cosineHemispherePdf(wiLocal.z);
    float pdfSpecular;
    if (p.inFlake) {
        pdfSpecular = pdfFlake(wo, wi, p);
    } else {
        float ax, ay;
        anisoAlpha(p.roughness, p.anisotropy, ax, ay);
        pdfSpecular = ggxVisiblePdf(woLocal, hLocal, ax, ay) / (4.0f * cosThetaD);
    }

    float pdfCoat = 0.0f;
    if (pCoat > 0.0f) {
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        pdfCoat = ggxVisiblePdf(woLocal, hLocal, alphaC, alphaC) / (4.0f * cosThetaD);
    }

    return pDiffRefl * pdfDiffuse + pSpec * pdfSpecular + pCoat * pdfCoat;
}

/// Taban speküler lobundan (ya da pul lobundan) VNDF ile yön; sonuç makro yerel çerçevede.
bool sampleSpecularLobe(const DisneyMaterial::ShadingParams& p, const Frame& frame, const Vec3f& wo,
                        const Vec3f& woLocal, float u1, float u2, Vec3f& wiLocal) {
    if (p.inFlake) {
        const Frame ff(p.flakeNormal);
        const Vec3f o = ff.toLocal(wo);
        if (o.z <= 0.0f) return false;
        const Vec3f i = reflectVec(o, ggxSampleVisibleNormal(o, FLAKE_ALPHA, FLAKE_ALPHA, u1, u2));
        if (i.z <= 0.0f) return false;
        wiLocal = frame.toLocal(ff.toWorld(i));
    } else {
        float ax, ay;
        anisoAlpha(p.roughness, p.anisotropy, ax, ay);
        wiLocal = reflectVec(woLocal, ggxSampleVisibleNormal(woLocal, ax, ay, u1, u2));
    }
    return wiLocal.z > 0.0f;
}

/// Tamsayı karması (lowbias32) ve [0,1) dönüşümü: pul hücreleri için.
uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t hashCell(int x, int y, int z) {
    return hashU32(static_cast<uint32_t>(x) * 0x8da6b343u ^ hashU32(static_cast<uint32_t>(y) * 0xd8163841u ^
                   hashU32(static_cast<uint32_t>(z) * 0xcb1ab31fu)));
}

float unitFloat(uint32_t h) { return static_cast<float>(h >> 8) * 0x1p-24f; }

/// Metalik pullar: uzay pul boyutunda hücrelere bölünür, her hücrede rastgele bir
/// "çekirdek" (3B Voronoi). Nokta en yakın çekirdeğin pulunda sayılır; pulun var olup
/// olmadığı (yoğunluk) ve eğimi o çekirdeğin karmasından gelir. Eğim GGX eğim dağılımı
/// gibi: tan²θ = r²·u/(1−u); r = 0 → tüm pullar yüzeye paralel (yalnız kaplama gibi
/// parlar), r büyüdükçe her yönde ışıltı. ponytail: dünya uzayında; nesne hareket edince
/// pullar kayar (kutu eşlemeyle aynı sınırlama).
void resolveFlake(const Vec3f& point, float size, float density, float rough, DisneyMaterial::ShadingParams& p) {
    const Vec3f q = point / size;
    const int cx = static_cast<int>(std::floor(q.x));
    const int cy = static_cast<int>(std::floor(q.y));
    const int cz = static_cast<int>(std::floor(q.z));
    float best = 1e30f;
    uint32_t bestHash = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const uint32_t h = hashCell(cx + dx, cy + dy, cz + dz);
                const Vec3f f(static_cast<float>(cx + dx) + unitFloat(h),
                              static_cast<float>(cy + dy) + unitFloat(hashU32(h ^ 0x68e31da4u)),
                              static_cast<float>(cz + dz) + unitFloat(hashU32(h ^ 0xb5297a4du)));
                const float d2 = (q - f).lengthSquared();
                if (d2 < best) {
                    best = d2;
                    bestHash = h;
                }
            }
    if (unitFloat(hashU32(bestHash ^ 0x1b56c4e9u)) >= density) return;
    const float u1 = std::min(unitFloat(hashU32(bestHash ^ 0x2c1b3c6du)), 0.999f);
    const float u2 = unitFloat(hashU32(bestHash ^ 0x297a2d39u));
    const float tan2 = rough * rough * u1 / (1.0f - u1);
    const float cosT = 1.0f / std::sqrt(1.0f + tan2);
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const float phi = TWO_PI * u2;
    const Frame f(p.normal);
    p.inFlake = true;
    p.flakeNormal = f.toWorld(Vec3f(sinT * std::cos(phi), sinT * std::sin(phi), cosT)).normalized();
}

} // namespace

std::shared_ptr<Image> DisneyMaterial::loadMap(const std::string& path, TextureEncoding encoding) {
    if (path.empty()) return nullptr;
    if (auto ldr = loadImageLDR(path, encoding)) {
        return std::make_shared<Image>(std::move(*ldr));
    }
    if (auto hdr = loadImageHDR(path)) {
        return std::make_shared<Image>(std::move(*hdr));
    }
    if (auto exr = loadImageEXR(path)) {
        return std::make_shared<Image>(std::move(*exr));
    }
    return nullptr;
}

void DisneyMaterial::setAlbedoMap(const std::string& p) {
    m_albedoMap = p;
    m_albedoTex = loadMap(p, TextureEncoding::SRGB);
}

void DisneyMaterial::setNormalMap(const std::string& p) {
    m_normalMap = p;
    m_normalTex = loadMap(p, TextureEncoding::Linear);
}

void DisneyMaterial::setRoughnessMap(const std::string& p) {
    m_roughnessMap = p;
    m_roughnessTex = loadMap(p, TextureEncoding::Linear);
}

void DisneyMaterial::setMetalnessMap(const std::string& p) {
    m_metalnessMap = p;
    m_metalnessTex = loadMap(p, TextureEncoding::Linear);
}

// Doku koordinatı. Kutu eşleme (triplanar'ın en basit hali): geometrik normalin
// mutlak değerce en büyük bileşeni hangi eksense, nokta o eksene dik düzleme
// izdüşürülür (ör. yukarı bakan yüzeyde (x, z)). ponytail: dünya uzayında; nesne
// taşınınca doku kayar. Nesne uzayı için bake sırasında yerel konum saklanmalı.
Vec2f DisneyMaterial::textureCoord(const SurfaceInteraction& si) const {
    if (m_mapping == TextureMapping::UV) return si.uv * m_texScale;
    const Vec3f n = si.ng.lengthSquared() > 0.0f ? si.ng : si.normal;
    const float ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
    const Vec3f& p = si.point;
    Vec2f uv = (ax >= ay && ax >= az) ? Vec2f(p.z, p.y) : (ay >= az ? Vec2f(p.x, p.z) : Vec2f(p.x, p.y));
    return uv * m_texScale;
}

DisneyMaterial::ShadingParams DisneyMaterial::resolve(const SurfaceInteraction& si) const {
    ShadingParams p;
    p.baseColor = m_baseColor;
    p.metallic = m_metallic;
    p.roughness = m_roughness;
    p.specular = m_specular;
    p.clearCoat = m_clearCoat;
    p.clearCoatRoughness = m_clearCoatRoughness;
    p.anisotropy = m_anisotropy;
    p.sheen = m_sheen;
    p.diffuseTransmission = m_diffuseTransmission;
    p.subsurface = m_subsurface;
    p.normal = si.normal;
    p.tangent = si.tangent;
    const Vec2f uv = (m_albedoTex || m_roughnessTex || m_metalnessTex || m_normalTex) ? textureCoord(si) : si.uv;

    if (m_albedoTex) {
        p.baseColor = m_albedoTex->sampleBilinear(uv.x, uv.y);
    }
    if (m_roughnessTex) {
        Color3f c = m_roughnessTex->sampleBilinear(uv.x, uv.y);
        // ponytail: use green channel (glTF ORM) with R fallback
        p.roughness = std::max(0.001f, c.g > 1e-6f ? c.g : c.r);
    }
    if (m_metalnessTex) {
        Color3f c = m_metalnessTex->sampleBilinear(uv.x, uv.y);
        p.metallic = std::clamp(c.b > 1e-6f ? c.b : c.r, 0.0f, 1.0f);
    }
    if (m_normalTex) {
        Color3f c = m_normalTex->sampleBilinear(uv.x, uv.y);
        Vec3f nTs(c.r * 2.0f - 1.0f, c.g * 2.0f - 1.0f, c.b * 2.0f - 1.0f);
        Vec3f T = si.tangent;
        if (T.lengthSquared() < 1e-8f) {
            Frame f(si.normal);
            T = f.s;
        }
        Vec3f B = si.normal.cross(T);
        if (B.lengthSquared() < 1e-8f) {
            Frame f(si.normal);
            B = f.t;
            T = f.s;
        } else {
            B = B.normalized();
            T = B.cross(si.normal).normalized();
        }
        p.normal = (T * nTs.x + B * nTs.y + si.normal * nTs.z).normalized();
        if (p.normal.dot(si.normal) < 0.0f) p.normal = -p.normal;
    }
    p.edgeTint = m_edgeTint;
    p.thinFilmThickness = m_thinFilmThickness;
    p.thinFilmIor = m_thinFilmIor;
    p.flakeColor = m_flakeColor;
    if (m_flakeDensity > 0.0f) resolveFlake(si.point, m_flakeSize, m_flakeDensity, m_flakeRoughness, p);
    return p;
}

/// Lob seçimi sample.x'in [0,1) aralığını lob olasılıklarına böler ve seçilen aralığı
/// yeniden [0,1)'e germe ile kullanır (örnek yeniden kullanımı). GGX loblarında
/// mikro-normal görünür normallerden (VNDF) örneklenir: wo'dan görünmeyen h'ler hiç
/// üretilmez, bu yüzden eski D·cosθ örneklemesine göre daha az örnek boşa gider.
bool DisneyMaterial::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                            Vec3f& wi, Color3f& brdf, float& pdf) const {
    ShadingParams p = resolve(si);
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    float pCoat, pSpec, pDiffRefl, pTrans, pSss;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans, &pSss);

    Vec3f wiLocal;
    float xi = sample.x;

    if (pCoat > 0.0f && xi < pCoat) {
        float scaledX = std::min(xi / pCoat, 0.99999994f);
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        Vec3f hLocal = ggxSampleVisibleNormal(woLocal, alphaC, alphaC, scaledX, sample.y);
        wiLocal = reflectVec(woLocal, hLocal);
        if (wiLocal.z <= 0.0f) return false;
    } else if (xi < pCoat + pSpec) {
        float scaledX = std::min((xi - pCoat) / std::max(pSpec, 1e-6f), 0.99999994f);
        if (!sampleSpecularLobe(p, frame, wo, woLocal, scaledX, sample.y, wiLocal)) return false;
    } else if (xi < pCoat + pSpec + pDiffRefl) {
        float scaledX = std::min((xi - pCoat - pSpec) / std::max(pDiffRefl, 1e-6f), 0.99999994f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
    } else if (xi < pCoat + pSpec + pDiffRefl + pSss) {
        // Yüzey altı lobu rastgele yürüyüş ister; yalnız sampleFull'u çağıran integratör yapar.
        return false;
    } else {
        float scaledX = std::min((xi - pCoat - pSpec - pDiffRefl - pSss) / std::max(pTrans, 1e-6f), 0.99999994f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
        wiLocal.z = -wiLocal.z;
    }

    wi = frame.toWorld(wiLocal).normalized();
    brdf = evalLobes(wo, wi, p);
    pdf = pdfLobes(wo, wi, p);
    return pdf > 0.0f;
}

Color3f DisneyMaterial::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return evalLobes(wo, wi, resolve(si));
}

float DisneyMaterial::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return pdfLobes(wo, wi, resolve(si));
}

} // namespace photon

namespace photon {

// NEE için: parametreler (dokular dahil) bir kez çözülür, eval ve pdf ikisi de kullanır.
Color3f DisneyMaterial::evalPdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si,
                                float& pdfOut) const {
    const ShadingParams p = resolve(si);
    pdfOut = pdfLobes(wo, wi, p);
    return evalLobes(wo, wi, p);
}

} // namespace photon

namespace photon {

// Örnekleme sırası sample() ile aynı, ama lob uc ile seçilir ve yön u'nun iki boyutunu da
// kullanır (iki karar birbirinden bağımsız boyutlardan gelir). Ek olarak yüzey altı lobu:
// ışık kosinüs ağırlıklı olarak yüzeyin İÇİNE girer; renk ve çıkış yerini integratördeki
// rastgele yürüyüş belirler. Giriş ağırlığı = ss·(1 − metal)·(1 − geçirgenlik)·kaplamaGeçişi
// / seçimOlasılığı (beklenen değer lobun enerji payı).
bool DisneyMaterial::sampleFull(const Vec3f& wo, const SurfaceInteraction& si, float uc, const Vec2f& u,
                                BSDFSample& out) const {
    out.delta = false;
    out.subsurface = false;
    const ShadingParams p = resolve(si);
    const Frame frame = shadingFrame(p.normal, p.tangent);
    const Vec3f woLocal = frame.toLocal(wo);
    if (woLocal.z <= 0.0f) return false;

    float pCoat, pSpec, pDiffRefl, pTrans, pSss;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans, &pSss);

    Vec3f wiLocal;
    if (uc < pCoat) {
        const float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        wiLocal = reflectVec(woLocal, ggxSampleVisibleNormal(woLocal, alphaC, alphaC, u.x, u.y));
        if (wiLocal.z <= 0.0f) return false;
    } else if (uc < pCoat + pSpec) {
        if (!sampleSpecularLobe(p, frame, wo, woLocal, u.x, u.y, wiLocal)) return false;
    } else if (uc < pCoat + pSpec + pDiffRefl) {
        wiLocal = cosineSampleHemisphere(u);
    } else if (uc < pCoat + pSpec + pDiffRefl + pSss) {
        wiLocal = cosineSampleHemisphere(u);
        wiLocal.z = -wiLocal.z;
        const float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
        const float coatO = 1.0f - p.clearCoat * schlick(COAT_F0, woLocal.z);
        const float w = p.subsurface * (1.0f - p.metallic) * (1.0f - trans) * coatO / std::max(pSss, 1e-6f);
        out.wi = frame.toWorld(wiLocal).normalized();
        out.subsurface = true;
        out.weight = Color3f(w);
        out.pdf = pSss * cosineHemispherePdf(-wiLocal.z);
        out.f = out.weight * out.pdf / std::max(-wiLocal.z, 1e-6f);
        return out.pdf > 0.0f;
    } else {
        wiLocal = cosineSampleHemisphere(u);
        wiLocal.z = -wiLocal.z;
    }
    out.wi = frame.toWorld(wiLocal).normalized();
    out.f = evalLobes(wo, out.wi, p);
    out.pdf = pdfLobes(wo, out.wi, p);
    return out.pdf > 0.0f;
}

// Chiang, Kutz, Burley 2016 "Practical and Controllable Subsurface Scattering for
// Production Path Tracing" (Cycles'ın da kullandığı ters çevirme): istenen çoklu saçılma
// rengi A'yı verecek tek saçılma albedosu α ve sönüm σt. Yarıçap d = ortalama serbest yol.
//   α = 1 − exp(A·(−5.09406 + A·(2.61188 − 4.31805·A)))
//   s = 1.9 − A + 3.5·(A − 0.8)²,   σt = 1 / (d·s)
// Anizotropi (benzerlik teorisi): indirgenmiş saçılma σs' = σs·(1 − g) korunur ki g
// değişince cismin genel rengi/yumuşaklığı aynı kalsın: σs = σs'/(1 − g).
bool DisneyMaterial::subsurfaceCoeffs(const SurfaceInteraction& si, SubsurfaceCoeffs& out) const {
    if (m_subsurface <= 0.0f) return false;
    const ShadingParams p = resolve(si);
    const float g = m_subsurfaceAnisotropy;
    for (int c = 0; c < 3; ++c) {
        const float A = std::clamp(p.baseColor[c], 0.0f, 0.999f);
        const float d = std::max(m_subsurfaceRadius[c] * m_subsurfaceScale, 1e-7f);
        const float alpha = 1.0f - std::exp(A * (-5.09406f + A * (2.61188f - 4.31805f * A)));
        const float sVal = 1.9f - A + 3.5f * (A - 0.8f) * (A - 0.8f);
        float sigmaT = 1.0f / std::max(d * sVal, 1e-12f);
        float sigmaS = alpha * sigmaT;
        const float sigmaA = sigmaT - sigmaS;
        sigmaS /= (1.0f - g);
        sigmaT = sigmaS + sigmaA;
        out.sigmaT[c] = sigmaT;
        out.albedo[c] = sigmaT > 0.0f ? sigmaS / sigmaT : 0.0f;
    }
    out.g = g;
    return true;
}

} // namespace photon
