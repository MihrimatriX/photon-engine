#include "preview/gl_preview.h"
#include "lights/environment_light.h"
#include "geometry/sphere.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "materials/dielectric.h"
#include "core/math/constants.h"
#include "core/math/vec.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace photon {
namespace {

// ── Minimal GL 3.3 loader (no new deps; glfw GetProcAddress) ───────────────
using GLchar = char;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGB16F 0x881B
#define GL_RGBA8 0x8058
#define GL_STREAM_DRAW 0x88E0
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_CCW 0x0901
#define GL_BACK 0x0405
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_VIEWPORT 0x0BA2
#endif

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif
#ifndef GL_LEQUAL
#define GL_LEQUAL 0x0203
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

using PFNGLCREATESHADERPROC = unsigned int (*)(unsigned int);
using PFNGLSHADERSOURCEPROC = void (*)(unsigned int, int, const char* const*, const int*);
using PFNGLCOMPILESHADERPROC = void (*)(unsigned int);
using PFNGLGETSHADERIVPROC = void (*)(unsigned int, unsigned int, int*);
using PFNGLGETSHADERINFOLOGPROC = void (*)(unsigned int, int, int*, char*);
using PFNGLDELETESHADERPROC = void (*)(unsigned int);
using PFNGLCREATEPROGRAMPROC = unsigned int (*)();
using PFNGLATTACHSHADERPROC = void (*)(unsigned int, unsigned int);
using PFNGLLINKPROGRAMPROC = void (*)(unsigned int);
using PFNGLGETPROGRAMIVPROC = void (*)(unsigned int, unsigned int, int*);
using PFNGLGETPROGRAMINFOLOGPROC = void (*)(unsigned int, int, int*, char*);
using PFNGLDELETEPROGRAMPROC = void (*)(unsigned int);
using PFNGLUSEPROGRAMPROC = void (*)(unsigned int);
using PFNGLGETUNIFORMLOCATIONPROC = int (*)(unsigned int, const char*);
using PFNGLUNIFORM1IPROC = void (*)(int, int);
using PFNGLUNIFORM1FPROC = void (*)(int, float);
using PFNGLUNIFORM3FVPROC = void (*)(int, int, const float*);
using PFNGLUNIFORMMATRIX4FVPROC = void (*)(int, int, unsigned char, const float*);
using PFNGLGENVERTEXARRAYSPROC = void (*)(int, unsigned int*);
using PFNGLBINDVERTEXARRAYPROC = void (*)(unsigned int);
using PFNGLDELETEVERTEXARRAYSPROC = void (*)(int, const unsigned int*);
using PFNGLGENBUFFERSPROC = void (*)(int, unsigned int*);
using PFNGLBINDBUFFERPROC = void (*)(unsigned int, unsigned int);
using PFNGLBUFFERDATAPROC = void (*)(unsigned int, GLsizeiptr, const void*, unsigned int);
using PFNGLDELETEBUFFERSPROC = void (*)(int, const unsigned int*);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (*)(unsigned int);
using PFNGLVERTEXATTRIBPOINTERPROC = void (*)(unsigned int, int, unsigned int, unsigned char, int, const void*);
using PFNGLGENFRAMEBUFFERSPROC = void (*)(int, unsigned int*);
using PFNGLBINDFRAMEBUFFERPROC = void (*)(unsigned int, unsigned int);
using PFNGLDELETEFRAMEBUFFERSPROC = void (*)(int, const unsigned int*);
using PFNGLFRAMEBUFFERTEXTURE2DPROC = void (*)(unsigned int, unsigned int, unsigned int, unsigned int, int);
using PFNGLGENRENDERBUFFERSPROC = void (*)(int, unsigned int*);
using PFNGLBINDRENDERBUFFERPROC = void (*)(unsigned int, unsigned int);
using PFNGLDELETERENDERBUFFERSPROC = void (*)(int, const unsigned int*);
using PFNGLRENDERBUFFERSTORAGEPROC = void (*)(unsigned int, unsigned int, int, int);
using PFNGLFRAMEBUFFERRENDERBUFFERPROC = void (*)(unsigned int, unsigned int, unsigned int, unsigned int);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC = unsigned int (*)(unsigned int);
using PFNGLACTIVETEXTUREPROC = void (*)(unsigned int);
using PFNGLDRAWBUFFERSPROC = void (*)(int, const unsigned int*);

#define PH_GL_FUNCS \
    X(CreateShader, PFNGLCREATESHADERPROC) \
    X(ShaderSource, PFNGLSHADERSOURCEPROC) \
    X(CompileShader, PFNGLCOMPILESHADERPROC) \
    X(GetShaderiv, PFNGLGETSHADERIVPROC) \
    X(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC) \
    X(DeleteShader, PFNGLDELETESHADERPROC) \
    X(CreateProgram, PFNGLCREATEPROGRAMPROC) \
    X(AttachShader, PFNGLATTACHSHADERPROC) \
    X(LinkProgram, PFNGLLINKPROGRAMPROC) \
    X(GetProgramiv, PFNGLGETPROGRAMIVPROC) \
    X(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC) \
    X(DeleteProgram, PFNGLDELETEPROGRAMPROC) \
    X(UseProgram, PFNGLUSEPROGRAMPROC) \
    X(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC) \
    X(Uniform1i, PFNGLUNIFORM1IPROC) \
    X(Uniform1f, PFNGLUNIFORM1FPROC) \
    X(Uniform3fv, PFNGLUNIFORM3FVPROC) \
    X(UniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC) \
    X(GenVertexArrays, PFNGLGENVERTEXARRAYSPROC) \
    X(BindVertexArray, PFNGLBINDVERTEXARRAYPROC) \
    X(DeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC) \
    X(GenBuffers, PFNGLGENBUFFERSPROC) \
    X(BindBuffer, PFNGLBINDBUFFERPROC) \
    X(BufferData, PFNGLBUFFERDATAPROC) \
    X(DeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    X(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    X(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC) \
    X(GenFramebuffers, PFNGLGENFRAMEBUFFERSPROC) \
    X(BindFramebuffer, PFNGLBINDFRAMEBUFFERPROC) \
    X(DeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC) \
    X(FramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC) \
    X(GenRenderbuffers, PFNGLGENRENDERBUFFERSPROC) \
    X(BindRenderbuffer, PFNGLBINDRENDERBUFFERPROC) \
    X(DeleteRenderbuffers, PFNGLDELETERENDERBUFFERSPROC) \
    X(RenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC) \
    X(FramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC) \
    X(CheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC) \
    X(ActiveTexture, PFNGLACTIVETEXTUREPROC)

#define X(name, type) type gl##name = nullptr;
PH_GL_FUNCS
#undef X

bool loadGLProcs() {
#define X(name, type) \
    gl##name = reinterpret_cast<type>(glfwGetProcAddress("gl" #name)); \
    if (!gl##name) return false;
    PH_GL_FUNCS
#undef X
    return true;
}

const char* kMeshVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    mat3 nMat = mat3(transpose(inverse(uModel)));
    vNormal = normalize(nMat * aNrm);
    gl_Position = uProj * uView * wp;
}
)";

const char* kMeshFS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uCamPos;
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uSpecular;
uniform float uClearCoat;
uniform float uClearCoatRoughness;
uniform float uExposure;
uniform samplerCube uEnv;
uniform vec3 uLightDir;
uniform vec3 uLightColor;

const float PI = 3.14159265;

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}
float G_Schlick(float NdotV, float NdotL, float a) {
    float k = (a + 1.0);
    k = (k * k) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
vec3 tonemapACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    float rough = max(uRoughness, 0.04);
    float a = rough * rough;
    vec3 F0 = mix(vec3(0.04 * uSpecular), uBaseColor, uMetallic);

    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = D_GGX(NdotH, a);
    float G = G_Schlick(NdotV, NdotL, a);
    vec3 F = F_Schlick(HdotV, F0);
    vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - uMetallic);
    vec3 direct = (kd * uBaseColor / PI + spec) * uLightColor * NdotL;

    // Clear-coat lobe (dielectric F0≈0.04) — matches CPU DisneyMaterial
    float coat = clamp(uClearCoat, 0.0, 1.0);
    if (coat > 0.0) {
        float ac = max(uClearCoatRoughness, 0.001);
        ac = ac * ac;
        float Dc = D_GGX(NdotH, ac);
        float Gc = G_Schlick(NdotV, NdotL, ac);
        float Fc = 0.04 + 0.96 * pow(1.0 - HdotV, 5.0);
        direct += vec3(coat * Fc * Dc * Gc / max(4.0 * NdotV * NdotL, 1e-4)) * uLightColor * NdotL;
    }

    vec3 R = reflect(-V, N);
    float lod = rough * 6.0;
    vec3 irradiance = textureLod(uEnv, N, 4.0).rgb;
    vec3 prefiltered = textureLod(uEnv, R, lod).rgb;
    vec3 Fenv = F_Schlick(NdotV, F0);
    vec3 ambient = (1.0 - uMetallic) * uBaseColor * irradiance
                 + Fenv * prefiltered;
    if (coat > 0.0) {
        float coatLod = max(uClearCoatRoughness, 0.001) * 6.0;
        float FcEnv = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);
        ambient += coat * FcEnv * textureLod(uEnv, R, coatLod).rgb;
    }

    vec3 color = direct + ambient * 0.85;
    color *= exp2(uExposure);
    color = tonemapACES(color);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)";

const char* kSkyVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
out vec3 vDir;
uniform mat4 uView;
uniform mat4 uProj;
void main() {
    vDir = aPos;
    mat4 rotView = mat4(mat3(uView));
    vec4 clip = uProj * rotView * vec4(aPos, 1.0);
    gl_Position = clip.xyww;
}
)";

const char* kSkyFS = R"(#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uEnv;
uniform float uExposure;
vec3 tonemapACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 color = texture(uEnv, normalize(vDir)).rgb;
    color *= exp2(uExposure);
    color = tonemapACES(color);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)";

struct PreviewMat {
    Vec3f baseColor{0.73f, 0.73f, 0.73f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.03f;
};

PreviewMat extractMat(const Material* mat) {
    PreviewMat m;
    if (!mat) return m;
    if (auto* d = dynamic_cast<const DisneyMaterial*>(mat)) {
        m.baseColor = Vec3f(d->baseColor().r, d->baseColor().g, d->baseColor().b);
        m.metallic = d->metallic();
        m.roughness = d->roughness();
        m.specular = d->specular();
        m.clearCoat = d->clearCoat();
        m.clearCoatRoughness = d->clearCoatRoughness();
    } else if (auto* l = dynamic_cast<const Lambertian*>(mat)) {
        m.baseColor = Vec3f(l->albedo().r, l->albedo().g, l->albedo().b);
        m.roughness = 1.0f;
        m.metallic = 0.0f;
    } else if (auto* mir = dynamic_cast<const Mirror*>(mat)) {
        m.baseColor = Vec3f(mir->reflectance().r, mir->reflectance().g, mir->reflectance().b);
        m.metallic = 1.0f;
        m.roughness = 0.02f;
        m.specular = 1.0f;
    } else if (auto* di = dynamic_cast<const Dielectric*>(mat)) {
        m.baseColor = Vec3f(di->tint().r, di->tint().g, di->tint().b);
        m.metallic = 0.0f;
        m.roughness = 0.05f;
        m.specular = 1.0f;
    }
    return m;
}

Vec3f cubeFaceDir(int face, float u, float v) {
    // u,v in [-1,1]; OpenGL cube face convention
    switch (face) {
    case 0: return Vec3f( 1, -v, -u).normalized();
    case 1: return Vec3f(-1, -v,  u).normalized();
    case 2: return Vec3f( u,  1,  v).normalized();
    case 3: return Vec3f( u, -1, -v).normalized();
    case 4: return Vec3f( u, -v,  1).normalized();
    default: return Vec3f(-u, -v, -1).normalized();
    }
}

Color3f sampleEquirect(const Image& img, const Vec3f& dir) {
    Vec2f uv = directionToEquirect(dir);
    int x = std::clamp(static_cast<int>(uv.x * static_cast<float>(img.width())), 0, img.width() - 1);
    int y = std::clamp(static_cast<int>(uv.y * static_cast<float>(img.height())), 0, img.height() - 1);
    return img.getPixel(x, y);
}

Color3f proceduralSky(const Vec3f& dir) {
    float t = std::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    Color3f horizon(0.55f, 0.58f, 0.62f);
    Color3f zenith(0.35f, 0.55f, 0.95f);
    Color3f ground(0.18f, 0.16f, 0.14f);
    if (dir.y < 0.0f) {
        float g = std::clamp(-dir.y, 0.0f, 1.0f);
        return Color3f(
            horizon.r * (1 - g) + ground.r * g,
            horizon.g * (1 - g) + ground.g * g,
            horizon.b * (1 - g) + ground.b * g) * 0.6f;
    }
    return Color3f(
        horizon.r * (1 - t) + zenith.r * t,
        horizon.g * (1 - t) + zenith.g * t,
        horizon.b * (1 - t) + zenith.b * t);
}

bool pickNode(const SceneNode& node, const Transform& parentXform, Ray& ray,
              float& bestT, uint32_t& bestId) {
    if (!node.visible) return false;
    Transform world = parentXform * node.localTransform;
    bool hit = false;

    if (node.pickId != 0) {
        SurfaceInteraction isect;
        if (node.type == SceneNodeType::Mesh && node.mesh) {
            const auto& pos = node.mesh->positions();
            const auto& idx = node.mesh->indices();
            const auto& nrm = node.mesh->normals();
            const auto& uvs = node.mesh->uvs();
            const bool hasN = nrm.size() == pos.size();
            const bool hasUV = uvs.size() == pos.size();
            const Material* mat = node.material ? node.material.get() : node.mesh->material();
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
                if (i0 >= pos.size() || i1 >= pos.size() || i2 >= pos.size()) continue;
                Vec3f p0 = world.transformPoint(pos[i0]);
                Vec3f p1 = world.transformPoint(pos[i1]);
                Vec3f p2 = world.transformPoint(pos[i2]);
                Vec3f n0 = hasN ? world.transformNormal(nrm[i0]) : Vec3f(0.0f);
                Vec3f n1 = hasN ? world.transformNormal(nrm[i1]) : Vec3f(0.0f);
                Vec3f n2 = hasN ? world.transformNormal(nrm[i2]) : Vec3f(0.0f);
                Vec2f uv0 = hasUV ? uvs[i0] : Vec2f(0.0f);
                Vec2f uv1 = hasUV ? uvs[i1] : Vec2f(0.0f);
                Vec2f uv2 = hasUV ? uvs[i2] : Vec2f(0.0f);
                Triangle tri(p0, p1, p2, n0, n1, n2, uv0, uv1, uv2, mat);
                Ray probe = ray;
                if (tri.intersect(probe, isect) && isect.t < bestT) {
                    bestT = isect.t;
                    bestId = node.pickId;
                    ray.tMax = bestT;
                    hit = true;
                }
            }
        } else if (node.type == SceneNodeType::Sphere && node.sphereRadius > 0) {
            Vec3f center = world.transformPoint(Vec3f(0, 0, 0));
            Sphere sphere(center, node.sphereRadius,
                          node.material ? node.material.get() : nullptr);
            Ray probe = ray;
            if (sphere.intersect(probe, isect) && isect.t < bestT) {
                bestT = isect.t;
                bestId = node.pickId;
                ray.tMax = bestT;
                hit = true;
            }
        }
    }

    for (const auto& child : node.children) {
        if (pickNode(*child, world, ray, bestT, bestId)) hit = true;
    }
    return hit;
}

void makeUnitSphere(std::vector<float>& verts, std::vector<uint32_t>& indices,
                    int slices, int stacks) {
    verts.clear();
    indices.clear();
    for (int y = 0; y <= stacks; ++y) {
        float v = static_cast<float>(y) / stacks;
        float phi = v * PI;
        for (int x = 0; x <= slices; ++x) {
            float u = static_cast<float>(x) / slices;
            float theta = u * TWO_PI;
            float sx = std::sin(phi) * std::cos(theta);
            float sy = std::cos(phi);
            float sz = std::sin(phi) * std::sin(theta);
            verts.push_back(sx); verts.push_back(sy); verts.push_back(sz);
            verts.push_back(sx); verts.push_back(sy); verts.push_back(sz);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            uint32_t i0 = static_cast<uint32_t>(y * (slices + 1) + x);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + static_cast<uint32_t>(slices + 1);
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
}

} // namespace

bool GLPreview::loadGL() {
    if (m_glLoaded) return true;
    if (!loadGLProcs()) return false;
    m_glLoaded = true;
    return true;
}

unsigned int GLPreview::compileProgram(const char* vsSrc, const char* fsSrc) {
    auto compile = [](unsigned int type, const char* src) -> unsigned int {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };
    unsigned int vs = compile(GL_VERTEX_SHADER, vsSrc);
    unsigned int fs = compile(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void GLPreview::init() {
    if (!loadGL()) return;

    m_meshProg = compileProgram(kMeshVS, kMeshFS);
    m_skyProg = compileProgram(kSkyVS, kSkyFS);
    if (!m_meshProg || !m_skyProg) return;

    // Sky cube (unit cube, only positions)
    float cubeVerts[] = {
        -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,
    };
    uint32_t cubeIdx[] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 2,6,7, 2,7,3,
        0,3,7, 0,7,4, 1,5,6, 1,6,2
    };
    // Expand indexed cube to triangles for simplicity
    std::vector<float> skyPos;
    skyPos.reserve(36 * 3);
    for (uint32_t i : cubeIdx) {
        skyPos.push_back(cubeVerts[i * 3 + 0]);
        skyPos.push_back(cubeVerts[i * 3 + 1]);
        skyPos.push_back(cubeVerts[i * 3 + 2]);
    }
    glGenVertexArrays(1, &m_cubeVao);
    glGenBuffers(1, &m_cubeVbo);
    glBindVertexArray(m_cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skyPos.size() * sizeof(float)),
                 skyPos.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    std::vector<float> sphereVerts;
    std::vector<uint32_t> sphereIdx;
    makeUnitSphere(sphereVerts, sphereIdx, 24, 16);
    m_sphereIndexCount = static_cast<int>(sphereIdx.size());
    glGenVertexArrays(1, &m_sphereVao);
    glGenBuffers(1, &m_sphereVbo);
    glGenBuffers(1, &m_sphereIbo);
    glBindVertexArray(m_sphereVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sphereVerts.size() * sizeof(float)),
                 sphereVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sphereIdx.size() * sizeof(uint32_t)),
                 sphereIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    glGenFramebuffers(1, &m_fbo);
    glGenTextures(1, &m_colorTex);
    glGenRenderbuffers(1, &m_depthRbo);
    glGenTextures(1, &m_envCube);

    ensureEnvCube(nullptr, 32);
    m_ready = true;
}

void GLPreview::destroyMeshCache() {
    for (auto& kv : m_meshCache) {
        if (kv.second.vao) glDeleteVertexArrays(1, &kv.second.vao);
        if (kv.second.vbo) glDeleteBuffers(1, &kv.second.vbo);
        if (kv.second.ibo) glDeleteBuffers(1, &kv.second.ibo);
    }
    m_meshCache.clear();
}

void GLPreview::shutdown() {
    if (!m_glLoaded) return;
    destroyMeshCache();
    if (m_meshProg) glDeleteProgram(m_meshProg);
    if (m_skyProg) glDeleteProgram(m_skyProg);
    if (m_cubeVao) glDeleteVertexArrays(1, &m_cubeVao);
    if (m_cubeVbo) glDeleteBuffers(1, &m_cubeVbo);
    if (m_sphereVao) glDeleteVertexArrays(1, &m_sphereVao);
    if (m_sphereVbo) glDeleteBuffers(1, &m_sphereVbo);
    if (m_sphereIbo) glDeleteBuffers(1, &m_sphereIbo);
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_colorTex) glDeleteTextures(1, &m_colorTex);
    if (m_depthRbo) glDeleteRenderbuffers(1, &m_depthRbo);
    if (m_envCube) glDeleteTextures(1, &m_envCube);
    m_ready = false;
}

void GLPreview::ensureFbo(int w, int h) {
    if (w == m_fbW && h == m_fbH && m_colorTex) return;
    m_fbW = w;
    m_fbH = h;
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLPreview::ensureEnvCube(const Image* envMap, int faceSize) {
    bool same = (envMap == m_lastEnv) &&
                (!envMap || (envMap->width() == m_lastEnvW && envMap->height() == m_lastEnvH)) &&
                faceSize == m_cubeSize;
    if (same && m_envCube) return;

    m_lastEnv = envMap;
    m_lastEnvW = envMap ? envMap->width() : 0;
    m_lastEnvH = envMap ? envMap->height() : 0;
    m_cubeSize = faceSize;

    std::vector<float> face(static_cast<size_t>(faceSize * faceSize * 3));
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    for (int f = 0; f < 6; ++f) {
        for (int y = 0; y < faceSize; ++y) {
            for (int x = 0; x < faceSize; ++x) {
                float u = (x + 0.5f) / faceSize * 2.0f - 1.0f;
                float v = (y + 0.5f) / faceSize * 2.0f - 1.0f;
                Vec3f dir = cubeFaceDir(f, u, v);
                Color3f c = envMap ? sampleEquirect(*envMap, dir) : proceduralSky(dir);
                size_t i = static_cast<size_t>((y * faceSize + x) * 3);
                face[i] = c.r;
                face[i + 1] = c.g;
                face[i + 2] = c.b;
            }
        }
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB16F, faceSize, faceSize,
                     0, GL_RGB, GL_FLOAT, face.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    // Generate mipmaps for roughness lod (core 3.0+)
    using PFNGLGENERATEMIPMAPPROC = void (*)(unsigned int);
    auto glGenerateMipmap =
        reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(glfwGetProcAddress("glGenerateMipmap"));
    if (glGenerateMipmap) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    else {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void GLPreview::drawMesh(const TriangleMesh& mesh, const Mat4f& model,
                         const Material* mat, const Mat4f& view, const Mat4f& proj,
                         const Vec3f& camPos, float exposure) {
    const auto& pos = mesh.positions();
    const auto& idx = mesh.indices();
    if (pos.empty() || idx.empty()) return;

    auto it = m_meshCache.find(&mesh);
    if (it == m_meshCache.end()) {
        std::vector<Vec3f> nrm = mesh.normals();
        if (nrm.size() != pos.size()) {
            nrm.assign(pos.size(), Vec3f(0, 1, 0));
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                Vec3f e1 = pos[idx[i + 1]] - pos[idx[i]];
                Vec3f e2 = pos[idx[i + 2]] - pos[idx[i]];
                Vec3f n = e1.cross(e2);
                nrm[idx[i]] = nrm[idx[i]] + n;
                nrm[idx[i + 1]] = nrm[idx[i + 1]] + n;
                nrm[idx[i + 2]] = nrm[idx[i + 2]] + n;
            }
            for (auto& n : nrm) {
                if (n.lengthSquared() > 0) n = n.normalized();
                else n = Vec3f(0, 1, 0);
            }
        }

        std::vector<float> interleaved;
        interleaved.reserve(pos.size() * 6);
        for (size_t i = 0; i < pos.size(); ++i) {
            interleaved.push_back(pos[i].x);
            interleaved.push_back(pos[i].y);
            interleaved.push_back(pos[i].z);
            interleaved.push_back(nrm[i].x);
            interleaved.push_back(nrm[i].y);
            interleaved.push_back(nrm[i].z);
        }

        MeshGpu gpu;
        glGenVertexArrays(1, &gpu.vao);
        glGenBuffers(1, &gpu.vbo);
        glGenBuffers(1, &gpu.ibo);
        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                     interleaved.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                     idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
        gpu.indexCount = static_cast<int>(idx.size());
        it = m_meshCache.emplace(&mesh, gpu).first;
    }

    const MeshGpu& gpu = it->second;
    PreviewMat pm = extractMat(mat);
    glUseProgram(m_meshProg);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uModel"), 1, GL_TRUE, &model.data[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uView"), 1, GL_TRUE, &view.data[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uProj"), 1, GL_TRUE, &proj.data[0][0]);
    float cam[3] = {camPos.x, camPos.y, camPos.z};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uCamPos"), 1, cam);
    float bc[3] = {pm.baseColor.x, pm.baseColor.y, pm.baseColor.z};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uBaseColor"), 1, bc);
    glUniform1f(glGetUniformLocation(m_meshProg, "uMetallic"), pm.metallic);
    glUniform1f(glGetUniformLocation(m_meshProg, "uRoughness"), pm.roughness);
    glUniform1f(glGetUniformLocation(m_meshProg, "uSpecular"), pm.specular);
    glUniform1f(glGetUniformLocation(m_meshProg, "uClearCoat"), pm.clearCoat);
    glUniform1f(glGetUniformLocation(m_meshProg, "uClearCoatRoughness"), pm.clearCoatRoughness);
    glUniform1f(glGetUniformLocation(m_meshProg, "uExposure"), exposure);
    float ldir[3] = {m_lightDir.x, m_lightDir.y, m_lightDir.z};
    float lcol[3] = {3.5f, 3.4f, 3.2f};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uLightDir"), 1, ldir);
    glUniform3fv(glGetUniformLocation(m_meshProg, "uLightColor"), 1, lcol);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    glUniform1i(glGetUniformLocation(m_meshProg, "uEnv"), 0);

    glBindVertexArray(gpu.vao);
    glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void GLPreview::drawSphere(float radius, const Mat4f& model, const Material* mat,
                           const Mat4f& view, const Mat4f& proj, const Vec3f& camPos,
                           float exposure) {
    Mat4f scaled = model * Mat4f::scale(Vec3f(radius, radius, radius));
    PreviewMat pm = extractMat(mat);
    glUseProgram(m_meshProg);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uModel"), 1, GL_TRUE, &scaled.data[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uView"), 1, GL_TRUE, &view.data[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_meshProg, "uProj"), 1, GL_TRUE, &proj.data[0][0]);
    float cam[3] = {camPos.x, camPos.y, camPos.z};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uCamPos"), 1, cam);
    float bc[3] = {pm.baseColor.x, pm.baseColor.y, pm.baseColor.z};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uBaseColor"), 1, bc);
    glUniform1f(glGetUniformLocation(m_meshProg, "uMetallic"), pm.metallic);
    glUniform1f(glGetUniformLocation(m_meshProg, "uRoughness"), pm.roughness);
    glUniform1f(glGetUniformLocation(m_meshProg, "uSpecular"), pm.specular);
    glUniform1f(glGetUniformLocation(m_meshProg, "uClearCoat"), pm.clearCoat);
    glUniform1f(glGetUniformLocation(m_meshProg, "uClearCoatRoughness"), pm.clearCoatRoughness);
    glUniform1f(glGetUniformLocation(m_meshProg, "uExposure"), exposure);
    float ldir[3] = {m_lightDir.x, m_lightDir.y, m_lightDir.z};
    float lcol[3] = {3.5f, 3.4f, 3.2f};
    glUniform3fv(glGetUniformLocation(m_meshProg, "uLightDir"), 1, ldir);
    glUniform3fv(glGetUniformLocation(m_meshProg, "uLightColor"), 1, lcol);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    glUniform1i(glGetUniformLocation(m_meshProg, "uEnv"), 0);
    glBindVertexArray(m_sphereVao);
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void GLPreview::drawNode(const SceneNode& node, const Transform& parent,
                         const Mat4f& view, const Mat4f& proj, const Vec3f& camPos,
                         float exposure) {
    if (!node.visible) return;
    Transform world = parent * node.localTransform;
    const Mat4f& model = world.matrix();
    const Material* mat = node.material.get();

    if (node.type == SceneNodeType::Mesh && node.mesh) {
        if (!mat) mat = node.mesh->material();
        drawMesh(*node.mesh, model, mat, view, proj, camPos, exposure);
    } else if (node.type == SceneNodeType::Sphere && node.sphereRadius > 0) {
        drawSphere(node.sphereRadius, model, mat, view, proj, camPos, exposure);
    }

    for (const auto& child : node.children) {
        drawNode(*child, world, view, proj, camPos, exposure);
    }
}

void GLPreview::render(const SceneGraph& graph,
                       const Mat4f& view, const Mat4f& proj, const Vec3f& camPos,
                       int width, int height, PreviewQuality quality,
                       const Image* envMap, float exposure, const Vec3f& lightDir) {
    if (!m_ready || width <= 0 || height <= 0) return;
    m_lightDir = lightDir;
    // ponytail: one revision for the whole graph (clear/delete). Per-mesh dirty if a delete shouldn't reupload everything.
    if (graph.geometryRevision() != m_meshRev) {
        destroyMeshCache();
        m_meshRev = graph.geometryRevision();
    }

    int faceSize = (quality == PreviewQuality::Fast) ? 32 : 64;
    ensureEnvCube(envMap && envMap->width() > 0 ? envMap : nullptr, faceSize);

    // Fast mode: half-res FBO for speed; ImGui scales it up
    int rw = width, rh = height;
    if (quality == PreviewQuality::Fast) {
        rw = std::max(1, width / 2);
        rh = std::max(1, height / 2);
    }
    ensureFbo(rw, rh);

    // Save a bit of GL state ImGui cares about
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, rw, rh);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Sky
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glUseProgram(m_skyProg);
    glUniformMatrix4fv(glGetUniformLocation(m_skyProg, "uView"), 1, GL_TRUE, &view.data[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(m_skyProg, "uProj"), 1, GL_TRUE, &proj.data[0][0]);
    glUniform1f(glGetUniformLocation(m_skyProg, "uExposure"), exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    glUniform1i(glGetUniformLocation(m_skyProg, "uEnv"), 0);
    glBindVertexArray(m_cubeVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    if (graph.root()) {
        drawNode(*graph.root(), Transform{}, view, proj, camPos, exposure);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void PickingPass::init() {}
void PickingPass::shutdown() {}

uint32_t PickingPass::pick(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                           int width, int height, float u, float v) {
    (void)flatScene; (void)width; (void)height;
    Ray ray = camera.generateRay(u, 1.0f - v, Vec2f(0.5f, 0.5f));
    float bestT = std::numeric_limits<float>::max();
    uint32_t bestId = 0;
    pickNode(*graph.root(), Transform{}, ray, bestT, bestId);
    return bestId;
}

} // namespace photon
