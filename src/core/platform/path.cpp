// path.cpp — Çalışan exe'nin klasörü ve assets/ kökünün bulunması (uygulama ve CLI ortak).
#include "core/platform/path.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace photon {

namespace fs = std::filesystem;

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#endif
    return fs::current_path();
}

// Önce exe'nin yanı (dağıtılan paket), sonra çalışma dizini; her birinden birkaç
// seviye yukarı çıkılır (derleme klasöründen ya da repo kökünden çalıştırma).
std::string findAssetsRoot() {
    for (fs::path start : {executableDir(), fs::current_path()}) {
        fs::path cur = start;
        for (int i = 0; i < 6; ++i) {
            if (fs::exists(cur / "assets" / "materials")) return pathToUtf8(cur / "assets");
            if (!cur.has_parent_path() || cur.parent_path() == cur) break;
            cur = cur.parent_path();
        }
    }
    return "assets";
}

} // namespace photon
