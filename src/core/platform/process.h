// process.h — Harici programı çalıştırma (DCC dönüştürücüleri: blender, mayapy, 3dsmaxbatch).
//
// Kabuk kullanılmaz: argümanlar ayrı ayrı verilir (POSIX: fork + execvp; Windows: CreateProcessW
// ve CommandLineToArgvW kurallarıyla tırnaklanmış komut satırı). Dosya adlarındaki boşluk, tırnak,
// noktalı virgül komut enjeksiyonuna yol açamaz. stdout ve stderr birlikte toplanır; süre aşılırsa
// süreç sonlandırılır.
#pragma once

#include <string>
#include <vector>

namespace photon {

struct ProcessResult {
    bool started = false;  ///< program bulunup başlatılabildi mi
    bool timedOut = false; ///< süre doldu, süreç sonlandırıldı
    int exitCode = -1;
    std::string output;    ///< stdout + stderr (en çok 1 MB)
};

/// @p argv[0] program (tam yol ya da PATH'te ad), kalanlar argümanlar (UTF-8).
ProcessResult runProcess(const std::vector<std::string>& argv, int timeoutSeconds);

/// Windows komut satırı için tek argümanın tırnaklanması (CommandLineToArgvW / MSVCRT kuralları).
/// Her platformda derlenir ki test edilebilsin.
std::string quoteWindowsArgument(const std::string& arg);

} // namespace photon
