// file_dialog.h — İşletim sisteminin yerel dosya aç/kaydet penceresi (UTF-8 yollar).
#pragma once

#include <string>

namespace photon {

enum class FileDialogMode { Open, Save };

struct FileDialogFilter {
    const char* label;
    const char* pattern;
};

/// ponytail: Windows'ta Win32 commdlg (W API), diğer platformlarda false döner.
/// @param inOutPath Başlangıç yolu; seçimde UTF-8 yol yazılır.
bool showFileDialog(std::string& inOutPath, FileDialogMode mode, const char* title,
                    const FileDialogFilter* filters = nullptr, int filterCount = 0);

/// Dosyayı Gezgin'de seçili gösterir.
void openInFileBrowser(const std::string& path);

} // namespace photon
