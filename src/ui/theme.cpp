#include "ui/theme.h"
#include "imgui.h"

namespace photon {

void applyKeyShotTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    // KeyShot product-viz chrome: charcoal panels, soft corners, muted steel accent
    s.WindowRounding = 6.0f;
    s.ChildRounding = 5.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 5.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.TabBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(6, 5);
    s.FramePadding = ImVec2(10, 6);
    s.WindowPadding = ImVec2(14, 12);
    s.IndentSpacing = 18.0f;
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 10.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(14, 5);
    s.CellPadding = ImVec2(6, 4);

    // Neutrals — cool charcoal, not purple / not neon
    const ImVec4 bg0(0.078f, 0.078f, 0.082f, 1.00f); // #141415
    const ImVec4 bg1(0.105f, 0.105f, 0.110f, 1.00f); // #1B1B1C
    const ImVec4 bg2(0.140f, 0.140f, 0.148f, 1.00f); // #242426
    const ImVec4 bg3(0.175f, 0.175f, 0.185f, 1.00f); // #2C2C2F
    const ImVec4 border(0.22f, 0.22f, 0.24f, 1.00f);
    const ImVec4 text(0.90f, 0.90f, 0.91f, 1.00f);
    const ImVec4 textDim(0.50f, 0.50f, 0.53f, 1.00f);
    // Subtle steel-blue accent (KeyShot-ish, not AI purple glow)
    const ImVec4 accent(0.32f, 0.48f, 0.62f, 1.00f);
    const ImVec4 accentHi(0.40f, 0.58f, 0.74f, 1.00f);
    const ImVec4 accentLo(0.24f, 0.38f, 0.50f, 1.00f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = bg0;
    c[ImGuiCol_ChildBg] = bg1;
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.105f, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.26f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.065f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.09f, 0.095f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = bg0;
    c[ImGuiCol_MenuBarBg] = ImVec4(0.07f, 0.07f, 0.075f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.065f, 0.55f);
    c[ImGuiCol_ScrollbarGrab] = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = accentLo;
    c[ImGuiCol_CheckMark] = accentHi;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHi;
    c[ImGuiCol_Button] = accentLo;
    c[ImGuiCol_ButtonHovered] = accent;
    c[ImGuiCol_ButtonActive] = accentHi;
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHi;
    c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = bg1;
    c[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_TabActive] = bg3;
    c[ImGuiCol_TabUnfocused] = bg0;
    c[ImGuiCol_TabUnfocusedActive] = bg2;
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = bg0;
    c[ImGuiCol_PlotLines] = accentHi;
    c[ImGuiCol_PlotLinesHovered] = accentHi;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHi;
    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DragDropTarget] = ImVec4(0.55f, 0.72f, 0.85f, 0.90f);
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.50f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.40f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.55f);
}

} // namespace photon
