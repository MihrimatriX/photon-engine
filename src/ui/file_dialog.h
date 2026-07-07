#pragma once

#include <string>

namespace photon {

enum class FileDialogMode { Open, Save };

struct FileDialogFilter {
    const char* label;
    const char* pattern;
};

/// ponytail: Win32 commdlg on Windows, path unchanged elsewhere
bool showFileDialog(std::string& inOutPath, FileDialogMode mode, const char* title,
                    const FileDialogFilter* filters = nullptr, int filterCount = 0);

} // namespace photon
