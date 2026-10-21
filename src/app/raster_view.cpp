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
#include "app/raster_view.h"
#include "core/math/transform.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

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
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))

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
    vec3 n = normalize(vN);
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

} // namespace

const char* viewModeName(ViewMode m) {
    switch (m) {
        case ViewMode::Render: return "Render";
        case ViewMode::Clay: return "Kil";
        case ViewMode::Solid: return "Katı";
        case ViewMode::Wire: return "Tel kafes";
        case ViewMode::Normals: return "Normaller";
    }
    return "";
}

bool RasterView::init() {
    if (m_ready) return true;
    if (!loadGl()) return false;
    m_progShade = link(kVertexSrc, kShadeSrc);
    m_progFlat = link(kVertexSrc, kFlatSrc);
    if (!m_progShade || !m_progFlat) return false;

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
    const auto& p = mesh->positions();
    const auto& n = mesh->normals();
    std::vector<float> v;
    v.reserve(p.size() * 6);
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3f nn = i < n.size() ? n[i] : Vec3f(0, 1, 0);
        v.insert(v.end(), {p[i].x, p[i].y, p[i].z, nn.x, nn.y, nn.z});
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
    gl_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_glEnableVertexAttribArray(0);
    gl_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
    gl_glEnableVertexAttribArray(1);
    gl_glBindVertexArray(0);
    return &(m_meshes[mesh.get()] = g);
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

} // namespace photon
