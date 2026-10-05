// main_app.cpp — photon_app giriş noktası: komut satırı seçenekleri ve hata kutusu.
//
//   photon_app [dosya.photon | model.obj]
//   photon_app --scene sample|cornell|empty
//   photon_app --screenshot cikti.png [--spp 16] [--size 1600x940] [--ui render]
// --screenshot: arayüzü çizer, viewport en az --spp örneğe ulaşınca pencerenin
// ekran görüntüsünü alır ve çıkar (otomatik görsel kontrol için).
#include "app/application.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    // argv Windows'ta ANSI kod sayfasında gelir; Türkçe dosya adları için UTF-16'dan çevir.
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; w && i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(std::max(0, len - 1)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(s);
    }
    if (w) LocalFree(w);
    (void)argc;
    (void)argv;
#else
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

void showError(const std::string& msg) {
    std::cerr << "Fatal: " << msg << std::endl;
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(0, len)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, w.data(), len);
    MessageBoxW(nullptr, w.c_str(), L"PhotonEngine — Hata", MB_OK | MB_ICONERROR);
#endif
}

} // namespace

int main(int argc, char** argv) {
    photon::LaunchOptions opts;
    const auto args = utf8Args(argc, argv);
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--screenshot") opts.screenshotPath = next();
        else if (a == "--spp") opts.screenshotSpp = std::atoi(next().c_str());
        else if (a == "--timeout") opts.screenshotTimeout = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--scene") opts.scene = next();
        else if (a == "--ui") opts.uiState = next();
        else if (a == "--size") {
            const std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &opts.windowW, &opts.windowH);
        } else if (!a.empty() && a[0] != '-') opts.openPath = a;
    }
    try {
        photon::Application app(opts);
        return app.run();
    } catch (const std::exception& e) {
        showError(e.what());
        return 1;
    }
}
