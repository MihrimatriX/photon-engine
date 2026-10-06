// ui_properties.cpp — Sağ alt panel: seçime ve sahneye ait tüm ayarlar.
//
// Sekmeler: Nesne (dönüşüm), Malzeme, Ortam (HDRI, arka plan, zemin), Işıklar,
// Kamera (lens, alan derinliği) ve Görüntü (ton eşleme, pozlama, önizleme).
//
// Düzenleme kuralları:
//   * Malzeme parametreleri render thread'inin okuduğu nesnede YERİNDE değişir:
//     editLive() mevcut pass'i iptal edip sahne kilidini alır (bkz. RenderController).
//   * Işık/ortam/dönüşüm değişiklikleri belgeyi değiştirir; sahne yeniden derlenir.
//   * Geri al: sürekli düzenlemeler (kaydırıcı sürükleme) tek adımda birleşir.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/drag_drop.h"
#include "ui/file_dialog.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "camera/orthographic_camera.h"
#include "engine/denoiser.h"
#include "core/math/constants.h"

#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace photon {

namespace {

// Aynı kontrol üzerindeki ardışık değişiklikler tek geri al adımıdır: anahtar
// değişince ya da 0.8 sn boyunca değişiklik olmayınca yeni anlık görüntü alınır.
// Anlık görüntü DEĞİŞİKLİK UYGULANMADAN önce alınmalıdır.
void undoPoint(Application& app, ImGuiID key) {
    static ImGuiID lastKey = 0;
    static double lastTime = -10.0;
    const double now = ImGui::GetTime();
    if (key != lastKey || now - lastTime > 0.8) app.pushUndo();
    lastKey = key;
    lastTime = now;
}

bool colorEdit(const char* id, Color3f& c) {
    float v[3] = {c.r, c.g, c.b};
    const bool changed = ImGui::ColorEdit3(id, v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel |
                                                      ImGuiColorEditFlags_NoInputs);
    if (changed) c = Color3f(v[0], v[1], v[2]);
    return changed;
}

void collectMaterialNodes(SceneNode& n, std::vector<SceneNode*>& out) {
    if (n.material) out.push_back(&n);
    for (auto& c : n.children) collectMaterialNodes(*c, out);
}

int countUsers(const SceneNode& root, const Material* m) {
    int count = 0;
    std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
        if (n.material.get() == m) ++count;
        for (const auto& c : n.children) walk(*c);
    };
    walk(root);
    return count;
}

void replaceMaterial(SceneNode& root, const Material* oldMat, const std::shared_ptr<Material>& newMat) {
    std::function<void(SceneNode&)> walk = [&](SceneNode& n) {
        if (n.material.get() == oldMat) n.material = newMat;
        for (auto& c : n.children) walk(*c);
    };
    walk(root);
}

const char* kTabNames[] = {"Nesne", "Malzeme", "Ortam", "Işıklar", "Kamera", "Görüntü"};
const char* kTabIcons[] = {ICON_BOX, ICON_PALETTE, ICON_GLOBE, ICON_LAMP_CEILING, ICON_CAMERA, ICON_SLIDERS_HORIZONTAL};

} // namespace

void Application::drawProperties() {
    if (!ImGui::Begin("Özellikler###Props")) {
        ImGui::End();
        return;
    }
    const ui::Palette& pal = ui::palette();
    const int n = 6;
    const float w = ImGui::GetContentRegionAvail().x;
    const float bw = (w - 3.0f * static_cast<float>(n - 1)) / static_cast<float>(n);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 7));
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0, 3);
        const bool active = m_s.rightTab == i;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? pal.bg3 : pal.bg1);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? pal.accent : pal.textDim);
        ImGui::PushID(i);
        if (ImGui::Button(kTabIcons[i], ImVec2(bw, 0))) m_s.rightTab = i;
        ImGui::PopID();
        ImGui::PopStyleColor(2);
        ui::Tooltip(kTabNames[i]);
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    ui::PanelHeader(kTabIcons[m_s.rightTab], kTabNames[m_s.rightTab]);
    ImGui::BeginChild("propscontent", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (m_s.rightTab) {
        case 0: {
            // ── Nesne: ad, görünürlük, konum / dönüş / ölçek ──
            SceneNode* node = selectedNode();
            if (!node) {
                ui::Hint("Bir parça seçin: viewport'ta tıklayın ya da Sahne panelinden seçin. "
                         "Ctrl+tık seçime ekler, Shift+tık panelde aralık seçer.");
                break;
            }
            if (const std::vector<SceneNode*> sel = selectedNodes(); sel.size() > 1) {
                // Çoklu seçim: dönüşüm tutamaçla (W/E/R) hepsine birlikte uygulanır;
                // burada yalnız toplu işlemler var.
                ImGui::PushFont(ui::fonts().semibold, 0.0f);
                ImGui::Text("%zu nesne seçili", sel.size());
                ImGui::PopFont();
                for (size_t i = 0; i < sel.size() && i < 8; ++i)
                    ImGui::TextColored(pal.textDim, "  %s", sel[i]->name.c_str());
                if (sel.size() > 8) ImGui::TextColored(pal.textFaint, "  … ve %zu tane daha", sel.size() - 8);
                ImGui::Spacing();
                const float hb = (ImGui::GetContentRegionAvail().x - 4.0f) / 2.0f;
                if (ui::GhostButton(ICON_GROUP "  Grupla", ImVec2(hb, 0))) groupSelection();
                ImGui::SameLine(0, 4);
                if (ui::GhostButton(ICON_FOCUS "  Odaklan", ImVec2(hb, 0))) frameSelection();
                if (ui::GhostButton("Zemine oturt", ImVec2(hb, 0))) placeSelectionOnGround();
                ImGui::SameLine(0, 4);
                if (ui::GhostButton(ICON_EYE_OFF "  Gizle / göster", ImVec2(hb, 0))) toggleSelectionVisibility();
                if (ui::GhostButton(ICON_COPY "  Çoğalt", ImVec2(hb, 0))) duplicateSelection();
                ImGui::SameLine(0, 4);
                if (ui::GhostButton(ICON_TRASH_2 "  Sil", ImVec2(hb, 0))) deleteSelection();
                ImGui::Spacing();
                ui::Hint("Birlikte taşımak, döndürmek ya da ölçeklemek için viewport'ta W / E / R tutamaçlarını kullanın.");
                break;
            }
            if (ui::BeginProps("obj")) {
                ui::Prop("Ad");
                std::string name = node->name;
                if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty()) {
                    pushUndo();
                    node->name = name;
                }
                ui::Prop("Görünür");
                bool vis = node->visible;
                if (ui::Toggle("vis", &vis)) {
                    pushUndo();
                    node->visible = vis;
                    markDocumentChanged();
                }
                ui::EndProps();
            }
            if (ui::Section(ICON_MOVE, "Dönüşüm")) {
                ui::TRS trs = ui::decompose(node->localTransform);
                bool changed = false;
                const float speed = std::max(0.001f, m_s.camera.radius * 0.002f);
                if (ui::BeginProps("xf", 0.28f)) {
                    ui::Prop("Konum");
                    float t[3] = {trs.t.x, trs.t.y, trs.t.z};
                    if (ImGui::DragFloat3("##t", t, speed, 0, 0, "%.3f")) {
                        undoPoint(*this, ImGui::GetID("t"));
                        trs.t = Vec3f(t[0], t[1], t[2]);
                        changed = true;
                    }
                    ui::Prop("Dönüş (°)");
                    float r[3] = {trs.rDeg.x, trs.rDeg.y, trs.rDeg.z};
                    if (ImGui::DragFloat3("##r", r, 0.5f, -360, 360, "%.1f")) {
                        undoPoint(*this, ImGui::GetID("r"));
                        trs.rDeg = Vec3f(r[0], r[1], r[2]);
                        changed = true;
                    }
                    ui::Prop("Ölçek");
                    static bool uniform = true;
                    if (uniform) {
                        float s = trs.s.y;
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6);
                        if (ImGui::DragFloat("##su", &s, 0.005f, 0.001f, 1000.0f, "%.3f")) {
                            undoPoint(*this, ImGui::GetID("su"));
                            const float k = s / std::max(1e-6f, trs.s.y);
                            trs.s = Vec3f(trs.s.x * k, s, trs.s.z * k);
                            changed = true;
                        }
                    } else {
                        float s[3] = {trs.s.x, trs.s.y, trs.s.z};
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6);
                        if (ImGui::DragFloat3("##s", s, 0.005f, 0.001f, 1000.0f, "%.3f")) {
                            undoPoint(*this, ImGui::GetID("s"));
                            trs.s = Vec3f(s[0], s[1], s[2]);
                            changed = true;
                        }
                    }
                    ImGui::SameLine(0, 6);
                    if (ui::IconButton(uniform ? ICON_SQUARE : ICON_GRID_3X3, uniform ? "Orantılı ölçek (tıkla: eksen başına)" : "Eksen başına ölçek", uniform))
                        uniform = !uniform;
                    ui::EndProps();
                }
                if (changed) {
                    node->localTransform = ui::compose(trs);
                    markDocumentChanged(true);
                }
                ImGui::Spacing();
                const float hb = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
                if (ui::GhostButton("Zemine oturt", ImVec2(hb, 0))) placeSelectionOnGround();
                ImGui::SameLine(0, 4);
                if (ui::GhostButton("Odaklan", ImVec2(hb, 0))) frameSelection();
                ImGui::SameLine(0, 4);
                if (ui::GhostButton("Sıfırla", ImVec2(hb, 0))) {
                    pushUndo();
                    node->localTransform = Transform{};
                    markDocumentChanged();
                }
                if (node->type == SceneNodeType::Group && !node->children.empty()) {
                    if (ui::GhostButton(ICON_UNGROUP "  Grubu çöz", ImVec2(-FLT_MIN, 0))) ungroupSelection();
                    ui::Tooltip("Parçalar grubun yerine geçer, dünyadaki yerleri değişmez (Ctrl+Shift+G)");
                }
            }
            if (ui::Section(ICON_INFO, "Bilgi", false)) {
                size_t tris = 0, parts = 0;
                std::function<void(const SceneNode&)> walk = [&](const SceneNode& s) {
                    if (s.mesh) {
                        tris += s.mesh->numTriangles();
                        ++parts;
                    }
                    for (const auto& c : s.children) walk(*c);
                };
                walk(*node);
                ImGui::TextColored(pal.textDim, "%zu parça, %zu üçgen", parts, tris);
                const AABB b = SceneGraph::nodeWorldBounds(*node);
                if (b.pMin.x <= b.pMax.x) {
                    const Vec3f d = b.pMax - b.pMin;
                    ImGui::TextColored(pal.textDim, "Boyut: %.3f × %.3f × %.3f", d.x, d.y, d.z);
                }
                if (!node->sourcePath.empty()) ImGui::TextColored(pal.textFaint, "%s", node->sourcePath.c_str());
            }
            break;
        }
        case 1: drawMaterialProps(); break;
        case 2: drawEnvironmentProps(); break;
        case 3: drawLightProps(); break;
        case 4: drawCameraProps(); break;
        default: drawImageProps(); break;
    }
    ImGui::EndChild();
    ImGui::End();
}

bool Application::textureSlot(const char* label, std::string& path) {
    const ui::Palette& pal = ui::palette();
    bool changed = false;
    ui::Prop(label);
    ImGui::PushID(label);
    const float clearW = path.empty() ? 0.0f : ImGui::GetFrameHeight() + 4.0f;
    std::string text = path.empty() ? std::string(ICON_PLUS "  Doku seç…") : std::string(ICON_IMAGE "  ") + ui::fileName(path);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, path.empty() ? pal.textFaint : pal.text);
    if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - clearW, 0))) {
        std::string p = path;
        FileDialogFilter f[] = {{"Resimler", "*.png;*.jpg;*.jpeg;*.tga;*.bmp"}};
        if (showFileDialog(p, FileDialogMode::Open, label, f, 1)) {
            path = p;
            changed = true;
        }
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!path.empty()) ui::Tooltip(path.c_str());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            path = static_cast<const char*>(p->Data);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    if (!path.empty()) {
        ImGui::SameLine(0, 4);
        if (ui::IconButton(ICON_X, "Dokuyu kaldır")) {
            path.clear();
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

void Application::drawMaterialProps() {
    const ui::Palette& pal = ui::palette();
    if (m_s.selKind == SelectionKind::Ground) {
        ui::Hint("Zemin malzemesi Ortam sekmesinden ayarlanır.");
        if (ui::GhostButton("Ortam sekmesine git")) m_s.rightTab = 2;
        return;
    }
    SceneNode* node = selectedNode();
    if (!node) {
        ui::Hint("Malzemesini düzenlemek için bir parça seçin. Kütüphaneden bir malzemeyi parçanın üzerine "
                 "sürükleyerek de atayabilirsiniz.");
        return;
    }
    std::vector<SceneNode*> holders;
    collectMaterialNodes(*node, holders);
    if (holders.empty()) {
        ui::Hint("Bu düğümde malzemeli parça yok.");
        return;
    }
    std::shared_ptr<Material> mat = holders.front()->material;
    const int users = countUsers(*m_s.graph.root(), mat.get());

    // Önizleme küresi: malzeme değişince arka planda yeniden render edilir.
    {
        const std::string key = "sel:" + std::to_string(reinterpret_cast<uintptr_t>(mat.get()));
        MaterialPreset preview = MaterialLibrary::presetFromMaterial(*mat, "önizleme");
        static std::string lastSig;
        static std::string lastKey;
        char sig[256];
        std::snprintf(sig, sizeof(sig), "%d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
                      static_cast<int>(preview.kind), preview.baseColor.r, preview.baseColor.g, preview.baseColor.b,
                      preview.metallic, preview.roughness, preview.specular, preview.clearCoat, preview.anisotropy,
                      preview.sheen, preview.emissive, preview.ior, preview.diffuseTransmission);
        if (key != lastKey || sig != lastSig) {
            m_s.thumbs.requestMaterial(key, preview, true);
            lastKey = key;
            lastSig = sig;
        }
        const float sz = 92.0f;
        const unsigned int tex = m_s.thumbs.texture(key);
        ui::ThumbCard("prev", tex, nullptr, ImVec2(sz, sz), false,
                      ImVec4(preview.baseColor.r, preview.baseColor.g, preview.baseColor.b, 1));
        ImGui::SameLine(0, 12);
        ImGui::BeginGroup();
        ImGui::PushFont(ui::fonts().semibold, 0.0f);
        ImGui::TextUnformatted(node->name.c_str());
        ImGui::PopFont();
        const bool glass = dynamic_cast<Dielectric*>(mat.get()) != nullptr;
        ImGui::TextColored(pal.textDim, "%s", glass ? "Cam (kırılma)" : "Principled (genel)");
        if (users > 1) {
            ImGui::TextColored(pal.textFaint, "%d parçada ortak", users);
            ImGui::SameLine();
            // Paylaşımı kopar: bu düğümün parçaları malzemenin kendi kopyasını alır.
            if (ImGui::SmallButton("Bağımsız yap")) {
                pushUndo();
                auto own = cloneMaterial(*mat);
                for (SceneNode* h : holders)
                    if (h->material == mat) h->material = own;
                markDocumentChanged();
                setStatus("Malzeme bu nesneye özel yapıldı");
            }
            ui::Tooltip("Kopyalar ve aynı malzemeyi paylaşan parçalar birlikte değişir; bu nesneyi ayırır.");
        }
        if (ui::GhostButton(ICON_SAVE "  Kütüphaneye kaydet")) ImGui::OpenPopup("savemat");
        if (ImGui::BeginPopup("savemat")) {
            static std::string name;
            if (ImGui::IsWindowAppearing()) name = node->name;
            ImGui::TextUnformatted("Malzeme adı");
            ImGui::SetNextItemWidth(220);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##mname", &name, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ui::PrimaryButton("Kaydet") || enter) && !name.empty()) {
                saveMaterialToLibrary(name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
    }
    ImGui::Spacing();

    // Tür değişimi: yeni bir malzeme nesnesi oluşturulur ve bu malzemeyi kullanan
    // tüm parçalara atanır.
    {
        int kind = dynamic_cast<Dielectric*>(mat.get()) ? 1 : 0;
        const char* kinds[] = {"Principled", "Cam"};
        if (ui::Segmented("kind", &kind, kinds, 2)) {
            pushUndo();
            MaterialPreset p = MaterialLibrary::presetFromMaterial(*mat, "x");
            p.kind = kind == 1 ? MaterialKind::Glass : MaterialKind::Generic;
            if (kind == 1) {
                p.baseColor = Color3f(1.0f);
                p.roughness = 0.0f;
            } else {
                p.roughness = 0.4f;
            }
            auto fresh = MaterialLibrary::createMaterial(p);
            replaceMaterial(*m_s.graph.root(), mat.get(), fresh);
            markDocumentChanged();
            return;
        }
    }
    ImGui::Spacing();

    if (auto g = std::dynamic_pointer_cast<Dielectric>(mat)) {
        // Dielectric değişmez alanlı; düzenleme = yeni nesne + değiştir (yerinde set yok).
        float ior = g->ior(), rough = g->roughness();
        Color3f tint = g->tint();
        bool changed = false;
        if (ui::BeginProps("glass")) {
            ui::Prop("Renk (geçirgenlik)");
            if (colorEdit("##tint", tint)) { undoPoint(*this, ImGui::GetID("tint")); changed = true; }
            ui::Prop("Kırılma indisi", "Işığın camda ne kadar büküleceği. Su 1.33, cam 1.5, elmas 2.42");
            if (ui::SliderF("##ior", &ior, 1.0f, 2.6f, "%.3f")) { undoPoint(*this, ImGui::GetID("ior")); changed = true; }
            ui::Prop("Pürüzlülük", "0 = berrak cam, yükseldikçe buzlu cam");
            if (ui::SliderF("##gr", &rough, 0.0f, 1.0f, "%.3f")) { undoPoint(*this, ImGui::GetID("gr")); changed = true; }
            ui::EndProps();
        }
        ImGui::Spacing();
        ImGui::TextColored(pal.textFaint, "Hazır:");
        struct IorPreset { const char* n; float v; };
        const IorPreset presets[] = {{"Su", 1.333f}, {"Cam", 1.5f}, {"Kristal", 1.6f}, {"Safir", 1.77f}, {"Elmas", 2.42f}};
        for (const auto& p : presets) {
            ImGui::SameLine();
            if (ui::GhostButton(p.n)) {
                pushUndo();
                ior = p.v;
                changed = true;
            }
        }
        if (changed) {
            auto fresh = std::make_shared<Dielectric>(ior, tint, rough <= 0.005f ? 0.0f : rough);
            replaceMaterial(*m_s.graph.root(), mat.get(), fresh);
            markDocumentChanged(true);
        }
        return;
    }

    auto d = std::dynamic_pointer_cast<DisneyMaterial>(mat);
    if (!d) {
        ui::Hint("Bu malzeme türü düzenlenemiyor (Lambert/ayna). Türü değiştirerek Principled'a çevirebilirsiniz.");
        return;
    }

    // Kaydırıcı yardımcıları: değişiklik önce geri al noktası, sonra kilit altında uygulanır.
    auto slider = [&](const char* label, const char* id, float value, float lo, float hi,
                      const std::function<void(float)>& set, const char* tip = nullptr, const char* fmt = "%.2f") {
        ui::Prop(label, tip);
        if (ui::SliderF(id, &value, lo, hi, fmt)) {
            undoPoint(*this, ImGui::GetID(id));
            editLive([&] { set(value); });
        }
    };

    if (ui::Section(ICON_PALETTE, "Temel")) {
        if (ui::BeginProps("base")) {
            ui::Prop("Taban rengi");
            Color3f bc = d->baseColor();
            if (colorEdit("##bc", bc)) {
                undoPoint(*this, ImGui::GetID("bc"));
                editLive([&] { d->setBaseColor(bc); });
            }
            slider("Metalik", "##met", d->metallic(), 0, 1, [&](float v) { d->setMetallic(v); },
                   "0 = yalıtkan (plastik, boya), 1 = metal");
            slider("Pürüzlülük", "##rough", d->roughness(), 0, 1, [&](float v) { d->setRoughness(v); },
                   "0 = ayna gibi parlak, 1 = tamamen mat");
            slider("Yansıma", "##spec", d->specular(), 0, 1, [&](float v) { d->setSpecular(v); },
                   "Yalıtkanlarda dik açıdaki yansıma gücü (0.5 ≈ %4)");
            ui::EndProps();
        }
    }
    if (ui::Section(ICON_SPARKLES, "Kaplama (vernik)", d->clearCoat() > 0.0f)) {
        if (ui::BeginProps("coat")) {
            slider("Kaplama", "##cc", d->clearCoat(), 0, 1, [&](float v) { d->setClearCoat(v); },
                   "Araç boyası, lake: taban üzerinde ince şeffaf parlak katman");
            slider("Kaplama pürüzlülüğü", "##ccr", d->clearCoatRoughness(), 0, 1,
                   [&](float v) { d->setClearCoatRoughness(v); });
            ui::EndProps();
        }
    }
    if (ui::Section(ICON_BLEND, "Gelişmiş", d->anisotropy() > 0 || d->sheen() > 0 || d->diffuseTransmission() > 0)) {
        if (ui::BeginProps("adv")) {
            slider("Anizotropi", "##aniso", d->anisotropy(), 0, 1, [&](float v) { d->setAnisotropy(v); },
                   "Fırçalanmış metal gibi yönlü parlama");
            slider("Kumaş parlaklığı", "##sheen", d->sheen(), 0, 1, [&](float v) { d->setSheen(v); },
                   "Kadife/kumaş kenarlarındaki yumuşak parlama (sheen)");
            slider("Işık geçirgenliği", "##dt", d->diffuseTransmission(), 0, 1,
                   [&](float v) { d->setDiffuseTransmission(v); }, "İnce yüzeylerde arkadan gelen ışık (silikon, kağıt)");
            ui::EndProps();
        }
    }
    {
        SurfaceInteraction si;
        const Color3f e = d->emitted(si);
        const float strength = std::max(e.r, std::max(e.g, e.b));
        if (ui::Section(ICON_LIGHTBULB, "Işık yayma", strength > 0.0f)) {
            Color3f ec = strength > 0.0f ? e / strength : Color3f(1.0f);
            float s = strength;
            bool changed = false;
            if (ui::BeginProps("emit")) {
                ui::Prop("Renk");
                if (colorEdit("##ec", ec)) { undoPoint(*this, ImGui::GetID("ec")); changed = true; }
                ui::Prop("Güç", "0 = kapalı. Sahnede ışık kaynağı olarak da örneklenir.");
                if (ui::SliderF("##es", &s, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
                    undoPoint(*this, ImGui::GetID("es"));
                    changed = true;
                }
                ui::EndProps();
            }
            if (changed) {
                // Işık yayan mesh'ler derleme sırasında MeshLight olarak kaydedilir: yeniden derle.
                editLive([&] { d->setEmission(ec * s); });
                markDocumentChanged(true);
            }
        }
    }
    if (ui::Section(ICON_IMAGE, "Dokular", !d->albedoMap().empty() || !d->normalMap().empty())) {
        if (ui::BeginProps("tex", 0.34f)) {
            std::string a = d->albedoMap(), nm = d->normalMap(), r = d->roughnessMap(), m = d->metalnessMap();
            if (textureSlot("Taban rengi", a)) { pushUndo(); editLive([&] { d->setAlbedoMap(a); }); }
            if (textureSlot("Normal", nm)) { pushUndo(); editLive([&] { d->setNormalMap(nm); }); }
            if (textureSlot("Pürüzlülük", r)) { pushUndo(); editLive([&] { d->setRoughnessMap(r); }); }
            if (textureSlot("Metalik", m)) { pushUndo(); editLive([&] { d->setMetalnessMap(m); }); }
            ui::EndProps();
        }
        int mapping = d->textureMapping() == DisneyMaterial::TextureMapping::Box ? 1 : 0;
        const char* maps[] = {"UV", "Kutu (UV'siz modeller)"};
        if (ui::Segmented("mapping", &mapping, maps, 2)) {
            pushUndo();
            editLive([&] { d->setTextureMapping(mapping == 1 ? DisneyMaterial::TextureMapping::Box : DisneyMaterial::TextureMapping::UV); });
        }
        ImGui::Spacing();
        if (ui::BeginProps("texscale", 0.34f)) {
            ui::Prop("Tekrar", "UV: doku kaç kez tekrarlansın. Kutu: 1 birimde kaç tekrar.");
            float s = d->textureScale();
            if (ui::SliderF("##ts", &s, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
                undoPoint(*this, ImGui::GetID("ts"));
                editLive([&] { d->setTextureScale(s); });
            }
            ui::EndProps();
        }
        ui::Hint("Dokular modelin UV koordinatlarıyla eşlenir. Normal/pürüzlülük haritaları doğrusal okunur.");
    }
}

void Application::drawEnvironmentProps() {
    const ui::Palette& pal = ui::palette();
    EnvironmentDesc& e = m_s.environment;
    bool changed = false;
    bool interactive = false;

    // Etkin HDRI'ın önizlemesi.
    {
        std::string key, name = "Prosedürel gökyüzü";
        for (const auto& a : m_s.environments) {
            if ((!e.hdrPath.empty() && a.path == e.hdrPath) ||
                (e.hdrPath.empty() && a.path.empty() && a.zenith.r == e.zenith.r && a.horizon.r == e.horizon.r)) {
                key = "env:" + a.id;
                name = a.name;
            }
        }
        if (key.empty() && !e.hdrPath.empty()) name = ui::fileName(e.hdrPath);
        const float wImg = ImGui::GetContentRegionAvail().x;
        ui::ThumbCard("envprev", key.empty() ? 0 : m_s.thumbs.texture(key), name.c_str(), ImVec2(wImg, wImg * 0.42f),
                      false, ImVec4(e.horizon.r, e.horizon.g, e.horizon.b, 1));
        if (ImGui::IsItemClicked()) m_s.leftTab = 1;
        ui::Tooltip("Ortamı değiştirmek için Kütüphane → Ortam");
    }
    ImGui::Spacing();
    if (ui::Section(ICON_GLOBE, "Aydınlatma")) {
        if (ui::BeginProps("env")) {
            ui::Prop("Parlaklık", "HDRI'ın ışık katkısı (çarpan)");
            float inten = e.intensity;
            if (ui::SliderF("##ei", &inten, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
                undoPoint(*this, ImGui::GetID("ei"));
                e.intensity = inten;
                changed = interactive = true;
            }
            ui::Prop("Döndürme", "Işığın geldiği yönü değiştirir (yansımaları da kaydırır)");
            float rot = e.rotationDeg;
            if (ui::SliderF("##er", &rot, -180.0f, 180.0f, "%.0f°")) {
                undoPoint(*this, ImGui::GetID("er"));
                e.rotationDeg = rot;
                changed = interactive = true;
            }
            if (e.hdrPath.empty()) {
                ui::Prop("Gökyüzü");
                if (colorEdit("##zen", e.zenith)) { undoPoint(*this, ImGui::GetID("zen")); changed = interactive = true; }
                ui::Prop("Ufuk");
                if (colorEdit("##hor", e.horizon)) { undoPoint(*this, ImGui::GetID("hor")); changed = interactive = true; }
            }
            ui::EndProps();
        }
    }
    if (ui::Section(ICON_IMAGE, "Arka plan")) {
        int mode = static_cast<int>(e.background.mode);
        const char* modes[] = {"Ortam", "Düz renk", "Şeffaf"};
        if (ui::Segmented("bgmode", &mode, modes, 3)) {
            pushUndo();
            e.background.mode = static_cast<Background::Mode>(mode);
            changed = true;
        }
        if (e.background.mode == Background::Mode::Color) {
            if (ui::BeginProps("bg")) {
                ui::Prop("Renk");
                if (colorEdit("##bgc", e.background.color)) { undoPoint(*this, ImGui::GetID("bgc")); changed = interactive = true; }
                ui::EndProps();
            }
        }
        ui::Hint(e.background.mode == Background::Mode::Transparent
                     ? "PNG çıktısı alfa kanallı olur; yansımalar yine ortamı görür."
                     : "Yalnız kameradan görünen arka planı değiştirir; aydınlatma ortamdan gelir.");
    }
    if (ui::Section(ICON_SQUARE_DASHED, "Zemin")) {
        if (m_s.selKind == SelectionKind::Ground) {
            ImGui::TextColored(pal.accent, ICON_MOUSE_POINTER_2 "  Zemin seçili");
        }
        if (ui::BeginProps("ground")) {
            ui::Prop("Zemin");
            bool g = e.groundEnabled;
            if (ui::Toggle("ge", &g)) {
                pushUndo();
                e.groundEnabled = g;
                changed = true;
            }
            if (e.groundEnabled) {
                ui::Prop("Renk");
                if (colorEdit("##gc", e.groundColor)) { undoPoint(*this, ImGui::GetID("gc")); changed = interactive = true; }
                ui::Prop("Pürüzlülük", "Düşük değer zemine yansıma verir (parlak stüdyo zemini)");
                float r = e.groundRoughness;
                if (ui::SliderF("##gr", &r, 0.02f, 1.0f, "%.2f")) {
                    undoPoint(*this, ImGui::GetID("gr"));
                    e.groundRoughness = r;
                    changed = interactive = true;
                }
            }
            ui::EndProps();
        }
        ui::Hint("Zemin, sahnedeki modellerin altına otomatik yerleşir ve gölgeleri taşır.");
    }
    if (changed) markDocumentChanged(interactive);
}

void Application::drawLightProps() {
    const ui::Palette& pal = ui::palette();
    const float bw = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (ui::GhostButton(ICON_LAMP_CEILING " Alan", ImVec2(bw, 0))) addLight(LightDesc::Type::Area);
    ui::Tooltip("Softbox: yumuşak gölgeli dikdörtgen ışık");
    ImGui::SameLine(0, 4);
    if (ui::GhostButton(ICON_SUN " Güneş", ImVec2(bw, 0))) addLight(LightDesc::Type::Directional);
    ui::Tooltip("Sonsuz uzaktaki yönlü ışık: keskin gölgeler");
    ImGui::SameLine(0, 4);
    if (ui::GhostButton(ICON_LIGHTBULB " Nokta", ImVec2(bw, 0))) addLight(LightDesc::Type::Point);
    ImGui::Spacing();

    if (m_s.lights.empty()) {
        ui::Hint("Sahnede ışık yok; aydınlatma yalnız ortamdan (HDRI) geliyor. Stüdyo preset'leri hazır ışık "
                 "düzenleri getirir (Kütüphane → Stüdyo).");
        return;
    }
    // Işık listesi.
    for (int i = 0; i < static_cast<int>(m_s.lights.size()); ++i) {
        const LightDesc& l = m_s.lights[static_cast<size_t>(i)];
        const char* icon = l.type == LightDesc::Type::Area ? ICON_LAMP_CEILING
                         : l.type == LightDesc::Type::Directional ? ICON_SUN : ICON_LIGHTBULB;
        char label[160];
        std::snprintf(label, sizeof(label), "%s  %s##l%d", icon, l.name.c_str(), i);
        if (ImGui::Selectable(label, m_s.selKind == SelectionKind::Light && m_s.selLight == i)) selectLight(i);
    }
    ImGui::Spacing();
    if (m_s.selKind != SelectionKind::Light || m_s.selLight < 0 || m_s.selLight >= static_cast<int>(m_s.lights.size())) {
        ui::Hint("Düzenlemek için bir ışık seçin.");
        return;
    }
    LightDesc& l = m_s.lights[static_cast<size_t>(m_s.selLight)];
    bool changed = false;
    ImGui::SeparatorText(l.name.c_str());
    if (ui::BeginProps("light")) {
        ui::Prop("Ad");
        std::string name = l.name;
        if (ImGui::InputText("##ln", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty()) {
            pushUndo();
            l.name = name;
        }
        ui::Prop("Açık");
        bool en = l.enabled;
        if (ui::Toggle("len", &en)) {
            pushUndo();
            l.enabled = en;
            changed = true;
        }
        ui::Prop("Renk");
        if (colorEdit("##lc", l.color)) { undoPoint(*this, ImGui::GetID("lc")); changed = true; }
        ui::Prop("Güç");
        float inten = l.intensity;
        const float maxI = l.type == LightDesc::Type::Point ? 1e5f : 200.0f;
        if (ui::SliderF("##li", &inten, 0.0f, maxI, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            undoPoint(*this, ImGui::GetID("li"));
            l.intensity = inten;
            changed = true;
        }
        // Konum: hedef etrafında küresel koordinatlar (yatay açı, yükseklik, mesafe).
        if (l.type == LightDesc::Type::Directional) {
            ui::Prop("Yatay açı");
            if (ui::SliderF("##az", &l.azimuthDeg, -180.0f, 180.0f, "%.0f°")) { undoPoint(*this, ImGui::GetID("az")); changed = true; }
            ui::Prop("Yükseklik");
            if (ui::SliderF("##el", &l.elevationDeg, 1.0f, 90.0f, "%.0f°")) { undoPoint(*this, ImGui::GetID("el")); changed = true; }
        } else {
            Vec3f rel = l.position - l.target;
            float dist = std::max(1e-4f, rel.length());
            float az = std::atan2(rel.z, rel.x) * RAD_TO_DEG;
            float el = std::asin(std::clamp(rel.y / dist, -1.0f, 1.0f)) * RAD_TO_DEG;
            bool moved = false;
            ui::Prop("Yatay açı", "Konunun etrafında döndür");
            if (ui::SliderF("##az", &az, -180.0f, 180.0f, "%.0f°")) { undoPoint(*this, ImGui::GetID("az")); moved = true; }
            ui::Prop("Yükseklik");
            if (ui::SliderF("##el", &el, -89.0f, 89.0f, "%.0f°")) { undoPoint(*this, ImGui::GetID("el")); moved = true; }
            ui::Prop("Mesafe");
            if (ImGui::DragFloat("##dist", &dist, dist * 0.01f, 1e-3f, 1e6f, "%.3f")) { undoPoint(*this, ImGui::GetID("dist")); moved = true; }
            if (moved) {
                const float a = az * DEG_TO_RAD, e2 = el * DEG_TO_RAD;
                l.position = l.target + Vec3f(std::cos(e2) * std::cos(a), std::sin(e2), std::cos(e2) * std::sin(a)) * dist;
                changed = true;
            }
            if (l.type == LightDesc::Type::Area) {
                ui::Prop("Genişlik");
                if (ImGui::DragFloat("##w", &l.width, std::max(1e-3f, l.width * 0.01f), 1e-3f, 1e5f, "%.3f")) { undoPoint(*this, ImGui::GetID("w")); changed = true; }
                ui::Prop("Yükseklik ");
                if (ImGui::DragFloat("##h", &l.height, std::max(1e-3f, l.height * 0.01f), 1e-3f, 1e5f, "%.3f")) { undoPoint(*this, ImGui::GetID("h")); changed = true; }
            }
        }
        ui::EndProps();
    }
    ImGui::Spacing();
    if (l.type == LightDesc::Type::Area) {
        if (ui::GhostButton(ICON_CROSSHAIR "  Seçili parçaya yönelt")) {
            if (SceneNode* n = selectedNode()) {
                pushUndo();
                l.target = SceneGraph::nodeWorldBounds(*n).centroid();
                changed = true;
            } else {
                // Işık seçiliyken parça seçili olamaz: sahnenin merkezine yönelt.
                pushUndo();
                l.target = m_s.graph.worldBounds().centroid();
                changed = true;
            }
        }
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, pal.danger);
    if (ui::GhostButton(ICON_TRASH_2 "  Sil")) {
        deleteSelection();
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PopStyleColor();
    if (changed) markDocumentChanged(true);
}

void Application::drawCameraProps() {
    OrbitCamera& c = m_s.camera;
    if (ui::Section(ICON_CAMERA, "Lens")) {
        int proj = c.orthographic ? 1 : 0;
        const char* projs[] = {"Perspektif", "Ortografik"};
        if (ui::Segmented("proj", &proj, projs, 2)) c.orthographic = proj == 1;
        ImGui::Spacing();
        if (ui::BeginProps("lens")) {
            ui::Prop("Odak uzaklığı", "Tam kare (36×24 mm) eşdeğeri. Küçük = geniş açı, büyük = tele");
            if (ui::SliderF("##fl", &c.focalLengthMm, 10.0f, 300.0f, "%.0f mm", ImGuiSliderFlags_Logarithmic))
                c.fov = fovDegreesFromFocalMm(c.focalLengthMm);
            ui::Prop("Görüş açısı");
            if (ui::SliderF("##fov", &c.fov, 5.0f, 120.0f, "%.1f°")) c.focalLengthMm = focalMmFromFovDegrees(c.fov);
            ui::EndProps();
        }
        const float lens[] = {24, 35, 50, 85, 135};
        for (int i = 0; i < 5; ++i) {
            if (i) ImGui::SameLine(0, 4);
            char b[16];
            std::snprintf(b, sizeof(b), "%.0f", lens[i]);
            const bool on = std::abs(c.focalLengthMm - lens[i]) < 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Text, on ? ui::palette().accent : ui::palette().textDim);
            if (ui::GhostButton(b, ImVec2((ImGui::GetContentRegionAvail().x - 16.0f) / static_cast<float>(5 - i), 0))) {
                c.focalLengthMm = lens[i];
                c.fov = fovDegreesFromFocalMm(c.focalLengthMm);
            }
            ImGui::PopStyleColor();
        }
    }
    if (ui::Section(ICON_APERTURE, "Alan derinliği", c.aperture > 0.0f)) {
        bool dof = c.aperture > 0.0f;
        if (ui::BeginProps("dof")) {
            ui::Prop("Etkin", "Odak dışındaki bölgeler bulanıklaşır (fotoğraf makinesi gibi)");
            if (ui::Toggle("dofen", &dof)) c.aperture = dof ? apertureFromFStop(c.focalLengthMm, c.fStop) : 0.0f;
            if (dof) {
                ui::Prop("f-durağı", "Küçük sayı = büyük açıklık = daha çok bulanıklık");
                if (ui::SliderF("##fs", &c.fStop, 0.7f, 32.0f, "f/%.1f", ImGuiSliderFlags_Logarithmic))
                    c.aperture = apertureFromFStop(c.focalLengthMm, c.fStop);
                ui::Prop("Odak mesafesi");
                float fd = effectiveFocusDistance(c);
                if (ImGui::DragFloat("##fd", &fd, std::max(1e-3f, fd * 0.005f), 1e-3f, 1e6f, "%.3f")) {
                    c.focusDistance = fd;
                    c.focusExplicit = true;
                }
            }
            ui::EndProps();
        }
        if (dof) {
            if (ui::GhostButton(ICON_CROSSHAIR "  Viewport'ta odak noktası seç")) m_s.pickFocus = true;
            ui::Hint("Ölçek: sahne birimi metre kabul edilir.");
        }
    }
    if (ui::Section(ICON_FILM, "Kayıtlı kameralar")) {
        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(m_s.savedCameras.size()); ++i) {
            ImGui::PushID(i);
            const auto& sc = m_s.savedCameras[static_cast<size_t>(i)];
            if (ImGui::Selectable((std::string(ICON_CAMERA "  ") + sc.name).c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6, 0)))
                m_s.camera = sc.cam;
            ui::Tooltip("Bu görünüme geç");
            ImGui::SameLine();
            if (ui::IconButton(ICON_X, "Sil")) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            m_s.savedCameras.erase(m_s.savedCameras.begin() + removeIdx);
            m_s.documentDirty = true;
        }
        if (ui::GhostButton(ICON_PLUS "  Mevcut görünümü kaydet", ImVec2(-FLT_MIN, 0))) {
            m_s.savedCameras.push_back({"Kamera " + std::to_string(m_s.savedCameras.size() + 1), c});
            m_s.documentDirty = true;
        }
        if (m_s.savedCameras.empty()) ui::Hint("Beğendiğiniz bakış açılarını kaydedip tek tıkla geri dönebilirsiniz.");
    }
    if (ui::Section(ICON_VIDEO, "Açılar ve hareket")) {
        const float bw = (ImGui::GetContentRegionAvail().x - 12.0f) / 4.0f;
        if (ui::GhostButton("Ön", ImVec2(bw, 0))) applyCameraPreset("front");
        ImGui::SameLine(0, 4);
        if (ui::GhostButton("Yan", ImVec2(bw, 0))) applyCameraPreset("side");
        ImGui::SameLine(0, 4);
        if (ui::GhostButton("Üst", ImVec2(bw, 0))) applyCameraPreset("top");
        ImGui::SameLine(0, 4);
        if (ui::GhostButton("3/4", ImVec2(bw, 0))) applyCameraPreset("three_quarter");
        ImGui::Spacing();
        if (ui::BeginProps("motion")) {
            ui::Prop("Turntable");
            ui::Toggle("tt", &c.turntable);
            ui::Prop("Dönüş hızı");
            ui::SliderF("##tts", &c.turntableSpeed, 0.05f, 2.0f, "%.2f rad/sn");
            ui::EndProps();
        }
        if (ui::PrimaryButton(ICON_MAXIMIZE "  Tümünü kadrajla", ImVec2(-FLT_MIN, 0))) frameAll();
    }
}

void Application::drawImageProps() {
    if (ui::Section(ICON_CONTRAST, "Ton ve pozlama")) {
        if (ui::BeginProps("tone")) {
            ui::Prop("Ton eşleme", "HDR ışığı ekrana sığdıran eğri. Ürün görselleri için PBR Nötr önerilir.");
            const ToneMapOperator ops[] = {ToneMapOperator::PBRNeutral, ToneMapOperator::AgX, ToneMapOperator::ACES,
                                           ToneMapOperator::Filmic, ToneMapOperator::Reinhard, ToneMapOperator::Linear};
            if (ImGui::BeginCombo("##tmo", toneMapName(m_s.settings.tmo))) {
                for (ToneMapOperator op : ops)
                    if (ImGui::Selectable(toneMapName(op), op == m_s.settings.tmo)) m_s.settings.tmo = op;
                ImGui::EndCombo();
            }
            ui::Prop("Pozlama (EV)", "+1 EV = iki kat parlak. Birikimi sıfırlamaz.");
            ui::SliderF("##ev", &m_s.settings.exposure, -5.0f, 5.0f, "%+.2f EV");
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_s.settings.exposure = 0.0f;
            ui::EndProps();
        }
    }
    if (ui::Section(ICON_MONITOR, "Önizleme")) {
        int scale = m_s.resolutionScale >= 0.99f ? 2 : m_s.resolutionScale >= 0.74f ? 1 : 0;
        const char* scales[] = {"%50", "%75", "%100"};
        ImGui::TextColored(ui::palette().textDim, "Çözünürlük");
        if (ui::Segmented("scale", &scale, scales, 3)) m_s.resolutionScale = scale == 0 ? 0.5f : scale == 1 ? 0.75f : 1.0f;
        ImGui::Spacing();
        if (ui::BeginProps("prev")) {
            ui::Prop("Örnek sınırı", "Viewport bu sayıya ulaşınca durur (0 = sınırsız)");
            const int limits[] = {0, 64, 256, 1024, 4096};
            const char* names[] = {"Sınırsız", "64", "256", "1024", "4096"};
            int idx = 0;
            for (int i = 0; i < 5; ++i)
                if (limits[i] == m_s.previewSpp) idx = i;
            if (ImGui::Combo("##lim", &idx, names, 5)) m_s.previewSpp = limits[idx];
            ui::Prop("Gürültü giderme", denoiseAvailable() ? "Intel Open Image Denoise (yapay zekâ)" : "OIDN bu derlemede yok");
            ImGui::BeginDisabled(!denoiseAvailable());
            ui::Toggle("dn", &m_s.denoise);
            ImGui::EndDisabled();
            ui::EndProps();
        }
    }
    if (ui::Section(ICON_GAUGE, "Işık taşıma", false)) {
        if (ui::BeginProps("lt")) {
            ui::Prop("Sekme sayısı", "Işının en çok kaç kez yansıyacağı. Cam ve iç mekân için yüksek tutun.");
            ui::SliderI("##mb", &m_s.settings.maxBounces, 1, 32);
            ui::Prop("Uyarlamalı örnekleme", "Düz bölgelerde örnek atlar (deneysel)");
            ui::Toggle("ad", &m_s.settings.adaptiveSampling);
            ui::EndProps();
        }
    }
}

} // namespace photon
