// file_dialog.cpp — İşletim sisteminin dosya aç/kaydet penceresi.
//
// Windows'ta Unicode (W) API kullanılır ve yollar UTF-8 <-> UTF-16 çevrilir;
// böylece "Masaüstü/şişe.obj" gibi Türkçe adlar bozulmaz. Diğer platformlarda
// şimdilik diyalog yok (false döner).
#include "ui/file_dialog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

#include <string>
#include <vector>

namespace photon {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace
#endif

bool showFileDialog(std::string& inOutPath, FileDialogMode mode, const char* title,
                    const FileDialogFilter* filters, int filterCount) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const std::wstring initial = widen(inOutPath);
    if (initial.size() < buffer.size()) std::copy(initial.begin(), initial.end(), buffer.begin());

    // Filtre biçimi: "Ad\0*.a;*.b\0Ad2\0*.*\0\0" — çift sıfırla biter.
    std::wstring filterSpec;
    if (filters && filterCount > 0) {
        for (int i = 0; i < filterCount; ++i) {
            filterSpec += widen(filters[i].label);
            filterSpec.push_back(L'\0');
            filterSpec += widen(filters[i].pattern);
            filterSpec.push_back(L'\0');
        }
        filterSpec.push_back(L'\0');
    }
    const std::wstring wtitle = widen(title ? title : "");

    // Varsayılan uzantı: ilk filtrenin ilk deseninden (ör. "*.png" → "png").
    std::wstring defExt;
    if (filters && filterCount > 0 && filters[0].pattern) {
        std::string pat = filters[0].pattern;
        auto dot = pat.find("*.");
        if (dot != std::string::npos) {
            auto end = pat.find(';', dot);
            std::string ext = pat.substr(dot + 2, end == std::string::npos ? std::string::npos : end - dot - 2);
            if (ext != "*") defExt = widen(ext);
        }
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!defExt.empty()) ofn.lpstrDefExt = defExt.c_str();
    if (filterSpec.size() > 1) ofn.lpstrFilter = filterSpec.c_str();

    BOOL ok = FALSE;
    if (mode == FileDialogMode::Open) {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameW(&ofn);
    } else {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ok = GetSaveFileNameW(&ofn);
    }
    if (!ok) return false;
    inOutPath = narrow(buffer.data());
    return true;
#else
    (void)inOutPath;
    (void)mode;
    (void)title;
    (void)filters;
    (void)filterCount;
    return false;
#endif
}

void openInFileBrowser(const std::string& path) {
#ifdef _WIN32
    const std::wstring w = widen(path);
    const std::wstring args = L"/select,\"" + w + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

} // namespace photon
