// gpu_types.h — CPU ↔ Vulkan Compute / RT / Wavefront shader std430/std140 ortak veri düzeni.
//
// Tüm yapılar 16 bayt (vec4) hizalıdır; pathtracer.comp ve wf_pathtracer.comp ile birebir
// aynı bayt düzenini paylaşır. static_assert'ler her yapının beklenen boyutunu derlemede doğrular.
#pragma once

#include <cstdint>

namespace photon::gpu {

struct alignas(16) Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

struct alignas(16) UVec4 {
    uint32_t x = 0, y = 0, z = 0, w = 0;
};

/// binding = 0 (UBO): Kare ve kamera parametreleri (hareket bulanıklığı çift pozu dahil).
struct alignas(16) GpuUniforms {
    UVec4 dims;          ///< x=width, y=height, z=firstSample, w=sampleCount
    UVec4 counts;        ///< x=primCount, y=bvhNodeCount, z=lightCount, w=maxBounces
    UVec4 envInfo;       ///< x=envWidth, y=envHeight, z=bgMode (0=Env,1=Color,2=Transp), w=envGroupIdx
    UVec4 flags;         ///< x=wantAovs, y=wantPasses, z=lightGroupCount, w=bitflags (0:MB,1:env,2:sun,3:camMB,4:secBvh,5:shadowLink,6:lightLink)
    Vec4 camPos_lens;    ///< xyz=position, w=lensRadius
    Vec4 camLL_ortho;    ///< xyz=lowerLeft, w=orthographic (0/1)
    Vec4 camHoriz_ao;    ///< xyz=horizontal, w=aoStrength
    Vec4 camVert_clamp;  ///< xyz=vertical, w=clampIndirect
    Vec4 camU_rot;       ///< xyz=u, w=envRotation
    Vec4 camV_int;       ///< xyz=v, w=envIntensity
    Vec4 camFwd_shadowQ; ///< xyz=forward, w=shadowQuality
    Vec4 bgColor_filter; ///< rgb=bgColor, w=filterWidth
    Vec4 sunDir_cosMax;  ///< xyz=sunDir, w=sunCosMax
    Vec4 sunRad_ftype;   ///< rgb=sunRadiance, w=filterType
    Vec4 camEndPos_lens; ///< xyz=endPosition, w=endLensRadius
    Vec4 camEndLL_sec;   ///< xyz=endLowerLeft, w=uintBitsToFloat(secBvhRoot)
    Vec4 camEndHoriz_p;  ///< xyz=endHorizontal, w=0
    Vec4 camEndVert_p;   ///< xyz=endVertical, w=0
    Vec4 camEndFwd_p;    ///< xyz=endForward, w=0
};
static_assert(sizeof(GpuUniforms) == 19 * 16, "GpuUniforms layout");

/// binding = 1 (SSBO): Üçgen ya da küre ilkeli.
/// e1_flags.w (uint):
///   bit 0: 0=triangle, 1=sphere
///   bit 1: cameraInvisible
///   bit 2: shadowCatcher
///   bit 3: hasMotion (p0, e1, e2 yerel uzayda; motionIdx ile t ∈ [0,1] dönüştürülür)
///   bit 8..12: visibilityMask (5 bit)
///   bit 16..23: linkSlot (0..63, Light/Shadow Linking 64-bit bitmask yuvası)
struct alignas(16) GpuPrim {
    Vec4 p0_mat;     ///< xyz=v0 (veya küre merkezi), w=uintBitsToFloat(matIdx)
    Vec4 e1_flags;   ///< xyz=v1-v0 (veya küre yarıçapı x'te), w=uintBitsToFloat(flags)
    Vec4 e2_meta;    ///< xyz=v2-v0, w=uintBitsToFloat((motionIdx+1)<<16 | (emitterIdx+1))
    Vec4 n0_u0;      ///< xyz=n0, w=uv0.x
    Vec4 n1_v0;      ///< xyz=n1, w=uv0.y
    Vec4 n2_u1;      ///< xyz=n2, w=uv1.x
    Vec4 ng_v1;      ///< xyz=geomNormal, w=uv1.y
    Vec4 uv2_crypto; ///< x=uv2.x, y=uv2.y, z=cryptoObjHash(float), w=cryptoMatHash(float)
    Vec4 objColor_p; ///< rgb=idColor(uid), w=uintBitsToFloat(matIdHash)
};
static_assert(sizeof(GpuPrim) == 9 * 16, "GpuPrim layout");

/// binding = 2 (SSBO): Düzleştirilmiş SAH BVH2 düğümü.
/// bmax_meta.w bit 31 = 1 → yaprak: firstPrim = bmin_left.w, primCount = bmax_meta.w & 0x7FFFFFFF.
/// bmax_meta.w bit 31 = 0 → iç düğüm: leftChild = bmin_left.w, rightChild = bmax_meta.w.
struct alignas(16) GpuBvhNode {
    Vec4 bmin_left;
    Vec4 bmax_meta;
};
static_assert(sizeof(GpuBvhNode) == 32, "GpuBvhNode layout");

/// binding = 3 (SSBO): Birleşik OpenPBR/Disney + Lambert + Dielectric + Mirror malzemesi.
struct alignas(16) GpuMaterial {
    Vec4 base_metallic;   ///< rgb=baseColor, w=metallic
    Vec4 rough_spec_ior_t;///< x=roughness, y=specular, z=ior, w=specularTransmission
    Vec4 coat_cr_ci_sheen;///< x=clearcoat, y=clearcoatRoughness, z=coatIor, w=sheen
    Vec4 coatTint_opacity;///< rgb=coatTint, w=opacity
    Vec4 sheenCol_type;   ///< rgb=sheenColor, w=uintBitsToFloat(type: 0=Disney,1=Lambert,2=Glass,3=Mirror)
    Vec4 emit_flags;      ///< rgb=emission, w=uintBitsToFloat(flags: 0:thinWalled,1:boxMap,2:transmitsShadow)
    Vec4 tex0;            ///< intBitsToFloat: x=albedo, y=rough, z=metal, w=normal (-1=yok)
    Vec4 tex1;            ///< x=intBitsToFloat(opacity), y=texScale, z=sheenTint, w=anisotropic
};
static_assert(sizeof(GpuMaterial) == 8 * 16, "GpuMaterial layout");

/// binding = 4 (SSBO): Işık kaynağı (Area, Point, Directional, Spot, Triangle).
struct alignas(16) GpuLight {
    Vec4 pos_type;   ///< xyz=pos (Directional: travelDir), w=uintBitsToFloat(type | (group<<8) | (lightIdx<<16))
    Vec4 u_area;     ///< xyz=u (Spot: axisDir), w=area
    Vec4 v_cosIn;    ///< xyz=v, w=cosInner
    Vec4 rad_cosOut; ///< rgb=radiance/intensity/irradiance, w=cosOuter
    Vec4 norm_power; ///< xyz=normal, w=power
};
static_assert(sizeof(GpuLight) == 5 * 16, "GpuLight layout");

/// binding = 5 (SSBO): Doku havuzu dizini (8-bit RGBA8 paketlenmiş pikseller binding 6'da).
struct alignas(16) GpuTexInfo {
    UVec4 info; ///< x=width, y=height, z=pixelOffset, w=isSrgb (0/1)
};
static_assert(sizeof(GpuTexInfo) == 16, "GpuTexInfo layout");

/// binding = 10 (SSBO): Işık başına O(1) 64-bit Light Linking + Shadow Linking maskeleri.
/// includeMask != 0 → yalnızca bit 1 olan yuvalar dahil.
/// excludeMask != 0 → bit 1 olan yuvalar hariç.
struct alignas(16) GpuLinkEntry {
    UVec4 lightMask;  ///< x=incLo, y=incHi, z=excLo, w=excHi
    UVec4 shadowMask; ///< x=sIncLo, y=sIncHi, z=sExcLo, w=sExcHi
};
static_assert(sizeof(GpuLinkEntry) == 32, "GpuLinkEntry layout");

/// binding = 11 (SSBO): Hareketli MeshInstance için t=0 ve t=1 TRS + SLERP kuaterniyon çifti.
struct alignas(16) GpuInstanceMotion {
    Vec4 pos0;   ///< xyz=t0 konum
    Vec4 quat0;  ///< xyzw=t0 birim kuaterniyon
    Vec4 scale0; ///< xyz=t0 ölçek
    Vec4 pos1;   ///< xyz=t1 konum
    Vec4 quat1;  ///< xyzw=t1 birim kuaterniyon (quat0 ile aynı yarımkürede)
    Vec4 scale1; ///< xyz=t1 ölçek
};
static_assert(sizeof(GpuInstanceMotion) == 6 * 16, "GpuInstanceMotion layout");

/// binding = 12 (SSBO): Piksel başına render geçişleri (29 × vec4):
///   [0..7]   LightPass 0..7
///   [8..23]  LightGroups 0..15 (LG_*)
///   [24]     objectId.rgb, matIdHash
///   [25..26] cryptoObject 4×(id, count) çifti
///   [27..28] cryptoMaterial 4×(id, count) çifti
constexpr uint32_t kGpuPassVec4Stride = 29u;

// ── Wavefront yürütme kuyrukları (bindings 14..18) ──

struct alignas(16) GpuPathState {
    Vec4 throughput_t;   ///< rgb=throughput, w=rayTime
    Vec4 radiance_pdf;   ///< rgb=radiance, w=lastBsdfPdf
    Vec4 lastPos_spec;   ///< xyz=lastPoint, w=uintBitsToFloat(flags: bit0=specBounce, bit1=camRay, bit2=inGlass, bits8..15=depth, bits16..23=diffBounces, bits24..31=prevLinkSlot)
    Vec4 lastNorm_rng;   ///< xyz=lastNormal, w=uintBitsToFloat(rngState)
    Vec4 firstLobe0;     ///< rgb=firstLobe[0], w=uintBitsToFloat(pixelIdx)
    Vec4 firstLobe1;     ///< rgb=firstLobe[1], w=uintBitsToFloat(sampleIdx)
    Vec4 firstLobe2;     ///< rgb=firstLobe[2], w=0
    Vec4 pad;
};
static_assert(sizeof(GpuPathState) == 128, "GpuPathState layout");

struct alignas(16) GpuRayWork {
    Vec4 origin_tMin; ///< xyz=origin, w=tMin
    Vec4 dir_tMax;    ///< xyz=dir, w=tMax
    UVec4 meta;       ///< x=pathIdx, y=visMask, z=floatBitsToUint(rayTime), w=0
};
static_assert(sizeof(GpuRayWork) == 48, "GpuRayWork layout");

struct alignas(16) GpuHitWork {
    Vec4 tuv_prim; ///< x=t, y=u, z=v, w=uintBitsToFloat(primIdx)
    UVec4 meta;    ///< x=pathIdx, y=hitFlag (0/1), z=0, w=0
};
static_assert(sizeof(GpuHitWork) == 32, "GpuHitWork layout");

struct alignas(16) GpuShadowWork {
    Vec4 origin_tMin;   ///< xyz=origin, w=tMin
    Vec4 dir_tMax;      ///< xyz=dir, w=tMax
    Vec4 contrib_group; ///< rgb=unoccludedContrib, w=uintBitsToFloat(lightGroup | (lightIdx<<8) | (lobeFlags<<24))
    UVec4 meta;         ///< x=pathIdx, y=isDepth0, z=floatBitsToUint(rayTime), w=0
};
static_assert(sizeof(GpuShadowWork) == 64, "GpuShadowWork layout");

} // namespace photon::gpu
