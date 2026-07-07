#include "ui/theme.h"
#include "imgui.h"

namespace photon {

void applyKeyShotTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    s.WindowRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 4.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.19f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.85f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.90f, 0.80f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.48f, 0.88f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.55f, 0.95f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.95f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.17f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.48f, 0.88f, 0.80f);
    c[ImGuiCol_TabActive] = ImVec4(0.18f, 0.40f, 0.75f, 1.0f);
    c[ImGuiCol_DockingPreview] = ImVec4(0.22f, 0.48f, 0.88f, 0.50f);
    s.ItemSpacing = ImVec2(8, 6);
    s.FramePadding = ImVec2(8, 4);
}

} // namespace photon
