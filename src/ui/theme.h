// theme.h — Arayüzün renk paleti, yazı tipleri ve ImGui stil ayarları.
//
// Tüm renkler tek yerde (Palette) tanımlıdır; paneller sabit renk yazmak yerine
// buradaki adlandırılmış renkleri kullanır. Tek vurgu rengi (turuncu, "foton")
// yalnız ana eylemlerde ve seçimde görünür; geri kalan her şey nötr gri tonlar.
#pragma once

#include <imgui.h>
#include <string>

namespace photon::ui {

struct Palette {
    ImVec4 bg0;        ///< En koyu: uygulama zemini, viewport çevresi
    ImVec4 bg1;        ///< Panel arka planı
    ImVec4 bg2;        ///< Girdi kutuları, kartlar
    ImVec4 bg3;        ///< Üzerine gelme (hover)
    ImVec4 border;
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 textFaint;
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 accentActive;
    ImVec4 accentSoft;  ///< Seçim zemini (yarı saydam vurgu)
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
};

const Palette& palette();

/// ImU32 kısayolu: col(palette().accent, 0.5f)
ImU32 col(const ImVec4& c, float alphaMul = 1.0f);

/// Yazı tipleri. Hepsinde Lucide ikonları birleşik (ICON_* makroları çalışır).
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* medium = nullptr;
    ImFont* semibold = nullptr;
    float baseSize = 14.0f;   ///< DPI ölçeksiz gövde metni boyutu
};

Fonts& fonts();

/// Inter + Lucide yükler. Dosyalar yoksa ImGui varsayılan fontuna düşer.
void loadFonts(const std::string& assetsRoot);

/// Stil ve renkleri uygular. @p dpiScale: monitör ölçeği (1.0 = %100).
void applyTheme(float dpiScale);

} // namespace photon::ui
