// ui_viewport.cpp — Ortadaki render görünümü.
//
// İçerik: path-traced görüntü (şeffaf modda dama deseni üstünde), fare ile
// kamera kontrolü (orbit/pan/zoom), tıklayarak seçme, çift tıkla pivot,
// seçili parçanın 3B sınır kutusu, ImGuizmo ile taşı/döndür/ölçekle, sağ altta
// yön küpü, üstte araç çubuğu ve bilgi rozeti (HUD), boş sahnede karşılama
// kartı, kütüphaneden sürükle-bırak hedefi.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/drag_drop.h"
#include "engine/denoiser.h"
#include "core/math/constants.h"
#include "camera/orthographic_camera.h"
#include "materials/disney.h"

#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace photon {

namespace {

// Mat4f satır-öncelikli (m(r,c)); OpenGL/ImGuizmo sütun-öncelikli dizi bekler.
void toColumnMajor(const Mat4f& m, float out[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = m(r, c);
}

Mat4f fromColumnMajor(const float in[16]) {
    Mat4f m = Mat4f::identity();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m(r, c) = in[c * 4 + r];
    return m;
}

struct ViewProj {
    Mat4f view;
    Mat4f proj;
};

ViewProj viewProj(const OrbitCamera& cam, float aspect) {
    ViewProj vp;
    vp.view = Mat4f::lookAt(cam.position(), cam.targetVec(), Vec3f(0, 1, 0));
    const float zNear = std::max(1e-3f, cam.radius * 0.01f);
    const float zFar = std::max(zNear * 10.0f, cam.radius * 100.0f);
    const float orthoH = 2.0f * std::tan(cam.fov * DEG_TO_RAD * 0.5f) * cam.radius;
    vp.proj = cam.orthographic ? Mat4f::ortho(orthoH, aspect, zNear, zFar)
                               : Mat4f::perspective(cam.fov * DEG_TO_RAD, aspect, zNear, zFar);
    return vp;
}

// Dünya noktası → ekran pikseli. Kameranın arkasındaysa false.
bool project(const ViewProj& vp, const Vec3f& p, const ImVec2& origin, const ImVec2& size, ImVec2& out) {
    const Vec4f c = (vp.proj * vp.view) * Vec4f(p.x, p.y, p.z, 1.0f);
    if (c.w <= 1e-6f) return false;
    const float x = c.x / c.w, y = c.y / c.w;
    out = ImVec2(origin.x + (x * 0.5f + 0.5f) * size.x, origin.y + (1.0f - (y * 0.5f + 0.5f)) * size.y);
    return true;
}

} // namespace

void Application::drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::palette().bg0);
    const bool open = ImGui::Begin("Görünüm###Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const int pw = std::max(1, static_cast<int>(size.x));
    const int ph = std::max(1, static_cast<int>(size.y));
    m_s.viewportPxW = pw;
    m_s.viewportPxH = ph;
    // Render çözünürlüğü = viewport piksel boyutu × ölçek; en-boy oranı hep aynı.
    m_s.viewport.setViewport(pw, ph, m_s.resolutionScale * ImGui::GetIO().DisplayFramebufferScale.x);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    // GPU dokusu alttan üste saklanır: uv (0,1)-(1,0) ile dikey çevrilir. İçeriği bu
    // karenin sonunda renderRasterView() doldurur; kimlik sabit olduğu için şimdiden konabilir.
    const ImTextureID rasterId = static_cast<ImTextureID>(static_cast<intptr_t>(m_rasterTex));
    if (isRasterMode(m_s.viewMode) && m_rasterTex) {
        dl->AddImage(rasterId, origin, end, ImVec2(0, 1), ImVec2(1, 0));
    } else {
        if (m_viewHasAlpha && m_checkerTex) {
            dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_checkerTex)), origin, end, ImVec2(0, 0),
                         ImVec2(size.x / 16.0f, size.y / 16.0f));
        }
        if (m_viewTex) dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_viewTex)), origin, end);
        if (m_s.wireOverlay && m_rasterTex)
            dl->AddImage(rasterId, origin, end, ImVec2(0, 1), ImVec2(1, 0), IM_COL32(255, 255, 255, 150));
    }

    // Tüm alanı kaplayan görünmez düğme: fare etkileşimi ve sürükle-bırak hedefi.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##viewport", ImVec2(static_cast<float>(pw), static_cast<float>(ph)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    drawSelectionOutline(origin, size);
    drawGizmo(origin, size);
    const bool gizmoBusy = ImGuizmo::IsUsing() || ImGuizmo::IsOver();

    // ── Kamera kontrolü ──
    if ((hovered || active) && !gizmoBusy) {
        const bool lmb = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
        const bool pan = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f) ||
                         ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f) || (lmb && io.KeyShift);
        if (pan) m_s.camera.pan(io.MouseDelta.x, io.MouseDelta.y, static_cast<float>(ph));
        else if (lmb) m_s.camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (hovered && io.MouseWheel != 0.0f) {
            if (io.KeyCtrl) {
                // Ctrl + tekerlek: odak uzaklığı (zoom lens), kamera yerinde kalır.
                m_s.camera.focalLengthMm = std::clamp(m_s.camera.focalLengthMm * std::exp(io.MouseWheel * 0.08f), 10.0f, 600.0f);
                m_s.camera.fov = fovDegreesFromFocalMm(m_s.camera.focalLengthMm);
            } else {
                // İmlece doğru zoom: yarıçap s oranında küçülürken hedef de imlecin
                // altındaki P noktasına doğru aynı oranda kayar: t' = P + (t − P)·s.
                // Böylece P ekranda yaklaşık aynı yerde kalır (kamera P'ye doğru yürür).
                Vec3f hit;
                const float u = (io.MousePos.x - origin.x) / size.x;
                const float v = (io.MousePos.y - origin.y) / size.y;
                const float before = m_s.camera.radius;
                m_s.camera.zoom(io.MouseWheel);
                const float s = m_s.camera.radius / before;
                if (!m_s.camera.orthographic && pickAt(u, v, &hit)) {
                    for (int k = 0; k < 3; ++k) {
                        const float p = k == 0 ? hit.x : k == 1 ? hit.y : hit.z;
                        m_s.camera.target[k] = p + (m_s.camera.target[k] - p) * s;
                    }
                }
            }
        }
    }
    // Üzerine gelme vurgusu: imlecin altındaki parça (sürüklemiyorsak).
    m_s.hoverUid = 0;
    if (hovered && !gizmoBusy && !ImGui::IsAnyMouseDown()) {
        const uint64_t h = pickAt((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
        m_s.hoverUid = h == kGroundNodeUid ? 0 : h;
    }
    // Tıklama (sürüklemeden bırakma) → seçim; çift tık → pivot.
    if (hovered && !gizmoBusy) {
        const ImVec2 mp = io.MousePos;
        const float u = (mp.x - origin.x) / size.x;
        const float v = (mp.y - origin.y) / size.y;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            Vec3f hit;
            if (pickAt(u, v, &hit)) {
                // Kamerayı oynatmadan pivotu taşı: hedef değişir, yarıçap korunur.
                const Vec3f eye = m_s.camera.position();
                m_s.camera.target[0] = hit.x;
                m_s.camera.target[1] = hit.y;
                m_s.camera.target[2] = hit.z;
                const Vec3f d = eye - hit;
                m_s.camera.radius = std::max(1e-3f, d.length());
                m_s.camera.theta = std::acos(std::clamp(d.y / m_s.camera.radius, -1.0f, 1.0f));
                m_s.camera.phi = std::atan2(d.z, d.x);
                setStatus("Döndürme merkezi taşındı");
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                   ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x +
                           ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y * ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y < 9.0f) {
            Vec3f hit;
            const uint64_t uid = pickAt(u, v, &hit);
            if (m_s.pickFocus) {
                if (uid) {
                    m_s.camera.focusDistance = (hit - m_s.camera.position()).length();
                    m_s.camera.focusExplicit = true;
                    setStatus("Odak mesafesi ayarlandı");
                }
                m_s.pickFocus = false;
            } else {
                // Grubun bir parçasına tıklanırsa önce grup seçilir; seçili grubun
                // içine tekrar tıklayınca parçanın kendisi seçilir (KeyShot davranışı).
                // Ctrl ya da Shift basılıysa aynı hedef seçime eklenir / çıkarılır.
                SceneNode* n = m_s.graph.findByUid(uid);
                uint64_t target = uid;
                if (n && n->parent && n->parent != m_s.graph.root() && !isNodeSelected(uid) &&
                    !isNodeSelected(n->parent->uid))
                    target = n->parent->uid;
                if ((io.KeyCtrl || io.KeyShift) && n) toggleNodeSelection(target);
                else if (!(io.KeyCtrl || io.KeyShift)) selectNode(target);
            }
        }
        if (ImGui::BeginPopupContextItem("##vpctx")) {
            // Sağ tık seçili olmayan bir parçanın üstündeyse önce onu seç.
            if (ImGui::IsWindowAppearing()) {
                const uint64_t under = pickAt((io.MousePos.x - origin.x) / size.x, (io.MousePos.y - origin.y) / size.y);
                if (m_s.graph.findByUid(under) && !isNodeSelected(under)) selectNode(under);
            }
            const bool hasNode = m_s.selKind == SelectionKind::Node;
            if (ImGui::MenuItem(ICON_FOCUS "  Seçime odaklan", "F", false, hasNode)) frameSelection();
            if (ImGui::MenuItem(ICON_MAXIMIZE "  Tümünü kadrajla", "Shift+A")) frameAll();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D", false, hasNode)) duplicateSelection();
            if (ImGui::MenuItem(ICON_GROUP "  Grupla", "Ctrl+G", false, hasNode)) groupSelection();
            if (ImGui::MenuItem("      Zemine oturt", "G", false, hasNode)) placeSelectionOnGround();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_EYE_OFF "  Gizle", "H", false, hasNode)) toggleSelectionVisibility();
            if (ImGui::MenuItem(ICON_SCAN_EYE "  Yalnız seçimi göster", "I", false, hasNode)) isolateSelection();
            if (ImGui::MenuItem(ICON_EYE "  Hepsini göster", "Alt+H")) showAllNodes();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del", false, hasNode)) deleteSelection();
            ImGui::EndPopup();
        }
    }

    // ── Sürükle-bırak: imlecin altındaki parça hedef alınır ──
    if (ImGui::BeginDragDropTarget()) {
        const ImVec2 mp = io.MousePos;
        const float u = (mp.x - origin.x) / size.x;
        const float v = (mp.y - origin.y) / size.y;
        const uint64_t under = pickAt(u, v);
        if (SceneNode* n = m_s.graph.findByUid(under)) {
            // Bırakılacak parçayı çerçevele.
            const ImGuiPayload* peek = ImGui::GetDragDropPayload();
            if (peek && (peek->IsDataType(kPayloadMaterial) || peek->IsDataType(kPayloadTexture))) {
                ImVec2 tl;
                if (project(viewProj(m_s.camera, size.x / size.y), SceneGraph::nodeWorldBounds(*n).centroid(), origin, size, tl))
                    dl->AddText(ImVec2(mp.x + 16, mp.y + 4), ui::col(ui::palette().accent), n->name.c_str());
            }
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadMaterial)) {
            if (const MaterialPreset* preset = m_s.materials.findById(static_cast<const char*>(p->Data)))
                applyMaterialPreset(under ? under : m_s.selUid, *preset);
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadHdr)) {
            for (const auto& e : m_s.environments)
                if (e.id == static_cast<const char*>(p->Data)) applyEnvironment(e);
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadStudio)) {
            for (const auto& s : m_s.studios)
                if (s.id == static_cast<const char*>(p->Data)) applyStudio(s);
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadModel)) {
            importModel(static_cast<const char*>(p->Data));
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadPrimitive)) {
            // Şekil imlecin altındaki yüzeye (zemin ya da başka bir nesnenin üstü) konur.
            int kind = 0;
            std::memcpy(&kind, p->Data, sizeof(kind));
            Vec3f hit;
            if (pickAt(u, v, &hit)) addPrimitive(static_cast<PrimitiveKind>(kind), &hit);
            else addPrimitive(static_cast<PrimitiveKind>(kind));
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            SceneNode* n = m_s.graph.findByUid(under);
            auto d = n ? std::dynamic_pointer_cast<DisneyMaterial>(n->material) : nullptr;
            if (d) {
                pushUndo();
                const std::string path = static_cast<const char*>(p->Data);
                editLive([&] { d->setAlbedoMap(path); });
                selectNode(n->uid);
                setStatus("Taban rengi dokusu: " + ui::fileName(path));
            }
        }
        ImGui::EndDragDropTarget();
    }

    drawViewportOverlay(origin, size);
    if (m_s.graph.empty()) drawWelcome(origin, size);
    ImGui::End();
}

// Seçili parçanın dünya uzayı sınır kutusu, 12 kenarı ekrana izdüşürülerek çizilir.
namespace {

// Dünya uzayı kutusunun 12 kenarını ekrana izdüşürerek çizer; isteğe bağlı ad etiketi.
void drawBox(const ViewProj& vp, const AABB& b, const ImVec2& origin, const ImVec2& size, ImU32 col,
             float thickness, const char* label) {
    if (b.pMin.x > b.pMax.x) return;
    Vec3f c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = Vec3f(i & 1 ? b.pMax.x : b.pMin.x, i & 2 ? b.pMax.y : b.pMin.y, i & 4 ? b.pMax.z : b.pMin.z);
    ImVec2 s[8];
    bool ok[8];
    for (int i = 0; i < 8; ++i) ok[i] = project(vp, c[i], origin, size, s[i]);
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& e : edges)
        if (ok[e[0]] && ok[e[1]]) dl->AddLine(s[e[0]], s[e[1]], col, thickness);
    if (!label) return;
    // Ad etiketi: kutunun ekrandaki en üst-sol noktasının üstünde.
    float top = 1e9f, left = 1e9f;
    for (int i = 0; i < 8; ++i)
        if (ok[i]) {
            top = std::min(top, s[i].y);
            left = std::min(left, s[i].x);
        }
    if (top < 1e8f) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 p(left, top - ts.y - 10.0f);
        dl->AddRectFilled(ImVec2(p.x - 6, p.y - 3), ImVec2(p.x + ts.x + 6, p.y + ts.y + 3), ui::col(ui::palette().accent), 5.0f);
        dl->AddText(p, IM_COL32(20, 16, 12, 255), label);
    }
}

} // namespace

// Seçim ve üzerine gelme vurguları + seçili ışığın şekli (alan ışığı dörtgeni,
// güneş yönü oku, nokta ışık dairesi). Hepsi yalnız ekrana çizilir; render'a girmez.
void Application::drawSelectionOutline(const ImVec2& origin, const ImVec2& size) {
    const ui::Palette& pal = ui::palette();
    const ViewProj vp = viewProj(m_s.camera, size.x / size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    if (m_s.hoverUid && !isNodeSelected(m_s.hoverUid)) {
        if (SceneNode* h = m_s.graph.findByUid(m_s.hoverUid))
            drawBox(vp, SceneGraph::nodeWorldBounds(*h), origin, size, ui::col(pal.text, 0.25f), 1.0f, nullptr);
    }
    // Tek seçim: kutu + ad etiketi. Çoklu seçim: her nesnenin kutusu ve hepsini
    // saran soluk bir ortak kutu; etiket ("N nesne") ortak kutunun üstünde.
    const std::vector<SceneNode*> sel = selectedNodes();
    if (sel.size() == 1) {
        drawBox(vp, SceneGraph::nodeWorldBounds(*sel.front()), origin, size, ui::col(pal.accent, 0.8f), 1.25f,
                sel.front()->name.c_str());
    } else if (sel.size() > 1) {
        AABB all = AABB::empty();
        for (SceneNode* n : sel) {
            const AABB b = SceneGraph::nodeWorldBounds(*n);
            all.merge(b);
            drawBox(vp, b, origin, size, ui::col(pal.accent, n->uid == m_s.selUid ? 0.85f : 0.55f), 1.25f, nullptr);
        }
        char label[48];
        std::snprintf(label, sizeof(label), "%zu nesne", sel.size());
        drawBox(vp, all, origin, size, ui::col(pal.accent, 0.18f), 1.0f, label);
    }

    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 && m_s.selLight < static_cast<int>(m_s.lights.size())) {
        const LightDesc& l = m_s.lights[static_cast<size_t>(m_s.selLight)];
        const ImU32 col = ui::col(pal.warning, 0.9f);
        ImVec2 a, b;
        if (l.type == LightDesc::Type::Area) {
            // buildLights ile aynı taban: n hedefe bakar, u ve v dörtgenin kenarları.
            Vec3f n = (l.target - l.position);
            n = n.lengthSquared() > 0.0f ? n.normalized() : Vec3f(0, -1, 0);
            const Vec3f up = std::abs(n.y) > 0.95f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0);
            const Vec3f u = up.cross(n).normalized() * (l.width * 0.5f);
            const Vec3f v = n.cross(up.cross(n).normalized()).normalized() * (l.height * 0.5f);
            const Vec3f q[4] = {l.position - u - v, l.position + u - v, l.position + u + v, l.position - u + v};
            ImVec2 s[4];
            bool ok = true;
            for (int i = 0; i < 4; ++i) ok = project(vp, q[i], origin, size, s[i]) && ok;
            if (ok) {
                dl->AddQuadFilled(s[0], s[1], s[2], s[3], ui::col(pal.warning, 0.12f));
                dl->AddQuad(s[0], s[1], s[2], s[3], col, 1.5f);
            }
            if (project(vp, l.position, origin, size, a) && project(vp, l.target, origin, size, b)) {
                dl->AddLine(a, b, ui::col(pal.warning, 0.45f), 1.0f);
                dl->AddCircleFilled(b, 3.0f, col);
            }
        } else if (l.type == LightDesc::Type::Point) {
            if (project(vp, l.position, origin, size, a)) {
                dl->AddCircle(a, 9.0f, col, 24, 1.5f);
                dl->AddCircleFilled(a, 3.0f, col);
            }
        } else {
            // Güneş: sahne merkezinden ışığın geldiği yöne doğru bir ok.
            const AABB box = m_s.graph.worldBounds();
            const Vec3f c = box.pMin.x <= box.pMax.x ? box.centroid() : m_s.camera.targetVec();
            const float r = box.pMin.x <= box.pMax.x ? (box.pMax - box.pMin).length() * 0.6f : 1.0f;
            const Vec3f from = c - directionalTravelDir(l) * r;
            if (project(vp, from, origin, size, a) && project(vp, c, origin, size, b)) {
                dl->AddLine(a, b, col, 2.0f);
                dl->AddCircleFilled(b, 4.0f, col);
                dl->AddText(ImVec2(a.x + 6, a.y - 18), col, ICON_SUN);
            }
        }
    }
    dl->PopClipRect();
}

// Tutamaç (ImGuizmo). Düğümlerin kendisi değil, seçimin ortasındaki bir PİVOT
// matrisi P düzenlenir. ImGuizmo P'yi P' yapınca dünya uzayındaki değişim
//     D = P' · P⁻¹
// her seçili düğüme uygulanır: dünya' = D · dünya, yerel' = ebeveynDünya⁻¹ · dünya'.
// Böylece çoklu seçim tek parça gibi döner/ölçeklenir ve dönme merkezi nesnenin
// orijini değil görünür ortasıdır (içe aktarılan parçaların orijini çoğu zaman
// modelin dışında kalır). P sürükleme boyunca sabit tutulur: her karede sınır
// kutusundan yeniden hesaplansaydı döndürürken kutu değiştiği için merkez kayardı.
void Application::drawGizmo(const ImVec2& origin, const ImVec2& size) {
    ImGuizmo::SetOrthographic(m_s.camera.orthographic);
    ImGuizmo::SetGizmoSizeClipSpace(0.16f);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    const ViewProj vp = viewProj(m_s.camera, size.x / size.y);
    float view[16], proj[16];
    toColumnMajor(vp.view, view);
    toColumnMajor(vp.proj, proj);

    const std::vector<SceneNode*> nodes = selectedNodes();
    LightDesc* light = nullptr;
    if (m_s.selKind == SelectionKind::Light && m_s.selLight >= 0 && m_s.selLight < static_cast<int>(m_s.lights.size()) &&
        m_s.lights[static_cast<size_t>(m_s.selLight)].type != LightDesc::Type::Directional)
        light = &m_s.lights[static_cast<size_t>(m_s.selLight)];

    if (m_s.gizmoOp > 0 && (!nodes.empty() || light)) {
        // Işıklarda yalnız taşıma: alan ışığı hedefine bakmaya devam eder.
        const int opIdx = light ? 1 : m_s.gizmoOp;
        const ImGuizmo::OPERATION op = opIdx == 1 ? ImGuizmo::TRANSLATE : opIdx == 2 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        if (!ImGuizmo::IsUsing()) {
            m_gizmoUndoPushed = false;
            Vec3f center;
            Mat4f rot = Mat4f::identity();
            if (light) {
                center = light->position;
            } else {
                AABB box = AABB::empty();
                for (SceneNode* n : nodes) box.merge(SceneGraph::nodeWorldBounds(*n));
                center = box.pMin.x <= box.pMax.x ? box.centroid() : Vec3f(0.0f);
                // Tek nesnede yerel eksenler (ölçek her zaman nesnenin kendi eksenlerinde):
                // dünya matrisinin sütunları Gram-Schmidt ile dik birim eksenlere çevrilir.
                if (nodes.size() == 1 && (m_s.gizmoLocal || op == ImGuizmo::SCALE)) {
                    const Mat4f w = nodes.front()->worldTransform().matrix();
                    Vec3f x(w(0, 0), w(1, 0), w(2, 0)), y(w(0, 1), w(1, 1), w(2, 1));
                    if (x.lengthSquared() > 1e-12f && y.lengthSquared() > 1e-12f) {
                        x = x.normalized();
                        y = (y - x * x.dot(y));
                        if (y.lengthSquared() > 1e-12f) {
                            y = y.normalized();
                            const Vec3f z = x.cross(y);
                            for (int r = 0; r < 3; ++r) {
                                rot(r, 0) = r == 0 ? x.x : r == 1 ? x.y : x.z;
                                rot(r, 1) = r == 0 ? y.x : r == 1 ? y.y : y.z;
                                rot(r, 2) = r == 0 ? z.x : r == 1 ? z.y : z.z;
                            }
                        }
                    }
                }
            }
            m_gizmoPivot = Transform::translate(center).matrix() * rot;
        }

        // Adım: Ctrl basılıyken mıknatıs ayarı tersine döner. Taşıma adımı sahne
        // boyutuna göre "yuvarlak" bir sayıdır (3 birimlik sahnede 0.1, 30'da 1).
        float snap[3] = {0, 0, 0};
        const bool snapping = m_s.snap != ImGui::GetIO().KeyCtrl;
        if (snapping) {
            const AABB all = m_s.graph.worldBounds();
            const float diag = all.pMin.x <= all.pMax.x ? (all.pMax - all.pMin).length() : 1.0f;
            const float step = op == ImGuizmo::TRANSLATE ? std::pow(10.0f, std::floor(std::log10(std::max(diag, 1e-4f)))) * 0.1f
                             : op == ImGuizmo::ROTATE    ? 15.0f
                                                         : 0.1f;
            snap[0] = snap[1] = snap[2] = step;
        }

        float model[16];
        toColumnMajor(m_gizmoPivot, model);
        const ImGuizmo::MODE mode = (m_s.gizmoLocal || op == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        if (ImGuizmo::Manipulate(view, proj, op, mode, model, nullptr, snapping ? snap : nullptr)) {
            // Geri al: sürüklemenin İLK DEĞİŞİKLİĞİNDE bir kez (ilk kare değişmeden geçebilir).
            if (!m_gizmoUndoPushed) {
                pushUndo();
                m_gizmoUndoPushed = true;
            }
            const Mat4f next = fromColumnMajor(model);
            const Mat4f delta = next * m_gizmoPivot.inverse();
            if (light) {
                const Vec4f p = delta * Vec4f(light->position.x, light->position.y, light->position.z, 1.0f);
                light->position = Vec3f(p.x, p.y, p.z);
            } else {
                for (SceneNode* n : nodes) {
                    const Mat4f world = delta * n->worldTransform().matrix();
                    n->localTransform = Transform(n->parent->worldTransform().inverseMatrix() * world);
                }
            }
            m_gizmoPivot = next;
            markDocumentChanged(true);
        }
    }

    // Yön küpü (sağ alt): tıklanan yüze kamerayı çevirir; sonuç görünüş matrisinden
    // orbit açılarına geri çevrilir.
    const float cube = 96.0f;
    float viewCopy[16];
    std::memcpy(viewCopy, view, sizeof(view));
    ImGuizmo::ViewManipulate(viewCopy, m_s.camera.radius,
                             ImVec2(origin.x + size.x - cube - 12.0f, origin.y + size.y - cube - 12.0f),
                             ImVec2(cube, cube), 0x00000000);
    if (std::memcmp(viewCopy, view, sizeof(view)) != 0) {
        const Mat4f inv = fromColumnMajor(viewCopy).inverse();
        const Vec3f eye(inv(0, 3), inv(1, 3), inv(2, 3));
        const Vec3f fwd = Vec3f(-inv(0, 2), -inv(1, 2), -inv(2, 2)).normalized();
        const Vec3f target = eye + fwd * m_s.camera.radius;
        const Vec3f d = (eye - target).normalized();
        m_s.camera.target[0] = target.x;
        m_s.camera.target[1] = target.y;
        m_s.camera.target[2] = target.z;
        m_s.camera.theta = std::clamp(std::acos(std::clamp(d.y, -1.0f, 1.0f)), 0.02f, PI - 0.02f);
        m_s.camera.phi = std::atan2(d.z, d.x);
    }
}

// GPU modu ya da tel kafes bindirmesi açıksa: görünür parçaları (dünya matrisi,
// katı modda gösterilecek renk, seçili mi) topla ve RasterView'a çizdir. Renk,
// malzemenin taban rengidir; cam şeffaf çizilemediği için açık mavi-gri gösterilir.
void Application::renderRasterView() {
    const bool raster = isRasterMode(m_s.viewMode);
    if (!m_rasterOk || (!raster && !m_s.wireOverlay) || m_s.viewportPxW <= 0 || m_s.viewportPxH <= 0) return;
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
    const ViewProj vp = viewProj(m_s.camera, static_cast<float>(m_s.viewportPxW) / static_cast<float>(m_s.viewportPxH));
    RasterView::Frame f;
    f.width = static_cast<int>(static_cast<float>(m_s.viewportPxW) * fb);
    f.height = static_cast<int>(static_cast<float>(m_s.viewportPxH) * fb);
    f.view = vp.view;
    f.proj = vp.proj;
    f.eye = m_s.camera.position();
    f.focus = m_s.camera.targetVec();
    const AABB box = m_s.graph.worldBounds();
    const bool has = box.pMin.x <= box.pMax.x;
    const float diag = has ? (box.pMax - box.pMin).length() : 3.0f;
    f.groundY = has ? box.pMin.y : 0.0f;
    // Hücre: sahne boyunun yuvarlak bir kesri (3 birimlik sahnede 0.25) — sık ama kalabalık değil.
    f.gridStep = std::pow(10.0f, std::floor(std::log10(std::max(diag, 1e-3f)))) * 0.25f;
    f.grid = m_s.environment.groundEnabled;

    static const std::shared_ptr<const TriangleMesh> unitSphere = makePrimitiveMesh(PrimitiveKind::Sphere);
    std::unordered_map<const Material*, Color3f> colors;
    auto colorOf = [&](const Material* m) -> Color3f {
        if (!m) return Color3f(0.7f);
        auto it = colors.find(m);
        if (it != colors.end()) return it->second;
        const MaterialPreset p = MaterialLibrary::presetFromMaterial(*m, "");
        const Color3f c = p.kind == MaterialKind::Glass ? Color3f(0.72f, 0.82f, 0.9f) : p.baseColor;
        return colors[m] = c;
    };
    std::vector<RasterView::Item> items;
    size_t tris = 0;
    auto walk = [&](auto&& self, const SceneNode& n, const Mat4f& parent, bool parentSel) -> void {
        if (!n.visible) return;
        const Mat4f world = parent * n.localTransform.matrix();
        const bool sel = parentSel || isNodeSelected(n.uid);
        if (n.type == SceneNodeType::Mesh && n.mesh) {
            items.push_back({n.mesh, world, colorOf(n.material.get()), sel});
            tris += n.mesh->numTriangles();
        } else if (n.type == SceneNodeType::Sphere && n.sphereRadius > 0.0f) {
            // Birim küre (merkez 0, 0.5, 0; yarıçap 0.5) → düğümün merkezine ve yarıçapına.
            const Mat4f m = world * Transform::scale(Vec3f(2.0f * n.sphereRadius)).matrix() *
                            Transform::translate(Vec3f(0.0f, -0.5f, 0.0f)).matrix();
            items.push_back({unitSphere, m, colorOf(n.material.get()), sel});
            tris += unitSphere->numTriangles();
        }
        for (const auto& c : n.children) self(self, *c, world, sel);
    };
    walk(walk, *m_s.graph.root(), Mat4f::identity(), false);
    m_rasterTris = tris;
    m_rasterTex = m_raster.render(f, items, raster ? m_s.viewMode : ViewMode::Wire, !raster);
}

void Application::drawViewportOverlay(const ImVec2& origin, const ImVec2& size) {
    const ui::Palette& pal = ui::palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Sol üst: araç çubuğu ──
    // Önce düğmeler (kanal 1), sonra gerçek boyutlarına göre arkalarına kutu (kanal 0):
    // düğme eklenip çıkarılınca genişliği elle hesaplamaya gerek kalmaz.
    const float btn = ImGui::GetFrameHeight() + 2.0f;
    const ImVec2 tb(origin.x + 10.0f, origin.y + 10.0f);
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(tb.x + 5.0f, tb.y + 4.0f));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    // Görüntü modu: ikon + ad + ok; açılır listede tüm modlar ve tel kafes bindirmesi.
    {
        static const char* kModeIcons[kViewModeCount] = {ICON_SPARKLES, ICON_CIRCLE, ICON_CUBOID, ICON_GRID_3X3, ICON_CONTRAST};
        static const char* kModeHints[kViewModeCount] = {
            "Işın izleme: son görüntü, malzemeler ve ışık",
            "Işın izleme, tüm yüzeyler mat gri: ışık ve formu değerlendirmek için",
            "GPU: anında, gölgesiz; büyük modellerde yerleştirme için",
            "GPU: üçgen ağı (arkadaki çizgiler gizli)",
            "GPU: yüzey yönleri renkli (ters dönmüş yüzleri bulmak için)"};
        const int cur = static_cast<int>(m_s.viewMode);
        char label[96];
        std::snprintf(label, sizeof(label), "%s  %s  " ICON_CHEVRON_DOWN, kModeIcons[cur], viewModeName(m_s.viewMode));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, (btn - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal.bg3);
        if (ImGui::Button(label)) ImGui::OpenPopup("viewmodes");
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ui::Tooltip("Görüntü modu (Z ile sırayla değiştir)");
        if (ImGui::BeginPopup("viewmodes")) {
            for (int i = 0; i < kViewModeCount; ++i) {
                const ViewMode m = static_cast<ViewMode>(i);
                char item[96];
                std::snprintf(item, sizeof(item), "%s  %s", kModeIcons[i], viewModeName(m));
                if (ImGui::MenuItem(item, i == 2 ? "GPU" : nullptr, cur == i, !isRasterMode(m) || m_rasterOk)) setViewMode(m);
                ui::Tooltip(kModeHints[i]);
            }
            ImGui::Separator();
            ImGui::MenuItem(ICON_GRID_3X3 "  Tel kafes bindir", nullptr, &m_s.wireOverlay,
                            m_rasterOk && !isRasterMode(m_s.viewMode));
            ui::Tooltip("Render ve Kil görüntüsünün üstüne üçgen ağını çizer");
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine(0, 10);
    if (ui::IconButton(ICON_MOUSE_POINTER_2, "Seç (Q)", m_s.gizmoOp == 0, btn)) m_s.gizmoOp = 0;
    ImGui::SameLine();
    if (ui::IconButton(ICON_MOVE, "Taşı (W)", m_s.gizmoOp == 1, btn)) m_s.gizmoOp = 1;
    ImGui::SameLine();
    if (ui::IconButton(ICON_ROTATE_3D, "Döndür (E)", m_s.gizmoOp == 2, btn)) m_s.gizmoOp = 2;
    ImGui::SameLine();
    if (ui::IconButton(ICON_SCALE_3D, "Ölçekle (R)", m_s.gizmoOp == 3, btn)) m_s.gizmoOp = 3;
    ImGui::SameLine(0, 10);
    if (ui::IconButton(m_s.gizmoLocal ? ICON_LOCATE_FIXED : ICON_EARTH,
                       m_s.gizmoLocal ? "Eksenler: nesnenin kendi eksenleri (tıkla: dünya)" : "Eksenler: dünya (tıkla: nesnenin kendi eksenleri)",
                       m_s.gizmoLocal, btn))
        m_s.gizmoLocal = !m_s.gizmoLocal;
    ImGui::SameLine();
    if (ui::IconButton(ICON_MAGNET, m_s.snap ? "Adımlı hareket açık (Ctrl basılıyken serbest)" : "Adımlı hareket (Ctrl basılı tutarak da olur)",
                       m_s.snap, btn))
        m_s.snap = !m_s.snap;
    ImGui::SameLine(0, 10);
    if (ui::IconButton(ICON_FOCUS, "Seçime odaklan (F)", false, btn)) frameSelection();
    ImGui::SameLine();
    if (ui::IconButton(ICON_CAMERA, "Kamera açıları", false, btn)) ImGui::OpenPopup("campresets");
    if (ImGui::BeginPopup("campresets")) {
        if (ImGui::MenuItem("Ön", "1")) applyCameraPreset("front");
        if (ImGui::MenuItem("Yan", "2")) applyCameraPreset("side");
        if (ImGui::MenuItem("Üst", "3")) applyCameraPreset("top");
        if (ImGui::MenuItem("Dörtte üç", "4")) applyCameraPreset("three_quarter");
        if (ImGui::MenuItem("Alçak açı")) applyCameraPreset("low");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ui::IconButton(ICON_ORBIT, "Turntable önizleme (T)", m_s.camera.turntable, btn)) m_s.camera.turntable = !m_s.camera.turntable;
    if (!isRasterMode(m_s.viewMode)) {
        ImGui::SameLine(0, 10);
        if (ui::IconButton(ICON_WAND_SPARKLES, denoiseAvailable() ? "Gürültü giderme (OIDN)" : "OIDN bulunamadı", m_s.denoise, btn))
            m_s.denoise = !m_s.denoise;
        ImGui::SameLine();
        if (ui::IconButton(ICON_REFRESH_CW, "Önizlemeyi yeniden başlat (F5)", false, btn)) m_s.viewport.restart(false);
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    {
        const ImVec2 gMax = ImGui::GetItemRectMax();
        dl->ChannelsSetCurrent(0);
        const ImVec2 bMax(gMax.x + 5.0f, gMax.y + 4.0f);
        dl->AddRectFilled(tb, bMax, ui::col(pal.bg1, 0.9f), 8.0f);
        dl->AddRect(tb, bMax, ui::col(pal.border), 8.0f);
        dl->ChannelsMerge();
    }

    // ── Sağ üst: ilerleme rozeti (GPU modunda: mod adı ve üçgen sayısı) ──
    const ViewportStats st = m_s.viewport.stats();
    char line1[64], line2[96];
    if (isRasterMode(m_s.viewMode)) {
        std::snprintf(line1, sizeof(line1), "%s", viewModeName(m_s.viewMode));
        std::snprintf(line2, sizeof(line2), "GPU  •  %s üçgen", ui::formatCount(m_rasterTris).c_str());
    } else {
        if (st.targetSpp > 0) std::snprintf(line1, sizeof(line1), "%d / %d örnek", st.spp, st.targetSpp);
        else std::snprintf(line1, sizeof(line1), "%d örnek", st.spp);
        std::snprintf(line2, sizeof(line2), "%s%s%s", ui::formatDuration(st.seconds).c_str(),
                      st.interactive ? "  •  hareket" : (st.denoised ? "  •  OIDN" : ""),
                      m_s.viewMode == ViewMode::Clay ? "  •  kil" : "");
    }
    const ImVec2 t1 = ImGui::CalcTextSize(line1);
    const ImVec2 t2 = ImGui::CalcTextSize(line2);
    const float boxW = std::max(t1.x, t2.x) + 44.0f;
    const float boxH = t1.y + t2.y + 12.0f;
    const ImVec2 bp(origin.x + size.x - boxW - 10.0f, origin.y + 10.0f);
    dl->AddRectFilled(bp, ImVec2(bp.x + boxW, bp.y + boxH), ui::col(pal.bg1, 0.9f), 8.0f);
    dl->AddRect(bp, ImVec2(bp.x + boxW, bp.y + boxH), ui::col(pal.border), 8.0f);
    // İlerleme halkası: hedef varsa doluluk, sınırsızsa dönen yay; GPU modunda dolu yeşil daire.
    const ImVec2 rc(bp.x + 17.0f, bp.y + boxH * 0.5f);
    const float rr = 7.5f;
    dl->AddCircle(rc, rr, ui::col(pal.bg3), 32, 2.5f);
    if (isRasterMode(m_s.viewMode)) {
        dl->AddCircleFilled(rc, rr - 2.0f, ui::col(pal.success), 24);
    } else if (st.targetSpp > 0) {
        const float f = std::clamp(static_cast<float>(st.spp) / static_cast<float>(st.targetSpp), 0.0f, 1.0f);
        dl->PathArcTo(rc, rr, -PI * 0.5f, -PI * 0.5f + TWO_PI * f, 32);
        dl->PathStroke(ui::col(st.converged ? pal.success : pal.accent), 0, 3.0f);
    } else {
        const float a = static_cast<float>(ImGui::GetTime()) * 4.0f;
        dl->PathArcTo(rc, rr, a, a + PI * 0.6f, 16);
        dl->PathStroke(ui::col(pal.accent), 0, 3.0f);
    }
    dl->AddText(ImVec2(bp.x + 32.0f, bp.y + 5.0f), ui::col(pal.text), line1);
    dl->AddText(ImVec2(bp.x + 32.0f, bp.y + 7.0f + t1.y), ui::col(pal.textDim), line2);

    // Odak seçme modu ipucu.
    if (m_s.pickFocus) {
        const char* msg = ICON_CROSSHAIR "  Odaklanacak noktaya tıklayın";
        const ImVec2 ms = ImGui::CalcTextSize(msg);
        const ImVec2 mp(origin.x + (size.x - ms.x) * 0.5f, origin.y + 18.0f);
        dl->AddRectFilled(ImVec2(mp.x - 12, mp.y - 6), ImVec2(mp.x + ms.x + 12, mp.y + ms.y + 6), ui::col(pal.accent), 8.0f);
        dl->AddText(mp, IM_COL32(20, 16, 12, 255), msg);
    }
}

void Application::drawWelcome(const ImVec2& origin, const ImVec2& size) {
    const ui::Palette& pal = ui::palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = std::min(460.0f, size.x - 40.0f);
    const float h = 300.0f;
    const ImVec2 p(origin.x + (size.x - w) * 0.5f, origin.y + (size.y - h) * 0.5f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ui::col(pal.bg1, 0.94f), 14.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ui::col(pal.border), 14.0f);

    // Kesikli bırakma alanı.
    const ImVec2 dz0(p.x + 24, p.y + 24), dz1(p.x + w - 24, p.y + 150);
    for (float x = dz0.x; x < dz1.x; x += 12.0f) {
        dl->AddLine(ImVec2(x, dz0.y), ImVec2(std::min(x + 6.0f, dz1.x), dz0.y), ui::col(pal.textFaint), 1.0f);
        dl->AddLine(ImVec2(x, dz1.y), ImVec2(std::min(x + 6.0f, dz1.x), dz1.y), ui::col(pal.textFaint), 1.0f);
    }
    for (float y = dz0.y; y < dz1.y; y += 12.0f) {
        dl->AddLine(ImVec2(dz0.x, y), ImVec2(dz0.x, std::min(y + 6.0f, dz1.y)), ui::col(pal.textFaint), 1.0f);
        dl->AddLine(ImVec2(dz1.x, y), ImVec2(dz1.x, std::min(y + 6.0f, dz1.y)), ui::col(pal.textFaint), 1.0f);
    }
    ImGui::PushFont(ui::fonts().semibold, ui::fonts().baseSize * 2.0f);
    const ImVec2 is = ImGui::CalcTextSize(ICON_UPLOAD);
    dl->AddText(ImVec2(p.x + (w - is.x) * 0.5f, dz0.y + 22), ui::col(pal.accent), ICON_UPLOAD);
    ImGui::PopFont();
    ImGui::PushFont(ui::fonts().semibold, ui::fonts().baseSize * 1.2f);
    const char* title = "Modelinizi buraya bırakın";
    const ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, dz0.y + 72), ui::col(pal.text), title);
    ImGui::PopFont();
    const char* sub = "OBJ  •  glTF  •  GLB      HDR / EXR ortamlar";
    const ImVec2 ss = ImGui::CalcTextSize(sub);
    dl->AddText(ImVec2(p.x + (w - ss.x) * 0.5f, dz0.y + 100), ui::col(pal.textDim), sub);

    const float bw = (w - 48.0f - 16.0f) / 3.0f;
    ImGui::SetCursorScreenPos(ImVec2(p.x + 24, p.y + 176));
    if (ui::PrimaryButton(ICON_BOX "  İçe aktar", ImVec2(bw, 38))) openModelDialog();
    ImGui::SameLine(0, 8);
    if (ui::GhostButton(ICON_SPARKLES "  Örnek sahne", ImVec2(bw, 38))) loadSampleScene();
    ImGui::SameLine(0, 8);
    if (ui::GhostButton(ICON_FOLDER_OPEN "  Proje aç", ImVec2(bw, 38))) openProjectDialog();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 24, p.y + 232));
    ImGui::PushTextWrapPos(p.x + w - 24.0f - ImGui::GetWindowPos().x); // pencereye göreli
    ImGui::TextColored(pal.textFaint, "İpucu: Kütüphaneden bir malzemeyi parçanın üzerine sürükleyin, "
                                      "ortamı değiştirmek için bir HDRI'a tıklayın, Ctrl+P ile render alın.");
    ImGui::PopTextWrapPos();
}

} // namespace photon
