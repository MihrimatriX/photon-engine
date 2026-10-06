// primitives.cpp — Temel şekillerin üçgen ağlarının üretimi.
//
// Yuvarlak yüzeyler (küre, silindir yanı, koni yanı, simit) yumuşak normalli tek
// bir köşe ızgarasıdır: u halkada, v boyda dolaşır; dikiş (u = 1) köşesi ayrı tutulur
// ki UV 1'e kadar gitsin. Düz yüzler (küpün yüzleri, kapaklar) kendi köşelerini alır:
// köşede iki yüz farklı normale ihtiyaç duyar, paylaşılırsa kenar yumuşak görünürdü.
#include "scene/primitives.h"
#include "core/math/constants.h"

#include <cmath>
#include <vector>

namespace photon {

namespace {

struct Builder {
    std::vector<Vec3f> p, n;
    std::vector<Vec2f> uv;
    std::vector<uint32_t> idx;

    uint32_t vertex(const Vec3f& pos, const Vec3f& nrm, const Vec2f& t) {
        p.push_back(pos);
        n.push_back(nrm);
        uv.push_back(t);
        return static_cast<uint32_t>(p.size() - 1);
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) {
        idx.push_back(a);
        idx.push_back(b);
        idx.push_back(c);
    }
    // a-b-c-d dışarıdan bakınca CCW sıralı dörtgen.
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        tri(a, b, c);
        tri(a, c, d);
    }

    // Izgara: f(u, v) konumu ve normali verir; (segU+1)×(segV+1) köşe.
    // flip: v ekseni yukarı doğru iken dış yüz CCW olsun diye sıra çevrilir.
    template <typename F>
    void grid(int segU, int segV, F&& f) {
        const uint32_t base = static_cast<uint32_t>(p.size());
        for (int j = 0; j <= segV; ++j) {
            for (int i = 0; i <= segU; ++i) {
                const float u = static_cast<float>(i) / static_cast<float>(segU);
                const float v = static_cast<float>(j) / static_cast<float>(segV);
                Vec3f pos, nrm;
                f(u, v, pos, nrm);
                vertex(pos, nrm, Vec2f(u, v));
            }
        }
        const uint32_t row = static_cast<uint32_t>(segU + 1);
        for (int j = 0; j < segV; ++j)
            for (int i = 0; i < segU; ++i) {
                const uint32_t a = base + static_cast<uint32_t>(j) * row + static_cast<uint32_t>(i);
                // (u, v) → (u+1, v) → (u+1, v+1) → (u, v+1); u dışarıdan bakınca sola
                // (−φ yönü) ilerlediği için bu sıra dışarıdan CCW'dir.
                quad(a, a + row, a + row + 1, a + 1);
            }
    }

    // Y eksenine dik disk (kapak). up: normal +y mi.
    void disk(float y, float r, bool up, int seg) {
        const Vec3f nrm(0.0f, up ? 1.0f : -1.0f, 0.0f);
        const uint32_t c = vertex(Vec3f(0.0f, y, 0.0f), nrm, Vec2f(0.5f, 0.5f));
        const uint32_t first = static_cast<uint32_t>(p.size());
        for (int i = 0; i <= seg; ++i) {
            const float a = TWO_PI * static_cast<float>(i) / static_cast<float>(seg);
            vertex(Vec3f(std::cos(a) * r, y, std::sin(a) * r), nrm, Vec2f(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)));
        }
        for (int i = 0; i < seg; ++i) {
            const uint32_t a = first + static_cast<uint32_t>(i), b = a + 1;
            // φ artarken (cos, sin) yukarıdan bakınca saat yönünde döner (x sağ, z aşağı
            // görünür): yukarı bakan kapakta c-b-a CCW'dir.
            if (up) tri(c, b, a);
            else tri(c, a, b);
        }
    }

    std::shared_ptr<TriangleMesh> build() { return std::make_shared<TriangleMesh>(p, n, uv, idx, nullptr); }
};

constexpr int kSeg = 48;

} // namespace

const char* primitiveName(PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::Cube: return "Küp";
        case PrimitiveKind::Sphere: return "Küre";
        case PrimitiveKind::Cylinder: return "Silindir";
        case PrimitiveKind::Cone: return "Koni";
        case PrimitiveKind::Plane: return "Düzlem";
        case PrimitiveKind::Torus: return "Simit";
    }
    return "Şekil";
}

std::shared_ptr<TriangleMesh> makePrimitiveMesh(PrimitiveKind kind) {
    Builder b;
    switch (kind) {
        case PrimitiveKind::Cube: {
            // Her yüz: normal n, yüz üzerinde iki eksen s ve t (s × t = n → CCW).
            const Vec3f faces[6][3] = {
                {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
            };
            const Vec3f center(0.0f, 0.5f, 0.0f);
            for (const auto& f : faces) {
                const Vec3f c = center + f[0] * 0.5f;
                const uint32_t v0 = b.vertex(c - f[1] * 0.5f - f[2] * 0.5f, f[0], Vec2f(0, 0));
                const uint32_t v1 = b.vertex(c + f[1] * 0.5f - f[2] * 0.5f, f[0], Vec2f(1, 0));
                const uint32_t v2 = b.vertex(c + f[1] * 0.5f + f[2] * 0.5f, f[0], Vec2f(1, 1));
                const uint32_t v3 = b.vertex(c - f[1] * 0.5f + f[2] * 0.5f, f[0], Vec2f(0, 1));
                b.quad(v0, v1, v2, v3);
            }
            break;
        }
        case PrimitiveKind::Sphere:
            // v = 0 güney kutbu, v = 1 kuzey; θ = π(1 − v) tepeden açı.
            b.grid(kSeg, kSeg / 2, [](float u, float v, Vec3f& pos, Vec3f& nrm) {
                const float phi = TWO_PI * u, theta = PI * (1.0f - v);
                nrm = Vec3f(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                pos = Vec3f(0.0f, 0.5f, 0.0f) + nrm * 0.5f;
            });
            break;
        case PrimitiveKind::Cylinder:
            b.grid(kSeg, 1, [](float u, float v, Vec3f& pos, Vec3f& nrm) {
                const float phi = TWO_PI * u;
                nrm = Vec3f(std::cos(phi), 0.0f, std::sin(phi));
                pos = Vec3f(nrm.x * 0.5f, v, nrm.z * 0.5f);
            });
            b.disk(1.0f, 0.5f, true, kSeg);
            b.disk(0.0f, 0.5f, false, kSeg);
            break;
        case PrimitiveKind::Cone:
            // Yan yüz normali: eğim r/h = 0.5 → n ∝ (cosφ·h, r, sinφ·h). Tepe noktası
            // her dilimde ayrı köşe (kendi normaliyle), yoksa tepe kararırdı.
            b.grid(kSeg, 1, [](float u, float v, Vec3f& pos, Vec3f& nrm) {
                const float phi = TWO_PI * u;
                nrm = Vec3f(std::cos(phi), 0.5f, std::sin(phi)).normalized();
                const float r = 0.5f * (1.0f - v);
                pos = Vec3f(std::cos(phi) * r, v, std::sin(phi) * r);
            });
            b.disk(0.0f, 0.5f, false, kSeg);
            break;
        case PrimitiveKind::Plane: {
            const Vec3f up(0, 1, 0);
            const uint32_t v0 = b.vertex(Vec3f(-0.5f, 0, 0.5f), up, Vec2f(0, 0));
            const uint32_t v1 = b.vertex(Vec3f(0.5f, 0, 0.5f), up, Vec2f(1, 0));
            const uint32_t v2 = b.vertex(Vec3f(0.5f, 0, -0.5f), up, Vec2f(1, 1));
            const uint32_t v3 = b.vertex(Vec3f(-0.5f, 0, -0.5f), up, Vec2f(0, 1));
            b.quad(v0, v1, v2, v3);
            break;
        }
        case PrimitiveKind::Torus: {
            // Ana yarıçap R = 0.35, boru r = 0.15: dış çap 1, yükseklik 0.3, taban y = 0.
            // v borunun etrafında döner: dış ekvatordan başlayıp önce yukarı çıkar
            // (kürede v'nin yukarı ilerlemesiyle aynı yön, aynı sarma).
            const float R = 0.35f, r = 0.15f;
            b.grid(kSeg, kSeg / 2, [R, r](float u, float v, Vec3f& pos, Vec3f& nrm) {
                const float phi = TWO_PI * u, psi = TWO_PI * v;
                const Vec3f radial(std::cos(phi), 0.0f, std::sin(phi));
                nrm = radial * std::cos(psi) + Vec3f(0.0f, std::sin(psi), 0.0f);
                pos = radial * R + nrm * r + Vec3f(0.0f, r, 0.0f);
            });
            break;
        }
    }
    return b.build();
}

} // namespace photon
