// theme.cpp — Renk paleti, font yükleme ve ImGui stilinin uygulanması.
#include "ui/theme.h"
#include "ui/icons.h"

#include <filesystem>

namespace photon::ui {

namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, a);
}

ImFont* loadWithIcons(const std::filesystem::path& text, const std::filesystem::path& icons, float size) {
    ImGuiIO& io = ImGui::GetIO();
    if (!std::filesystem::exists(text)) return nullptr;
    ImFont* f = io.Fonts->AddFontFromFileTTF(text.string().c_str(), size);
    if (!f) return nullptr;
    if (std::filesystem::exists(icons)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        // İkonları metnin x-yüksekliğine hizala; Lucide çizgileri ince, biraz büyüt.
        cfg.GlyphOffset = ImVec2(0.0f, 2.0f);
        cfg.GlyphMinAdvanceX = size * 1.1f;
        io.Fonts->AddFontFromFileTTF(icons.string().c_str(), size * 1.05f, &cfg);
    }
    return f;
}

} // namespace

const Palette& palette() {
    static const Palette p{
        rgb(14, 15, 17),        // bg0
        rgb(22, 23, 26),        // bg1
        rgb(31, 33, 37),        // bg2
        rgb(42, 44, 50),        // bg3
        rgb(44, 46, 52),        // border
        rgb(230, 231, 234),     // text
        rgb(146, 150, 160),     // textDim
        rgb(98, 102, 112),      // textFaint
        rgb(255, 138, 61),      // accent
        rgb(255, 160, 96),      // accentHover
        rgb(232, 116, 42),      // accentActive
        rgb(255, 138, 61, 0.16f), // accentSoft
        rgb(76, 195, 138),      // success
        rgb(245, 185, 74),      // warning
        rgb(240, 96, 93),       // danger
    };
    return p;
}

ImU32 col(const ImVec4& c, float alphaMul) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul));
}

Fonts& fonts() {
    static Fonts f;
    return f;
}

void loadFonts(const std::string& assetsRoot) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(assetsRoot) / "fonts";
    const fs::path icons = dir / "lucide.ttf";
    Fonts& f = fonts();
    // ImGui 1.92 glifleri ihtiyaç anında, istenen boyutta rasterleştirir: Türkçe
    // karakterler (ş, ğ, İ) için glif aralığı vermeye gerek yok.
    f.regular = loadWithIcons(dir / "Inter-Regular.ttf", icons, f.baseSize);
    f.medium = loadWithIcons(dir / "Inter-Medium.ttf", icons, f.baseSize);
    f.semibold = loadWithIcons(dir / "Inter-SemiBold.ttf", icons, f.baseSize);
    if (!f.regular) {
#ifdef _WIN32
        if (fs::exists("C:\\Windows\\Fonts\\segoeui.ttf"))
            f.regular = loadWithIcons("C:\\Windows\\Fonts\\segoeui.ttf", icons, f.baseSize);
#endif
        if (!f.regular) f.regular = ImGui::GetIO().Fonts->AddFontDefault();
    }
    if (!f.medium) f.medium = f.regular;
    if (!f.semibold) f.semibold = f.medium;
    ImGui::GetIO().FontDefault = f.regular;
}

void applyTheme(float dpiScale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    const Palette& p = palette();

    // Geometri: 8 px ızgara, yumuşak köşeler; yerleşik (docked) paneller köşesiz.
    s.WindowRounding = 0.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 9.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.TabBarBorderSize = 1.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 7);
    s.ItemInnerSpacing = ImVec2(6, 6);
    s.CellPadding = ImVec2(6, 5);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 12.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None; // panel başlığındaki üçgen menü kapalı
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.DockingSeparatorSize = 3.0f;
    s.DisabledAlpha = 0.45f;
    s.HoverDelayNormal = 0.45f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(p.bg2.x, p.bg2.y, p.bg2.z, 0.98f);
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.bg2;
    c[ImGuiCol_FrameBgHovered] = p.bg3;
    c[ImGuiCol_FrameBgActive] = rgb(50, 53, 60);
    c[ImGuiCol_TitleBg] = p.bg0;
    c[ImGuiCol_TitleBgActive] = p.bg0;
    c[ImGuiCol_TitleBgCollapsed] = p.bg0;
    c[ImGuiCol_MenuBarBg] = p.bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(58, 61, 68);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(74, 78, 87);
    c[ImGuiCol_ScrollbarGrabActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = rgb(200, 202, 208);
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_Button] = p.bg2;
    c[ImGuiCol_ButtonHovered] = p.bg3;
    c[ImGuiCol_ButtonActive] = rgb(56, 59, 66);
    c[ImGuiCol_Header] = p.accentSoft;
    c[ImGuiCol_HeaderHovered] = rgb(255, 255, 255, 0.06f);
    c[ImGuiCol_HeaderActive] = rgb(255, 138, 61, 0.24f);
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = p.accent;
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.accent;
    c[ImGuiCol_Tab] = p.bg1;
    c[ImGuiCol_TabHovered] = p.bg3;
    c[ImGuiCol_TabSelected] = p.bg2;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.bg1;
    c[ImGuiCol_TabDimmedSelected] = p.bg2;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = rgb(255, 138, 61, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = p.bg0;
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = p.accentHover;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.accentHover;
    c[ImGuiCol_TableHeaderBg] = p.bg2;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = rgb(36, 38, 43);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.02f);
    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_TextSelectedBg] = rgb(255, 138, 61, 0.30f);
    c[ImGuiCol_TreeLines] = p.border;
    c[ImGuiCol_DragDropTarget] = p.accent;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.5f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.4f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.62f);

    s.ScaleAllSizes(dpiScale);
    s.FontScaleDpi = dpiScale;
}

} // namespace photon::ui
