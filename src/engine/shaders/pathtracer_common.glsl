// pathtracer_common.glsl — Megakernel (Vulkan 1.1 Compute & VK_KHR_ray_query RT) ve Wavefront
// çekirdeklerinin ortak fizik, matematik, BVH2/RT kesişim, OpenPBR/Disney, Işık/Gölge Bağlama,
// Hareket Bulanıklığı ve Light Groups / Cryptomatte kütüphanesi.

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float INV_PI = 0.31830988618;
const float INV_FOUR_PI = 0.07957747155;
const float INF_T = 1e30;

const uint VIS_CAMERA       = 1u << 0;
const uint VIS_DIFFUSE      = 1u << 1;
const uint VIS_GLOSSY       = 1u << 2;
const uint VIS_TRANSMISSION = 1u << 3;
const uint VIS_SHADOW       = 1u << 4;
const uint VIS_ALL          = 0x1Fu;

const uint PASS_STRIDE = 29u;

struct GpuPrim {
    vec4 p0_mat;
    vec4 e1_flags;
    vec4 e2_meta;
    vec4 n0_u0;
    vec4 n1_v0;
    vec4 n2_u1;
    vec4 ng_v1;
    vec4 uv2_crypto;
    vec4 objColor_p;
};

struct GpuBvhNode {
    vec4 bmin_left;
    vec4 bmax_meta;
};

struct GpuMaterial {
    vec4 base_metallic;
    vec4 rough_spec_ior_t;
    vec4 coat_cr_ci_sheen;
    vec4 coatTint_opacity;
    vec4 sheenCol_type;
    vec4 emit_flags;
    vec4 tex0;
    vec4 tex1;
};

struct GpuLight {
    vec4 pos_type;
    vec4 u_area;
    vec4 v_cosIn;
    vec4 rad_cosOut;
    vec4 norm_power;
};

struct GpuTexInfo {
    uvec4 info; // x=w, y=h, z=offset, w=isSrgb
};

struct GpuLinkEntry {
    uvec4 lightMask;  // xy=inc64, zw=exc64
    uvec4 shadowMask; // xy=sInc64, zw=sExc64
};

struct GpuInstanceMotion {
    vec4 pos0;
    vec4 quat0;
    vec4 scale0;
    vec4 pos1;
    vec4 quat1;
    vec4 scale1;
};

layout(set = 0, binding = 0, std140) uniform UniformBlock {
    uvec4 dims;          // x=w, y=h, z=firstSample, w=sampleCount
    uvec4 counts;        // x=primCount, y=bvhNodeCount, z=lightCount, w=maxBounces
    uvec4 envInfo;       // x=envW, y=envH, z=bgMode, w=envGroupIdx
    uvec4 flags;         // x=wantAovs, y=wantPasses, z=lightGroupCount, w=bitflags
    vec4 camPos_lens;
    vec4 camLL_ortho;
    vec4 camHoriz_ao;
    vec4 camVert_clamp;
    vec4 camU_rot;
    vec4 camV_int;
    vec4 camFwd_shadowQ;
    vec4 bgColor_filter;
    vec4 sunDir_cosMax;
    vec4 sunRad_ftype;
    vec4 camEndPos_lens;
    vec4 camEndLL_sec;
    vec4 camEndHoriz_p;
    vec4 camEndVert_p;
    vec4 camEndFwd_p;
} u;

layout(set = 0, binding = 1, std430) readonly buffer PrimBuf { GpuPrim prims[]; };
layout(set = 0, binding = 2, std430) readonly buffer BvhBuf { GpuBvhNode bvhNodes[]; };
layout(set = 0, binding = 3, std430) readonly buffer MatBuf { GpuMaterial materials[]; };
layout(set = 0, binding = 4, std430) readonly buffer LightBuf { GpuLight lights[]; };
layout(set = 0, binding = 5, std430) readonly buffer TexInfoBuf { GpuTexInfo texInfos[]; };
layout(set = 0, binding = 6, std430) readonly buffer TexPixBuf { uint texPixels[]; };
layout(set = 0, binding = 7, std430) readonly buffer EnvPixBuf { vec4 envPixels[]; };
layout(set = 0, binding = 8, std430) buffer OutBeautyBuf { vec4 outBeauty[]; };
layout(set = 0, binding = 9, std430) buffer OutAovBuf { vec4 outAovs[]; };
layout(set = 0, binding = 10, std430) readonly buffer LinkBuf { GpuLinkEntry linkTable[]; };
layout(set = 0, binding = 11, std430) readonly buffer MotionBuf { GpuInstanceMotion motionTable[]; };
layout(set = 0, binding = 12, std430) buffer OutPassBuf { vec4 outPasses[]; };

#ifdef PHOTON_USE_RAY_QUERY
layout(set = 0, binding = 13) uniform accelerationStructureEXT topLevelAS;
#endif

// ── PCG / hash RNG (deterministik, piksel + örnek başına bağımsız) ──
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint initRng(uint px, uint py, uint sampleIdx) {
    return pcgHash(px + pcgHash(py + pcgHash(sampleIdx * 9781u + 12345u)));
}

float rand1(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    uint r = (word >> 22u) ^ word;
    return float(r & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

vec2 rand2(inout uint state) {
    return vec2(rand1(state), rand1(state));
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

bool isFiniteVec3(vec3 v) {
    return !(any(isnan(v)) || any(isinf(v)));
}

float powerHeuristic(float nf, float fPdf, float ng, float gPdf) {
    float f = nf * fPdf;
    float g = ng * gPdf;
    float denom = f * f + g * g;
    return denom > 0.0 ? (f * f) / denom : 0.0;
}

// ── Çerçeve (ONB, Frisvad / Duff) ──
void makeFrame(vec3 n, out vec3 s, out vec3 t) {
    float sg = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sg + n.z);
    float b = n.x * n.y * a;
    s = vec3(1.0 + sg * n.x * n.x * a, sg * b, -sg * n.x);
    t = vec3(b, sg + n.y * n.y * a, -n.y);
}

vec3 toLocal(vec3 v, vec3 s, vec3 t, vec3 n) {
    return vec3(dot(v, s), dot(v, t), dot(v, n));
}

vec3 toWorld(vec3 v, vec3 s, vec3 t, vec3 n) {
    return s * v.x + t * v.y + n * v.z;
}

vec2 sampleDisk(vec2 u) {
    float r = sqrt(max(u.x, 0.0));
    float theta = TWO_PI * u.y;
    return vec2(r * cos(theta), r * sin(theta));
}

vec3 cosineSampleHemisphere(vec2 u) {
    vec2 d = sampleDisk(u);
    float z = sqrt(max(0.0, 1.0 - dot(d, d)));
    return vec3(d.x, d.y, z);
}

// ── Kuaterniyon SLERP + TRS hareket enterpolasyonu ──
vec4 quatSlerp(vec4 a, vec4 b, float t) {
    float d = dot(a, b);
    if (d < 0.0) { b = -b; d = -d; }
    if (d > 0.9995) return normalize(mix(a, b, t));
    float theta = acos(clamp(d, -1.0, 1.0));
    float s = sin(theta);
    return normalize((sin((1.0 - t) * theta) * a + sin(t * theta) * b) / max(s, 1e-6));
}

vec3 quatRotate(vec4 q, vec3 v) {
    vec3 uq = q.xyz;
    float s = q.w;
    return 2.0 * dot(uq, v) * uq + (s * s - dot(uq, uq)) * v + 2.0 * s * cross(uq, v);
}

void evalMotionPointNormal(uint motionIdx, float t, vec3 lp0, vec3 le1, vec3 le2,
                           out vec3 wp0, out vec3 we1, out vec3 we2, out vec4 qOut, out vec3 scaleOut) {
    GpuInstanceMotion m = motionTable[motionIdx];
    vec3 pos = mix(m.pos0.xyz, m.pos1.xyz, t);
    vec4 q = quatSlerp(m.quat0, m.quat1, t);
    vec3 sc = mix(m.scale0.xyz, m.scale1.xyz, t);
    wp0 = quatRotate(q, lp0 * sc) + pos;
    vec3 wp1 = quatRotate(q, (lp0 + le1) * sc) + pos;
    vec3 wp2 = quatRotate(q, (lp0 + le2) * sc) + pos;
    we1 = wp1 - wp0;
    we2 = wp2 - wp0;
    qOut = q;
    scaleOut = sc;
}

vec3 motionTransformNormal(vec4 q, vec3 sc, vec3 ln) {
    vec3 invS = vec3(
        abs(sc.x) > 1e-8 ? 1.0 / sc.x : 0.0,
        abs(sc.y) > 1e-8 ? 1.0 / sc.y : 0.0,
        abs(sc.z) > 1e-8 ? 1.0 / sc.z : 0.0
    );
    return normalize(quatRotate(q, ln * invS));
}

// ── Doku & Ortam Haritası Örneklemesi ──
float srgbToLinear(float c) {
    return c <= 0.04045 ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
}

vec4 decodeTexel(uint packed, bool isSrgb) {
    vec4 c = vec4(
        float(packed & 0xFFu),
        float((packed >> 8u) & 0xFFu),
        float((packed >> 16u) & 0xFFu),
        float((packed >> 24u) & 0xFFu)
    ) * (1.0 / 255.0);
    if (isSrgb) {
        c.r = srgbToLinear(c.r);
        c.g = srgbToLinear(c.g);
        c.b = srgbToLinear(c.b);
    }
    return c;
}

vec4 sampleTexture(int texId, vec2 uv) {
    if (texId < 0) return vec4(1.0);
    uvec4 info = texInfos[texId].info;
    uint w = info.x;
    uint h = info.y;
    uint off = info.z;
    bool isSrgb = info.w != 0u;
    if (w == 0u || h == 0u) return vec4(1.0);
    float fu = fract(uv.x) * float(w) - 0.5;
    float fv = fract(uv.y) * float(h) - 0.5;
    int x0 = int(floor(fu));
    int y0 = int(floor(fv));
    float fx = fu - float(x0);
    float fy = fv - float(y0);
    int iw = int(w);
    int ih = int(h);
    uint ux0 = uint(((x0 % iw) + iw) % iw);
    uint uy0 = uint(((y0 % ih) + ih) % ih);
    uint ux1 = (ux0 + 1u) % w;
    uint uy1 = (uy0 + 1u) % h;
    vec4 c00 = decodeTexel(texPixels[off + uy0 * w + ux0], isSrgb);
    vec4 c10 = decodeTexel(texPixels[off + uy0 * w + ux1], isSrgb);
    vec4 c01 = decodeTexel(texPixels[off + uy1 * w + ux0], isSrgb);
    vec4 c11 = decodeTexel(texPixels[off + uy1 * w + ux1], isSrgb);
    return mix(mix(c00, c10, fx), mix(c01, c11, fx), fy);
}

vec3 evalEnvironment(vec3 dir) {
    if ((u.flags.w & 2u) == 0u) return vec3(0.0);
    vec3 d = normalize(dir);
    vec3 col = vec3(0.5 * u.camV_int.w);
    uint ew = u.envInfo.x;
    uint eh = u.envInfo.y;
    if (ew > 0u && eh > 0u) {
        float theta = acos(clamp(d.y, -1.0, 1.0));
        float phi = atan(-d.z, d.x) + PI + u.camU_rot.w;
        float fu = fract(phi / TWO_PI) * float(ew) - 0.5;
        float fv = clamp(theta / PI, 0.0, 1.0) * float(eh) - 0.5;
        int x0 = int(floor(fu));
        int y0 = clamp(int(floor(fv)), 0, int(eh) - 1);
        int y1 = clamp(y0 + 1, 0, int(eh) - 1);
        float fx = fu - float(x0);
        float fy = clamp(fv - float(y0), 0.0, 1.0);
        int iew = int(ew);
        uint ux0 = uint(((x0 % iew) + iew) % iew);
        uint ux1 = (ux0 + 1u) % ew;
        vec3 c00 = envPixels[uint(y0) * ew + ux0].rgb;
        vec3 c10 = envPixels[uint(y0) * ew + ux1].rgb;
        vec3 c01 = envPixels[uint(y1) * ew + ux0].rgb;
        vec3 c11 = envPixels[uint(y1) * ew + ux1].rgb;
        col = mix(mix(c00, c10, fx), mix(c01, c11, fx), fy) * u.camV_int.w;
    }
    if ((u.flags.w & 4u) != 0u && dot(d, u.sunDir_cosMax.xyz) >= u.sunDir_cosMax.w) {
        col += u.sunRad_ftype.rgb;
    }
    return col;
}

// ── Light Linking & Shadow Linking (O(1) 64-bit bitmask) ──
bool bitTest64(uint lo, uint hi, uint slot) {
    if (slot >= 64u) return false;
    return slot < 32u ? ((lo & (1u << slot)) != 0u) : ((hi & (1u << (slot - 32u))) != 0u);
}

bool isLightLinked(uint lightIdx, uint slot) {
    if ((u.flags.w & 64u) == 0u) return true;
    uvec4 m = linkTable[lightIdx].lightMask;
    if ((m.x | m.y) != 0u) return bitTest64(m.x, m.y, slot);
    if ((m.z | m.w) != 0u) return !bitTest64(m.z, m.w, slot);
    return true;
}

bool isShadowLinked(int lightIdx, uint slot) {
    if (lightIdx < 0 || (u.flags.w & 32u) == 0u) return true;
    uvec4 m = linkTable[uint(lightIdx)].shadowMask;
    if ((m.x | m.y) != 0u) return bitTest64(m.x, m.y, slot);
    if ((m.z | m.w) != 0u) return !bitTest64(m.z, m.w, slot);
    return true;
}

// ── Kesişim veri yapısı & BVH2 / VK_KHR_ray_query ──
struct HitInfo {
    bool hit;
    float t;
    float u;
    float v;
    uint primIdx;
};

bool intersectAabb(vec3 bmin, vec3 bmax, vec3 ro, vec3 invRd, float tMin, float tMax) {
    vec3 t0 = (bmin - ro) * invRd;
    vec3 t1 = (bmax - ro) * invRd;
    vec3 tsmall = min(t0, t1);
    vec3 tbig = max(t0, t1);
    float tn = max(max(tsmall.x, tsmall.y), max(tsmall.z, tMin));
    float tf = min(min(tbig.x, tbig.y), min(tbig.z, tMax));
    return tf >= tn;
}

bool intersectPrim(uint idx, vec3 ro, vec3 rd, float tMin, float tMax, float rayTime, uint visMask, bool camRay,
                   out float outT, out float outU, out float outV) {
    GpuPrim p = prims[idx];
    uint flags = floatBitsToUint(p.e1_flags.w);
    uint pVis = (flags >> 8u) & 0x1Fu;
    if ((pVis & visMask) == 0u) return false;
    if (camRay && ((flags & 2u) != 0u)) return false;

    if ((flags & 1u) != 0u) {
        // Küre
        vec3 oc = ro - p.p0_mat.xyz;
        float r = p.e1_flags.x;
        float a = dot(rd, rd);
        float halfB = dot(oc, rd);
        float c = dot(oc, oc) - r * r;
        float disc = halfB * halfB - a * c;
        if (disc < 0.0) return false;
        float sq = sqrt(disc);
        float root = (-halfB - sq) / a;
        if (root <= tMin || root >= tMax) {
            root = (-halfB + sq) / a;
            if (root <= tMin || root >= tMax) return false;
        }
        outT = root;
        outU = 0.0;
        outV = 0.0;
        return true;
    }

    // Üçgen (statik veya hareketli MeshInstance)
    vec3 v0 = p.p0_mat.xyz;
    vec3 e1 = p.e1_flags.xyz;
    vec3 e2 = p.e2_meta.xyz;
    if ((flags & 8u) != 0u) {
        uint meta = floatBitsToUint(p.e2_meta.w);
        uint mIdx = (meta >> 16u) - 1u;
        vec4 qTmp;
        vec3 sTmp;
        evalMotionPointNormal(mIdx, rayTime, v0, e1, e2, v0, e1, e2, qTmp, sTmp);
    }
    vec3 h = cross(rd, e2);
    float a = dot(e1, h);
    if (abs(a) < 1e-9) return false;
    float f = 1.0 / a;
    vec3 s = ro - v0;
    float bu = f * dot(s, h);
    if (bu < 0.0 || bu > 1.0) return false;
    vec3 q = cross(s, e1);
    float bv = f * dot(rd, q);
    if (bv < 0.0 || bu + bv > 1.0) return false;
    float t = f * dot(e2, q);
    if (t <= tMin || t >= tMax) return false;
    outT = t;
    outU = bu;
    outV = bv;
    return true;
}

void traverseBvh(uint rootIdx, vec3 ro, vec3 rd, float tMin, float rayTime, uint visMask, bool camRay,
                 inout HitInfo best) {
    if (u.counts.y == 0u || rootIdx >= u.counts.y) return;
    vec3 invRd = 1.0 / vec3(
        abs(rd.x) > 1e-12 ? rd.x : (rd.x >= 0.0 ? 1e-12 : -1e-12),
        abs(rd.y) > 1e-12 ? rd.y : (rd.y >= 0.0 ? 1e-12 : -1e-12),
        abs(rd.z) > 1e-12 ? rd.z : (rd.z >= 0.0 ? 1e-12 : -1e-12)
    );
    uint stack[64];
    int sp = 0;
    stack[sp++] = rootIdx;
    while (sp > 0) {
        uint ni = stack[--sp];
        GpuBvhNode node = bvhNodes[ni];
        if (!intersectAabb(node.bmin_left.xyz, node.bmax_meta.xyz, ro, invRd, tMin, best.t)) continue;
        uint w1 = floatBitsToUint(node.bmin_left.w);
        uint w2 = floatBitsToUint(node.bmax_meta.w);
        if ((w2 & 0x80000000u) != 0u) {
            uint cnt = w2 & 0x7FFFFFFFu;
            for (uint i = 0u; i < cnt; ++i) {
                float ht, hu, hv;
                if (intersectPrim(w1 + i, ro, rd, tMin, best.t, rayTime, visMask, camRay, ht, hu, hv)) {
                    best.hit = true;
                    best.t = ht;
                    best.u = hu;
                    best.v = hv;
                    best.primIdx = w1 + i;
                }
            }
        } else {
            if (sp + 2 <= 64) {
                stack[sp++] = w2;
                stack[sp++] = w1;
            }
        }
    }
}

HitInfo traceScene(vec3 ro, vec3 rd, float tMin, float tMax, float rayTime, uint visMask, bool camRay) {
    HitInfo best;
    best.hit = false;
    best.t = tMax;
    best.u = 0.0;
    best.v = 0.0;
    best.primIdx = 0u;

#ifdef PHOTON_USE_RAY_QUERY
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsNoneEXT, visMask, ro, tMin, rd, tMax);
    while (rayQueryProceedEXT(rq)) {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint pIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false) +
                        rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
            uint pFlags = floatBitsToUint(prims[pIdx].e1_flags.w);
            if (camRay && ((pFlags & 2u) != 0u)) continue;
            rayQueryConfirmIntersectionEXT(rq);
        }
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        best.hit = true;
        best.t = rayQueryGetIntersectionTEXT(rq, true);
        vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
        best.u = bary.x;
        best.v = bary.y;
        best.primIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true) +
                       rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    }
    if ((u.flags.w & 16u) != 0u) {
        uint secRoot = floatBitsToUint(u.camEndLL_sec.w);
        traverseBvh(secRoot, ro, rd, tMin, rayTime, visMask, camRay, best);
    }
#else
    traverseBvh(0u, ro, rd, tMin, rayTime, visMask, camRay, best);
#endif
    return best;
}

// ── Gölge ışını geçirgenliği (Shadow Linking + ince duvar cam + kesik opaklık) ──
vec3 traceShadowTransmittance(vec3 ro, vec3 rd, float tMax, float rayTime, int lightIdx) {
    vec3 T = vec3(1.0);
    vec3 curO = ro;
    float rem = tMax;
    for (int step = 0; step < 32; ++step) {
        HitInfo h = traceScene(curO, rd, 1e-4, rem, rayTime, VIS_SHADOW, false);
        if (!h.hit) return T;
        GpuPrim p = prims[h.primIdx];
        uint pFlags = floatBitsToUint(p.e1_flags.w);
        uint linkSlot = (pFlags >> 16u) & 0xFFu;
        if (!isShadowLinked(lightIdx, linkSlot)) {
            curO = curO + rd * (h.t + 1e-4);
            rem -= (h.t + 1e-4);
            if (rem <= 1e-4) return T;
            continue;
        }
        uint matIdx = floatBitsToUint(p.p0_mat.w);
        GpuMaterial m = materials[matIdx];
        uint mFlags = floatBitsToUint(m.emit_flags.w);
        if ((mFlags & 4u) == 0u) return vec3(0.0);
        // İnce duvarlı cam ya da kısmi opaklık geçirgenliği
        uint mType = floatBitsToUint(m.sheenCol_type.w);
        if (mType == 2u) {
            T *= m.base_metallic.rgb;
        } else {
            float op = clamp(m.coatTint_opacity.w, 0.0, 1.0);
            T *= (1.0 - op);
        }
        if (max(T.r, max(T.g, T.b)) < 1e-4) return vec3(0.0);
        curO = curO + rd * (h.t + 1e-4);
        rem -= (h.t + 1e-4);
        if (rem <= 1e-4) return T;
    }
    return vec3(0.0);
}

// ── Kesişim yüzey verisi çözümlemesi ──
struct SurfHit {
    vec3 p;
    vec3 n;
    vec3 ng;
    vec2 uv;
    bool frontFace;
    uint matIdx;
    uint linkSlot;
    int emitterIdx;
    bool shadowCatcher;
    float cryptoObj;
    float cryptoMat;
    vec3 objColor;
    uint matIdHash;
};

SurfHit resolveHit(HitInfo h, vec3 ro, vec3 rd, float rayTime) {
    SurfHit sh;
    GpuPrim pr = prims[h.primIdx];
    uint flags = floatBitsToUint(pr.e1_flags.w);
    uint meta = floatBitsToUint(pr.e2_meta.w);
    sh.p = ro + rd * h.t;
    sh.matIdx = floatBitsToUint(pr.p0_mat.w);
    sh.linkSlot = (flags >> 16u) & 0xFFu;
    sh.emitterIdx = int(meta & 0xFFFFu) - 1;
    sh.shadowCatcher = (flags & 4u) != 0u;
    sh.cryptoObj = pr.uv2_crypto.z;
    sh.cryptoMat = pr.uv2_crypto.w;
    sh.objColor = pr.objColor_p.rgb;
    sh.matIdHash = floatBitsToUint(pr.objColor_p.w);

    if ((flags & 1u) != 0u) {
        vec3 outN = normalize(sh.p - pr.p0_mat.xyz);
        sh.ng = outN;
        sh.n = outN;
        float theta = acos(clamp(outN.y, -1.0, 1.0));
        float phi = atan(outN.z, outN.x);
        if (phi < 0.0) phi += TWO_PI;
        sh.uv = vec2(phi / TWO_PI, theta / PI);
    } else {
        float w = 1.0 - h.u - h.v;
        vec3 sn = normalize(pr.n0_u0.xyz * w + pr.n1_v0.xyz * h.u + pr.n2_u1.xyz * h.v);
        vec3 gn = pr.ng_v1.xyz;
        if ((flags & 8u) != 0u) {
            uint mIdx = (meta >> 16u) - 1u;
            vec3 wp0, we1, we2, sc;
            vec4 q;
            evalMotionPointNormal(mIdx, rayTime, pr.p0_mat.xyz, pr.e1_flags.xyz, pr.e2_meta.xyz, wp0, we1, we2, q, sc);
            sn = motionTransformNormal(q, sc, sn);
            gn = motionTransformNormal(q, sc, gn);
        }
        sh.n = dot(sn, sn) > 1e-12 ? normalize(sn) : normalize(gn);
        sh.ng = dot(gn, gn) > 1e-12 ? normalize(gn) : sh.n;
        vec2 uv0 = vec2(pr.n0_u0.w, pr.n1_v0.w);
        vec2 uv1 = vec2(pr.n2_u1.w, pr.ng_v1.w);
        vec2 uv2 = pr.uv2_crypto.xy;
        sh.uv = uv0 * w + uv1 * h.u + uv2 * h.v;
    }

    sh.frontFace = dot(rd, sh.ng) < 0.0;
    if (!sh.frontFace) {
        sh.ng = -sh.ng;
        sh.n = -sh.n;
    } else if (dot(sh.n, sh.ng) < 0.0) {
        sh.n = -sh.n;
    }
    return sh;
}

vec3 offsetOrigin(vec3 p, vec3 ng, vec3 dir) {
    float s = dot(dir, ng) >= 0.0 ? 1.0 : -1.0;
    float eps = max(1e-4, length(p) * 1e-5);
    return p + ng * (s * eps);
}

// ── Birleşik OpenPBR / Disney + Lambert + Dielectric + Mirror BSDF ──
float schlickFresnel(float cosT, float f0) {
    float x = clamp(1.0 - cosT, 0.0, 1.0);
    float x2 = x * x;
    return f0 + (1.0 - f0) * x2 * x2 * x;
}

vec3 schlickFresnel3(float cosT, vec3 f0) {
    float x = clamp(1.0 - cosT, 0.0, 1.0);
    float x2 = x * x;
    return f0 + (vec3(1.0) - f0) * (x2 * x2 * x);
}

float ggxD(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-8);
}

float ggxG1(float cosT, float a) {
    float a2 = a * a;
    float c2 = max(cosT * cosT, 1e-8);
    return 2.0 / (1.0 + sqrt(1.0 + a2 * (1.0 - c2) / c2));
}

vec3 sampleGgxH(vec2 u, float a) {
    float phi = TWO_PI * u.x;
    float cosT = sqrt((1.0 - u.y) / max(1.0 + (a * a - 1.0) * u.y, 1e-8));
    float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
    return vec3(sinT * cos(phi), sinT * sin(phi), cosT);
}

struct ResolvedMat {
    uint type; // 0=Disney, 1=Lambert, 2=Glass, 3=Mirror
    vec3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float ior;
    float specTrans;
    float coat;
    float coatRough;
    float coatIor;
    vec3 coatTint;
    float sheen;
    vec3 sheenColor;
    vec3 emission;
    float opacity;
    bool thinWalled;
};

ResolvedMat resolveMaterial(uint matIdx, SurfHit sh) {
    GpuMaterial m = materials[matIdx];
    ResolvedMat r;
    r.type = floatBitsToUint(m.sheenCol_type.w);
    r.baseColor = m.base_metallic.rgb;
    r.metallic = m.base_metallic.w;
    r.roughness = m.rough_spec_ior_t.x;
    r.specular = m.rough_spec_ior_t.y;
    r.ior = m.rough_spec_ior_t.z;
    r.specTrans = m.rough_spec_ior_t.w;
    r.coat = m.coat_cr_ci_sheen.x;
    r.coatRough = m.coat_cr_ci_sheen.y;
    r.coatIor = m.coat_cr_ci_sheen.z;
    r.sheen = m.coat_cr_ci_sheen.w;
    r.coatTint = m.coatTint_opacity.rgb;
    r.opacity = m.coatTint_opacity.w;
    r.sheenColor = m.sheenCol_type.rgb;
    r.emission = m.emit_flags.rgb;
    uint mf = floatBitsToUint(m.emit_flags.w);
    r.thinWalled = (mf & 1u) != 0u;

    vec2 uv = sh.uv * m.tex1.y;
    if ((mf & 2u) != 0u) {
        vec3 a = abs(sh.ng);
        uv = ((a.x >= a.y && a.x >= a.z) ? sh.p.zy : (a.y >= a.z ? sh.p.xz : sh.p.xy)) * m.tex1.y;
    }
    int tAlb = floatBitsToInt(m.tex0.x);
    int tRgh = floatBitsToInt(m.tex0.y);
    int tMet = floatBitsToInt(m.tex0.z);
    int tOp  = floatBitsToInt(m.tex1.x);
    if (tAlb >= 0) r.baseColor = sampleTexture(tAlb, uv).rgb;
    if (tRgh >= 0) r.roughness = sampleTexture(tRgh, uv).g;
    if (tMet >= 0) r.metallic = sampleTexture(tMet, uv).b;
    if (tOp >= 0)  r.opacity *= sampleTexture(tOp, uv).r;
    return r;
}

bool isMatDelta(ResolvedMat rm) {
    if (rm.type == 3u) return true;
    if (rm.type == 2u && rm.roughness <= 0.005) return true;
    return false;
}

void evalBsdf(ResolvedMat rm, vec3 wo, vec3 wi, SurfHit sh,
              out vec3 fTotal, out float pdfOut, out vec3 fDiff, out vec3 fSpec, out vec3 fTrans) {
    fTotal = vec3(0.0);
    pdfOut = 0.0;
    fDiff = vec3(0.0);
    fSpec = vec3(0.0);
    fTrans = vec3(0.0);

    if (rm.type == 3u) return;
    float NoO = dot(sh.n, wo);
    float NoI = dot(sh.n, wi);
    if (rm.type == 1u) {
        if (NoO > 0.0 && NoI > 0.0) {
            fDiff = rm.baseColor * INV_PI;
            fTotal = fDiff;
            pdfOut = NoI * INV_PI;
        }
        return;
    }
    if (rm.type == 2u) {
        if (rm.roughness <= 0.005) return;
        float a = max(rm.roughness * rm.roughness, 0.001);
        float eta = sh.frontFace ? (1.0 / rm.ior) : rm.ior;
        float f0 = pow((rm.ior - 1.0) / (rm.ior + 1.0), 2.0);
        if (NoO > 0.0 && NoI > 0.0) {
            vec3 H = normalize(wo + wi);
            float NoH = max(dot(sh.n, H), 1e-6);
            float VoH = max(dot(wo, H), 1e-6);
            float F = schlickFresnel(VoH, f0);
            float D = ggxD(NoH, a);
            float G = ggxG1(NoO, a) * ggxG1(NoI, a);
            fSpec = vec3(F * D * G / max(4.0 * NoO * NoI, 1e-6));
            fTotal = fSpec;
            pdfOut = F * D * NoH / max(4.0 * VoH, 1e-6);
        } else if (NoO > 0.0 && NoI < 0.0) {
            vec3 H = -(wo * eta + wi);
            float hl = length(H);
            if (hl < 1e-6) return;
            H /= hl;
            if (dot(H, sh.n) < 0.0) H = -H;
            float VoH = dot(wo, H);
            float IoH = dot(wi, H);
            if (VoH * IoH >= 0.0) return;
            float NoH = max(dot(sh.n, H), 1e-6);
            float F = schlickFresnel(abs(VoH), f0);
            float D = ggxD(NoH, a);
            float G = ggxG1(NoO, a) * ggxG1(abs(NoI), a);
            float denom = eta * VoH + IoH;
            float denom2 = max(denom * denom, 1e-6);
            float factor = abs(VoH * IoH) / max(NoO * abs(NoI), 1e-6);
            fTrans = rm.baseColor * ((1.0 - F) * D * G * factor * (eta * eta) / denom2);
            fTotal = fTrans;
            pdfOut = (1.0 - F) * D * NoH * abs(IoH) / denom2;
        }
        return;
    }

    // Birleşik OpenPBR / Disney
    float a = max(rm.roughness * rm.roughness, 0.002);
    float f0s = abs(rm.ior - 1.5) > 1e-3 ? pow((rm.ior - 1.0) / (rm.ior + 1.0), 2.0) : (0.08 * rm.specular);
    vec3 f0 = mix(vec3(f0s), rm.baseColor, rm.metallic);
    float diffW = (1.0 - rm.metallic) * (1.0 - rm.specTrans);
    float transW = (1.0 - rm.metallic) * rm.specTrans;

    float wSpec = clamp(mix(0.25, 0.9, rm.metallic), 0.1, 0.95);
    float wCoat = rm.coat > 0.0 ? 0.25 * rm.coat : 0.0;
    float wTrans = transW * 0.6;
    float wDiff = max(0.05, diffW * (1.0 - wSpec));
    float wSum = wDiff + wSpec + wCoat + wTrans;
    wDiff /= wSum; wSpec /= wSum; wCoat /= wSum; wTrans /= wSum;

    if (NoO > 0.0 && NoI > 0.0) {
        vec3 H = normalize(wo + wi);
        float NoH = max(dot(sh.n, H), 1e-6);
        float VoH = max(dot(wo, H), 1e-6);
        // Difüz + Sheen
        float fd90 = 0.5 + 2.0 * rm.roughness * VoH * VoH;
        float fl = 1.0 + (fd90 - 1.0) * pow(1.0 - NoI, 5.0);
        float fv = 1.0 + (fd90 - 1.0) * pow(1.0 - NoO, 5.0);
        vec3 diff = rm.baseColor * (INV_PI * fl * fv * diffW);
        if (rm.sheen > 0.0) {
            diff += rm.sheenColor * (rm.sheen * pow(1.0 - VoH, 5.0) * diffW * INV_PI);
        }
        // GGX Speküler
        vec3 F = schlickFresnel3(VoH, f0);
        float D = ggxD(NoH, a);
        float G = ggxG1(NoO, a) * ggxG1(NoI, a);
        vec3 spec = F * (D * G / max(4.0 * NoO * NoI, 1e-6));
        float pSpec = D * NoH / max(4.0 * VoH, 1e-6);
        float pCoat = 0.0;
        if (rm.coat > 0.0) {
            float ac = max(rm.coatRough * rm.coatRough, 0.002);
            float cf0 = pow((rm.coatIor - 1.0) / (rm.coatIor + 1.0), 2.0);
            float Fc = schlickFresnel(VoH, cf0);
            float Dc = ggxD(NoH, ac);
            float Gc = ggxG1(NoO, ac) * ggxG1(NoI, ac);
            vec3 atten = mix(vec3(1.0), rm.coatTint * (1.0 - Fc), rm.coat);
            diff *= atten;
            spec = spec * atten + rm.coatTint * (rm.coat * Fc * Dc * Gc / max(4.0 * NoO * NoI, 1e-6));
            pCoat = Dc * NoH / max(4.0 * VoH, 1e-6);
        }
        fDiff = diff;
        fSpec = spec;
        fTotal = diff + spec;
        pdfOut = wDiff * (NoI * INV_PI) + wSpec * pSpec + wCoat * pCoat;
    } else if (NoO > 0.0 && NoI < 0.0 && transW > 0.0) {
        float eta = sh.frontFace ? (1.0 / rm.ior) : rm.ior;
        vec3 H = -(wo * eta + wi);
        float hl = length(H);
        if (hl < 1e-6) return;
        H /= hl;
        if (dot(H, sh.n) < 0.0) H = -H;
        float VoH = dot(wo, H);
        float IoH = dot(wi, H);
        if (VoH * IoH >= 0.0) return;
        float NoH = max(dot(sh.n, H), 1e-6);
        float F = schlickFresnel(abs(VoH), f0s);
        float D = ggxD(NoH, a);
        float G = ggxG1(NoO, a) * ggxG1(abs(NoI), a);
        float denom = eta * VoH + IoH;
        float denom2 = max(denom * denom, 1e-6);
        float factor = abs(VoH * IoH) / max(NoO * abs(NoI), 1e-6);
        fTrans = rm.baseColor * (transW * (1.0 - F) * D * G * factor * (eta * eta) / denom2);
        fTotal = fTrans;
        pdfOut = wTrans * (D * NoH * abs(IoH) / denom2);
    }
}

struct BsdfSampleGpu {
    bool valid;
    vec3 wi;
    vec3 f;
    float pdf;
    bool delta;
    bool passThrough;
    bool refracted;
    vec3 lobeFrac0;
    vec3 lobeFrac1;
    vec3 lobeFrac2;
};

BsdfSampleGpu sampleBsdf(ResolvedMat rm, vec3 wo, SurfHit sh, inout uint rng) {
    BsdfSampleGpu bs;
    bs.valid = false;
    bs.delta = false;
    bs.passThrough = false;
    bs.refracted = false;
    bs.lobeFrac0 = vec3(1.0);
    bs.lobeFrac1 = vec3(0.0);
    bs.lobeFrac2 = vec3(0.0);

    vec3 fs, ft;
    makeFrame(sh.n, fs, ft);
    float NoO = dot(sh.n, wo);
    if (NoO <= 0.0) return bs;

    if (rm.type == 3u) {
        // Ayna
        bs.wi = reflect(-wo, sh.n);
        bs.f = rm.baseColor / max(abs(dot(bs.wi, sh.n)), 1e-6);
        bs.pdf = 1.0;
        bs.delta = true;
        bs.valid = true;
        bs.lobeFrac0 = vec3(0.0);
        bs.lobeFrac1 = vec3(1.0);
        return bs;
    }

    if (rm.type == 1u) {
        // Lambert
        vec3 wl = cosineSampleHemisphere(rand2(rng));
        bs.wi = normalize(toWorld(wl, fs, ft, sh.n));
        bs.f = rm.baseColor * INV_PI;
        bs.pdf = max(wl.z, 1e-6) * INV_PI;
        bs.valid = true;
        return bs;
    }

    if (rm.type == 2u) {
        // Cam (pürüzsüz delta veya kaba GGX + ince duvar)
        float eta = sh.frontFace ? (1.0 / rm.ior) : rm.ior;
        float f0 = pow((rm.ior - 1.0) / (rm.ior + 1.0), 2.0);
        if (rm.thinWalled) {
            float F = schlickFresnel(NoO, f0);
            if (rand1(rng) < F) {
                bs.wi = reflect(-wo, sh.n);
                bs.f = vec3(1.0) / max(abs(dot(bs.wi, sh.n)), 1e-6);
                bs.pdf = 1.0;
                bs.delta = true;
                bs.valid = true;
                bs.lobeFrac0 = vec3(0.0);
                bs.lobeFrac1 = vec3(1.0);
            } else {
                bs.wi = -wo;
                bs.f = rm.baseColor / max(abs(dot(bs.wi, sh.n)), 1e-6);
                bs.pdf = 1.0;
                bs.delta = true;
                bs.passThrough = true;
                bs.refracted = true;
                bs.valid = true;
                bs.lobeFrac0 = vec3(0.0);
                bs.lobeFrac2 = vec3(1.0);
            }
            return bs;
        }
        if (rm.roughness <= 0.005) {
            vec3 refr = refract(-wo, sh.n, eta);
            float F = dot(refr, refr) < 1e-6 ? 1.0 : schlickFresnel(NoO, f0);
            if (rand1(rng) < F) {
                bs.wi = reflect(-wo, sh.n);
                bs.f = vec3(1.0) / max(abs(dot(bs.wi, sh.n)), 1e-6);
                bs.pdf = 1.0;
                bs.delta = true;
                bs.valid = true;
                bs.lobeFrac0 = vec3(0.0);
                bs.lobeFrac1 = vec3(1.0);
            } else {
                bs.wi = normalize(refr);
                bs.f = rm.baseColor * (eta * eta) / max(abs(dot(bs.wi, sh.n)), 1e-6);
                bs.pdf = 1.0;
                bs.delta = true;
                bs.refracted = true;
                bs.valid = true;
                bs.lobeFrac0 = vec3(0.0);
                bs.lobeFrac2 = vec3(1.0);
            }
            return bs;
        }
        float a = max(rm.roughness * rm.roughness, 0.001);
        vec3 H = normalize(toWorld(sampleGgxH(rand2(rng), a), fs, ft, sh.n));
        float VoH = dot(wo, H);
        if (VoH <= 0.0) return bs;
        vec3 refr = refract(-wo, H, eta);
        float F = dot(refr, refr) < 1e-6 ? 1.0 : schlickFresnel(VoH, f0);
        if (rand1(rng) < F) {
            bs.wi = reflect(-wo, H);
            if (dot(bs.wi, sh.n) <= 0.0) return bs;
        } else {
            bs.wi = normalize(refr);
            if (dot(bs.wi, sh.n) >= 0.0) return bs;
            bs.refracted = true;
        }
        vec3 fd, fsp, ftr;
        evalBsdf(rm, wo, bs.wi, sh, bs.f, bs.pdf, fd, fsp, ftr);
        bs.valid = bs.pdf > 1e-7 && max(bs.f.r, max(bs.f.g, bs.f.b)) > 0.0;
        bs.lobeFrac0 = vec3(0.0);
        bs.lobeFrac1 = bs.refracted ? vec3(0.0) : vec3(1.0);
        bs.lobeFrac2 = bs.refracted ? vec3(1.0) : vec3(0.0);
        return bs;
    }

    // Birleşik OpenPBR / Disney örneklemesi
    float a = max(rm.roughness * rm.roughness, 0.002);
    float diffW = (1.0 - rm.metallic) * (1.0 - rm.specTrans);
    float transW = (1.0 - rm.metallic) * rm.specTrans;
    float wSpec = clamp(mix(0.25, 0.9, rm.metallic), 0.1, 0.95);
    float wCoat = rm.coat > 0.0 ? 0.25 * rm.coat : 0.0;
    float wTrans = transW * 0.6;
    float wDiff = max(0.05, diffW * (1.0 - wSpec));
    float wSum = wDiff + wSpec + wCoat + wTrans;
    wDiff /= wSum; wSpec /= wSum; wCoat /= wSum; wTrans /= wSum;

    float uc = rand1(rng);
    vec2 u2 = rand2(rng);
    if (uc < wDiff) {
        vec3 wl = cosineSampleHemisphere(u2);
        bs.wi = normalize(toWorld(wl, fs, ft, sh.n));
    } else if (uc < wDiff + wSpec) {
        vec3 H = normalize(toWorld(sampleGgxH(u2, a), fs, ft, sh.n));
        bs.wi = reflect(-wo, H);
        if (dot(bs.wi, sh.n) <= 0.0) return bs;
    } else if (uc < wDiff + wSpec + wCoat) {
        float ac = max(rm.coatRough * rm.coatRough, 0.002);
        vec3 H = normalize(toWorld(sampleGgxH(u2, ac), fs, ft, sh.n));
        bs.wi = reflect(-wo, H);
        if (dot(bs.wi, sh.n) <= 0.0) return bs;
    } else {
        float eta = sh.frontFace ? (1.0 / rm.ior) : rm.ior;
        vec3 H = normalize(toWorld(sampleGgxH(u2, a), fs, ft, sh.n));
        vec3 refr = refract(-wo, H, eta);
        if (dot(refr, refr) < 1e-6) {
            bs.wi = reflect(-wo, H);
            if (dot(bs.wi, sh.n) <= 0.0) return bs;
        } else {
            bs.wi = normalize(refr);
            if (dot(bs.wi, sh.n) >= 0.0) return bs;
            bs.refracted = true;
        }
    }
    vec3 fd, fsp, ftr;
    evalBsdf(rm, wo, bs.wi, sh, bs.f, bs.pdf, fd, fsp, ftr);
    bs.valid = bs.pdf > 1e-7 && max(bs.f.r, max(bs.f.g, bs.f.b)) > 0.0;
    vec3 ssum = fd + fsp + ftr;
    bs.lobeFrac0 = vec3(ssum.r > 0.0 ? fd.r / ssum.r : 1.0, ssum.g > 0.0 ? fd.g / ssum.g : 1.0, ssum.b > 0.0 ? fd.b / ssum.b : 1.0);
    bs.lobeFrac1 = vec3(ssum.r > 0.0 ? fsp.r / ssum.r : 0.0, ssum.g > 0.0 ? fsp.g / ssum.g : 0.0, ssum.b > 0.0 ? fsp.b / ssum.b : 0.0);
    bs.lobeFrac2 = vec3(ssum.r > 0.0 ? ftr.r / ssum.r : 0.0, ssum.g > 0.0 ? ftr.g / ssum.g : 0.0, ssum.b > 0.0 ? ftr.b / ssum.b : 0.0);
    return bs;
}

// ── Işık örneklemesi (NEE) & MIS PDF ──
struct LightSampleGpu {
    bool valid;
    vec3 wi;
    vec3 Li;
    float pdf;
    float dist;
    bool delta;
    uint groupIdx;
    int lightIdx;
};

float lightPdfHit(uint li, vec3 refP, vec3 hitP) {
    GpuLight l = lights[li];
    uint meta = floatBitsToUint(l.pos_type.w);
    uint type = meta & 0xFFu;
    if (type != 0u && type != 4u) return 0.0;
    vec3 d = hitP - refP;
    float d2 = dot(d, d);
    float dist = sqrt(max(d2, 1e-12));
    float cosL = abs(dot(d / dist, l.norm_power.xyz));
    float area = l.u_area.w;
    if (cosL < 1e-6 || area <= 0.0) return 0.0;
    return d2 / (area * cosL);
}

LightSampleGpu sampleOneLight(vec3 refP, vec3 refN, uint receiverSlot, inout uint rng) {
    LightSampleGpu ls;
    ls.valid = false;
    ls.delta = false;
    ls.groupIdx = 0u;
    ls.lightIdx = -1;

    uint nExplicit = u.counts.z;
    bool hasEnv = (u.flags.w & 2u) != 0u;
    uint total = nExplicit + (hasEnv ? 1u : 0u);
    if (total == 0u) return ls;

    uint idx = min(uint(rand1(rng) * float(total)), total - 1u);
    float pmf = 1.0 / float(total);
    vec2 u2 = rand2(rng);

    if (idx >= nExplicit) {
        // Ortam ışığı: kosinüs yarıküre örneklemesi
        vec3 fs, ft;
        makeFrame(refN, fs, ft);
        vec3 wl = cosineSampleHemisphere(u2);
        ls.wi = normalize(toWorld(wl, fs, ft, refN));
        ls.Li = evalEnvironment(ls.wi);
        ls.pdf = pmf * max(wl.z, 1e-6) * INV_PI;
        ls.dist = INF_T;
        ls.delta = false;
        ls.groupIdx = u.envInfo.w;
        ls.lightIdx = -1;
        ls.valid = ls.pdf > 0.0 && max(ls.Li.r, max(ls.Li.g, ls.Li.b)) > 0.0;
        return ls;
    }

    if (!isLightLinked(idx, receiverSlot)) return ls;

    GpuLight l = lights[idx];
    uint meta = floatBitsToUint(l.pos_type.w);
    uint type = meta & 0xFFu;
    ls.groupIdx = (meta >> 8u) & 0xFFu;
    ls.lightIdx = int(idx);

    if (type == 0u) {
        // Dikdörtgen Alan Işığı (iki yüzlü)
        vec3 lp = l.pos_type.xyz + l.u_area.xyz * u2.x + l.v_cosIn.xyz * u2.y;
        vec3 d = lp - refP;
        float d2 = dot(d, d);
        if (d2 < 1e-10) return ls;
        float dist = sqrt(d2);
        ls.wi = d / dist;
        float cosL = abs(dot(ls.wi, l.norm_power.xyz));
        if (cosL < 1e-6 || l.u_area.w <= 0.0) return ls;
        ls.Li = l.rad_cosOut.rgb;
        ls.pdf = pmf * d2 / (l.u_area.w * cosL);
        ls.dist = dist * 0.9995;
        ls.delta = false;
        ls.valid = true;
    } else if (type == 4u) {
        // Üçgen Işık
        float su = sqrt(max(u2.x, 0.0));
        float b0 = 1.0 - su;
        float b1 = u2.y * su;
        vec3 lp = l.pos_type.xyz + l.u_area.xyz * b0 + l.v_cosIn.xyz * b1;
        vec3 d = lp - refP;
        float d2 = dot(d, d);
        if (d2 < 1e-10) return ls;
        float dist = sqrt(d2);
        ls.wi = d / dist;
        float cosL = abs(dot(ls.wi, l.norm_power.xyz));
        if (cosL < 1e-6 || l.u_area.w <= 0.0) return ls;
        ls.Li = l.rad_cosOut.rgb;
        ls.pdf = pmf * d2 / (l.u_area.w * cosL);
        ls.dist = dist * 0.9995;
        ls.delta = false;
        ls.valid = true;
    } else if (type == 1u) {
        // Nokta Işık
        vec3 d = l.pos_type.xyz - refP;
        float d2 = max(dot(d, d), 1e-10);
        float dist = sqrt(d2);
        ls.wi = d / dist;
        ls.Li = l.rad_cosOut.rgb / d2;
        ls.pdf = pmf;
        ls.dist = dist * 0.9995;
        ls.delta = true;
        ls.valid = true;
    } else if (type == 2u) {
        // Yönlü Işık
        ls.wi = -normalize(l.pos_type.xyz);
        ls.Li = l.rad_cosOut.rgb;
        ls.pdf = pmf;
        ls.dist = INF_T;
        ls.delta = true;
        ls.valid = true;
    } else if (type == 3u) {
        // Spot Işık
        vec3 d = l.pos_type.xyz - refP;
        float d2 = max(dot(d, d), 1e-10);
        float dist = sqrt(d2);
        ls.wi = d / dist;
        float cosA = dot(-ls.wi, l.u_area.xyz);
        float cosOut = l.rad_cosOut.w;
        float cosIn = l.v_cosIn.w;
        float fall = 1.0;
        if (cosOut > -1.5) {
            if (cosA <= cosOut) return ls;
            if (cosA < cosIn) {
                float t = (cosA - cosOut) / max(cosIn - cosOut, 1e-6);
                fall = t * t * (3.0 - 2.0 * t);
            }
        }
        ls.Li = l.rad_cosOut.rgb * (fall / d2);
        ls.pdf = pmf;
        ls.dist = dist * 0.9995;
        ls.delta = true;
        ls.valid = true;
    }
    return ls;
}

float envNeePdf(vec3 wi, vec3 lastN) {
    uint nExplicit = u.counts.z;
    bool hasEnv = (u.flags.w & 2u) != 0u;
    if (!hasEnv) return 0.0;
    float pmf = 1.0 / float(nExplicit + 1u);
    return pmf * max(dot(wi, lastN), 0.0) * INV_PI;
}

// ── Kamera ışını üretimi (hareket bulanıklığı + DOF + ortografik) ──
void generateCameraRay(uint px, uint py, inout uint rng, out vec3 ro, out vec3 rd, out float rayTime) {
    vec2 jitter = rand2(rng) - vec2(0.5);
    float su = (float(px) + 0.5 + jitter.x) / float(u.dims.x);
    float sv = 1.0 - (float(py) + 0.5 + jitter.y) / float(u.dims.y);
    vec2 lensU = rand2(rng);
    rayTime = ((u.flags.w & 1u) != 0u) ? rand1(rng) : 0.0;

    bool camMb = ((u.flags.w & 8u) != 0u) && rayTime > 0.0;
    vec3 pos = camMb ? mix(u.camPos_lens.xyz, u.camEndPos_lens.xyz, rayTime) : u.camPos_lens.xyz;
    vec3 ll  = camMb ? mix(u.camLL_ortho.xyz, u.camEndLL_sec.xyz, rayTime) : u.camLL_ortho.xyz;
    vec3 hor = camMb ? mix(u.camHoriz_ao.xyz, u.camEndHoriz_p.xyz, rayTime) : u.camHoriz_ao.xyz;
    vec3 ver = camMb ? mix(u.camVert_clamp.xyz, u.camEndVert_p.xyz, rayTime) : u.camVert_clamp.xyz;
    float lensR = camMb ? mix(u.camPos_lens.w, u.camEndPos_lens.w, rayTime) : u.camPos_lens.w;

    if (u.camLL_ortho.w > 0.5) {
        vec3 fwd = camMb ? normalize(mix(u.camFwd_shadowQ.xyz, u.camEndFwd_p.xyz, rayTime)) : u.camFwd_shadowQ.xyz;
        ro = ll + hor * su + ver * sv;
        rd = normalize(fwd);
    } else {
        ro = pos;
        if (lensR > 0.0) {
            vec2 lp = sampleDisk(lensU) * lensR;
            ro += u.camU_rot.xyz * lp.x + u.camV_int.xyz * lp.y;
        }
        vec3 target = ll + hor * su + ver * sv;
        rd = normalize(target - ro);
    }
}
