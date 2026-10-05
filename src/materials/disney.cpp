// Basitleştirilmiş Disney Principled BRDF (Burley 2012/2015): Burley difüz, GGX
// speküler (anizotrop olabilir), sheen, ince-yüzey difüz geçirgenlik ve enerjiyi
// koruyan bir clearcoat katmanı. Örnekleme Heitz 2018 görünür normalleri (VNDF) ile.

#include "materials/disney.h"
#include "materials/microfacet.h"
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

    float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    if (wiLocal.z <= 0.0f) {
        if (trans <= 0.0f) return Color3f::black();
        // İnce yaprak: alttan gelen ışık kaplamayı yalnızca wo tarafında bir kez geçer.
        return p.baseColor * (trans * INV_PI * coatO);
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    const float cosThetaI = wiLocal.z;
    const float cosThetaD = woLocal.dot(hLocal);

    // Burley difüz: F_D90 = 0.5 + 2·r·cos²θd ile kenarlarda geri-saçılma.
    float F90 = 0.5f + 2.0f * p.roughness * cosThetaD * cosThetaD;
    float Fo = 1.0f + (F90 - 1.0f) * pow5(1.0f - cosThetaO);
    float Fi = 1.0f + (F90 - 1.0f) * pow5(1.0f - cosThetaI);
    Color3f fDiffuse = p.baseColor * INV_PI * Fo * Fi * (1.0f - p.metallic) * (1.0f - trans);

    // GGX speküler. F0: dielektrikte 0.08·specular (Burley 2012; specular = 0.5 →
    // F0 = 0.04, yani IOR 1.5). Metalde baseColor. (materials-9: eskiden 0.04·specular.)
    float ax, ay;
    anisoAlpha(p.roughness, p.anisotropy, ax, ay);
    float D = ggxD(hLocal, ax, ay);
    float G = ggxG(woLocal, wiLocal, ax, ay);
    Color3f F0 = lerp(Color3f(0.08f * p.specular), p.baseColor, p.metallic);
    Color3f F = F0 + (Color3f(1.0f) - F0) * pow5(1.0f - cosThetaD);
    Color3f fSpecular = (F * D * G) / (4.0f * cosThetaO * cosThetaI);

    Color3f fSheen(0.0f);
    if (p.sheen > 0.0f) {
        // ponytail: sheen rides the diffuse sample; a dedicated sheen lobe if cloth noise matters
        float fh = pow5(std::max(0.0f, 1.0f - cosThetaD));
        float lum = p.baseColor.luminance();
        Color3f tint = lum > 1e-6f ? p.baseColor * (1.0f / lum) : Color3f(1.0f);
        Color3f csheen = Color3f(1.0f) * 0.5f + tint * 0.5f;
        fSheen = csheen * (p.sheen * fh * (1.0f - p.metallic));
    }

    Color3f base = fDiffuse + fSpecular + fSheen;
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
                 float& pDiffRefl, float& pTrans) {
    pCoat = 0.25f * p.clearCoat;
    pSpec = (1.0f - pCoat) * lerp(0.5f, 1.0f, p.metallic);
    float pDiff = 1.0f - pCoat - pSpec;
    float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    pTrans = pDiff * trans;
    pDiffRefl = pDiff - pTrans;
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
    float ax, ay;
    anisoAlpha(p.roughness, p.anisotropy, ax, ay);
    float pdfSpecular = ggxVisiblePdf(woLocal, hLocal, ax, ay) / (4.0f * cosThetaD);

    float pdfCoat = 0.0f;
    if (pCoat > 0.0f) {
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        pdfCoat = ggxVisiblePdf(woLocal, hLocal, alphaC, alphaC) / (4.0f * cosThetaD);
    }

    return pDiffRefl * pdfDiffuse + pSpec * pdfSpecular + pCoat * pdfCoat;
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
    p.normal = si.normal;
    p.tangent = si.tangent;

    if (m_albedoTex) {
        p.baseColor = m_albedoTex->sampleBilinear(si.uv.x, si.uv.y);
    }
    if (m_roughnessTex) {
        Color3f c = m_roughnessTex->sampleBilinear(si.uv.x, si.uv.y);
        // ponytail: use green channel (glTF ORM) with R fallback
        p.roughness = std::max(0.001f, c.g > 1e-6f ? c.g : c.r);
    }
    if (m_metalnessTex) {
        Color3f c = m_metalnessTex->sampleBilinear(si.uv.x, si.uv.y);
        p.metallic = std::clamp(c.b > 1e-6f ? c.b : c.r, 0.0f, 1.0f);
    }
    if (m_normalTex) {
        Color3f c = m_normalTex->sampleBilinear(si.uv.x, si.uv.y);
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

    float pCoat, pSpec, pDiffRefl, pTrans;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans);

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
        float ax, ay;
        anisoAlpha(p.roughness, p.anisotropy, ax, ay);
        Vec3f hLocal = ggxSampleVisibleNormal(woLocal, ax, ay, scaledX, sample.y);
        wiLocal = reflectVec(woLocal, hLocal);
        if (wiLocal.z <= 0.0f) return false;
    } else if (xi < pCoat + pSpec + pDiffRefl) {
        float scaledX = std::min((xi - pCoat - pSpec) / std::max(pDiffRefl, 1e-6f), 0.99999994f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
    } else {
        float scaledX = std::min((xi - pCoat - pSpec - pDiffRefl) / std::max(pTrans, 1e-6f), 0.99999994f);
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
