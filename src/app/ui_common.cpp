// ui_common.cpp — Panel başlığı, küçük resim kartı ve TRS ayrıştırma/birleştirme.
#include "app/ui_common.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>

namespace photon::ui {

void PanelHeader(const char* icon, const char* title, const char* subtitle) {
    const Palette& pal = palette();
    ImGui::TextColored(pal.accent, "%s", icon);
    ImGui::SameLine(0, 8);
    ImGui::PushFont(fonts().semibold, 0.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle && *subtitle) {
        ImGui::SameLine();
        ImGui::TextColored(pal.textFaint, "%s", subtitle);
    }
    ImGui::Spacing();
}

bool ThumbCard(const char* id, unsigned int texture, const char* label, const ImVec2& imageSize,
               bool active, const ImVec4& fallbackColor) {
    const Palette& pal = palette();
    ImGui::PushID(id);
    const float textH = label ? ImGui::GetTextLineHeight() + 4.0f : 0.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(imageSize.x, imageSize.y + textH);
    const bool clicked = ImGui::InvisibleButton("card", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 imgMax(p.x + imageSize.x, p.y + imageSize.y);
    const float r = 6.0f;
    dl->AddRectFilled(p, imgMax, col(pal.bg2), r);
    if (texture) {
        dl->AddImageRounded(static_cast<ImTextureID>(static_cast<intptr_t>(texture)), p, imgMax, ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32_WHITE, r);
    } else {
        // Küçük resim henüz hazır değil: taban renginde yumuşak bir daire.
        const ImVec2 c((p.x + imgMax.x) * 0.5f, (p.y + imgMax.y) * 0.5f);
        dl->AddCircleFilled(c, std::min(imageSize.x, imageSize.y) * 0.32f, col(fallbackColor, 0.85f), 48);
    }
    if (active) dl->AddRect(p, imgMax, col(pal.accent), r, 0, 2.0f);
    else if (hovered) dl->AddRect(p, imgMax, col(pal.text, 0.35f), r, 0, 1.5f);
    if (label) {
        // Uzun adı üç noktayla kısalt.
        std::string text = label;
        const float maxW = imageSize.x - 2.0f;
        if (ImGui::CalcTextSize(text.c_str()).x > maxW) {
            while (text.size() > 1 && ImGui::CalcTextSize((text + "…").c_str()).x > maxW) {
                // UTF-8: devam baytlarını (10xxxxxx) da birlikte sil.
                do { text.pop_back(); } while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80);
                if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0xC0) text.pop_back();
            }
            text += "…";
        }
        const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->AddText(ImVec2(p.x + (imageSize.x - ts.x) * 0.5f, imgMax.y + 2.0f),
                    col(active ? pal.text : (hovered ? pal.text : pal.textDim)), text.c_str());
    }
    ImGui::PopID();
    return clicked;
}

TRS decompose(const Transform& xf) {
    const Mat4f& m = xf.matrix();
    TRS out;
    out.t = Vec3f(m(0, 3), m(1, 3), m(2, 3));
    Vec3f c0(m(0, 0), m(1, 0), m(2, 0));
    Vec3f c1(m(0, 1), m(1, 1), m(2, 1));
    Vec3f c2(m(0, 2), m(1, 2), m(2, 2));
    out.s = Vec3f(c0.length(), c1.length(), c2.length());
    // Negatif determinant: ayna. Bir eksenin ölçeği negatif kabul edilir.
    if (c0.cross(c1).dot(c2) < 0.0f) out.s.x = -out.s.x;
    const float sx = std::abs(out.s.x) > 1e-12f ? out.s.x : 1.0f;
    const float sy = out.s.y > 1e-12f ? out.s.y : 1.0f;
    const float sz = out.s.z > 1e-12f ? out.s.z : 1.0f;
    c0 = c0 / sx;
    c1 = c1 / sy;
    c2 = c2 / sz;
    // R = Rz·Ry·Rx için: R[2][0] = -sin(ry). Gimbal kilidinde (|sin ry| ≈ 1) rz = 0 alınır.
    const float r20 = std::clamp(c0.z, -1.0f, 1.0f);
    const float ry = -std::asin(r20);
    float rx, rz;
    if (std::abs(r20) < 0.9999f) {
        rx = std::atan2(c1.z, c2.z);
        rz = std::atan2(c0.y, c0.x);
    } else {
        rx = std::atan2(-c2.y, c1.y);
        rz = 0.0f;
    }
    out.rDeg = Vec3f(rx, ry, rz) * RAD_TO_DEG;
    // -0.0 yerine 0.0 gösterilsin.
    for (float* c : {&out.rDeg.x, &out.rDeg.y, &out.rDeg.z})
        if (std::abs(*c) < 1e-4f) *c = 0.0f;
    return out;
}

Transform compose(const TRS& trs) {
    return Transform::translate(trs.t) * Transform::rotateZ(trs.rDeg.z * DEG_TO_RAD) *
           Transform::rotateY(trs.rDeg.y * DEG_TO_RAD) * Transform::rotateX(trs.rDeg.x * DEG_TO_RAD) *
           Transform::scale(Vec3f(std::abs(trs.s.x) < 1e-6f ? 1e-6f : trs.s.x, std::max(1e-6f, trs.s.y),
                                  std::max(1e-6f, trs.s.z)));
}

} // namespace photon::ui
