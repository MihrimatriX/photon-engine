// Canlı bağlantı (3ds Max / Maya / Blender → PhotonEngine) testleri:
//   - sunucu yalnız 127.0.0.1'i dinler, livelink.json'a port ve jeton yazar;
//   - geçerli istek kuyruğa girer ve {"ok": true} döner; "ping" uygulama adını döndürür;
//   - yanlış jeton, olmayan dosya, desteklenmeyen uzantı reddedilir, kuyruğa bir şey girmez;
//   - tarayıcıdan gelebilecek bir HTTP isteği ("POST / HTTP/1.1") sessizce kapatılır;
//   - DCC betiklerindeki istemci bloğu ortak istemcinin birebir kopyasıdır (sync_client.py).
#include <gtest/gtest.h>
#include "app/live_link.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace photon;
namespace fs = std::filesystem;

namespace {

const std::string kFbx = std::string(PHOTON_TEST_DATA) + "/fbx/dcc_camera_lights_7400_ascii.fbx";

bool acceptsFbx(const std::string& p) { return fs::path(p).extension() == ".fbx"; }

/// Ham istek gönderir, sunucunun yanıt satırını döndürür (kapatırsa boş).
std::string roundTrip(uint16_t port, const std::string& payload) {
#if defined(_WIN32)
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    std::string out;
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
        ::send(s, payload.data(), static_cast<int>(payload.size()), 0);
        char buf[1024];
        for (;;) {
            const auto n = ::recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
            if (out.find('\n') != std::string::npos) break;
        }
    }
#if defined(_WIN32)
    closesocket(s);
#else
    ::close(s);
#endif
    return out;
}

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string clientBlock(const std::string& text) {
    const std::string b = "# --- photon_livelink istemci başlangıcı ---", e = "# --- photon_livelink istemci bitişi ---";
    const size_t i = text.find(b), j = text.find(e);
    if (i == std::string::npos || j == std::string::npos) return {};
    return text.substr(i, j + e.size() - i);
}

} // namespace

TEST(LiveLink, AcceptsValidRequestsAndRejectsEverythingElse) {
    const fs::path info = fs::temp_directory_path() / "photon_livelink_test" / "livelink.json";
    fs::remove_all(info.parent_path());
    LiveLinkServer server;
    std::string err;
    ASSERT_TRUE(server.start(info, &acceptsFbx, 0, &err)) << err; // 0: boş port
    ASSERT_TRUE(server.running());
    const nlohmann::json j = nlohmann::json::parse(readText(info));
    ASSERT_EQ(j.value("port", 0), server.port());
    const std::string token = j.value("token", std::string());
    ASSERT_EQ(token.size(), 32u);

    auto reply = [&](const nlohmann::json& req) {
        const std::string r = roundTrip(server.port(), req.dump() + "\n");
        return r.empty() ? nlohmann::json() : nlohmann::json::parse(r);
    };
    auto waitPoll = [&](LiveLinkRequest& out) {
        for (int i = 0; i < 50; ++i) {
            if (server.poll(out)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };

    EXPECT_TRUE(reply({{"token", token}, {"cmd", "ping"}}).value("ok", false));

    const std::string abs = fs::absolute(kFbx).string();
    auto ok = reply({{"token", token}, {"cmd", "import"}, {"path", abs}, {"source", "3dsmax"}, {"name", "Araba"}});
    EXPECT_TRUE(ok.value("ok", false)) << ok.dump();
    LiveLinkRequest r;
    ASSERT_TRUE(waitPoll(r));
    EXPECT_EQ(r.path, abs);
    EXPECT_EQ(r.source, "3dsmax");
    EXPECT_EQ(r.name, "Araba");
    EXPECT_TRUE(r.replace);

    // Reddedilenler: hiçbiri kuyruğa girmez.
    EXPECT_FALSE(reply({{"token", "yanlis"}, {"cmd", "import"}, {"path", abs}}).value("ok", true));
    EXPECT_FALSE(reply({{"token", token}, {"cmd", "import"}, {"path", abs + ".yok"}}).value("ok", true));
    EXPECT_FALSE(reply({{"token", token}, {"cmd", "import"}, {"path", "goreli/yol.fbx"}}).value("ok", true));
    const std::string txt = (info.parent_path() / "not.txt").string();
    std::ofstream(txt) << "x";
    EXPECT_FALSE(reply({{"token", token}, {"cmd", "import"}, {"path", txt}}).value("ok", true));
    EXPECT_FALSE(reply({{"token", token}, {"cmd", "rm"}, {"path", abs}}).value("ok", true));
    // Tarayıcıdan localhost'a HTTP: ilk satır JSON değil → yanıtsız kapanır.
    EXPECT_TRUE(roundTrip(server.port(), "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n{\"cmd\":\"import\"}\n").empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(server.poll(r));

    server.stop();
    EXPECT_FALSE(fs::exists(info)) << "kapanınca bağlantı bilgisi silinmeli";
}

TEST(LiveLink, DccScriptsEmbedTheSharedClient) {
    const fs::path root = fs::path(PHOTON_TEST_DATA).parent_path().parent_path() / "integrations";
    const std::string shared = clientBlock(readText(root / "common" / "photon_livelink.py"));
    ASSERT_FALSE(shared.empty());
    for (const char* rel : {"blender/photon_engine_livelink.py", "maya/photon_engine_livelink.py",
                            "3dsmax/photon_engine_livelink.py"})
        EXPECT_EQ(clientBlock(readText(root / rel)), shared) << rel << ": python integrations/sync_client.py çalıştırın";
}
