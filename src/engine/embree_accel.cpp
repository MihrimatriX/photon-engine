// embree_accel.cpp — Sahne şekillerini Embree geometrilerine çevirme ve ışın sorguları.
#include "engine/embree_accel.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
#include "geometry/shape.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>

#if defined(PHOTON_HAS_EMBREE)
#include <embree4/rtcore.h>
#endif

namespace photon {

#if defined(PHOTON_HAS_EMBREE)

namespace {

// Uygulama boyunca tek bir Embree cihazı (iş parçacığı havuzu ve ayarlar burada).
RTCDevice sharedDevice() {
    static RTCDevice device = [] {
        RTCDevice d = rtcNewDevice(nullptr);
        if (!d) std::cerr << "Embree: cihaz oluşturulamadı" << std::endl;
        rtcSetDeviceErrorFunction(
            d, [](void*, RTCError code, const char* msg) { std::cerr << "Embree hata " << code << ": " << (msg ? msg : "") << std::endl; },
            nullptr);
        return d;
    }();
    return device;
}

struct GeomInfo {
    const TriangleMesh* mesh = nullptr;  ///< Üçgen mesh (çoğunluk)
    const Triangle* triangle = nullptr;  ///< Tek üçgen (alan ışığı dörtgenleri)
    const Shape* shape = nullptr;        ///< Diğer şekiller (küre): kullanıcı geometrisi
};

// ── Kullanıcı geometrisi (küre vb.) için geri çağrılar ──
// Embree yalnız kutuyu bilir; kesişimi motorun kendi Shape::intersect'i yapar.

void userBounds(const RTCBoundsFunctionArguments* args) {
    const auto* shape = static_cast<const Shape*>(args->geometryUserPtr);
    const AABB b = shape->bounds();
    args->bounds_o->lower_x = b.pMin.x;
    args->bounds_o->lower_y = b.pMin.y;
    args->bounds_o->lower_z = b.pMin.z;
    args->bounds_o->upper_x = b.pMax.x;
    args->bounds_o->upper_y = b.pMax.y;
    args->bounds_o->upper_z = b.pMax.z;
}

void userIntersect(const RTCIntersectFunctionNArguments* args) {
    if (!args->valid[0]) return;
    const auto* shape = static_cast<const Shape*>(args->geometryUserPtr);
    RTCRayHit* rh = reinterpret_cast<RTCRayHit*>(args->rayhit); // N = 1
    Ray r(Vec3f(rh->ray.org_x, rh->ray.org_y, rh->ray.org_z), Vec3f(rh->ray.dir_x, rh->ray.dir_y, rh->ray.dir_z),
          rh->ray.tnear, rh->ray.tfar);
    SurfaceInteraction tmp;
    if (!shape->intersect(r, tmp)) return;
    rh->ray.tfar = tmp.t;
    rh->hit.geomID = args->geomID;
    rh->hit.primID = args->primID;
    rh->hit.u = 0.0f;
    rh->hit.v = 0.0f;
    rh->hit.Ng_x = tmp.ng.x;
    rh->hit.Ng_y = tmp.ng.y;
    rh->hit.Ng_z = tmp.ng.z;
}

void userOccluded(const RTCOccludedFunctionNArguments* args) {
    if (!args->valid[0]) return;
    const auto* shape = static_cast<const Shape*>(args->geometryUserPtr);
    RTCRay* ray = reinterpret_cast<RTCRay*>(args->ray);
    Ray r(Vec3f(ray->org_x, ray->org_y, ray->org_z), Vec3f(ray->dir_x, ray->dir_y, ray->dir_z), ray->tnear, ray->tfar);
    SurfaceInteraction tmp;
    if (shape->intersect(r, tmp)) ray->tfar = -std::numeric_limits<float>::infinity();
}

} // namespace

struct EmbreeAccel::Impl {
    RTCScene scene = nullptr;
    std::vector<GeomInfo> geoms;  // geomID → kaynak
    ~Impl() {
        if (scene) rtcReleaseScene(scene);
    }
};

EmbreeAccel::EmbreeAccel() : m_impl(std::make_unique<Impl>()) {}
EmbreeAccel::~EmbreeAccel() = default;

bool EmbreeAccel::available() {
    return sharedDevice() != nullptr;
}

void EmbreeAccel::build(const std::vector<std::shared_ptr<Shape>>& shapes) {
    RTCDevice dev = sharedDevice();
    if (m_impl->scene) rtcReleaseScene(m_impl->scene);
    m_impl->geoms.clear();
    m_bounds = AABB::empty();
    RTCScene scene = rtcNewScene(dev);
    // ROBUST: kenarlarda ve köşelerde "su geçirmez" test; ince boşluklardan ışık sızmaz.
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_ROBUST);
    rtcSetSceneBuildQuality(scene, RTC_BUILD_QUALITY_MEDIUM);

    auto attach = [&](RTCGeometry g, const GeomInfo& info) {
        rtcCommitGeometry(g);
        const unsigned id = rtcAttachGeometry(scene, g);
        rtcReleaseGeometry(g);
        if (m_impl->geoms.size() <= id) m_impl->geoms.resize(id + 1);
        m_impl->geoms[id] = info;
    };

    for (const auto& s : shapes) {
        if (!s) continue;
        m_bounds.merge(s->bounds());
        if (const auto* mesh = dynamic_cast<const TriangleMesh*>(s.get())) {
            const auto& pos = mesh->positions();
            const auto& idx = mesh->indices();
            if (pos.empty() || idx.size() < 3) continue;
            RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
            // Embree tamponları kendisi ayırır (SSE yükleri için sonda dolgu ister); kopyalanır.
            auto* v = static_cast<float*>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
                                                                  3 * sizeof(float), pos.size()));
            for (size_t i = 0; i < pos.size(); ++i) {
                v[i * 3 + 0] = pos[i].x;
                v[i * 3 + 1] = pos[i].y;
                v[i * 3 + 2] = pos[i].z;
            }
            auto* ib = static_cast<uint32_t*>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
                                                                      3 * sizeof(uint32_t), idx.size() / 3));
            std::memcpy(ib, idx.data(), (idx.size() / 3) * 3 * sizeof(uint32_t));
            attach(g, GeomInfo{mesh, nullptr, nullptr});
        } else if (const auto* tri = dynamic_cast<const Triangle*>(s.get())) {
            RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
            auto* v = static_cast<float*>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
                                                                  3 * sizeof(float), 3));
            const Vec3f p[3] = {tri->v0(), tri->v1(), tri->v2()};
            for (int i = 0; i < 3; ++i) {
                v[i * 3 + 0] = p[i].x;
                v[i * 3 + 1] = p[i].y;
                v[i * 3 + 2] = p[i].z;
            }
            auto* ib = static_cast<uint32_t*>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
                                                                      3 * sizeof(uint32_t), 1));
            ib[0] = 0;
            ib[1] = 1;
            ib[2] = 2;
            attach(g, GeomInfo{nullptr, tri, nullptr});
        } else {
            RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_USER);
            rtcSetGeometryUserPrimitiveCount(g, 1);
            rtcSetGeometryUserData(g, const_cast<Shape*>(s.get()));
            rtcSetGeometryBoundsFunction(g, userBounds, nullptr);
            rtcSetGeometryIntersectFunction(g, userIntersect);
            rtcSetGeometryOccludedFunction(g, userOccluded);
            attach(g, GeomInfo{nullptr, nullptr, s.get()});
        }
    }
    rtcCommitScene(scene);
    m_impl->scene = scene;
}

bool EmbreeAccel::intersect(Ray& ray, SurfaceInteraction& isect) const {
    if (!m_impl->scene) return false;
    RTCRayHit rh;
    rh.ray.org_x = ray.origin.x;
    rh.ray.org_y = ray.origin.y;
    rh.ray.org_z = ray.origin.z;
    rh.ray.dir_x = ray.direction.x;
    rh.ray.dir_y = ray.direction.y;
    rh.ray.dir_z = ray.direction.z;
    rh.ray.tnear = ray.tMin;
    rh.ray.tfar = ray.tMax;
    rh.ray.time = 0.0f;
    rh.ray.mask = 0xFFFFFFFFu;
    rh.ray.id = 0;
    rh.ray.flags = 0;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(m_impl->scene, &rh);
    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return false;

    const GeomInfo& g = m_impl->geoms[rh.hit.geomID];
    const float t = rh.ray.tfar;
    if (g.mesh) {
        g.mesh->shadeTriangle(rh.hit.primID, ray, t, rh.hit.u, rh.hit.v, isect);
        isect.hitObject = g.mesh;
    } else if (g.triangle) {
        g.triangle->fillHit(ray, t, rh.hit.u, rh.hit.v, isect);
        isect.hitObject = g.triangle;
    } else {
        // Kullanıcı geometrisi: tam yüzey verisi için şeklin kendi testini tekrarla.
        Ray r = ray;
        r.tMax = t * 1.00001f + 1e-6f;
        if (!g.shape->intersect(r, isect)) return false;
        isect.hitObject = g.shape;
    }
    ray.tMax = t;
    return true;
}

bool EmbreeAccel::intersectAny(const Ray& ray) const {
    if (!m_impl->scene) return false;
    RTCRay r;
    r.org_x = ray.origin.x;
    r.org_y = ray.origin.y;
    r.org_z = ray.origin.z;
    r.dir_x = ray.direction.x;
    r.dir_y = ray.direction.y;
    r.dir_z = ray.direction.z;
    r.tnear = ray.tMin;
    r.tfar = ray.tMax;
    r.time = 0.0f;
    r.mask = 0xFFFFFFFFu;
    r.id = 0;
    r.flags = 0;
    rtcOccluded1(m_impl->scene, &r);
    return r.tfar < 0.0f; // Embree engel bulunca tfar = -∞ yazar
}

#else // Embree yok: boş uygulama, Scene kendi BVH'sini kullanır.

struct EmbreeAccel::Impl {};
EmbreeAccel::EmbreeAccel() : m_impl(std::make_unique<Impl>()) {}
EmbreeAccel::~EmbreeAccel() = default;
bool EmbreeAccel::available() { return false; }
void EmbreeAccel::build(const std::vector<std::shared_ptr<Shape>>&) {}
bool EmbreeAccel::intersect(Ray&, SurfaceInteraction&) const { return false; }
bool EmbreeAccel::intersectAny(const Ray&) const { return false; }

#endif

} // namespace photon
