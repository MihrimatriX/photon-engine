// ui_scene.cpp — Sağ üst panel: sahne ağacı (modeller ve parçalar), ışıklar ve
// ortam/zemin girdileri. Görünürlük gözü, sağ tık menüsü, çift tıkla yeniden
// adlandırma ve malzemeyi ağaçtaki bir düğüme bırakma burada.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/drag_drop.h"

#include <imgui_stdlib.h>
#include <functional>

namespace photon {

namespace {

const char* nodeIcon(const SceneNode& n) {
    switch (n.type) {
        case SceneNodeType::Group: return n.sourcePath.empty() ? ICON_GROUP : ICON_BOXES;
        case SceneNodeType::Mesh: return ICON_BOX;
        case SceneNodeType::Sphere: return ICON_CIRCLE_DOT;
        case SceneNodeType::Light: return ICON_LIGHTBULB;
    }
    return ICON_BOX;
}

const char* lightIcon(LightDesc::Type t) {
    switch (t) {
        case LightDesc::Type::Area: return ICON_LAMP_CEILING;
        case LightDesc::Type::Directional: return ICON_SUN;
        case LightDesc::Type::Point: return ICON_LIGHTBULB;
    }
    return ICON_LIGHTBULB;
}

// Satırın sağ ucuna göz ikonu (görünürlük). true dönerse değer değişti.
bool eyeToggle(const char* id, bool& visible) {
    const ui::Palette& pal = ui::palette();
    const float w = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.bg3);
    ImGui::PushStyleColor(ImGuiCol_Text, visible ? pal.textDim : pal.textFaint);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    const bool clicked = ImGui::Button(visible ? ICON_EYE : ICON_EYE_OFF, ImVec2(w, ImGui::GetTextLineHeight() + 2));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    if (clicked) visible = !visible;
    return clicked;
}

} // namespace

void Application::drawSceneTree() {
    if (!ImGui::Begin("Sahne###Scene")) {
        ImGui::End();
        return;
    }
    const ui::Palette& pal = ui::palette();
    ui::PanelHeader(ICON_LIST_TREE, "Sahne");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y);
    if (ui::IconButton(ICON_PLUS, "Ekle")) ImGui::OpenPopup("addmenu");
    if (ImGui::BeginPopup("addmenu")) {
        if (ImGui::MenuItem(ICON_BOX "  Model…")) openModelDialog();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LAMP_CEILING "  Alan ışığı")) addLight(LightDesc::Type::Area);
        if (ImGui::MenuItem(ICON_SUN "  Güneş")) addLight(LightDesc::Type::Directional);
        if (ImGui::MenuItem(ICON_LIGHTBULB "  Nokta ışık")) addLight(LightDesc::Type::Point);
        ImGui::EndPopup();
    }

    static uint64_t renaming = 0;
    static std::string renameBuf;
    SceneNode* toDelete = nullptr;
    bool visChanged = false;

    std::function<void(SceneNode&)> drawNode = [&](SceneNode& n) {
        ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(n.uid)));
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DrawLinesToNodes;
        if (n.children.empty()) f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool selected = m_s.selKind == SelectionKind::Node && m_s.selUid == n.uid;
        if (selected) f |= ImGuiTreeNodeFlags_Selected;
        // Seçili düğümün ataları açık gelsin.
        if (m_s.selKind == SelectionKind::Node && !selected) {
            for (SceneNode* s = m_s.graph.findByUid(m_s.selUid); s; s = s->parent)
                if (s->parent == &n) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        bool open;
        if (renaming == n.uid) {
            open = ImGui::TreeNodeEx("##rn", f | ImGuiTreeNodeFlags_AllowOverlap, "%s", "");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##name", &renameBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                if (!renameBuf.empty() && renameBuf != n.name) {
                    pushUndo();
                    n.name = renameBuf;
                }
                renaming = 0;
            } else if (ImGui::IsItemDeactivated()) {
                renaming = 0;
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, n.visible ? pal.text : pal.textFaint);
            open = ImGui::TreeNodeEx("##node", f | ImGuiTreeNodeFlags_AllowOverlap, "%s  %s", nodeIcon(n), n.name.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) selectNode(n.uid);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            renaming = n.uid;
            renameBuf = n.name;
        }
        // Malzemeyi ağaçtaki düğüme bırakma.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadMaterial))
                if (const MaterialPreset* preset = m_s.materials.findById(static_cast<const char*>(p->Data)))
                    applyMaterialPreset(n.uid, *preset);
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem("ctx")) {
            selectNode(n.uid);
            if (ImGui::MenuItem(ICON_FOCUS "  Odaklan", "F")) frameSelection();
            if (ImGui::MenuItem("      Yeniden adlandır")) {
                renaming = n.uid;
                renameBuf = n.name;
            }
            if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D")) duplicateSelection();
            if (ImGui::MenuItem("      Zemine oturt", "G")) placeSelectionOnGround();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_PALETTE "  Malzemeyi kopyala", "Ctrl+C")) copyMaterial();
            if (ImGui::MenuItem("      Malzemeyi yapıştır", "Ctrl+V", false, m_s.clipboardMaterial != nullptr)) pasteMaterial();
            if (ImGui::MenuItem(n.visible ? ICON_EYE_OFF "  Gizle" : ICON_EYE "  Göster")) {
                pushUndo();
                n.visible = !n.visible;
                visChanged = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del")) toDelete = &n;
            ImGui::EndPopup();
        }
        bool vis = n.visible;
        if (eyeToggle("eye", vis)) {
            pushUndo();
            n.visible = vis;
            visChanged = true;
        }
        if (open && !n.children.empty()) {
            for (auto& c : n.children) drawNode(*c);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None);
    if (m_s.graph.empty()) ui::Hint("Sahne boş. Bir model içe aktarın ya da örnek sahneyi açın.");
    for (auto& c : m_s.graph.root()->children) drawNode(*c);

    // Işıklar
    if (!m_s.lights.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Işıklar");
        for (int i = 0; i < static_cast<int>(m_s.lights.size()); ++i) {
            LightDesc& l = m_s.lights[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const bool sel = m_s.selKind == SelectionKind::Light && m_s.selLight == i;
            ImGui::PushStyleColor(ImGuiCol_Text, l.enabled ? pal.text : pal.textFaint);
            char label[160];
            std::snprintf(label, sizeof(label), "%s  %s", lightIcon(l.type), l.name.c_str());
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowOverlap)) selectLight(i);
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("lctx")) {
                selectLight(i);
                if (ImGui::MenuItem(ICON_TRASH_2 "  Sil")) deleteSelection();
                ImGui::EndPopup();
            }
            bool en = l.enabled;
            if (eyeToggle("len", en)) {
                pushUndo();
                l.enabled = en;
                visChanged = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Ortam");
    {
        const bool sel = m_s.rightTab == 2 && m_s.selKind != SelectionKind::Node && m_s.selKind != SelectionKind::Light;
        std::string envName = m_s.environment.hdrPath.empty() ? "Prosedürel gökyüzü" : ui::fileName(m_s.environment.hdrPath);
        if (ImGui::Selectable((std::string(ICON_GLOBE "  ") + envName).c_str(), sel)) {
            clearSelection();
            m_s.rightTab = 2;
        }
        if (ImGui::Selectable(ICON_SQUARE_DASHED "  Zemin", m_s.selKind == SelectionKind::Ground)) selectNode(~0ull);
        bool g = m_s.environment.groundEnabled;
        if (eyeToggle("ground", g)) {
            pushUndo();
            m_s.environment.groundEnabled = g;
            visChanged = true;
        }
    }
    ImGui::EndChild();

    if (toDelete) {
        selectNode(toDelete->uid);
        deleteSelection();
    }
    if (visChanged) markDocumentChanged();
    ImGui::End();
}

} // namespace photon
