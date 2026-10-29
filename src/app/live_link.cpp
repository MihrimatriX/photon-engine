// live_link.cpp — Yerel TCP sunucusu (Winsock / POSIX soketleri) ve istek doğrulama.
#include "app/live_link.h"
#include "core/platform/path.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <random>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketLen = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
using SocketLen = socklen_t;
#endif

#ifndef PHOTON_VERSION
#define PHOTON_VERSION "dev"
#endif

namespace photon {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxRequest = 64 * 1024;

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
bool netInit() {
    static const bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
}
#else
using Socket = int;
constexpr Socket kInvalid = -1;
void closeSocket(Socket s) { ::close(s); }
bool netInit() { return true; }
#endif

Socket toSocket(intptr_t v) { return static_cast<Socket>(v); }

std::string randomToken() {
    std::random_device rd;
    std::array<uint32_t, 4> words{};
    for (uint32_t& w : words) w = rd();
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (uint32_t w : words)
        for (int i = 7; i >= 0; --i) t.push_back(hex[(w >> (i * 4)) & 0xF]);
    return t;
}

bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

void sendLine(Socket s, const nlohmann::json& j) {
    const std::string line = j.dump() + "\n";
#if defined(_WIN32)
    ::send(s, line.data(), static_cast<int>(line.size()), 0);
#else
    ::send(s, line.data(), line.size(), MSG_NOSIGNAL);
#endif
}

} // namespace

LiveLinkServer::~LiveLinkServer() { stop(); }

bool LiveLinkServer::start(const fs::path& infoFile, bool (*accepts)(const std::string&), uint16_t preferredPort,
                           std::string* error) {
    if (m_running) return true;
    if (!netInit()) {
        if (error) *error = "Ağ yığını başlatılamadı";
        return false;
    }
    Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) {
        if (error) *error = "Soket açılamadı";
        return false;
    }
#if !defined(_WIN32)
    const int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // yalnız bu bilgisayar
    addr.sin_port = htons(preferredPort);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        addr.sin_port = 0; // dolu: işletim sistemi seçsin (livelink.json'a yazılır)
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closeSocket(s);
            if (error) *error = "Canlı bağlantı portu açılamadı";
            return false;
        }
    }
    SocketLen len = sizeof(addr);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    if (::listen(s, 4) != 0) {
        closeSocket(s);
        if (error) *error = "Canlı bağlantı dinlenemiyor";
        return false;
    }
    m_listen = static_cast<intptr_t>(s);
    m_port = ntohs(addr.sin_port);
    m_token = randomToken();
    m_accepts = accepts;
    m_infoFile = infoFile;

    // Bağlantı bilgisi: önce geçici dosya (yalnız sahibi okuyabilir), sonra yerine.
    std::error_code ec;
    fs::create_directories(infoFile.parent_path(), ec);
    fs::path tmp = infoFile;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << nlohmann::json{{"port", m_port}, {"token", m_token}, {"app", "PhotonEngine"}, {"version", PHOTON_VERSION}}.dump(2);
    }
#if !defined(_WIN32)
    ::chmod(tmp.c_str(), S_IRUSR | S_IWUSR);
#endif
    fs::rename(tmp, infoFile, ec);

    m_quit = false;
    m_running = true;
    m_thread = std::thread([this] { serve(); });
    return true;
}

void LiveLinkServer::stop() {
    if (!m_running) return;
    m_quit = true;
    if (m_thread.joinable()) m_thread.join();
    closeSocket(toSocket(m_listen));
    m_listen = -1;
    m_running = false;
    // Bilgi dosyası hâlâ bizimse sil (ikinci bir örnek üzerine yazmış olabilir).
    std::ifstream in(m_infoFile);
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    in.close();
    std::error_code ec;
    if (j.is_object() && j.value("token", std::string()) == m_token) fs::remove(m_infoFile, ec);
}

bool LiveLinkServer::poll(LiveLinkRequest& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_queue.empty()) return false;
    out = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

void LiveLinkServer::serve() {
    const Socket listener = toSocket(m_listen);
    while (!m_quit) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(listener, &set);
        timeval tv{0, 200000}; // 200 ms: kapanış isteğine bu sürede tepki
        const int r = ::select(static_cast<int>(listener) + 1, &set, nullptr, nullptr, &tv);
        if (r <= 0) continue;
        const Socket c = ::accept(listener, nullptr, nullptr);
        if (c == kInvalid) continue;
        handleClient(static_cast<intptr_t>(c));
        closeSocket(c);
    }
}

void LiveLinkServer::handleClient(intptr_t client) {
    const Socket c = toSocket(client);
    // Yavaş/askıda istemci sunucuyu kilitlemesin: okuma zaman aşımı 2 sn.
#if defined(_WIN32)
    DWORD timeoutMs = 2000;
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval tv{2, 0};
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    std::string line;
    char buf[4096];
    while (line.find('\n') == std::string::npos && line.size() < kMaxRequest) {
#if defined(_WIN32)
        const int n = ::recv(c, buf, static_cast<int>(sizeof(buf)), 0);
#else
        const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        line.append(buf, static_cast<size_t>(n));
    }
    if (const size_t nl = line.find('\n'); nl != std::string::npos) line.resize(nl);
    const nlohmann::json req = nlohmann::json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object()) return; // HTTP vb.: sessizce kapat
    auto reject = [&](const std::string& why) { sendLine(c, {{"ok", false}, {"error", why}}); };
    const auto str = [&](const char* key) {
        auto it = req.find(key);
        return (it != req.end() && it->is_string()) ? it->get<std::string>() : std::string();
    };
    if (!constantTimeEqual(str("token"), m_token)) return reject("Geçersiz jeton (livelink.json'u yeniden okuyun)");
    const std::string cmd = str("cmd");
    if (cmd == "ping") {
        sendLine(c, {{"ok", true}, {"app", "PhotonEngine"}, {"version", PHOTON_VERSION}});
        return;
    }
    if (cmd != "import") return reject("Bilinmeyen komut: " + cmd);
    LiveLinkRequest r;
    r.cmd = cmd;
    r.path = str("path");
    r.source = str("source");
    r.name = str("name");
    if (auto it = req.find("replace"); it != req.end() && it->is_boolean()) r.replace = it->get<bool>();
    std::error_code ec;
    const fs::path p = pathFromUtf8(r.path);
    if (r.path.empty() || !p.is_absolute() || !fs::is_regular_file(p, ec)) return reject("Dosya bulunamadı: " + r.path);
    if (m_accepts && !m_accepts(r.path)) return reject("Desteklenmeyen dosya türü: " + r.path);
    if (r.source.size() > 64 || r.name.size() > 256) return reject("Kaynak ya da ad çok uzun");
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_queue.size() >= 16) return reject("Uygulama meşgul, birazdan tekrar deneyin");
        m_queue.push_back(std::move(r));
    }
    sendLine(c, {{"ok", true}});
}

} // namespace photon
