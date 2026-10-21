// widgets.cpp — Ortak arayüz bileşenlerinin çizimi.
#include "ui/widgets.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "core/platform/path.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace photon::ui {

bool BeginProps(const char* id, float labelFraction) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, labelFraction);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelFraction);
    return true;
}

void Prop(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndProps() {
    ImGui::EndTable();
}

bool Section(const char* icon, const char* label, bool defaultOpen) {
    ImGui::PushID(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID("open");
    bool open = storage->GetBool(id, defaultOpen);

    ImGui::Spacing();
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton("hdr", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (clicked) {
        open = !open;
        storage->SetBool(id, open);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Palette& pal = palette();
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(pal.bg3, 0.6f), 6.0f);
    const float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
    float x = p.x + 4.0f;
    if (icon && *icon) {
        dl->AddText(ImVec2(x, ty), col(pal.accent), icon);
        x += ImGui::GetFontSize() * 1.45f;
    }
    ImGui::PushFont(fonts().semibold, 0.0f);
    dl->AddText(ImVec2(x, ty), col(pal.text), label);
    ImGui::PopFont();
    const char* chev = open ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT;
    dl->AddText(ImVec2(p.x + w - ImGui::GetFontSize() * 1.2f, ty), col(pal.textFaint), chev);
    ImGui::PopID();
    if (open) ImGui::Spacing();
    return open;
}

bool IconButton(const char* icon, const char* tooltip, bool active, float size) {
    const Palette& pal = palette();
    if (size <= 0.0f) size = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.accentSoft : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.28f) : pal.bg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.accent : pal.text);
    const bool pressed = ImGui::Button(icon, ImVec2(size, size));
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    if (tooltip && *tooltip) Tooltip(tooltip);
    return pressed;
}

bool PrimaryButton(const char* label, const ImVec2& size) {
    const Palette& pal = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, pal.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, pal.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.07f, 0.06f, 0.05f, 1.0f));
    ImGui::PushFont(fonts().semibold, 0.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool GhostButton(const char* label, const ImVec2& size) {
    const Palette& pal = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.bg3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return pressed;
}

bool Segmented(const char* id, int* current, const char* const* items, int count, float width) {
    const Palette& pal = palette();
    ImGui::PushID(id);
    if (width < 0.0f) width = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), col(pal.bg2), 6.0f);
    const float segW = width / static_cast<float>(count);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + segW * static_cast<float>(i), p.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(segW, h))) {
            if (*current != i) changed = true;
            *current = i;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 a(p.x + segW * static_cast<float>(i) + 2.0f, p.y + 2.0f);
        const ImVec2 b(a.x + segW - 4.0f, p.y + h - 2.0f);
        if (*current == i) {
            dl->AddRectFilled(a, b, col(pal.bg3), 5.0f);
            dl->AddRect(a, b, col(pal.accent, 0.55f), 5.0f);
        } else if (hovered) {
            dl->AddRectFilled(a, b, col(pal.bg3, 0.5f), 5.0f);
        }
        const ImVec2 ts = ImGui::CalcTextSize(items[i]);
        dl->AddText(ImVec2(a.x + (segW - 4.0f - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f),
                    col(*current == i ? pal.text : pal.textDim), items[i]);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::PopID();
    return changed;
}

bool Toggle(const char* id, bool* value) {
    const Palette& pal = palette();
    const float h = ImGui::GetFrameHeight() * 0.78f;
    const float w = h * 1.8f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float yOff = (ImGui::GetFrameHeight() - h) * 0.5f;
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("tgl", ImVec2(w, ImGui::GetFrameHeight()));
    ImGui::PopID();
    if (clicked) *value = !*value;
    // Düğmenin kayması küçük bir animasyonla (ImGui'nin son tıklama zamanına göre).
    float t = *value ? 1.0f : 0.0f;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.LastActiveId == g.CurrentWindow->GetID(id) && g.LastActiveIdTimer < 0.12f) {
        const float k = g.LastActiveIdTimer / 0.12f;
        t = *value ? k : 1.0f - k;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + yOff);
    const ImVec2 b(p.x + w, p.y + yOff + h);
    const ImVec4 off = pal.bg3;
    const ImVec4 on = pal.accent;
    const ImVec4 bg(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.0f);
    dl->AddRectFilled(a, b, col(bg), h * 0.5f);
    const float r = h * 0.5f - 2.5f;
    dl->AddCircleFilled(ImVec2(a.x + h * 0.5f + t * (w - h), a.y + h * 0.5f), r, IM_COL32(245, 245, 247, 255));
    return clicked;
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextColored(palette().textFaint, ICON_CIRCLE_HELP);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textFaint);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void Badge(const char* text, const ImVec4& color) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float padX = 6.0f, padY = 2.0f;
    const float h = ImGui::GetTextLineHeight() + padY * 2.0f;
    const float y = p.y + (ImGui::GetFrameHeight() - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + ts.x + padX * 2.0f, y + h), col(color, 0.18f), 4.0f);
    dl->AddText(ImVec2(p.x + padX, y + padY), col(color), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2.0f, ImGui::GetFrameHeight()));
}

void Tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

std::string fileName(const std::string& path) {
    return pathToUtf8(pathFromUtf8(path).filename());
}

std::string formatCount(size_t n) {
    char buf[32];
    if (n < 1000) std::snprintf(buf, sizeof(buf), "%zu", n);
    else if (n < 1000000) std::snprintf(buf, sizeof(buf), "%.1f bin", static_cast<double>(n) / 1e3);
    else std::snprintf(buf, sizeof(buf), "%.1f milyon", static_cast<double>(n) / 1e6);
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string formatDuration(double seconds) {
    char buf[64];
    if (seconds < 60.0) {
        std::snprintf(buf, sizeof(buf), "%.1f sn", seconds);
    } else if (seconds < 3600.0) {
        const int m = static_cast<int>(seconds / 60.0);
        std::snprintf(buf, sizeof(buf), "%d dk %02d sn", m, static_cast<int>(seconds) % 60);
    } else {
        const int h = static_cast<int>(seconds / 3600.0);
        std::snprintf(buf, sizeof(buf), "%d sa %02d dk", h, (static_cast<int>(seconds) / 60) % 60);
    }
    return buf;
}

} // namespace photon::ui

namespace photon::ui {

namespace {

// Değerin çubuktaki oranı [0,1]. Logaritmik kaydırıcıda log ölçeğinde hesaplanır.
float sliderFraction(float v, float lo, float hi, bool logarithmic) {
    if (hi <= lo) return 0.0f;
    v = std::clamp(v, lo, hi);
    if (logarithmic) {
        if (lo > 0.0f) return std::log(v / lo) / std::log(hi / lo);
        return std::log1p(v - lo) / std::log1p(hi - lo);
    }
    return (v - lo) / (hi - lo);
}

template <typename Fn>
bool filledSlider(float fraction, Fn&& draw) {
    const Palette& pal = palette();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Kanal 0: dolgu (arkada), kanal 1: ImGui kaydırıcısı (çerçeve + metin).
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.04f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
    const bool changed = draw();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(5);
    dl->ChannelsSetCurrent(0);
    const float r = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(pal.bg2), r);
    const float fx = p.x + w * std::clamp(fraction, 0.0f, 1.0f);
    if (fx > p.x + 1.0f) {
        dl->AddRectFilled(p, ImVec2(fx, p.y + h), active ? col(pal.accent, 0.45f) : IM_COL32(60, 64, 73, 255), r,
                          fx >= p.x + w - r ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
    }
    // İnce işaretçi: yalnız üzerine gelince/sürüklerken ve alt kenarda (sayının üstüne binmesin).
    if (active || hovered)
        dl->AddRectFilled(ImVec2(std::max(p.x, fx - 1.5f), p.y + h - 3.0f), ImVec2(std::min(p.x + w, fx + 1.5f), p.y + h),
                          col(pal.accent), 1.0f);
    dl->ChannelsMerge();
    return changed;
}

} // namespace

bool SliderF(const char* id, float* v, float vMin, float vMax, const char* fmt, ImGuiSliderFlags flags) {
    const float f = sliderFraction(*v, vMin, vMax, (flags & ImGuiSliderFlags_Logarithmic) != 0);
    return filledSlider(f, [&] { return ImGui::SliderFloat(id, v, vMin, vMax, fmt, flags); });
}

bool SliderI(const char* id, int* v, int vMin, int vMax, const char* fmt, ImGuiSliderFlags flags) {
    const float f = sliderFraction(static_cast<float>(*v), static_cast<float>(vMin), static_cast<float>(vMax),
                                   (flags & ImGuiSliderFlags_Logarithmic) != 0);
    return filledSlider(f, [&] { return ImGui::SliderInt(id, v, vMin, vMax, fmt, flags); });
}

} // namespace photon::ui
