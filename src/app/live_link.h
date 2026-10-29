// live_link.h — Canlı bağlantı: 3ds Max, Maya ve Blender'dan tek tıkla gönderme.
//
// DCC betikleri (integrations/) sahneyi FBX olarak dışa aktarır ve dosya yolunu bu sunucuya
// gönderir; uygulama içe aktarır. Aynı kaynaktan aynı ad yeniden gelirse model yerinde güncellenir
// (kullanıcının atadığı malzemeler ve taşıma korunur), KeyShot LiveLinking gibi.
//
// Protokol (satır tabanlı JSON, bağlantı başına bir istek):
//   istemci → {"token": "...", "cmd": "import", "path": "C:/.../sahne.fbx",
//              "source": "3dsmax", "name": "Araba", "replace": true}\n
//   sunucu  → {"ok": true}\n   ya da   {"ok": false, "error": "..."}\n
//   "cmd": "ping" → {"ok": true, "app": "PhotonEngine", "version": "..."}
//
// Güvenlik:
//   - yalnız 127.0.0.1 dinlenir (ağdan erişilemez; Windows güvenlik duvarı uyarısı da çıkmaz);
//   - her açılışta rastgele 128 bit jeton üretilir ve kullanıcı veri klasöründeki livelink.json'a
//     (yalnız sahibi okuyabilir) port ile birlikte yazılır; DCC betikleri aynı kullanıcı olarak
//     çalıştığı için okur, başka kullanıcılar okuyamaz. Jeton sabit sürede karşılaştırılır;
//   - ilk satır JSON değilse bağlantı kapatılır: tarayıcıdan localhost'a gönderilen HTTP istekleri
//     ("POST / HTTP/1.1") hiçbir şey yaptıramaz;
//   - yalnız var olan, desteklenen uzantılı dosyalar kabul edilir; istek boyutu 64 KB ile sınırlı.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace photon {

struct LiveLinkRequest {
    std::string cmd;     ///< "import"
    std::string path;    ///< mutlak dosya yolu (UTF-8)
    std::string source;  ///< "3dsmax", "maya", "blender", ...
    std::string name;    ///< DCC'deki sahne / seçim adı: yeniden gönderimi eşlemek için
    bool replace = true; ///< aynı kaynak+ad varsa yerinde güncelle
};

class LiveLinkServer {
public:
    static constexpr uint16_t kDefaultPort = 47260;

    LiveLinkServer() = default;
    ~LiveLinkServer();
    LiveLinkServer(const LiveLinkServer&) = delete;
    LiveLinkServer& operator=(const LiveLinkServer&) = delete;

    /// Dinlemeye başlar ve @p infoFile'a {port, token} yazar. Varsayılan port doluysa işletim
    /// sistemine boş port seçtirilir. @p accepts: dosya yolu kabul edilebilir mi (uzantı denetimi).
    bool start(const std::filesystem::path& infoFile, bool (*accepts)(const std::string& path),
               uint16_t preferredPort = kDefaultPort, std::string* error = nullptr);
    void stop();
    bool running() const { return m_running.load(); }
    uint16_t port() const { return m_port; }
    const std::string& token() const { return m_token; }

    /// UI thread'i: bekleyen bir istek varsa @p out'a taşır.
    bool poll(LiveLinkRequest& out);

private:
    void serve();
    void handleClient(intptr_t client);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_quit{false};
    intptr_t m_listen = -1;
    uint16_t m_port = 0;
    std::string m_token;
    std::filesystem::path m_infoFile;
    bool (*m_accepts)(const std::string&) = nullptr;
    std::mutex m_mutex;
    std::deque<LiveLinkRequest> m_queue;
};

} // namespace photon
