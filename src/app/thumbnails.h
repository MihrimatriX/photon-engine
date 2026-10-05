// thumbnails.h — Kütüphane küçük resimleri: malzeme küreleri ve HDRI önizlemeleri.
//
// Malzeme küçük resimleri motorun kendisiyle (path tracer + OIDN) bir stüdyo
// ortamında render edilir; kütüphanede görünen küre son render'la birebir aynı
// fiziği kullanır. İş arka plan thread'inde yapılır, sonuçlar diske (kullanıcı
// önbellek klasörü) yazılır: ikinci açılışta anında yüklenir. OpenGL dokuları
// yalnız UI thread'inde (uploadReady) oluşturulur.
#pragma once

#include "scene/material_library.h"
#include "scene/document.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace photon {

class ThumbnailBaker {
public:
    ThumbnailBaker() = default;
    ~ThumbnailBaker();

    /// @param cacheDir  Disk önbelleği klasörü.
    /// @param studioHdr Malzeme küreleri için aydınlatma HDRI'ı (boşsa prosedürel).
    void start(const std::string& cacheDir, const std::string& studioHdr, EnvironmentCache* cache);
    void stop();

    void requestMaterial(const std::string& key, const MaterialPreset& preset, bool force = false);
    void requestEnvironment(const std::string& key, const std::string& hdrPath,
                            const Color3f& zenith, const Color3f& horizon);

    /// Biten işleri GL dokusuna çevirir (UI thread'i). Yeni doku geldiyse true.
    bool uploadReady();
    /// Anahtarın dokusu; henüz yoksa 0.
    unsigned int texture(const std::string& key) const;
    bool busy() const { return m_pending.load() > 0; }

    static constexpr int kMaterialSize = 112;

private:
    struct Job {
        std::string key;
        bool isMaterial = true;
        MaterialPreset preset;
        std::string hdrPath;
        Color3f zenith, horizon;
        bool force = false;
    };
    struct Result {
        std::string key;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };

    void threadMain();
    Result bakeMaterial(const Job& job);
    Result bakeEnvironment(const Job& job);

    std::string m_cacheDir;
    std::string m_studioHdr;
    EnvironmentCache* m_envCache = nullptr;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::vector<Result> m_done;
    bool m_quit = false;
    std::atomic<int> m_pending{0};
    std::map<std::string, unsigned int> m_textures;
};

} // namespace photon
