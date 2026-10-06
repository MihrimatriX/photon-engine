// ui_scene.cpp — Sağ üst panel: sahne ağacı (modeller, gruplar, parçalar), ışıklar
// ve ortam/zemin girdileri.
//
// Etkileşimler:
//   tık / Ctrl+tık / Shift+tık   tek seçim / seçime ekle-çıkar / aralık seç
//   ↑ ↓ (Shift ile)              satırlar arasında gezin (aralığı genişlet)
//   sürükle-bırak                gruba taşı (gruba bırak), yanına taşı (parçaya bırak),
//                                en üst seviyeye taşı (listenin altındaki alana bırak)
//   çift tık / F2                yeniden adlandır (Enter ya da başka yere tıklamak kaydeder, Esc iptal)
//   göz ikonu                    görünürlük; gizli bir grubun çocukları soluk yazılır
//   arama kutusu                 ada göre süz (Türkçe harflerde büyük/küçük ayrımı yok)
// Ağacı değiştiren işlemler (sil, taşı, grupla…) çizim sırasında değil, ağaç
// çizildikten SONRA uygulanır: dolaşılan çocuk listesi çizim ortasında değişirse
// yineleyiciler geçersiz kalırdı.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/drag_drop.h"

#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cstring>
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

// Arama için harf katlama: ASCII küçük harf; Türkçede I/İ/ı hepsi "i" sayılır
// (kullanıcı "ışık" yazsa da "Işık" bulunsun), Ş Ğ Ü Ö Ç küçük karşılıklarına iner.
// UTF-8 iki baytlık dizileri doğrudan eşlenir (C4 B0 = İ, C4 B1 = ı, ...).
std::string foldTr(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            o += c == 'I' ? 'i' : static_cast<char>(std::tolower(c));
            continue;
        }
        if (i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xC4 && (d == 0xB0 || d == 0xB1)) { o += 'i'; ++i; continue; }        // İ ı
            if ((c == 0xC5 || c == 0xC4) && d == 0x9E) { o += static_cast<char>(c); o += '\x9F'; ++i; continue; } // Ş Ğ
            if (c == 0xC3 && (d == 0x9C || d == 0x96 || d == 0x87)) {                       // Ü Ö Ç
                o += static_cast<char>(c);
                o += static_cast<char>(d + 0x20);
                ++i;
                continue;
            }
        }
        o += static_cast<char>(c);
    }
    return o;
}

bool subtreeMatches(const SceneNode& n, const std::string& q) {
    if (foldTr(n.name).find(q) != std::string::npos) return true;
    for (const auto& c : n.children)
        if (subtreeMatches(*c, q)) return true;
    return false;
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
    ui::Tooltip(visible ? "Gizle (H)" : "Göster (H)");
    return clicked;
}

size_t indexInParent(const SceneNode& n) {
    const auto& sib = n.parent->children;
    for (size_t i = 0; i < sib.size(); ++i)
        if (sib[i].get() == &n) return i;
    return sib.size();
}

} // namespace

void Application::drawSceneTree() {
    if (!ImGui::Begin("Sahne###Scene")) {
        ImGui::End();
        return;
    }
    const ui::Palette& pal = ui::palette();
    ImGuiIO& io = ImGui::GetIO();

    char subtitle[64]; // en üst seviye nesneler (modeller ve gruplar), parçalar sayılmaz
    std::snprintf(subtitle, sizeof(subtitle), "%zu nesne · %zu ışık", m_s.graph.root()->children.size(), m_s.lights.size());
    ui::PanelHeader(ICON_LIST_TREE, "Sahne", subtitle);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.y);
    if (ui::IconButton(ICON_PLUS, "Ekle")) ImGui::OpenPopup("addmenu");
    if (ImGui::BeginPopup("addmenu")) {
        if (ImGui::MenuItem(ICON_BOX "  Model…")) openModelDialog();
        if (ImGui::BeginMenu(ICON_SHAPES "  Şekil")) {
            for (int k = 0; k < kPrimitiveCount; ++k)
                if (ImGui::MenuItem(primitiveName(static_cast<PrimitiveKind>(k)))) addPrimitive(static_cast<PrimitiveKind>(k));
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_LAMP_CEILING "  Alan ışığı")) addLight(LightDesc::Type::Area);
        if (ImGui::MenuItem(ICON_SUN "  Güneş")) addLight(LightDesc::Type::Directional);
        if (ImGui::MenuItem(ICON_LIGHTBULB "  Nokta ışık")) addLight(LightDesc::Type::Point);
        ImGui::EndPopup();
    }

    ImGui::SetNextItemWidth(m_s.sceneFilter.empty() ? -FLT_MIN : -ImGui::GetFrameHeight() - 4.0f);
    ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Ada göre süz…", &m_s.sceneFilter);
    if (!m_s.sceneFilter.empty()) {
        ImGui::SameLine(0, 4);
        if (ui::IconButton(ICON_X, "Süzgeci temizle")) m_s.sceneFilter.clear();
    }
    const std::string q = foldTr(m_s.sceneFilter);

    // Ad yazılırken başka bir satıra tıklandıysa: yazılan ad kaydedilir (dosya
    // gezginlerindeki gibi). Tıklama, ad kutusunun "bırakıldı" karesinden önce seçimi
    // değiştirdiği için bu durum kutunun kendi kontrolüne hiç ulaşmaz.
    auto commitName = [&](std::string& name) {
        if (!m_s.renameBuf.empty() && m_s.renameBuf != name) {
            pushUndo();
            name = m_s.renameBuf;
        }
    };
    if (m_s.renamingUid && !isNodeSelected(m_s.renamingUid)) {
        if (SceneNode* r = m_s.graph.findByUid(m_s.renamingUid)) commitName(r->name);
        m_s.renamingUid = 0;
    }
    if (m_s.renamingLight >= 0 && !(m_s.selKind == SelectionKind::Light && m_s.selLight == m_s.renamingLight)) {
        if (m_s.renamingLight < static_cast<int>(m_s.lights.size()))
            commitName(m_s.lights[static_cast<size_t>(m_s.renamingLight)].name);
        m_s.renamingLight = -1;
    }

    // Seçim değiştiği karede birincil seçimin ataları açılır ve satır görünür kaydırılır;
    // sonraki karelerde kullanıcı o dalları serbestçe kapatabilir.
    static uint64_t revealedUid = 0;
    const bool reveal = m_s.selKind == SelectionKind::Node && m_s.selUid != revealedUid;
    SceneNode* primary = reveal ? m_s.graph.findByUid(m_s.selUid) : nullptr;

    std::function<void()> action; // ağaç çizildikten sonra uygulanacak yapısal değişiklik
    std::vector<uint64_t> order;
    bool visChanged = false;

    // Süzgeç: adı eşleşen düğüm tüm alt ağacıyla görünür (inside = eşleşen bir atanın içinde);
    // eşleşme yalnız torunlardaysa düğüm o torunlara giden yol olarak açılır.
    std::function<void(SceneNode&, bool, bool)> drawNode = [&](SceneNode& n, bool parentVisible, bool inside) {
        const bool selfMatch = !q.empty() && foldTr(n.name).find(q) != std::string::npos;
        if (!q.empty() && !inside && !selfMatch && !subtreeMatches(n, q)) return;
        ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(n.uid)));
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_AllowOverlap;
        if (n.children.empty()) f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool selected = isNodeSelected(n.uid);
        if (selected) f |= ImGuiTreeNodeFlags_Selected;
        if (primary && SceneGraph::isAncestor(n, *primary)) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (!q.empty() && !inside && !selfMatch && !n.children.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool shown = parentVisible && n.visible;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        bool open;
        if (m_s.renamingUid == n.uid) {
            open = ImGui::TreeNodeEx("##rn", f, "%s", nodeIcon(n));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6);
            if (m_s.renameFocus) {
                ImGui::SetKeyboardFocusHere();
                m_s.renameFocus = false;
            }
            ImGui::InputText("##name", &m_s.renameBuf, ImGuiInputTextFlags_AutoSelectAll);
            // Enter ya da başka yere tıklamak kaydeder; Esc metni geri alır (ad değişmez).
            if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsItemDeactivatedAfterEdit() && !m_s.renameBuf.empty() && m_s.renameBuf != n.name) {
                    pushUndo();
                    n.name = m_s.renameBuf;
                }
                m_s.renamingUid = 0;
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, shown ? pal.text : pal.textFaint);
            open = ImGui::TreeNodeEx("##node", f, "%s  %s", nodeIcon(n), n.name.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        order.push_back(n.uid);
        if (primary == &n && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);

        // Seçim. Çoklu seçimdeki bir satıra basmak seçimi hemen bozmaz: kullanıcı
        // hepsini sürükleyecek olabilir. Sürüklemeden bırakırsa tek seçime iner.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            if (io.KeyCtrl) toggleNodeSelection(n.uid);
            else if (io.KeyShift) selectNodeRange(n.uid);
            else if (selected && m_s.selNodes.size() > 1) m_treePendingUid = n.uid;
            else selectNode(n.uid);
        }
        if (m_treePendingUid == n.uid && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (io.MouseDragMaxDistanceSqr[0] < io.MouseDragThreshold * io.MouseDragThreshold) selectNode(n.uid);
            m_treePendingUid = 0;
        }
        if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) m_s.hoverUid = n.uid; // viewport'ta vurgula
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !io.KeyCtrl && !io.KeyShift) {
            selectNode(n.uid);
            beginRename();
        }

        // Sürükleme kaynağı: satır seçimin parçasıysa tüm seçim taşınır.
        if (ImGui::BeginDragDropSource()) {
            const uint64_t uid = n.uid;
            ImGui::SetDragDropPayload(kPayloadNode, &uid, sizeof(uid));
            if (isNodeSelected(uid) && m_s.selNodes.size() > 1) ImGui::Text(ICON_LAYERS "  %zu nesne", m_s.selNodes.size());
            else ImGui::Text("%s  %s", nodeIcon(n), n.name.c_str());
            ImGui::EndDragDropSource();
        }
        // Bırakma hedefi: malzeme → bu düğüme ata; düğüm → gruba ya da yanına taşı.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadMaterial))
                if (const MaterialPreset* preset = m_s.materials.findById(static_cast<const char*>(p->Data)))
                    applyMaterialPreset(n.uid, *preset);
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadNode)) {
                uint64_t src = 0;
                std::memcpy(&src, p->Data, sizeof(src));
                std::vector<uint64_t> uids = isNodeSelected(src) ? m_s.selNodes : std::vector<uint64_t>{src};
                if (n.type == SceneNodeType::Group) {
                    const uint64_t target = n.uid;
                    action = [this, uids, target] { moveNodes(uids, target, SIZE_MAX); };
                } else {
                    const uint64_t parent = n.parent == m_s.graph.root() ? 0 : n.parent->uid;
                    const size_t at = indexInParent(n) + 1;
                    action = [this, uids, parent, at] { moveNodes(uids, parent, at); };
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("ctx")) {
            if (!isNodeSelected(n.uid)) selectNode(n.uid);
            const size_t selCount = m_s.selNodes.size();
            if (ImGui::MenuItem(ICON_FOCUS "  Odaklan", "F")) frameSelection();
            if (ImGui::MenuItem(ICON_PENCIL "  Yeniden adlandır", "F2", false, selCount == 1)) beginRename();
            if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D")) action = [this] { duplicateSelection(); };
            if (ImGui::MenuItem("      Zemine oturt", "G")) placeSelectionOnGround();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_GROUP "  Grupla", "Ctrl+G")) action = [this] { groupSelection(); };
            if (ImGui::MenuItem(ICON_UNGROUP "  Grubu çöz", "Ctrl+Shift+G", false,
                                n.type == SceneNodeType::Group && !n.children.empty()))
                action = [this] { ungroupSelection(); };
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_PALETTE "  Malzemeyi kopyala", "Ctrl+C")) copyMaterial();
            if (ImGui::MenuItem("      Malzemeyi yapıştır", "Ctrl+V", false, m_s.clipboardMaterial != nullptr)) pasteMaterial();
            ImGui::Separator();
            if (ImGui::MenuItem(n.visible ? ICON_EYE_OFF "  Gizle" : ICON_EYE "  Göster", "H")) toggleSelectionVisibility();
            if (ImGui::MenuItem(ICON_SCAN_EYE "  Yalnız bunu göster", "I")) isolateSelection();
            if (ImGui::MenuItem("      Hepsini göster", "Alt+H")) showAllNodes();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del")) action = [this] { deleteSelection(); };
            ImGui::EndPopup();
        }
        bool vis = n.visible;
        if (eyeToggle("eye", vis)) {
            pushUndo();
            n.visible = vis;
            visChanged = true;
        }
        if (open && !n.children.empty()) {
            for (auto& c : n.children) drawNode(*c, shown, inside || selfMatch);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    ImGui::BeginChild("tree", ImVec2(0, 0), ImGuiChildFlags_None);
    if (m_s.graph.empty()) ui::Hint("Sahne boş. Bir model içe aktarın ya da örnek sahneyi açın.");
    for (auto& c : m_s.graph.root()->children) drawNode(*c, true, false);
    if (!q.empty() && order.empty() && !m_s.graph.empty()) ui::Hint("Eşleşen nesne yok.");
    if (primary) revealedUid = m_s.selUid;

    // Bir düğüm sürüklenirken: en üst seviyeye bırakma alanı.
    if (const ImGuiPayload* drag = ImGui::GetDragDropPayload(); drag && drag->IsDataType(kPayloadNode)) {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 sz(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + 4.0f);
        ImGui::InvisibleButton("##rootdrop", sz);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ui::col(pal.textFaint), 6.0f);
        const char* msg = ICON_FOLDER_INPUT "  En üst seviyeye taşı";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (sz.x - ts.x) * 0.5f, p0.y + (sz.y - ts.y) * 0.5f), ui::col(pal.textDim), msg);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadNode)) {
                uint64_t src = 0;
                std::memcpy(&src, p->Data, sizeof(src));
                std::vector<uint64_t> uids = isNodeSelected(src) ? m_s.selNodes : std::vector<uint64_t>{src};
                action = [this, uids] { moveNodes(uids, 0, SIZE_MAX); };
            }
            ImGui::EndDragDropTarget();
        }
    }

    // ── Işıklar ──
    const bool anyLight = std::any_of(m_s.lights.begin(), m_s.lights.end(), [&](const LightDesc& l) {
        return q.empty() || foldTr(l.name).find(q) != std::string::npos;
    });
    if (anyLight) {
        ImGui::Spacing();
        ImGui::SeparatorText("Işıklar");
        for (int i = 0; i < static_cast<int>(m_s.lights.size()); ++i) {
            LightDesc& l = m_s.lights[static_cast<size_t>(i)];
            if (!q.empty() && foldTr(l.name).find(q) == std::string::npos) continue;
            ImGui::PushID(i);
            const bool sel = m_s.selKind == SelectionKind::Light && m_s.selLight == i;
            if (m_s.renamingLight == i) {
                ImGui::TextUnformatted(lightIcon(l.type));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6);
                if (m_s.renameFocus) {
                    ImGui::SetKeyboardFocusHere();
                    m_s.renameFocus = false;
                }
                ImGui::InputText("##lname", &m_s.renameBuf, ImGuiInputTextFlags_AutoSelectAll);
                if (ImGui::IsItemDeactivated()) {
                    if (ImGui::IsItemDeactivatedAfterEdit() && !m_s.renameBuf.empty() && m_s.renameBuf != l.name) {
                        pushUndo();
                        l.name = m_s.renameBuf;
                    }
                    m_s.renamingLight = -1;
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, l.enabled ? pal.text : pal.textFaint);
                char label[160];
                std::snprintf(label, sizeof(label), "%s  %s", lightIcon(l.type), l.name.c_str());
                if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectLight(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginRename();
                }
                ImGui::PopStyleColor();
            }
            if (ImGui::BeginPopupContextItem("lctx")) {
                selectLight(i);
                if (ImGui::MenuItem(ICON_FOCUS "  Odaklan", "F")) frameSelection();
                if (ImGui::MenuItem(ICON_PENCIL "  Yeniden adlandır", "F2")) beginRename();
                if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D")) action = [this] { duplicateSelection(); };
                if (ImGui::MenuItem(l.enabled ? ICON_EYE_OFF "  Kapat" : ICON_EYE "  Aç", "H")) toggleSelectionVisibility();
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del")) action = [this] { deleteSelection(); };
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
        if (ImGui::Selectable(ICON_SQUARE_DASHED "  Zemin", m_s.selKind == SelectionKind::Ground)) selectNode(kGroundNodeUid);
        bool g = m_s.environment.groundEnabled;
        if (eyeToggle("ground", g)) {
            pushUndo();
            m_s.environment.groundEnabled = g;
            visChanged = true;
        }
    }

    // ↑/↓: panel odaktayken birincil seçimi görünen satırlar arasında gezdirir.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !order.empty() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        const int dir = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
        if (dir) {
            auto it = std::find(order.begin(), order.end(), m_s.selKind == SelectionKind::Node ? m_s.selUid : 0);
            int idx = it == order.end() ? (dir > 0 ? 0 : static_cast<int>(order.size()) - 1)
                                        : std::clamp(static_cast<int>(it - order.begin()) + dir, 0, static_cast<int>(order.size()) - 1);
            const uint64_t uid = order[static_cast<size_t>(idx)];
            if (io.KeyShift) selectNodeRange(uid);
            else selectNode(uid);
            revealedUid = 0; // yeni satırı görünür kaydır
        }
    }
    ImGui::EndChild();
    m_s.treeOrder = std::move(order);

    if (action) action();
    if (visChanged) markDocumentChanged();
    ImGui::End();
}

} // namespace photon
