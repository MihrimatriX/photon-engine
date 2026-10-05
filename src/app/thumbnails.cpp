// thumbnails.cpp — Küçük resimlerin arka planda render edilmesi ve GL'ye yüklenmesi.
#include "app/thumbnails.h"
#include "app/render_controller.h"
#include "engine/renderer.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "core/image/image_io.h"
#include "core/color/transfer.h"
#include "core/platform/path.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>

namespace photon {

namespace {

// Önbellek anahtarı: parametrelerden kararlı bir özet. Preset değişirse dosya adı
// değişir ve küçük resim yeniden render edilir. Sürüm numarası motor değiştikçe artar.
std::string materialHash(const MaterialPreset& p, const std::string& studio) {
    std::ostringstream ss;
    ss << "v3|" << studio << '|' << static_cast<int>(p.kind) << '|' << p.baseColor.r << ',' << p.baseColor.g << ','
       << p.baseColor.b << '|' << p.metallic << '|' << p.roughness << '|' << p.specular << '|' << p.clearCoat << '|'
       << p.clearCoatRoughness << '|' << p.anisotropy << '|' << p.sheen << '|' << p.diffuseTransmission << '|'
       << p.emissive << '|' << p.ior;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(std::hash<std::string>{}(ss.str())));
    return buf;
}

unsigned int uploadRGBA(const std::vector<uint8_t>& rgba, int w, int h) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

std::vector<uint8_t> toRGBA(const Image& img, ToneMapOperator tmo, float exposure) {
    std::vector<uint8_t> out(static_cast<size_t>(img.width()) * static_cast<size_t>(img.height()) * 4);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const Color3f d = toneMap(img.getPixel(x, y), tmo, exposure);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(img.width()) + static_cast<size_t>(x)) * 4;
            out[i] = quantizeUnorm8(d.r, tpdfDither(x, y, 0));
            out[i + 1] = quantizeUnorm8(d.g, tpdfDither(x, y, 1));
            out[i + 2] = quantizeUnorm8(d.b, tpdfDither(x, y, 2));
            out[i + 3] = 255;
        }
    }
    return out;
}

} // namespace

ThumbnailBaker::~ThumbnailBaker() {
    stop();
    // GL dokuları uygulama kapanırken bağlam yok edilmeden önce temizlenir
    // (Application yıkıcısı stop() çağırır; burada yalnız kayıtlar bırakılır).
}

void ThumbnailBaker::start(const std::string& cacheDir, const std::string& studioHdr, EnvironmentCache* cache) {
    m_cacheDir = cacheDir;
    m_studioHdr = studioHdr;
    m_envCache = cache;
    std::error_code ec;
    std::filesystem::create_directories(pathFromUtf8(cacheDir), ec);
    m_quit = false;
    m_thread = std::thread([this] { threadMain(); });
}

void ThumbnailBaker::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_jobs.clear();
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void ThumbnailBaker::requestMaterial(const std::string& key, const MaterialPreset& preset, bool force) {
    Job j;
    j.key = key;
    j.isMaterial = true;
    j.preset = preset;
    j.force = force;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Aynı anahtar için bekleyen eski iş varsa (ör. kaydırıcı sürüklenirken) yenisiyle değiştir.
        auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const Job& q) { return q.key == key; });
        if (it != m_jobs.end()) *it = j;
        else {
            m_jobs.push_back(j);
            ++m_pending;
        }
    }
    m_cv.notify_one();
}

void ThumbnailBaker::requestEnvironment(const std::string& key, const std::string& hdrPath,
                                        const Color3f& zenith, const Color3f& horizon) {
    Job j;
    j.key = key;
    j.isMaterial = false;
    j.hdrPath = hdrPath;
    j.zenith = zenith;
    j.horizon = horizon;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        // Ortamlar önce: hızlılar ve kütüphanenin ilk sekmesinde görünürler.
        m_jobs.push_front(j);
        ++m_pending;
    }
    m_cv.notify_one();
}

bool ThumbnailBaker::uploadReady() {
    std::vector<Result> done;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        done.swap(m_done);
    }
    for (auto& r : done) {
        if (r.rgba.empty()) continue;
        auto it = m_textures.find(r.key);
        if (it != m_textures.end() && it->second) glDeleteTextures(1, &it->second);
        m_textures[r.key] = uploadRGBA(r.rgba, r.w, r.h);
    }
    return !done.empty();
}

unsigned int ThumbnailBaker::texture(const std::string& key) const {
    auto it = m_textures.find(key);
    return it == m_textures.end() ? 0u : it->second;
}

void ThumbnailBaker::threadMain() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&] { return m_quit || !m_jobs.empty(); });
            if (m_quit) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        Result r;
        try {
            r = job.isMaterial ? bakeMaterial(job) : bakeEnvironment(job);
        } catch (const std::exception&) {
            r.key = job.key;
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_done.push_back(std::move(r));
        }
        --m_pending;
    }
}

// Shader topu sahnesi: 1 birim yarıçaplı küre, gri yarı mat zemin, stüdyo HDRI.
// 2 thread'lik ayrı bir havuzla render edilir ki viewport'u yavaşlatmasın.
ThumbnailBaker::Result ThumbnailBaker::bakeMaterial(const Job& job) {
    namespace fs = std::filesystem;
    Result r;
    r.key = job.key;
    const fs::path file = pathFromUtf8(m_cacheDir) / pathFromUtf8("mat_" + materialHash(job.preset, m_studioHdr) + ".png");
    if (!job.force && fs::exists(file) && loadRGBA8(pathToUtf8(file), r.rgba, r.w, r.h)) return r;

    Scene scene;
    auto mat = MaterialLibrary::createMaterial(job.preset);
    auto groundMat = MaterialLibrary::createMaterial([] {
        MaterialPreset g;
        g.baseColor = Color3f(0.3f);
        g.roughness = 0.6f;
        return g;
    }());
    scene.retainMaterial(mat);
    scene.retainMaterial(groundMat);
    scene.addShape(std::make_shared<Sphere>(Vec3f(0.0f, 1.0f, 0.0f), 1.0f, mat.get()));
    const float s = 30.0f;
    scene.addShape(std::make_shared<TriangleMesh>(
        std::vector<Vec3f>{{-s, 0, -s}, {s, 0, -s}, {s, 0, s}, {-s, 0, s}}, std::vector<Vec3f>{},
        std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, groundMat.get()));
    std::shared_ptr<const Image> env;
    if (!m_studioHdr.empty() && m_envCache) env = m_envCache->get(m_studioHdr);
    if (!env) env = makeProceduralSky(Color3f(0.8f, 0.82f, 0.9f), Color3f(0.5f));
    scene.setEnvironment(std::make_shared<EnvironmentLight>(env, 1.2f, 1.0f));
    scene.buildAccelerator();

    CameraParams cam;
    cam.eye = Vec3f(0.0f, 1.7f, 4.6f);
    cam.target = Vec3f(0.0f, 0.95f, 0.0f);
    cam.fovDeg = 28.0f;
    auto camera = makeCamera(cam, 1.0f);

    RenderSettings rs;
    rs.width = kMaterialSize;
    rs.height = kMaterialSize;
    rs.samplesPerPixel = 48;
    rs.maxBounces = 10;
    rs.denoiseEnabled = true;
    rs.numThreads = 2;
    Renderer renderer;
    Image img = renderer.render(scene, *camera, rs);
    r.w = img.width();
    r.h = img.height();
    r.rgba = toRGBA(img, ToneMapOperator::PBRNeutral, 0.0f);
    saveRGBA8PNG(r.rgba.data(), r.w, r.h, pathToUtf8(file), false);
    return r;
}

// HDRI önizlemesi: 2:1 küçültme (kutu filtresi) ve otomatik pozlama
// (ortalama parlaklık ~0.2'ye çekilir) — çok parlak ve çok karanlık HDRI'lar da okunur.
ThumbnailBaker::Result ThumbnailBaker::bakeEnvironment(const Job& job) {
    Result r;
    r.key = job.key;
    std::shared_ptr<const Image> src;
    if (!job.hdrPath.empty() && m_envCache) src = m_envCache->get(job.hdrPath);
    if (!src) src = makeProceduralSky(job.zenith, job.horizon);
    const int W = 224, H = 112;
    Image small(W, H);
    double lumSum = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int x0 = x * src->width() / W, x1 = std::max(x0 + 1, (x + 1) * src->width() / W);
            const int y0 = y * src->height() / H, y1 = std::max(y0 + 1, (y + 1) * src->height() / H);
            Color3f sum(0.0f);
            int n = 0;
            for (int yy = y0; yy < y1; yy += std::max(1, (y1 - y0) / 4))
                for (int xx = x0; xx < x1; xx += std::max(1, (x1 - x0) / 4)) {
                    sum += src->getPixel(xx, yy);
                    ++n;
                }
            const Color3f c = sum / static_cast<float>(std::max(1, n));
            small.setPixel(x, y, c);
            lumSum += std::min(c.luminance(), 50.0f);
        }
    }
    const double mean = std::max(1e-4, lumSum / (W * H));
    const float ev = static_cast<float>(std::log2(0.22 / mean));
    r.w = W;
    r.h = H;
    r.rgba = toRGBA(small, ToneMapOperator::AgX, std::clamp(ev, -8.0f, 8.0f));
    return r;
}

} // namespace photon
