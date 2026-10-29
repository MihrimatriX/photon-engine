// raster_view.cpp — GPU görüntü modlarının OpenGL 3.3 çizimi.
//
// Windows'un opengl32.dll'i yalnız OpenGL 1.1 işlevlerini dışa verir; VAO, shader,
// FBO gibi 3.x işlevleri sürücüden adresleriyle istenir (glfwGetProcAddress). Gereken
// ~40 işlev aşağıdaki listeden işaretçi olarak yüklenir; biri eksikse modlar kapanır
// ve viewport ışın izlenmiş görüntüyle çalışmaya devam eder.
//
// Çizim sırası (tam modlar):
//   1. Arka plan rengiyle temizle.
//   2. Yüzeyler: Katı → aydınlatılmış renk, Normaller → normal rengi, Tel kafes →
//      zemine yakın koyu renk (yalnız arkadaki çizgileri gizlemek için). Yüzeyler
//      derinlikte biraz geri itilir (polygon offset) ki aynı üçgenin kenar çizgisi
//      kendi yüzeyiyle "z-fighting" yapmadan önde kalsın.
//   3. Tel kafes modunda kenarlar (glPolygonMode GL_LINE).
//   4. Zemin ızgarası: derinlik testli (nesneler önünü kapatır), yazmasız, uzakta söner.
// Bindirme (overlay): şeffaf zemin, yüzeyler yalnız derinliğe yazılır (renk maskesi
// kapalı), sonra görünür kenarlar. Çıktı ışın izlenmiş görüntünün üstüne konur.
//
// Gerçek zamanlı mod (renderRealtime):
//   1. Gölge haritası: ana ışığın gözünden derinlik (2048², eğim ofsetli).
//   2. Arka plan: ortam haritası (ya da düz renk), tam ekran üçgen.
//   3. Opak yüzeyler: forward PBR; her piksel ton eşlenip sRGB yazılır (MSAA çözümü sonrası
//      kenarlar ekran uzayında yumuşar).
//   4. Zemin: aynı gölgelendirici + temas gölgesi; gölge yakalayıcıda yalnız karartma.
//   5. Cam: arkadan öne sıralı, iki geçiş — çarpımsal geçirgenlik (renkli cam), sonra Fresnel
//      ağırlıklı yansıma (harmanlama; ekran değerlerini toplamak sRGB'de sütümsü sonuç verirdi).
//      Kırılma yok. Camlar ayrı bir derinlik atlasına da yazılır: gölgeleri kısmidir (ışın
//      izleyicide ışığın bir kısmı geçer, kostik olur; opak gölge fazla, hiç gölge az olurdu).
#include "app/raster_view.h"
#include "core/color/transfer.h"
#include "core/math/transform.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

namespace photon {

namespace {

#ifdef _WIN32
#define PHOTON_GLAPI __stdcall
#else
#define PHOTON_GLAPI
#endif
using GLchar_ = char;
using GLsizeiptr_ = std::ptrdiff_t;

// GL 3.x sabitleri (Windows'un gl.h'si yalnız 1.1'i tanımlar).
constexpr GLenum kArrayBuffer = 0x8892, kElementArrayBuffer = 0x8893, kStaticDraw = 0x88E4;
constexpr GLenum kVertexShader = 0x8B31, kFragmentShader = 0x8B30, kCompileStatus = 0x8B81, kLinkStatus = 0x8B82;
constexpr GLenum kFramebuffer = 0x8D40, kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9, kRenderbuffer = 0x8D41;
constexpr GLenum kColorAttachment0 = 0x8CE0, kDepthAttachment = 0x8D00, kDepthComponent24 = 0x81A6;
constexpr GLenum kFramebufferComplete = 0x8CD5, kMaxSamples = 0x8D57, kClampToEdge = 0x812F, kRgba8 = 0x8058;
constexpr GLenum kTexture0 = 0x84C0, kTexture3D = 0x806F, kTextureWrapR = 0x8072, kRgb16f = 0x881B, kRgb8 = 0x8051;
constexpr GLenum kSrgb8Alpha8 = 0x8C43, kTextureMaxLevel = 0x813D, kTextureCompareMode = 0x884C;
constexpr GLenum kTextureCompareFunc = 0x884D, kCompareRefToTexture = 0x884E;

#define PHOTON_GL_FUNCS(X)                                                                              \
    X(void, glGenVertexArrays, (GLsizei, GLuint*))                                                      \
    X(void, glBindVertexArray, (GLuint))                                                                \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint*))                                             \
    X(void, glGenBuffers, (GLsizei, GLuint*))                                                           \
    X(void, glBindBuffer, (GLenum, GLuint))                                                             \
    X(void, glBufferData, (GLenum, GLsizeiptr_, const void*, GLenum))                                   \
    X(void, glDeleteBuffers, (GLsizei, const GLuint*))                                                  \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))            \
    X(void, glEnableVertexAttribArray, (GLuint))                                                        \
    X(GLuint, glCreateShader, (GLenum))                                                                 \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar_* const*, const GLint*))                     \
    X(void, glCompileShader, (GLuint))                                                                  \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*))                                                    \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                                  \
    X(void, glDeleteShader, (GLuint))                                                                   \
    X(GLuint, glCreateProgram, ())                                                                      \
    X(void, glAttachShader, (GLuint, GLuint))                                                           \
    X(void, glLinkProgram, (GLuint))                                                                    \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*))                                                   \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                                 \
    X(void, glDeleteProgram, (GLuint))                                                                  \
    X(void, glUseProgram, (GLuint))                                                                     \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar_*))                                            \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                            \
    X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                            \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                   \
    X(void, glUniform1i, (GLint, GLint))                                                                \
    X(void, glUniform1f, (GLint, GLfloat))                                                              \
    X(void, glGenFramebuffers, (GLsizei, GLuint*))                                                      \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                        \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*))                                             \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                            \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                                       \
    X(void, glGenRenderbuffers, (GLsizei, GLuint*))                                                     \
    X(void, glBindRenderbuffer, (GLenum, GLuint))                                                       \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint*))                                            \
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))              \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                     \
    X(void, glActiveTexture, (GLenum))                                                                  \
    X(void, glGenerateMipmap, (GLenum))                                                                 \
    X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat))                                                     \
    X(void, glUniform3fv, (GLint, GLsizei, const GLfloat*))                                             \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))                                             \
    X(void, glUniform1fv, (GLint, GLsizei, const GLfloat*))                                             \
    X(void, glUniform2fv, (GLint, GLsizei, const GLfloat*))                                             \
    X(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat*))                            \
    X(void, glGenSamplers, (GLsizei, GLuint*))                                                          \
    X(void, glDeleteSamplers, (GLsizei, const GLuint*))                                                 \
    X(void, glBindSampler, (GLuint, GLuint))                                                            \
    X(void, glSamplerParameteri, (GLuint, GLenum, GLint))

#define PHOTON_GL_DECLARE(ret, name, args) \
    using PFN_##name = ret(PHOTON_GLAPI*) args; \
    PFN_##name gl_##name = nullptr;
PHOTON_GL_FUNCS(PHOTON_GL_DECLARE)
#undef PHOTON_GL_DECLARE

bool loadGl() {
    bool ok = true;
#define PHOTON_GL_LOAD(ret, name, args)                                                     \
    gl_##name = reinterpret_cast<PFN_##name>(glfwGetProcAddress(#name));                    \
    if (!gl_##name) {                                                                       \
        std::fprintf(stderr, "RasterView: %s yok\n", #name);                                \
        ok = false;                                                                         \
    }
    PHOTON_GL_FUNCS(PHOTON_GL_LOAD)
#undef PHOTON_GL_LOAD
    return ok;
}

const char* kVertexSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uNormal;   // modelin tersi; devrik okunarak ters-devrik olur
out vec3 vW;
out vec3 vN;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vW = w.xyz;
    vN = mat3(uNormal) * aNrm;
    gl_Position = uViewProj * w;
}
)";

// Katı: yarımküre ortam (üst gök mavisi, alt sıcak gri) + kameranın biraz üstünden
// gelen ana ışık + yumuşak parlama + kenar ışığı. Malzemenin taban rengi doğrusaldır;
// çıktı yaklaşık sRGB (1/2.2) kodlanır. Normaller modu: dünya normali → renk.
const char* kShadeSrc = R"(#version 330 core
in vec3 vW;
in vec3 vN;
uniform vec3 uEye;
uniform vec3 uColor;
uniform vec3 uAccent;
uniform float uSelected;
uniform int uMode;
out vec4 oColor;
void main() {
    vec3 n = dot(vN, vN) > 1e-12 ? normalize(vN) : normalize(cross(dFdx(vW), dFdy(vW)));
    vec3 v = normalize(uEye - vW);
    if (dot(n, v) < 0.0) n = -n;   // içten görünen yüz de aydınlansın (açık mesh'ler)
    if (uMode == 1) { oColor = vec4(n * 0.5 + 0.5, 1.0); return; }
    vec3 L = normalize(v * 0.8 + vec3(0.0, 0.6, 0.0));
    float diff = max(dot(n, L), 0.0);
    vec3 amb = mix(vec3(0.20, 0.19, 0.18), vec3(0.40, 0.43, 0.48), 0.5 + 0.5 * n.y);
    vec3 base = mix(uColor, uAccent, uSelected * 0.35);
    float spec = pow(max(dot(n, normalize(L + v)), 0.0), 48.0) * 0.22;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.12;
    vec3 c = base * (amb + 0.85 * diff) + vec3(spec) + rim * vec3(0.6, 0.7, 0.8);
    oColor = vec4(pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
)";

// Tek renk; uFade > 0 ise odak noktasından (uEye: ızgarada kameranın baktığı nokta)
// yatayda uzaklaştıkça söner — ızgara ürünün çevresinde belirgin, ufka doğru kaybolur.
const char* kFlatSrc = R"(#version 330 core
in vec3 vW;
in vec3 vN;
uniform vec4 uColor;
uniform vec3 uEye;
uniform float uFade;
out vec4 oColor;
void main() {
    float a = uColor.a;
    if (uFade > 0.0) a *= 1.0 - smoothstep(uFade * 0.3, uFade, length(vW.xz - uEye.xz));
    oColor = vec4(uColor.rgb, a);
}
)";

// ── Gerçek zamanlı mod ──

const char* kPbrVertexSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uNormal;
out vec3 vW;
out vec3 vN;
out vec2 vUV;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vW = w.xyz;
    vN = mat3(uNormal) * aNrm;
    vUV = aUV;
    gl_Position = uViewProj * w;
}
)";

// Ortam, SH ışınımı ve ekran dönüşümü: yüzey ve arka plan gölgelendiricilerinde ortak.
// equirect(): directionToEquirect ile aynı (phi = atan2(−z, x) + π). display(): beyaz dengesi,
// pozlama ve viewport'un ton eşleme LUT'u (log2 shaper; realtime_scene.h ToneLut).
const char* kRealtimeCommonSrc = R"(
const float PI = 3.14159265359;
uniform sampler2D tEnv;
uniform float uEnvLevels;
uniform float uEnvWidth;
uniform vec3 uGroundExtra;
uniform float uExposure;
uniform mat3 uWB;
uniform sampler3D tLut;
uniform vec3 uLut;

vec3 display(vec3 hdr) {
    vec3 x = max(uWB * hdr * uExposure, vec3(1e-30));
    vec3 s = clamp((log2(x) - uLut.x) / (uLut.y - uLut.x), 0.0, 1.0);
    return texture(tLut, (s * (uLut.z - 1.0) + 0.5) / uLut.z).rgb;
}
vec2 equirect(vec3 d) {
    float theta = acos(clamp(d.y, -1.0, 1.0));
    float phi = atan(-d.z, d.x) + PI;
    return vec2(phi / (2.0 * PI), theta / PI);
}
vec3 envRadiance(vec3 d, float lod) {
    // Açık zeminde alt yarıküre: ortamla aydınlanmış zemin haritada; ışıkların payı uGroundExtra.
    return textureLod(tEnv, equirect(d), lod).rgb + uGroundExtra * clamp(0.5 - d.y * 8.0, 0.0, 1.0);
}
)";

const char* kPbrFragmentSrc = R"(
in vec3 vW;
in vec3 vN;
in vec2 vUV;
out vec4 oColor;

uniform vec3 uEye;
uniform vec3 uBase;
uniform float uMetal;
uniform float uRough;
uniform float uSpec;
uniform float uCoat;
uniform float uCoatRough;
uniform vec3 uEmit;
uniform int uGlass;        // 0 opak, 1 cam geçirgenlik geçişi, 2 cam yansıma geçişi
uniform float uIor;
uniform vec3 uTint;
uniform int uMaps;         // bit 0 albedo, 1 pürüzlülük, 2 metallik, 3 normal, 4 kutu eşleme
uniform float uTexScale;
uniform sampler2D tAlbedo;
uniform sampler2D tRough;
uniform sampler2D tMetal;
uniform sampler2D tNormal;
uniform vec3 uSH[9];

#define MAXL 16
uniform int uLightCount;
uniform vec4 uLPos[MAXL];  // xyz konum, w tür (0 nokta, 1 spot, 2 alan, 3 yönlü)
uniform vec4 uLDir[MAXL];  // xyz yön, w kaynak yarıçapı
uniform vec3 uLCol[MAXL];
uniform vec4 uLCone[MAXL]; // cosInner, cosOuter (−2: koni yok), gölge yuvası (−1: gölgesiz)
uniform mat4 uShadowVP[4];
uniform vec4 uShadowA[4];  // x: ortografik mi, y: yakın, z: uzak, w: harita uv'si / dünya birimi
uniform vec4 uShadowB[4];  // x: ışık yarıçapı (konumlu: dünya, yönlü: açısal), y: normal yönünde kaydırma
uniform sampler2DShadow tShadow;   // karşılaştırmalı (donanım PCF)
uniform sampler2D tShadowDepth;    // aynı doku, ham derinlik (engelleyici araması)
uniform sampler2DShadow tGlassShadow; // yalnız camlar (aynı izdüşüm, ½ çözünürlük)

uniform int uGround;       // 0 nesne, 1 zemin, 2 gölge yakalayıcı
uniform int uBgMode;       // arka plan: 0 ortam, 1 düz renk, 2 şeffaf
uniform vec3 uBgColor;
uniform int uFootCount;
uniform vec4 uFoot[32];
uniform vec4 uFootGap[8];  // 32 yükseklik, dörder paketli (eski sürücülerin uniform sınırı)
uniform float uSelected;
uniform vec3 uAccent;

vec3 shIrradiance(vec3 n) {
    vec3 e = uSH[0] * 0.282095
           + uSH[1] * (0.488603 * n.y) + uSH[2] * (0.488603 * n.z) + uSH[3] * (0.488603 * n.x)
           + uSH[4] * (1.092548 * n.x * n.y) + uSH[5] * (1.092548 * n.y * n.z)
           + uSH[6] * (0.315392 * (3.0 * n.z * n.z - 1.0)) + uSH[7] * (1.092548 * n.x * n.z)
           + uSH[8] * (0.546274 * (n.x * n.x - n.y * n.y));
    // Düzgün radyanslı alt yarıkürenin ışınımı: π/2·(1 − n_y).
    return max(e, vec3(0.0)) + uGroundExtra * (0.5 * PI * (1.0 - n.y));
}
float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float V_Smith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(vec3 f0, float c) { return f0 + (1.0 - f0) * pow(1.0 - c, 5.0); }
// Split-sum'un ön-integral terimi, analitik yaklaşım (Karis 2014, mobil).
vec3 envBrdf(vec3 f0, float rough, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}
// Pürüzlülük → mip: GGX lobunun açısal genişliği (~2α) bir teksele denk gelsin.
float envLod(float rough) {
    float a = max(rough * rough, 1e-4);
    return clamp(log2(2.0 * a * uEnvWidth / (2.0 * PI)), 0.0, uEnvLevels - 1.0);
}
// Harita derinliği → ışığa uzaklık (perspektifte GL derinliği doğrusal değil).
float lightDistance(int slot, float d) {
    vec4 A = uShadowA[slot];
    if (A.x > 0.5) return A.y + d * (A.z - A.y);
    float z = d * 2.0 - 1.0;
    return 2.0 * A.y * A.z / (A.z + A.y - z * (A.z - A.y));
}
// Vogel (altın açı) diski: az örnekle düzgün dağılım; piksel başına döndürülür (gürültü, şerit değil).
vec2 vogel(int i, int n, float phi) {
    float r = sqrt((float(i) + 0.5) / float(n));
    float t = float(i) * 2.39996323 + phi;
    return r * vec2(cos(t), sin(t));
}
// PCSS (Fernando 2005): engelleyicilerin ortalama uzaklığından yarı gölge genişliği; temas
// noktasında keskin, uzaklaştıkça yumuşayan gölge (büyük softbox'ta ışın izleyicideki gibi).
// 2×2 atlas: yuva k'nin karosu (k & 1, k >> 1)·½; örnekler karonun içinde tutulur. Kadrajın
// kenarına yaklaşırken gölge söner (sert bir kesim çizgisi kalmasın).
float shadowAt(int slot, vec3 p, vec3 ng) {
    vec4 A = uShadowA[slot], B = uShadowB[slot];
    vec4 s = uShadowVP[slot] * vec4(p + ng * B.y, 1.0);
    vec3 c = s.xyz / s.w * 0.5 + 0.5;
    if (c.x <= 0.0 || c.y <= 0.0 || c.x >= 1.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;
    float edge = smoothstep(0.0, 0.03, min(min(c.x, 1.0 - c.x), min(c.y, 1.0 - c.y)));
    vec2 tile = vec2(float(slot & 1), float(slot >> 1)) * 0.5;
    vec2 lo = tile + vec2(1.0 / 8192.0), hi = tile + vec2(0.5 - 1.0 / 8192.0);
    vec2 uv0 = tile + c.xy * 0.5;
    float zR = lightDistance(slot, c.z);
    bool ortho = A.x > 0.5;
    float uvw = 0.5 * (ortho ? A.w : A.w / zR);  // atlas uv'si / dünya birimi, alıcının düzleminde
    const float texel = 1.0 / 4096.0;
    float phi = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float searchW = ortho ? (zR - A.y) * B.x : B.x * (zR - A.y) / A.y;
    float searchUV = clamp(searchW * uvw, 2.0 * texel, 0.03);
    float zB = 0.0, nB = 0.0;
    for (int i = 0; i < 16; ++i) {
        float d = texture(tShadowDepth, clamp(uv0 + vogel(i, 16, phi) * searchUV, lo, hi)).r;
        if (d < c.z) {
            zB += lightDistance(slot, d);
            nB += 1.0;
        }
    }
    if (nB < 0.5) return 1.0;
    zB /= nB;
    float pen = ortho ? (zR - zB) * B.x : B.x * (zR - zB) / max(zB, 1e-4);
    float filterUV = clamp(pen * uvw, 1.5 * texel, 0.03);
    float sum = 0.0;
    for (int i = 0; i < 32; ++i) sum += texture(tShadow, vec3(clamp(uv0 + vogel(i, 32, phi) * filterUV, lo, hi), c.z));
    return mix(1.0, sum / 32.0, edge);
}
// Camın gölgesi: ışığın ~%55'ini keser (geri kalanı kırılarak geçer). Opak gölgeyle çarpılır.
float glassShadowAt(int slot, vec3 p, vec3 ng) {
    vec4 s = uShadowVP[slot] * vec4(p + ng * uShadowB[slot].y * 2.0, 1.0);
    vec3 c = s.xyz / s.w * 0.5 + 0.5;
    if (c.x <= 0.0 || c.y <= 0.0 || c.x >= 1.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;
    vec2 tile = vec2(float(slot & 1), float(slot >> 1)) * 0.5;
    vec2 uv0 = tile + c.xy * 0.5;
    const float t = 1.0 / 2048.0;
    float sum = 0.0;
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) sum += texture(tGlassShadow, vec3(uv0 + vec2(i, j) * t, c.z));
    return 1.0 - 0.55 * (1.0 - sum / 9.0);
}
// Temas gölgesi: nesne izlerinin altında gökyüzü ışığı kapanır (eliptik Gauss, zeminden
// yükseldikçe söner). Ekran uzayı AO'nun ucuz, titreşimsiz karşılığı.
float footAO(vec3 p) {
    float ao = 1.0;
    for (int i = 0; i < uFootCount; ++i) {
        vec4 f = uFoot[i];
        vec2 r = (p.xz - f.xy) / max(f.zw * 1.25, vec2(1e-4));
        float k = 0.7 * (1.0 - smoothstep(0.0, max(f.z, f.w), uFootGap[i >> 2][i & 3]));
        ao *= 1.0 - k * exp(-dot(r, r) * 1.6);
    }
    return ao;
}
// Teğetsiz normal haritası: türevlerden kotanjant çerçevesi (Schüler 2013).
mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float m = max(dot(t, t), dot(b, b));
    if (m < 1e-30) return mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), n);
    float inv = inversesqrt(m);
    return mat3(t * inv, b * inv, n);
}
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    vec3 V = normalize(uEye - vW);
    vec3 ng = normalize(cross(dFdx(vW), dFdy(vW)));
    if (dot(ng, V) < 0.0) ng = -ng;
    vec3 N = dot(vN, vN) > 1e-12 ? normalize(vN) : ng;
    if (dot(N, V) < 0.0) N = -N;

    vec3 base = uBase;
    float rough = uRough;
    float metal = uMetal;
    vec2 uv = vUV * uTexScale;
    if ((uMaps & 16) != 0) {
        vec3 a = abs(ng);
        uv = ((a.x >= a.y && a.x >= a.z) ? vW.zy : (a.y >= a.z ? vW.xz : vW.xy)) * uTexScale;
    }
    if ((uMaps & 1) != 0) base = texture(tAlbedo, uv).rgb;
    if ((uMaps & 2) != 0) rough = max(0.001, texture(tRough, uv).g);
    if ((uMaps & 4) != 0) metal = clamp(texture(tMetal, uv).b, 0.0, 1.0);
    if ((uMaps & 8) != 0) {
        vec3 t = texture(tNormal, uv).rgb * 2.0 - 1.0;
        vec3 pn = normalize(cotangentFrame(N, vW, uv) * t);
        N = dot(pn, N) < 0.0 ? -pn : pn;
    }

    float NoV = max(dot(N, V), 1e-4);
    float a = max(rough * rough, 0.002);
    vec3 f0, diffC;
    float coat = uCoat;
    if (uGlass != 0) {
        float r0 = (uIor - 1.0) / (uIor + 1.0);
        f0 = vec3(r0 * r0);
        diffC = vec3(0.0);
        coat = 0.0;
    } else {
        f0 = mix(vec3(0.08 * uSpec), base, metal);
        diffC = base * (1.0 - metal);
    }
    if (uGlass == 1) {
        // Geçirgenlik: arkadaki görüntü camın rengiyle çarpılır (ekran değerinde ≈ doğrusal^(1/2.2));
        // (1 − F) payı yansıma geçişinin harmanlamasından gelir.
        oColor = vec4(pow(max(uTint, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }
    float ac = max(uCoatRough * uCoatRough, 0.002);

    vec3 direct = vec3(0.0), directFull = vec3(0.0);
    vec3 R = reflect(-V, N);
    for (int i = 0; i < uLightCount; ++i) {
        int type = int(uLPos[i].w + 0.5);
        vec3 L, Ls, E;
        float aa = a, norm = 1.0;
        if (type == 3) {
            L = -uLDir[i].xyz;
            Ls = L;
            E = uLCol[i];
        } else {
            vec3 d = uLPos[i].xyz - vW;
            float d2 = max(dot(d, d), 1e-8);
            float dist = sqrt(d2);
            L = d / dist;
            float k = 1.0;
            if (type == 1 && uLCone[i].y > -1.5) {
                float c = dot(-L, uLDir[i].xyz);
                k = uLCone[i].x > uLCone[i].y ? smoothstep(uLCone[i].y, uLCone[i].x, c) : step(uLCone[i].y, c);
            }
            if (type == 2) k *= abs(dot(L, uLDir[i].xyz));
            E = uLCol[i] * (k / d2);
            Ls = L;
            float r = uLDir[i].w;
            if (r > 0.0) {
                // Küre ışık (Karis 2013): yansıma ışınına en yakın kaynak noktası + lob genişletme.
                vec3 c2r = dot(d, R) * R - d;
                Ls = normalize(d + c2r * clamp(r / max(length(c2r), 1e-6), 0.0, 1.0));
                aa = min(1.0, a + r / (2.0 * dist));
                norm = (a / aa) * (a / aa);
            }
        }
        float NoL = max(dot(N, L), 0.0);
        float NoLs = max(dot(N, Ls), 0.0);
        if (NoL <= 0.0 && NoLs <= 0.0) continue;
        vec3 H = normalize(V + Ls);
        float NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
        vec3 F = F_Schlick(f0, VoH);
        vec3 c = (1.0 - F) * diffC * (NoL / PI) + F * (D_GGX(NoH, aa) * norm * V_Smith(NoV, NoLs, aa) * NoLs);
        if (coat > 0.0) {
            float Fc = 0.04 + 0.96 * pow(1.0 - VoH, 5.0);
            float acc = min(1.0, ac + (aa - a));
            c = c * (1.0 - coat * Fc) + vec3(coat * Fc * D_GGX(NoH, acc) * (ac / acc) * (ac / acc) * V_Smith(NoV, NoLs, acc) * NoLs);
        }
        int slot = int(uLCone[i].z + 0.5);
        float sh = uLCone[i].z >= -0.5 ? shadowAt(slot, vW, ng) * glassShadowAt(slot, vW, ng) : 1.0;
        direct += c * E * sh;
        directFull += c * E;
    }

    float ao = uGround > 0 ? footAO(vW) : 1.0;
    vec3 amb = diffC * shIrradiance(N) / PI + envRadiance(R, envLod(rough)) * envBrdf(f0, rough, NoV);
    if (coat > 0.0) {
        float Fc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
        amb = amb * (1.0 - coat * Fc) + coat * envRadiance(R, envLod(uCoatRough)) * envBrdf(vec3(0.04), uCoatRough, NoV);
    }
    if (uGround == 2) {
        // Gölge yakalayıcı: gölgeli/gölgesiz aydınlanma oranı arka planı karartır (alfa ile).
        float full = lum(directFull + amb);
        float occ = full > 1e-8 ? clamp(1.0 - lum(direct + amb * ao) / full, 0.0, 1.0) : 0.0;
        oColor = vec4(0.0, 0.0, 0.0, occ);
        return;
    }
    if (uGlass == 2) {
        // Ön çarpımlı bindirme: F·yansıma + (1 − F)·w·kırılan ortam; geri kalan (1 − a) arkadaki
        // görüntüden gelir. Dolu camda kırılma kenarlara doğru büyür: w merkezde 0 (arkası görünür),
        // kenarda 1 (bükülmüş ortam) — ekran uzayı kırılması olmadan camın kalın görünüşü.
        float F = clamp(F_Schlick(f0, NoV).r, 0.0, 1.0);
        vec3 Lr = envRadiance(R, envLod(rough)) + direct / max(F, 1e-3);
        vec3 T = refract(-V, N, 1.0 / uIor);
        // Camın içinden görünen sonsuz: düz renk arka planda ışın izleyici o rengi gösterir.
        vec3 Lt = (uBgMode == 1 ? uBgColor : (dot(T, T) > 0.0 ? envRadiance(T, envLod(rough)) : Lr)) * uTint;
        float w = smoothstep(0.15, 0.85, 1.0 - NoV);
        float a = F + (1.0 - F) * w;
        oColor = vec4(F * display(Lr) + (1.0 - F) * w * display(Lt), a);
        return;
    }
    vec3 col = display(direct + amb * ao + uEmit);
    if (uSelected > 0.0) col = mix(col, uAccent, 0.06 + 0.4 * pow(1.0 - NoV, 2.0));
    oColor = vec4(col, 1.0);
}
)";

const char* kDepthVertexSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main() { gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }
)";
const char* kDepthFragmentSrc = R"(#version 330 core
void main() {}
)";

// Tam ekran üçgen: köşeler gl_VertexID'den, tampon yok.
const char* kBgVertexSrc = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";
const char* kBgFragmentSrc = R"(
in vec2 vNdc;
out vec4 oColor;
uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform int uMode;
uniform vec3 uColor;
void main() {
    if (uMode == 1) { oColor = vec4(display(uColor), 1.0); return; }
    vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w - uEye);
    oColor = vec4(display(envRadiance(d, 0.0)), 1.0);
}
)";

GLuint compile(GLenum type, const char* src) {
    const GLuint s = gl_glCreateShader(type);
    gl_glShaderSource(s, 1, &src, nullptr);
    gl_glCompileShader(s);
    GLint ok = 0;
    gl_glGetShaderiv(s, kCompileStatus, &ok);
    if (!ok) {
        char log[1024];
        gl_glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "RasterView shader: %s\n", log);
        gl_glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    const GLuint v = compile(kVertexShader, vs), f = compile(kFragmentShader, fs);
    if (!v || !f) return 0;
    const GLuint p = gl_glCreateProgram();
    gl_glAttachShader(p, v);
    gl_glAttachShader(p, f);
    gl_glLinkProgram(p);
    gl_glDeleteShader(v);
    gl_glDeleteShader(f);
    GLint ok = 0;
    gl_glGetProgramiv(p, kLinkStatus, &ok);
    if (!ok) {
        char log[1024];
        gl_glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "RasterView link: %s\n", log);
        gl_glDeleteProgram(p);
        return 0;
    }
    return p;
}

void setMat(GLuint prog, const char* name, const Mat4f& m, bool transpose = true) {
    // Mat4f satır öncelikli: GL'ye "devrik" bayrağıyla verilir. Bayraksız vermek
    // matrisin devriğini yükler (normal matrisi için kullanılır).
    gl_glUniformMatrix4fv(gl_glGetUniformLocation(prog, name), 1, transpose ? GL_TRUE : GL_FALSE, &m.data[0][0]);
}

GLint loc(GLuint prog, const char* name) { return gl_glGetUniformLocation(prog, name); }
void set1i(GLuint p, const char* n, int v) { gl_glUniform1i(loc(p, n), v); }
void set1f(GLuint p, const char* n, float v) { gl_glUniform1f(loc(p, n), v); }
void set3f(GLuint p, const char* n, const Vec3f& v) { gl_glUniform3f(loc(p, n), v.x, v.y, v.z); }
void set3c(GLuint p, const char* n, const Color3f& c) { gl_glUniform3f(loc(p, n), c.r, c.g, c.b); }

/// Görüntü → RGBA8 (en uzun kenar ≤ maxSize, kutu süzgeçli küçültme). sRGB: doğrusal değerler
/// sRGB kodlanır (GPU'da GL_SRGB8_ALPHA8 okurken geri çözer).
std::vector<uint8_t> imageBytes(const Image& img, bool srgb, int maxSize, int& w, int& h) {
    const int step = std::max(1, (std::max(img.width(), img.height()) + maxSize - 1) / maxSize);
    w = std::max(1, img.width() / step);
    h = std::max(1, img.height() / step);
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    const float inv = 1.0f / static_cast<float>(step * step);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Color3f c(0.0f);
            for (int j = 0; j < step; ++j)
                for (int i = 0; i < step; ++i) c += img.getPixel(std::min(x * step + i, img.width() - 1), std::min(y * step + j, img.height() - 1));
            c = c * inv;
            uint8_t* p = &out[(static_cast<size_t>(y) * w + x) * 4];
            const float v[3] = {c.r, c.g, c.b};
            for (int k = 0; k < 3; ++k) {
                const float e = srgb ? srgbEncode(std::clamp(v[k], 0.0f, 1.0f)) : std::clamp(v[k], 0.0f, 1.0f);
                p[k] = static_cast<uint8_t>(std::lround(e * 255.0f));
            }
            p[3] = 255;
        }
    return out;
}

GLuint linkSources(const std::string& vs, const std::string& fs) { return link(vs.c_str(), fs.c_str()); }

} // namespace

const char* viewModeName(ViewMode m) {
    switch (m) {
        case ViewMode::Render: return "Render";
        case ViewMode::Clay: return "Kil";
        case ViewMode::Solid: return "Katı";
        case ViewMode::Wire: return "Tel kafes";
        case ViewMode::Normals: return "Normaller";
        case ViewMode::Realtime: return "Gerçek zamanlı";
    }
    return "";
}

bool RasterView::init() {
    if (m_ready) return true;
    if (!loadGl()) return false;
    m_progShade = link(kVertexSrc, kShadeSrc);
    m_progFlat = link(kVertexSrc, kFlatSrc);
    if (!m_progShade || !m_progFlat) return false;
    // Gerçek zamanlı mod: derlenemezse (eski sürücü) yalnız o mod kapanır.
    const std::string head = "#version 330 core\n";
    m_progPbr = linkSources(kPbrVertexSrc, head + kRealtimeCommonSrc + kPbrFragmentSrc);
    m_progDepth = link(kDepthVertexSrc, kDepthFragmentSrc);
    m_progBg = linkSources(kBgVertexSrc, head + kRealtimeCommonSrc + kBgFragmentSrc);
    if (m_progPbr && m_progDepth && m_progBg) {
        // Birim zemin dörtgeni (konum, normal, uv); dünya dönüşümü GroundQuad'dan.
        const float q[4][8] = {{0, 0, 0, 0, 1, 0, 0, 0}, {1, 0, 0, 0, 1, 0, 1, 0}, {1, 0, 1, 0, 1, 0, 1, 1}, {0, 0, 1, 0, 1, 0, 0, 1}};
        const uint32_t qi[6] = {0, 2, 1, 0, 3, 2};
        gl_glGenVertexArrays(1, &m_quadVao);
        gl_glGenBuffers(1, &m_quadVbo);
        gl_glGenBuffers(1, &m_quadIbo);
        gl_glBindVertexArray(m_quadVao);
        gl_glBindBuffer(kArrayBuffer, m_quadVbo);
        gl_glBufferData(kArrayBuffer, sizeof(q), q, kStaticDraw);
        gl_glBindBuffer(kElementArrayBuffer, m_quadIbo);
        gl_glBufferData(kElementArrayBuffer, sizeof(qi), qi, kStaticDraw);
        for (GLuint a = 0; a < 3; ++a) {
            gl_glVertexAttribPointer(a, a == 2 ? 2 : 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                                     reinterpret_cast<const void*>(static_cast<size_t>(a * 3) * sizeof(float)));
            gl_glEnableVertexAttribArray(a);
        }
        gl_glGenVertexArrays(1, &m_emptyVao);
        gl_glBindVertexArray(0);
        // Gölge örnekleyicileri (dokular ilk gerçek zamanlı karede: ensureShadowTargets).
        gl_glGenSamplers(1, &m_sampCmp);
        gl_glSamplerParameteri(m_sampCmp, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_glSamplerParameteri(m_sampCmp, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_glSamplerParameteri(m_sampCmp, GL_TEXTURE_WRAP_S, kClampToEdge);
        gl_glSamplerParameteri(m_sampCmp, GL_TEXTURE_WRAP_T, kClampToEdge);
        gl_glSamplerParameteri(m_sampCmp, kTextureCompareMode, kCompareRefToTexture);
        gl_glSamplerParameteri(m_sampCmp, kTextureCompareFunc, GL_LEQUAL);
        gl_glGenSamplers(1, &m_sampRaw);
        gl_glSamplerParameteri(m_sampRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_glSamplerParameteri(m_sampRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_glSamplerParameteri(m_sampRaw, GL_TEXTURE_WRAP_S, kClampToEdge);
        gl_glSamplerParameteri(m_sampRaw, GL_TEXTURE_WRAP_T, kClampToEdge);
        gl_glSamplerParameteri(m_sampRaw, kTextureCompareMode, GL_NONE);
    }

    // Zemin ızgarası: XZ düzleminde [-50, 50] birimlik çizgiler; çizimde hücre
    // boyuna ölçeklenir. Her köşe (konum, normal) — gölgelendirici ortak.
    std::vector<float> v;
    const int n = 50;
    for (int i = -n; i <= n; ++i) {
        const float a = static_cast<float>(i), e = static_cast<float>(n);
        const float line[2][6] = {{a, 0, -e, 0, 1, 0}, {a, 0, e, 0, 1, 0}};
        const float line2[2][6] = {{-e, 0, a, 0, 1, 0}, {e, 0, a, 0, 1, 0}};
        for (const auto& p : line) v.insert(v.end(), p, p + 6);
        for (const auto& p : line2) v.insert(v.end(), p, p + 6);
    }
    m_gridVerts = static_cast<int>(v.size() / 6);
    gl_glGenVertexArrays(1, &m_gridVao);
    gl_glGenBuffers(1, &m_gridVbo);
    gl_glBindVertexArray(m_gridVao);
    gl_glBindBuffer(kArrayBuffer, m_gridVbo);
    gl_glBufferData(kArrayBuffer, static_cast<GLsizeiptr_>(v.size() * sizeof(float)), v.data(), kStaticDraw);
    gl_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_glEnableVertexAttribArray(0);
    gl_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl_glEnableVertexAttribArray(1);
    gl_glBindVertexArray(0);
    m_ready = true;
    return true;
}

void RasterView::release() {
    if (!m_ready) return;
    for (auto& [k, g] : m_meshes) {
        gl_glDeleteVertexArrays(1, &g.vao);
        gl_glDeleteBuffers(1, &g.vbo);
        gl_glDeleteBuffers(1, &g.ibo);
    }
    m_meshes.clear();
    gl_glDeleteVertexArrays(1, &m_gridVao);
    gl_glDeleteBuffers(1, &m_gridVbo);
    gl_glDeleteFramebuffers(1, &m_msFbo);
    gl_glDeleteRenderbuffers(1, &m_msColor);
    gl_glDeleteRenderbuffers(1, &m_msDepth);
    gl_glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_tex);
    gl_glDeleteProgram(m_progShade);
    gl_glDeleteProgram(m_progFlat);
    gl_glDeleteProgram(m_progPbr);
    gl_glDeleteProgram(m_progDepth);
    gl_glDeleteProgram(m_progBg);
    gl_glDeleteVertexArrays(1, &m_quadVao);
    gl_glDeleteBuffers(1, &m_quadVbo);
    gl_glDeleteBuffers(1, &m_quadIbo);
    gl_glDeleteVertexArrays(1, &m_emptyVao);
    gl_glDeleteFramebuffers(1, &m_shadowFbo);
    glDeleteTextures(1, &m_shadowTex);
    gl_glDeleteFramebuffers(1, &m_glassFbo);
    glDeleteTextures(1, &m_glassTex);
    gl_glDeleteSamplers(1, &m_sampCmp);
    gl_glDeleteSamplers(1, &m_sampRaw);
    glDeleteTextures(1, &m_envTex);
    glDeleteTextures(1, &m_lutTex);
    for (auto& map : m_textures) {
        for (auto& [k, t] : map) glDeleteTextures(1, &t.tex);
        map.clear();
    }
    m_shadowFbo = m_shadowTex = m_glassFbo = m_glassTex = m_envTex = m_lutTex = 0;
    m_shadowOk = false;
    m_envSource.reset();
    m_lutSource.reset();
    m_ready = false;
}

RasterView::GpuMesh* RasterView::upload(const std::shared_ptr<const TriangleMesh>& mesh) {
    auto it = m_meshes.find(mesh.get());
    // Aynı adreste yeni bir mesh (eskisi silinmiş) olabilir: weak_ptr eşleşmesine bak.
    if (it != m_meshes.end() && it->second.source.lock() == mesh) return &it->second;
    if (it != m_meshes.end()) {
        gl_glDeleteVertexArrays(1, &it->second.vao);
        gl_glDeleteBuffers(1, &it->second.vbo);
        gl_glDeleteBuffers(1, &it->second.ibo);
        m_meshes.erase(it);
    }
    // Köşe: konum, normal, uv (8 float). Normalsiz mesh'te normal 0: gölgelendirici yüz normaline düşer.
    const auto& p = mesh->positions();
    const auto& n = mesh->normals();
    const auto& t = mesh->uvs();
    std::vector<float> v;
    v.reserve(p.size() * 8);
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3f nn = i < n.size() ? n[i] : Vec3f(0.0f);
        const Vec2f uv = i < t.size() ? t[i] : Vec2f(0.0f);
        v.insert(v.end(), {p[i].x, p[i].y, p[i].z, nn.x, nn.y, nn.z, uv.x, uv.y});
    }
    GpuMesh g;
    g.source = mesh;
    g.indexCount = static_cast<int>(mesh->indices().size());
    gl_glGenVertexArrays(1, &g.vao);
    gl_glGenBuffers(1, &g.vbo);
    gl_glGenBuffers(1, &g.ibo);
    gl_glBindVertexArray(g.vao);
    gl_glBindBuffer(kArrayBuffer, g.vbo);
    gl_glBufferData(kArrayBuffer, static_cast<GLsizeiptr_>(v.size() * sizeof(float)), v.data(), kStaticDraw);
    gl_glBindBuffer(kElementArrayBuffer, g.ibo);
    gl_glBufferData(kElementArrayBuffer, static_cast<GLsizeiptr_>(mesh->indices().size() * sizeof(uint32_t)),
                    mesh->indices().data(), kStaticDraw);
    gl_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    gl_glEnableVertexAttribArray(0);
    gl_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl_glEnableVertexAttribArray(1);
    gl_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<const void*>(6 * sizeof(float)));
    gl_glEnableVertexAttribArray(2);
    gl_glBindVertexArray(0);
    return &(m_meshes[mesh.get()] = g);
}

unsigned int RasterView::texture(const std::shared_ptr<const Image>& image, bool srgb) {
    if (!image || image->width() <= 0 || image->height() <= 0) return 0;
    auto& map = m_textures[srgb ? 1 : 0];
    auto it = map.find(image.get());
    if (it != map.end() && it->second.source.lock() == image) return it->second.tex;
    if (it != map.end()) {
        glDeleteTextures(1, &it->second.tex);
        map.erase(it);
    }
    int w = 0, h = 0;
    const std::vector<uint8_t> bytes = imageBytes(*image, srgb, 2048, w, h);
    GpuTexture t;
    t.source = image;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? kSrgb8Alpha8 : kRgba8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl_glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    map[image.get()] = t;
    return t.tex;
}

// Gölge atlasları (~80 MB) yalnız gerçek zamanlı mod ilk kez açılınca ayrılır.
bool RasterView::ensureShadowTargets() {
    if (m_shadowFbo) return m_shadowOk;
    auto depthTarget = [&](GLuint& tex, GLuint& fbo, int size) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, kDepthComponent24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl_glGenFramebuffers(1, &fbo);
        gl_glBindFramebuffer(kFramebuffer, fbo);
        gl_glFramebufferTexture2D(kFramebuffer, kDepthAttachment, GL_TEXTURE_2D, tex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool ok = gl_glCheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
        gl_glBindFramebuffer(kFramebuffer, 0);
        return ok;
    };
    // Atlas: 4 ışık × 2048²; camlar ayrı ve yarı çözünürlükte.
    const bool a = depthTarget(m_shadowTex, m_shadowFbo, kShadowAtlas);
    const bool b = depthTarget(m_glassTex, m_glassFbo, kShadowSize);
    m_shadowOk = a && b;
    if (!m_shadowOk) std::fprintf(stderr, "RasterView: gölge haritası kurulamadı; gerçek zamanlı mod kapalı\n");
    return m_shadowOk;
}

void RasterView::drawMesh(const GpuMesh& g) const {
    gl_glBindVertexArray(g.vao);
    glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, nullptr);
}

// Silinen mesh'lerin GPU kopyalarını bırak (bellek sızıntısı olmasın).
void RasterView::purge() {
    for (auto it = m_meshes.begin(); it != m_meshes.end();) {
        if (it->second.source.expired()) {
            gl_glDeleteVertexArrays(1, &it->second.vao);
            gl_glDeleteBuffers(1, &it->second.vbo);
            gl_glDeleteBuffers(1, &it->second.ibo);
            it = m_meshes.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& map : m_textures)
        for (auto it = map.begin(); it != map.end();) {
            if (it->second.source.expired()) {
                glDeleteTextures(1, &it->second.tex);
                it = map.erase(it);
            } else {
                ++it;
            }
        }
}

// Nesneler bir kez yaratılır; boyut değişince yalnız depolamaları yeniden ayrılır.
// Doku kimliği değişmez: ImGui bu karenin çizim listesinde onu zaten kullanıyor,
// silinip yeniden yaratılsaydı bir kare boyunca siyah görünürdü.
bool RasterView::ensureTargets(int w, int h) {
    if (w == m_w && h == m_h && m_fbo) return true;
    if (!m_fbo) {
        gl_glGenRenderbuffers(1, &m_msColor);
        gl_glGenRenderbuffers(1, &m_msDepth);
        gl_glGenFramebuffers(1, &m_msFbo);
        gl_glGenFramebuffers(1, &m_fbo);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
    }
    GLint maxSamples = 1;
    glGetIntegerv(kMaxSamples, &maxSamples);
    const GLsizei samples = std::clamp<GLint>(maxSamples, 1, 4);
    gl_glBindRenderbuffer(kRenderbuffer, m_msColor);
    gl_glRenderbufferStorageMultisample(kRenderbuffer, samples, kRgba8, w, h);
    gl_glBindRenderbuffer(kRenderbuffer, m_msDepth);
    gl_glRenderbufferStorageMultisample(kRenderbuffer, samples, kDepthComponent24, w, h);
    gl_glBindFramebuffer(kFramebuffer, m_msFbo);
    gl_glFramebufferRenderbuffer(kFramebuffer, kColorAttachment0, kRenderbuffer, m_msColor);
    gl_glFramebufferRenderbuffer(kFramebuffer, kDepthAttachment, kRenderbuffer, m_msDepth);
    const bool msOk = gl_glCheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    gl_glBindFramebuffer(kFramebuffer, m_fbo);
    gl_glFramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, m_tex, 0);
    const bool ok = msOk && gl_glCheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
    gl_glBindFramebuffer(kFramebuffer, 0);
    m_w = w;
    m_h = h;
    return ok;
}

unsigned int RasterView::render(const Frame& f, const std::vector<Item>& items, ViewMode mode, bool overlay) {
    if (!m_ready || f.width <= 0 || f.height <= 0) return 0;
    purge();
    if (!ensureTargets(f.width, f.height)) return 0;
    if (mode == ViewMode::Realtime && !overlay) {
        if (!m_progPbr || !f.realtime || !ensureShadowTargets()) mode = ViewMode::Solid; // sürücü desteklemiyorsa Katı
        else {
            renderRealtime(f, items);
            gl_glBindFramebuffer(kReadFramebuffer, m_msFbo);
            gl_glBindFramebuffer(kDrawFramebuffer, m_fbo);
            gl_glBlitFramebuffer(0, 0, f.width, f.height, 0, 0, f.width, f.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            gl_glBindFramebuffer(kFramebuffer, 0);
            gl_glBindVertexArray(0);
            gl_glUseProgram(0);
            gl_glActiveTexture(kTexture0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glEnable(GL_BLEND);
            gl_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            return m_tex;
        }
    }

    gl_glBindFramebuffer(kFramebuffer, m_msFbo);
    glViewport(0, 0, f.width, f.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // Arka plan: koyu nötr gri (arayüzün bg0'ından biraz açık, modeller öne çıksın).
    if (overlay) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    else glClearColor(0.105f, 0.11f, 0.125f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    gl_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const Mat4f viewProj = f.proj * f.view;
    const bool wire = overlay || mode == ViewMode::Wire;

    // ── Yüzeyler ──
    // Tel kafeste yüzey yalnız arkadaki kenarları örter; bindirmede renge hiç yazılmaz.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    if (overlay) glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    const GLuint surfProg = wire ? m_progFlat : m_progShade;
    gl_glUseProgram(surfProg);
    setMat(surfProg, "uViewProj", viewProj);
    if (wire) {
        gl_glUniform4f(gl_glGetUniformLocation(surfProg, "uColor"), 0.135f, 0.14f, 0.155f, 1.0f);
        gl_glUniform1f(gl_glGetUniformLocation(surfProg, "uFade"), 0.0f);
    } else {
        gl_glUniform3f(gl_glGetUniformLocation(surfProg, "uEye"), f.eye.x, f.eye.y, f.eye.z);
        gl_glUniform3f(gl_glGetUniformLocation(surfProg, "uAccent"), 1.0f, 0.54f, 0.24f);
        gl_glUniform1i(gl_glGetUniformLocation(surfProg, "uMode"), mode == ViewMode::Normals ? 1 : 0);
    }
    for (const Item& it : items) {
        GpuMesh* g = upload(it.mesh);
        if (!g) continue;
        setMat(surfProg, "uModel", it.model);
        setMat(surfProg, "uNormal", it.model.inverse(), false);
        if (!wire) {
            gl_glUniform3f(gl_glGetUniformLocation(surfProg, "uColor"), it.color.r, it.color.g, it.color.b);
            gl_glUniform1f(gl_glGetUniformLocation(surfProg, "uSelected"), it.selected ? 1.0f : 0.0f);
        }
        gl_glBindVertexArray(g->vao);
        glDrawElements(GL_TRIANGLES, g->indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // ── Kenarlar ──
    if (wire) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        gl_glUseProgram(m_progFlat);
        setMat(m_progFlat, "uViewProj", viewProj);
        gl_glUniform1f(gl_glGetUniformLocation(m_progFlat, "uFade"), 0.0f);
        const GLint colLoc = gl_glGetUniformLocation(m_progFlat, "uColor");
        for (const Item& it : items) {
            GpuMesh* g = upload(it.mesh);
            if (!g) continue;
            setMat(m_progFlat, "uModel", it.model);
            if (it.selected) gl_glUniform4f(colLoc, 1.0f, 0.54f, 0.24f, 1.0f);
            else if (overlay) gl_glUniform4f(colLoc, 0.92f, 0.93f, 0.95f, 1.0f);
            else gl_glUniform4f(colLoc, 0.62f, 0.64f, 0.68f, 1.0f);
            gl_glBindVertexArray(g->vao);
            glDrawElements(GL_TRIANGLES, g->indexCount, GL_UNSIGNED_INT, nullptr);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ── Zemin ızgarası ──
    if (!overlay && f.grid && m_gridVerts > 0) {
        glDepthMask(GL_FALSE);
        gl_glUseProgram(m_progFlat);
        setMat(m_progFlat, "uViewProj", viewProj);
        // Izgara kameranın altında kalsın: hücreye yuvarlanmış konuma taşınır (çizgiler
        // dünyada sabit görünür, kamera dolaşınca kaymaz), hücre boyuna ölçeklenir.
        Mat4f m = Transform::translate(Vec3f(std::floor(f.focus.x / f.gridStep) * f.gridStep, f.groundY,
                                             std::floor(f.focus.z / f.gridStep) * f.gridStep)).matrix();
        m(0, 0) = m(1, 1) = m(2, 2) = f.gridStep;
        setMat(m_progFlat, "uModel", m);
        gl_glUniform3f(gl_glGetUniformLocation(m_progFlat, "uEye"), f.focus.x, f.focus.y, f.focus.z);
        gl_glUniform1f(gl_glGetUniformLocation(m_progFlat, "uFade"), f.gridStep * 50.0f);
        gl_glUniform4f(gl_glGetUniformLocation(m_progFlat, "uColor"), 0.6f, 0.62f, 0.68f, 0.32f);
        gl_glBindVertexArray(m_gridVao);
        glDrawArrays(GL_LINES, 0, m_gridVerts);
        glDepthMask(GL_TRUE);
    }

    // Çok örnekli hedefi dokuya çöz (kenar yumuşatma burada olur).
    gl_glBindFramebuffer(kReadFramebuffer, m_msFbo);
    gl_glBindFramebuffer(kDrawFramebuffer, m_fbo);
    gl_glBlitFramebuffer(0, 0, f.width, f.height, 0, 0, f.width, f.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // ImGui'nin kendi kurmadığı durumları geri al.
    gl_glBindFramebuffer(kFramebuffer, 0);
    gl_glBindVertexArray(0);
    gl_glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    return m_tex;
}

void RasterView::renderRealtime(const Frame& f, const std::vector<Item>& items) {
    const Realtime& rt = *f.realtime;
    // ── Ortam haritası ve ton eşleme LUT'u: yalnız değişince yüklenir ──
    if (rt.env && m_envSource.lock() != rt.env) {
        if (!m_envTex) glGenTextures(1, &m_envTex);
        glBindTexture(GL_TEXTURE_2D, m_envTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgb16f, rt.env->width, rt.env->height, 0, GL_RGB, GL_FLOAT, rt.env->rgb.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl_glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   // u sarılır
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge); // kutuplar
        m_envLevels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(rt.env->width, rt.env->height)))));
        m_envSource = rt.env;
    }
    if (rt.lut && m_lutSource.lock() != rt.lut) {
        if (!m_lutTex) glGenTextures(1, &m_lutTex);
        glBindTexture(kTexture3D, m_lutTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_glTexImage3D(kTexture3D, 0, kRgb8, rt.lut->size, rt.lut->size, rt.lut->size, 0, GL_RGB, GL_UNSIGNED_BYTE, rt.lut->rgb.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(kTexture3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(kTexture3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(kTexture3D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(kTexture3D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexParameteri(kTexture3D, kTextureWrapR, kClampToEdge);
        glBindTexture(kTexture3D, 0);
        m_lutSource = rt.lut;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    const Mat4f viewProj = f.proj * f.view;
    const AABB& box = rt.sceneBounds;
    const int lightCount = static_cast<int>(std::min<size_t>(rt.lights.size(), 16));

    // ── 1. Gölge haritaları: gölgeli her ışık atlasın bir karosuna (en çok 4) ──
    int slotOf[16];
    std::fill(slotOf, slotOf + 16, -1);
    Mat4f shadowVP[4] = {Mat4f::identity(), Mat4f::identity(), Mat4f::identity(), Mat4f::identity()};
    float shadowA[16] = {}, shadowB[16] = {};
    int slots = 0;
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    gl_glBindFramebuffer(kFramebuffer, m_shadowFbo);
    glViewport(0, 0, kShadowAtlas, kShadowAtlas);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    gl_glUseProgram(m_progDepth);
    for (int i = 0; i < lightCount && slots < 4; ++i) {
        const RealtimeLight& L = rt.lights[static_cast<size_t>(i)];
        if (!L.castsShadow) continue;
        const int k = slots++;
        slotOf[i] = k;
        const ShadowProjection sp = shadowProjection(L, box);
        shadowVP[k] = sp.viewProj;
        // Normal yönünde kaydırma: alıcının düzleminde ~1.5 teksel (gölge sivilcesi olmasın).
        const float dist = sp.ortho ? 1.0f : std::max(1e-3f, (L.position - box.centroid()).length());
        const float texelWorld = dist / (sp.uvPerWorld * kShadowSize);
        shadowA[k * 4] = sp.ortho ? 1.0f : 0.0f;
        shadowA[k * 4 + 1] = sp.zNear;
        shadowA[k * 4 + 2] = sp.zFar;
        shadowA[k * 4 + 3] = sp.uvPerWorld;
        shadowB[k * 4] = L.radius;
        shadowB[k * 4 + 1] = 1.5f * texelWorld;
        glViewport((k & 1) * kShadowSize, (k >> 1) * kShadowSize, kShadowSize, kShadowSize);
        setMat(m_progDepth, "uViewProj", shadowVP[k]);
        for (const Item& it : items) {
            // Cam ışığı geçirir: ışın izleyicide gölgesi neredeyse yok (kostik bile düşer); opak
            // gölge düşürmesi yanlış ve göze batan bir kama bırakırdı.
            if (it.material && it.material->glass) continue;
            GpuMesh* g = upload(it.mesh);
            if (!g) continue;
            setMat(m_progDepth, "uModel", it.model);
            drawMesh(*g);
        }
    }
    // Camların derinliği (aynı izdüşümler, ½ çözünürlüklü atlas): kısmi gölge.
    gl_glBindFramebuffer(kFramebuffer, m_glassFbo);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    for (int k = 0; k < slots; ++k) {
        glViewport((k & 1) * (kShadowSize / 2), (k >> 1) * (kShadowSize / 2), kShadowSize / 2, kShadowSize / 2);
        setMat(m_progDepth, "uViewProj", shadowVP[k]);
        for (const Item& it : items) {
            if (!it.material || !it.material->glass) continue;
            GpuMesh* g = upload(it.mesh);
            if (!g) continue;
            setMat(m_progDepth, "uModel", it.model);
            drawMesh(*g);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ── 2. Arka plan ──
    gl_glBindFramebuffer(kFramebuffer, m_msFbo);
    glViewport(0, 0, f.width, f.height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, rt.background == 2 ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_envTex || !m_lutTex) return;
    gl_glActiveTexture(kTexture0);
    glBindTexture(GL_TEXTURE_2D, m_envTex);
    gl_glActiveTexture(kTexture0 + 1);
    glBindTexture(kTexture3D, m_lutTex);
    gl_glActiveTexture(kTexture0 + 2);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    gl_glBindSampler(2, m_sampCmp);
    gl_glActiveTexture(kTexture0 + 7);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    gl_glBindSampler(7, m_sampRaw);
    gl_glActiveTexture(kTexture0 + 8);
    glBindTexture(GL_TEXTURE_2D, m_glassTex);
    gl_glBindSampler(8, m_sampCmp);
    auto setCommon = [&](GLuint p) {
        gl_glUseProgram(p);
        set1i(p, "tEnv", 0);
        set1i(p, "tLut", 1);
        set1f(p, "uEnvLevels", static_cast<float>(m_envLevels));
        set1f(p, "uEnvWidth", static_cast<float>(rt.env ? rt.env->width : 1));
        set3c(p, "uGroundExtra", rt.groundExtra);
        set1f(p, "uExposure", rt.exposure);
        gl_glUniformMatrix3fv(loc(p, "uWB"), 1, GL_TRUE, rt.whiteBalance);
        gl_glUniform3f(loc(p, "uLut"), rt.lut->minLog2, rt.lut->maxLog2, static_cast<float>(rt.lut->size));
    };
    if (rt.background != 2) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        setCommon(m_progBg);
        setMat(m_progBg, "uInvViewProj", viewProj.inverse());
        set3f(m_progBg, "uEye", f.eye);
        set1i(m_progBg, "uMode", rt.background);
        set3c(m_progBg, "uColor", rt.backgroundColor);
        gl_glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // ── 3. Opak yüzeyler ──
    const GLuint p = m_progPbr;
    setCommon(p);
    setMat(p, "uViewProj", viewProj);
    set3f(p, "uEye", f.eye);
    set1i(p, "tShadow", 2);
    set1i(p, "tAlbedo", 3);
    set1i(p, "tRough", 4);
    set1i(p, "tMetal", 5);
    set1i(p, "tNormal", 6);
    float sh[27] = {};
    if (rt.env)
        for (int i = 0; i < 9; ++i)
            for (int c = 0; c < 3; ++c) sh[i * 3 + c] = rt.env->sh[i][c];
    gl_glUniform3fv(loc(p, "uSH"), 9, sh);
    float lpos[64] = {}, ldir[64] = {}, lcol[48] = {}, lcone[64] = {};
    for (int i = 0; i < lightCount; ++i) {
        const RealtimeLight& L = rt.lights[static_cast<size_t>(i)];
        const float pp[4] = {L.position.x, L.position.y, L.position.z, static_cast<float>(static_cast<int>(L.type))};
        const float dd[4] = {L.direction.x, L.direction.y, L.direction.z, L.radius};
        std::copy(pp, pp + 4, lpos + i * 4);
        std::copy(dd, dd + 4, ldir + i * 4);
        lcol[i * 3] = L.intensity.r;
        lcol[i * 3 + 1] = L.intensity.g;
        lcol[i * 3 + 2] = L.intensity.b;
        lcone[i * 4] = L.cosInner;
        lcone[i * 4 + 1] = L.cosOuter;
        lcone[i * 4 + 2] = static_cast<float>(slotOf[i]);
    }
    set1i(p, "uLightCount", lightCount);
    if (lightCount > 0) {
        gl_glUniform4fv(loc(p, "uLPos"), lightCount, lpos);
        gl_glUniform4fv(loc(p, "uLDir"), lightCount, ldir);
        gl_glUniform3fv(loc(p, "uLCol"), lightCount, lcol);
        gl_glUniform4fv(loc(p, "uLCone"), lightCount, lcone);
    }
    float vps[64];
    for (int k = 0; k < 4; ++k)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) vps[k * 16 + r * 4 + c] = shadowVP[k].data[r][c];
    gl_glUniformMatrix4fv(loc(p, "uShadowVP"), 4, GL_TRUE, vps);
    gl_glUniform4fv(loc(p, "uShadowA"), 4, shadowA);
    gl_glUniform4fv(loc(p, "uShadowB"), 4, shadowB);
    set1i(p, "tShadowDepth", 7);
    set1i(p, "tGlassShadow", 8);
    const int fc = static_cast<int>(std::min<size_t>({rt.footprints.size(), rt.footprintGaps.size(), 32}));
    float foot[128] = {}, gaps[32] = {};
    for (int i = 0; i < fc; ++i) {
        const Vec4f& v = rt.footprints[static_cast<size_t>(i)];
        foot[i * 4] = v.x;
        foot[i * 4 + 1] = v.y;
        foot[i * 4 + 2] = v.z;
        foot[i * 4 + 3] = v.w;
        gaps[i] = rt.footprintGaps[static_cast<size_t>(i)];
    }
    set1i(p, "uFootCount", fc);
    if (fc > 0) {
        gl_glUniform4fv(loc(p, "uFoot"), fc, foot);
        gl_glUniform4fv(loc(p, "uFootGap"), 8, gaps);
    }
    gl_glUniform3f(loc(p, "uAccent"), 1.0f, 0.54f, 0.24f);
    set1i(p, "uBgMode", rt.background);
    set3c(p, "uBgColor", rt.backgroundColor);
    set1i(p, "uGround", 0);

    auto bindMap = [&](int unit, const std::shared_ptr<const Image>& img, bool srgb) {
        const GLuint t = texture(img, srgb);
        if (!t) return false;
        gl_glActiveTexture(kTexture0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, t);
        return true;
    };
    auto setMaterial = [&](const Material& m, bool selected) {
        set3c(p, "uBase", m.baseColor);
        set1f(p, "uMetal", m.metallic);
        set1f(p, "uRough", m.roughness);
        set1f(p, "uSpec", m.specular);
        set1f(p, "uCoat", m.clearCoat);
        set1f(p, "uCoatRough", m.clearCoatRoughness);
        set3c(p, "uEmit", m.emission);
        set1f(p, "uIor", m.ior);
        set3c(p, "uTint", m.tint);
        int maps = 0;
        if (bindMap(3, m.albedo, true)) maps |= 1;
        if (bindMap(4, m.roughnessMap, false)) maps |= 2;
        if (bindMap(5, m.metalnessMap, false)) maps |= 4;
        if (bindMap(6, m.normalMap, false)) maps |= 8;
        if (maps && m.boxMapping) maps |= 16;
        set1i(p, "uMaps", maps);
        set1f(p, "uTexScale", m.texScale);
        set1f(p, "uSelected", selected ? 1.0f : 0.0f);
    };
    std::vector<const Item*> glass;
    set1i(p, "uGlass", 0);
    for (const Item& it : items) {
        if (it.material && it.material->glass) {
            glass.push_back(&it);
            continue;
        }
        GpuMesh* g = upload(it.mesh);
        if (!g) continue;
        Material fallback;
        fallback.baseColor = it.color;
        setMaterial(it.material ? *it.material : fallback, it.selected);
        setMat(p, "uModel", it.model);
        setMat(p, "uNormal", it.model.inverse(), false);
        drawMesh(*g);
    }

    // ── 4. Zemin ──
    if (rt.ground) {
        const GroundQuad& q = rt.groundQuad;
        Mat4f m = Mat4f::identity();
        for (int r = 0; r < 3; ++r) {
            m.data[r][0] = q.edgeU[r];
            m.data[r][1] = r == 1 ? 1.0f : 0.0f;
            m.data[r][2] = q.edgeV[r];
            m.data[r][3] = q.corner[r];
        }
        Material gm;
        gm.baseColor = rt.groundColor;
        gm.roughness = rt.groundRoughness;
        setMaterial(gm, false);
        setMat(p, "uModel", m);
        setMat(p, "uNormal", m.inverse(), false);
        set1i(p, "uGround", rt.groundShadowOnly ? 2 : 1);
        if (rt.groundShadowOnly) {
            glEnable(GL_BLEND);
            gl_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        gl_glBindVertexArray(m_quadVao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        set1i(p, "uGround", 0);
    }

    // ── 5. Cam: arkadan öne; önce geçirgenlik (çarpım), sonra yansıma (ekleme) ──
    if (!glass.empty()) {
        auto distOf = [&](const Item* it) {
            const Vec3f c(it->model.data[0][3], it->model.data[1][3], it->model.data[2][3]);
            return (c - f.eye).lengthSquared();
        };
        std::sort(glass.begin(), glass.end(), [&](const Item* a, const Item* b) { return distOf(a) > distOf(b); });
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        for (const Item* it : glass) {
            GpuMesh* g = upload(it->mesh);
            if (!g) continue;
            setMaterial(*it->material, it->selected);
            setMat(p, "uModel", it->model);
            setMat(p, "uNormal", it->model.inverse(), false);
            set1i(p, "uGlass", 1);
            glBlendFunc(GL_ZERO, GL_SRC_COLOR);
            drawMesh(*g);
            set1i(p, "uGlass", 2);
            gl_glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            drawMesh(*g);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        set1i(p, "uGlass", 0);
    }
    gl_glBindSampler(2, 0);
    gl_glBindSampler(7, 0);
    gl_glBindSampler(8, 0);
    for (int u = 8; u >= 0; --u) {
        gl_glActiveTexture(kTexture0 + static_cast<GLenum>(u));
        glBindTexture(GL_TEXTURE_2D, 0);
        if (u == 1) glBindTexture(kTexture3D, 0);
    }
}

} // namespace photon
