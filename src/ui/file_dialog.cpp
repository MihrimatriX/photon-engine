#include "ui/file_dialog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include <cstring>

namespace photon {

bool showFileDialog(std::string& inOutPath, FileDialogMode mode, const char* title,
                    const FileDialogFilter* filters, int filterCount) {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    if (!inOutPath.empty()) {
        std::strncpy(buffer, inOutPath.c_str(), MAX_PATH - 1);
    }

    std::string filterSpec;
    if (filters && filterCount > 0) {
        for (int i = 0; i < filterCount; ++i) {
            filterSpec += filters[i].label;
            filterSpec.push_back('\0');
            filterSpec += filters[i].pattern;
            filterSpec.push_back('\0');
        }
        filterSpec.push_back('\0');
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (filterSpec.size() > 1) {
        ofn.lpstrFilter = filterSpec.c_str();
    }

    BOOL ok = FALSE;
    if (mode == FileDialogMode::Open) {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameA(&ofn);
    } else {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ok = GetSaveFileNameA(&ofn);
    }

    if (!ok) return false;
    inOutPath = buffer;
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

} // namespace photon
