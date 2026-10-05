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
    if (m_viewHasAlpha && m_checkerTex) {
        dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_checkerTex)), origin, end, ImVec2(0, 0),
                     ImVec2(size.x / 16.0f, size.y / 16.0f));
    }
    if (m_viewTex) {
        dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_viewTex)), origin, end);
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
                m_s.camera.zoom(io.MouseWheel);
            }
        }
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
                SceneNode* n = m_s.graph.findByUid(uid);
                if (n && n->parent && n->parent != m_s.graph.root() && m_s.selUid != uid &&
                    m_s.selUid != n->parent->uid)
                    selectNode(n->parent->uid);
                else
                    selectNode(uid);
            }
        }
        if (ImGui::BeginPopupContextItem("##vpctx")) {
            if (ImGui::MenuItem(ICON_FOCUS "  Seçime odaklan", "F", false, m_s.selKind == SelectionKind::Node)) frameSelection();
            if (ImGui::MenuItem(ICON_MAXIMIZE "  Tümünü kadrajla", "Shift+A")) frameAll();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D", false, m_s.selKind == SelectionKind::Node)) duplicateSelection();
            if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del", false, m_s.selKind == SelectionKind::Node)) deleteSelection();
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
void Application::drawSelectionOutline(const ImVec2& origin, const ImVec2& size) {
    SceneNode* n = selectedNode();
    if (!n) return;
    const AABB b = SceneGraph::nodeWorldBounds(*n);
    if (b.pMin.x > b.pMax.x) return;
    const ViewProj vp = viewProj(m_s.camera, size.x / size.y);
    Vec3f c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = Vec3f(i & 1 ? b.pMax.x : b.pMin.x, i & 2 ? b.pMax.y : b.pMin.y, i & 4 ? b.pMax.z : b.pMin.z);
    ImVec2 s[8];
    bool ok[8];
    for (int i = 0; i < 8; ++i) ok[i] = project(vp, c[i], origin, size, s[i]);
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const ImU32 col = ui::col(ui::palette().accent, 0.75f);
    for (const auto& e : edges)
        if (ok[e[0]] && ok[e[1]]) dl->AddLine(s[e[0]], s[e[1]], col, 1.25f);
    // Ad etiketi: kutunun ekrandaki en üst noktasının üstünde.
    float top = 1e9f, left = 1e9f;
    for (int i = 0; i < 8; ++i)
        if (ok[i]) {
            top = std::min(top, s[i].y);
            left = std::min(left, s[i].x);
        }
    if (top < 1e8f) {
        const ImVec2 ts = ImGui::CalcTextSize(n->name.c_str());
        const ImVec2 p(left, top - ts.y - 10.0f);
        dl->AddRectFilled(ImVec2(p.x - 6, p.y - 3), ImVec2(p.x + ts.x + 6, p.y + ts.y + 3), ui::col(ui::palette().accent), 5.0f);
        dl->AddText(p, IM_COL32(20, 16, 12, 255), n->name.c_str());
    }
    dl->PopClipRect();
}

// ImGuizmo: seçili düğümün DÜNYA matrisi düzenlenir, sonra ebeveynin tersiyle
// çarpılarak yerel dönüşüme çevrilir: yerel = ebeveyn⁻¹ · dünya.
void Application::drawGizmo(const ImVec2& origin, const ImVec2& size) {
    ImGuizmo::SetOrthographic(m_s.camera.orthographic);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    const ViewProj vp = viewProj(m_s.camera, size.x / size.y);
    float view[16], proj[16];
    toColumnMajor(vp.view, view);
    toColumnMajor(vp.proj, proj);

    static bool wasUsing = false;
    SceneNode* n = selectedNode();
    if (n && m_s.gizmoOp > 0) {
        float model[16];
        toColumnMajor(n->worldTransform().matrix(), model);
        const ImGuizmo::OPERATION op = m_s.gizmoOp == 1 ? ImGuizmo::TRANSLATE
                                     : m_s.gizmoOp == 2 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
        ImGuizmo::SetID(static_cast<int>(n->uid & 0x7fffffff));
        if (ImGuizmo::Manipulate(view, proj, op, op == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD, model)) {
            if (!wasUsing) pushUndo(); // sürüklemenin başında tek bir geri al adımı
            Transform parentWorld = n->parent ? n->parent->worldTransform() : Transform{};
            n->localTransform = Transform(parentWorld.inverseMatrix() * fromColumnMajor(model));
            markDocumentChanged(true);
        }
    }
    wasUsing = ImGuizmo::IsUsing();

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

void Application::drawViewportOverlay(const ImVec2& origin, const ImVec2& size) {
    const ui::Palette& pal = ui::palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Sol üst: araç çubuğu ──
    const float btn = ImGui::GetFrameHeight() + 4.0f;
    const ImVec2 tb(origin.x + 12.0f, origin.y + 12.0f);
    const int count = 9;
    const float tbW = btn * static_cast<float>(count) + 4.0f * static_cast<float>(count - 1) + 16.0f + 2 * 10.0f;
    dl->AddRectFilled(tb, ImVec2(tb.x + tbW, tb.y + btn + 8.0f), ui::col(pal.bg1, 0.88f), 10.0f);
    dl->AddRect(tb, ImVec2(tb.x + tbW, tb.y + btn + 8.0f), ui::col(pal.border), 10.0f);
    ImGui::SetCursorScreenPos(ImVec2(tb.x + 8.0f, tb.y + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    if (ui::IconButton(ICON_MOUSE_POINTER_2, "Seç (Q)", m_s.gizmoOp == 0, btn)) m_s.gizmoOp = 0;
    ImGui::SameLine();
    if (ui::IconButton(ICON_MOVE, "Taşı (W)", m_s.gizmoOp == 1, btn)) m_s.gizmoOp = 1;
    ImGui::SameLine();
    if (ui::IconButton(ICON_ROTATE_3D, "Döndür (E)", m_s.gizmoOp == 2, btn)) m_s.gizmoOp = 2;
    ImGui::SameLine();
    if (ui::IconButton(ICON_SCALE_3D, "Ölçekle (R)", m_s.gizmoOp == 3, btn)) m_s.gizmoOp = 3;
    ImGui::SameLine(0, 14);
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
    ImGui::SameLine(0, 14);
    if (ui::IconButton(ICON_WAND_SPARKLES, denoiseAvailable() ? "Gürültü giderme (OIDN)" : "OIDN bulunamadı", m_s.denoise, btn))
        m_s.denoise = !m_s.denoise;
    ImGui::SameLine();
    if (ui::IconButton(ICON_REFRESH_CW, "Önizlemeyi yeniden başlat (F5)", false, btn)) m_s.viewport.restart(false);
    ImGui::PopStyleVar();

    // ── Sağ üst: ilerleme rozeti ──
    const ViewportStats st = m_s.viewport.stats();
    char line1[64], line2[96];
    if (st.targetSpp > 0) std::snprintf(line1, sizeof(line1), "%d / %d örnek", st.spp, st.targetSpp);
    else std::snprintf(line1, sizeof(line1), "%d örnek", st.spp);
    std::snprintf(line2, sizeof(line2), "%s%s", ui::formatDuration(st.seconds).c_str(),
                  st.interactive ? "  •  hareket" : (st.denoised ? "  •  OIDN" : ""));
    const ImVec2 t1 = ImGui::CalcTextSize(line1);
    const ImVec2 t2 = ImGui::CalcTextSize(line2);
    const float boxW = std::max(t1.x, t2.x) + 52.0f;
    const float boxH = t1.y + t2.y + 18.0f;
    const ImVec2 bp(origin.x + size.x - boxW - 12.0f, origin.y + 12.0f);
    dl->AddRectFilled(bp, ImVec2(bp.x + boxW, bp.y + boxH), ui::col(pal.bg1, 0.88f), 10.0f);
    dl->AddRect(bp, ImVec2(bp.x + boxW, bp.y + boxH), ui::col(pal.border), 10.0f);
    // İlerleme halkası: hedef varsa doluluk, sınırsızsa dönen yay.
    const ImVec2 rc(bp.x + 20.0f, bp.y + boxH * 0.5f);
    const float rr = 9.0f;
    dl->AddCircle(rc, rr, ui::col(pal.bg3), 32, 3.0f);
    if (st.targetSpp > 0) {
        const float f = std::clamp(static_cast<float>(st.spp) / static_cast<float>(st.targetSpp), 0.0f, 1.0f);
        dl->PathArcTo(rc, rr, -PI * 0.5f, -PI * 0.5f + TWO_PI * f, 32);
        dl->PathStroke(ui::col(st.converged ? pal.success : pal.accent), 0, 3.0f);
    } else {
        const float a = static_cast<float>(ImGui::GetTime()) * 4.0f;
        dl->PathArcTo(rc, rr, a, a + PI * 0.6f, 16);
        dl->PathStroke(ui::col(pal.accent), 0, 3.0f);
    }
    dl->AddText(ImVec2(bp.x + 38.0f, bp.y + 7.0f), ui::col(pal.text), line1);
    dl->AddText(ImVec2(bp.x + 38.0f, bp.y + 9.0f + t1.y), ui::col(pal.textDim), line2);

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
    ImGui::PushTextWrapPos(p.x + w - 24);
    ImGui::TextColored(pal.textFaint, "İpucu: Kütüphaneden bir malzemeyi parçanın üzerine sürükleyin, "
                                      "ortamı değiştirmek için bir HDRI'a tıklayın, Ctrl+P ile render alın.");
    ImGui::PopTextWrapPos();
}

} // namespace photon
