// ui_library.cpp — Sol panel: malzeme, ortam (HDRI), stüdyo, model ve doku kütüphaneleri.
//
// Her öğe sürüklenebilir (ImGui drag & drop yükü olarak kimliğini taşır) ve
// viewport'a ya da sahne ağacına bırakılabilir. Malzemeler çift tıkla seçili
// parçaya uygulanır.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/drag_drop.h"
#include "ui/file_dialog.h"

#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace photon {

namespace {

// Türkçe büyük/küçük harf duyarsız arama (ASCII + İ/ı kabaca).
std::string foldCase(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matches(const std::string& text, const std::string& query) {
    if (query.empty()) return true;
    return foldCase(text).find(foldCase(query)) != std::string::npos;
}

// Kart ızgarası: genişliğe sığan sütun sayısı, kartlar eşit genişlikte.
struct Grid {
    int columns = 1;
    float cell = 80.0f;
    int index = 0;
    void begin(float minCell, float spacing) {
        const float avail = ImGui::GetContentRegionAvail().x;
        columns = std::max(1, static_cast<int>((avail + spacing) / (minCell + spacing)));
        cell = std::floor((avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns));
        index = 0;
    }
    void next(float spacing) {
        ++index;
        if (index % columns != 0) ImGui::SameLine(0.0f, spacing);
    }
};

} // namespace

void Application::drawLibrary() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 200), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Kütüphane###Library")) {
        ImGui::End();
        return;
    }
    const ui::Palette& pal = ui::palette();
    ui::PanelHeader(ICON_LAYERS, "Kütüphane");

    // Sekme düğmeleri: ikon + kısa ad, seçili olan vurgulu.
    struct Tab { const char* icon; const char* name; };
    const Tab tabs[] = {{ICON_PALETTE, "Malzeme"}, {ICON_GLOBE, "Ortam"}, {ICON_LAMP_CEILING, "Stüdyo"},
                        {ICON_BOX, "Model"}, {ICON_IMAGE, "Doku"}};
    const int n = static_cast<int>(sizeof(tabs) / sizeof(tabs[0]));
    const float w = ImGui::GetContentRegionAvail().x;
    const float bw = (w - 4.0f * static_cast<float>(n - 1)) / static_cast<float>(n);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 6));
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, 4);
        const bool active = m_s.leftTab == i;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.bg3 : pal.bg1);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.accent : pal.textDim);
        if (ImGui::Button(tabs[i].icon, ImVec2(bw, 0))) m_s.leftTab = i;
        ImGui::PopStyleColor(2);
        ui::Tooltip(tabs[i].name);
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##search", ICON_SEARCH "  Ara…", &m_s.librarySearch);
    ImGui::Spacing();

    ImGui::BeginChild("libcontent", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (m_s.leftTab) {
        case 0: drawLibraryMaterials(); break;
        case 1: drawLibraryEnvironments(); break;
        case 2: drawLibraryStudios(); break;
        case 3: drawLibraryModels(); break;
        default: drawLibraryTextures(); break;
    }
    ImGui::EndChild();
    ImGui::End();
}

void Application::drawLibraryMaterials() {
    const ui::Palette& pal = ui::palette();
    static std::string category; // boş = tümü
    // Kategori çipleri (satıra sığmazsa alta kayar).
    std::vector<std::string> cats = m_s.materials.categories();
    cats.insert(cats.begin(), "");
    float x = 0.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    for (const auto& c : cats) {
        const char* label = c.empty() ? "Tümü" : c.c_str();
        const float bw = ImGui::CalcTextSize(label).x + 18.0f;
        if (x > 0.0f && x + bw > avail) x = 0.0f;
        else if (x > 0.0f) ImGui::SameLine(0, 5);
        const bool on = category == c;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? pal.accentSoft : pal.bg2);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? pal.accent : pal.textDim);
        if (ImGui::Button(label)) category = c;
        ImGui::PopStyleColor(2);
        x += bw + 5.0f;
    }
    ImGui::PopStyleVar(2);
    ImGui::Spacing();
    ImGui::Spacing();

    const float spacing = 8.0f;
    Grid grid;
    grid.begin(86.0f, spacing);
    SceneNode* sel = selectedNode();
    int shown = 0;
    for (const auto& p : m_s.materials.presets()) {
        if (!category.empty() && p.category != category) continue;
        if (!matches(p.name, m_s.librarySearch) && !matches(p.category, m_s.librarySearch)) continue;
        const unsigned int tex = m_s.thumbs.texture("mat:" + p.id);
        if (ui::ThumbCard(p.id.c_str(), tex, p.name.c_str(), ImVec2(grid.cell, grid.cell), false, ImVec4(p.baseColor.r, p.baseColor.g, p.baseColor.b, 1))) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && sel) applyMaterialPreset(sel->uid, p);
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadMaterial, p.id.c_str(), p.id.size() + 1);
            if (tex) ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(tex)), ImVec2(72, 72));
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::EndDragDropSource();
        } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::BeginTooltip();
            if (tex) ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(tex)), ImVec2(160, 160));
            ImGui::PushFont(ui::fonts().semibold, 0.0f);
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::PopFont();
            ImGui::TextColored(pal.textDim, "%s  •  %s", p.category.c_str(),
                               p.kind == MaterialKind::Glass ? "Cam (kırılma)" : "Principled");
            ImGui::TextColored(pal.textFaint, "Parçaya sürükleyin ya da çift tıklayın");
            ImGui::EndTooltip();
        }
        if (ImGui::BeginPopupContextItem("matctx")) {
            if (ImGui::MenuItem("Seçili parçaya uygula", nullptr, false, sel != nullptr)) applyMaterialPreset(sel->uid, p);
            ImGui::EndPopup();
        }
        grid.next(spacing);
        ++shown;
    }
    if (shown == 0) ui::Hint("Eşleşen malzeme yok.");
    ImGui::NewLine();
}

void Application::drawLibraryEnvironments() {
    const float spacing = 8.0f;
    if (ui::GhostButton(ICON_FOLDER_OPEN "  HDRI dosyası aç…", ImVec2(-FLT_MIN, 0))) openHdrDialog();
    ImGui::Spacing();
    Grid grid;
    grid.begin(140.0f, spacing);
    for (const auto& e : m_s.environments) {
        if (!matches(e.name, m_s.librarySearch)) continue;
        const bool active = m_s.environment.hdrPath == e.path &&
                            (!e.path.empty() || (m_s.environment.zenith.r == e.zenith.r && m_s.environment.horizon.r == e.horizon.r));
        if (ui::ThumbCard(e.id.c_str(), m_s.thumbs.texture("env:" + e.id), e.name.c_str(),
                          ImVec2(grid.cell, grid.cell * 0.5f), active, ImVec4(e.horizon.r, e.horizon.g, e.horizon.b, 1)))
            applyEnvironment(e);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadHdr, e.id.c_str(), e.id.size() + 1);
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::EndDragDropSource();
        }
        grid.next(spacing);
    }
    ImGui::NewLine();
    ui::Hint("HDRI'lar: Poly Haven (CC0). Kendi .hdr/.exr dosyanızı pencereye sürükleyebilirsiniz.");
}

void Application::drawLibraryStudios() {
    const ui::Palette& pal = ui::palette();
    ui::Hint("Stüdyo = ortam + ışık düzeni. Işıklar sahnenin boyutuna göre yerleşir.");
    ImGui::Spacing();
    for (const auto& s : m_s.studios) {
        if (!matches(s.name, m_s.librarySearch)) continue;
        ImGui::PushID(s.id.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 64.0f;
        const bool clicked = ImGui::InvisibleButton("studio", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ui::col(hovered ? pal.bg3 : pal.bg2), 8.0f);
        // Ortamın küçük resmi solda.
        std::string envKey;
        for (const auto& e : m_s.environments)
            if (e.id == s.environment || ui::fileName(e.path) == s.environment) envKey = "env:" + e.id;
        const unsigned int tex = envKey.empty() ? 0 : m_s.thumbs.texture(envKey);
        const float iw = 96.0f;
        if (tex)
            dl->AddImageRounded(static_cast<ImTextureID>(static_cast<intptr_t>(tex)), ImVec2(p.x + 6, p.y + 6),
                                ImVec2(p.x + 6 + iw, p.y + h - 6), ImVec2(0.25f, 0.1f), ImVec2(0.75f, 0.9f),
                                IM_COL32_WHITE, 6.0f);
        else
            dl->AddRectFilled(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + 6 + iw, p.y + h - 6), ui::col(pal.bg3), 6.0f);
        ImGui::PushFont(ui::fonts().semibold, 0.0f);
        dl->AddText(ImVec2(p.x + iw + 18, p.y + 10), ui::col(pal.text), s.name.c_str());
        ImGui::PopFont();
        char info[96];
        std::snprintf(info, sizeof(info), "%d ışık", static_cast<int>(s.lights.size()));
        dl->AddText(ImVec2(p.x + iw + 18, p.y + 32), ui::col(pal.textDim), s.description.empty() ? info : s.description.c_str());
        if (clicked) applyStudio(s);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadStudio, s.id.c_str(), s.id.size() + 1);
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
}

void Application::drawLibraryModels() {
    const ui::Palette& pal = ui::palette();
    if (ui::PrimaryButton(ICON_UPLOAD "  Model içe aktar…", ImVec2(-FLT_MIN, 0))) openModelDialog();
    ImGui::Spacing();
    ui::Hint("OBJ, glTF ve GLB desteklenir. Dosyayı pencereye sürükleyip bırakmak da yeterli.");
    ImGui::Spacing();
    ImGui::SeparatorText("Örnek modeller");
    for (const auto& m : m_s.models) {
        const std::string name = ui::fileName(m);
        if (!matches(name, m_s.librarySearch)) continue;
        ImGui::PushID(m.c_str());
        ImGui::TextColored(pal.textDim, ICON_BOX);
        ImGui::SameLine();
        if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            importModel(m);
        ui::Tooltip("Çift tıkla ya da viewport'a sürükle");
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadModel, m.c_str(), m.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Örnek sahneler");
    if (ImGui::Selectable(ICON_SPARKLES "  Ürün vitrini")) loadSampleScene();
    if (ImGui::Selectable(ICON_BOXES "  Cornell kutusu")) loadCornellScene();
}

void Application::drawLibraryTextures() {
    const ui::Palette& pal = ui::palette();
    if (ui::GhostButton(ICON_IMAGE_PLUS "  Doku ekle…", ImVec2(-FLT_MIN, 0))) {
        std::string path;
        FileDialogFilter f[] = {{"Resimler", "*.png;*.jpg;*.jpeg;*.tga;*.bmp"}};
        if (showFileDialog(path, FileDialogMode::Open, "Doku seç", f, 1)) m_s.textures.push_back(path);
    }
    ImGui::Spacing();
    ui::Hint("Dokuyu Malzeme sekmesindeki bir yuvaya (taban rengi, normal, pürüzlülük, metal) sürükleyin.");
    ImGui::Spacing();
    if (m_s.textures.empty()) ui::Hint("Henüz doku yok. assets/textures klasörüne koyun ya da pencereye sürükleyin.");
    for (const auto& t : m_s.textures) {
        const std::string name = ui::fileName(t);
        if (!matches(name, m_s.librarySearch)) continue;
        ImGui::PushID(t.c_str());
        ImGui::TextColored(pal.textDim, ICON_IMAGE);
        ImGui::SameLine();
        ImGui::Selectable(name.c_str());
        ui::Tooltip(t.c_str());
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kPayloadTexture, t.c_str(), t.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
}

} // namespace photon
