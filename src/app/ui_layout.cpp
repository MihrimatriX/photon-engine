// ui_layout.cpp — Ana pencere düzeni: menü çubuğu, panel yerleşimi (docking),
// alt durum çubuğu ve kısayollar/hakkında pencereleri.
#include "app/application.h"
#include "ui/theme.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "engine/denoiser.h"

#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <thread>

namespace photon {

// Varsayılan yerleşim: solda kütüphane, ortada viewport, sağda sahne ağacı (üst)
// ve özellikler (alt). Kullanıcı panelleri sürükleyip değiştirebilir; düzen
// %APPDATA%/PhotonEngine/layout_v3.ini'ye kaydedilir.
void Application::buildDefaultLayout(unsigned int dockId) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dockId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.205f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
    ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.34f, nullptr, &right);
    ImGui::DockBuilderDockWindow("###Library", left);
    ImGui::DockBuilderDockWindow("###Viewport", center);
    ImGui::DockBuilderDockWindow("###Scene", rightTop);
    ImGui::DockBuilderDockWindow("###Props", right);
    for (ImGuiID id : {left, center, right, rightTop}) {
        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(id)) n->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    }
    ImGui::DockBuilderFinish(dockId);
}

void Application::drawDockspace() {
    // Kenar çubukları (menü, durum) DockSpace'ten ÖNCE gönderilir ki çalışma alanı küçülsün.
    drawMenuBar();
    drawStatusBar();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiID dockId = ImGui::GetID("PhotonDock_v3");
    if (!m_layoutBuilt) {
        m_layoutBuilt = true;
        if (!ImGui::DockBuilderGetNode(dockId) || !m_opts.screenshotPath.empty()) buildDefaultLayout(dockId);
    }
    ImGui::DockSpaceOverViewport(dockId, vp, ImGuiDockNodeFlags_None);
}

void Application::drawMenuBar() {
    const ui::Palette& pal = ui::palette();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, pal.bg0);
    if (!ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return;
    }
    // Logo: vurgu renginde ikon + kalın yazı.
    ImGui::TextColored(pal.accent, ICON_APERTURE);
    ImGui::SameLine(0, 6);
    ImGui::PushFont(ui::fonts().semibold, 0.0f);
    ImGui::TextUnformatted("Photon");
    ImGui::PopFont();
    ImGui::SameLine(0, 18);

    const bool busy = m_s.finalJob.active.load();
    if (ImGui::BeginMenu("Dosya")) {
        if (ImGui::MenuItem(ICON_FILE_PLUS "  Yeni sahne", "Ctrl+N")) newScene();
        if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Proje aç…", "Ctrl+Shift+O")) openProjectDialog();
        if (ImGui::MenuItem(ICON_SAVE "  Kaydet", "Ctrl+S")) saveProject();
        if (ImGui::MenuItem("      Farklı kaydet…", "Ctrl+Shift+S")) saveProjectAs();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_BOX "  Model içe aktar…", "Ctrl+O")) openModelDialog();
        if (ImGui::MenuItem(ICON_GLOBE "  HDRI ortam aç…")) openHdrDialog();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_IMAGE_DOWN "  Viewport görüntüsünü kaydet…", "Ctrl+E")) exportViewport();
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_SPARKLES "  Örnek sahneler")) {
            if (ImGui::MenuItem("Ürün vitrini")) loadSampleScene();
            if (ImGui::MenuItem("Cornell kutusu")) loadCornellScene();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("      Çıkış", "Alt+F4")) glfwSetWindowShouldClose(m_window, 1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Düzen")) {
        if (ImGui::MenuItem(ICON_UNDO_2 "  Geri al", "Ctrl+Z", false, m_s.undo.canUndo())) undo();
        if (ImGui::MenuItem(ICON_REDO_2 "  Yinele", "Ctrl+Y", false, m_s.undo.canRedo())) redo();
        ImGui::Separator();
        const bool hasSel = m_s.selKind == SelectionKind::Node || m_s.selKind == SelectionKind::Light;
        if (ImGui::MenuItem(ICON_COPY "  Çoğalt", "Ctrl+D", false, m_s.selKind == SelectionKind::Node)) duplicateSelection();
        if (ImGui::MenuItem(ICON_TRASH_2 "  Sil", "Del", false, hasSel)) deleteSelection();
        if (ImGui::MenuItem("      Zemine oturt", "G", false, m_s.selKind == SelectionKind::Node)) placeSelectionOnGround();
        if (ImGui::MenuItem("      Seçimi kaldır", "Esc", false, hasSel)) clearSelection();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ekle")) {
        if (ImGui::MenuItem(ICON_LAMP_CEILING "  Alan ışığı (softbox)")) addLight(LightDesc::Type::Area);
        if (ImGui::MenuItem(ICON_SUN "  Güneş (yönlü)")) addLight(LightDesc::Type::Directional);
        if (ImGui::MenuItem(ICON_LIGHTBULB "  Nokta ışık")) addLight(LightDesc::Type::Point);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_BOX "  Model…", "Ctrl+O")) openModelDialog();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Kamera")) {
        if (ImGui::MenuItem(ICON_FOCUS "  Seçime odaklan", "F")) frameSelection();
        if (ImGui::MenuItem(ICON_MAXIMIZE "  Tümünü kadrajla", "Shift+A")) frameAll();
        ImGui::Separator();
        if (ImGui::MenuItem("Ön", "1")) applyCameraPreset("front");
        if (ImGui::MenuItem("Yan", "2")) applyCameraPreset("side");
        if (ImGui::MenuItem("Üst", "3")) applyCameraPreset("top");
        if (ImGui::MenuItem("Dörtte üç", "4")) applyCameraPreset("three_quarter");
        if (ImGui::MenuItem("Alçak açı")) applyCameraPreset("low");
        ImGui::Separator();
        ImGui::MenuItem(ICON_ORBIT "  Turntable önizleme", "T", &m_s.camera.turntable);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem(ICON_CLAPPERBOARD "  Render…", "Ctrl+P")) m_s.showRenderDialog = true;
        if (ImGui::MenuItem(ICON_REFRESH_CW "  Önizlemeyi yeniden başlat", "F5")) m_s.viewport.restart(false);
        ImGui::Separator();
        ImGui::MenuItem(ICON_WAND_SPARKLES "  Gürültü giderme (OIDN)", nullptr, &m_s.denoise, denoiseAvailable());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Yardım")) {
        if (ImGui::MenuItem(ICON_KEYBOARD "  Kısayollar", "F1")) m_s.showShortcuts = true;
        if (ImGui::MenuItem(ICON_INFO "  Hakkında")) m_s.showAbout = true;
        ImGui::EndMenu();
    }

    // Sağ köşe: belge adı ve ana eylem (Render).
    const char* label = busy ? ICON_CLAPPERBOARD "  Render sürüyor…" : ICON_CLAPPERBOARD "  Render";
    const float bw = ImGui::CalcTextSize(label).x + 28.0f;
    std::string doc = m_s.projectPath.empty() ? "Adsız proje" : ui::fileName(m_s.projectPath);
    if (m_s.documentDirty) doc += "  •";
    const float dw = ImGui::CalcTextSize(doc.c_str()).x;
    const float right = ImGui::GetWindowWidth() - bw - 14.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 20.0f, right - dw - 22.0f));
    ImGui::TextColored(pal.textDim, "%s", doc.c_str());
    ImGui::SameLine(right);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 5));
    if (ui::PrimaryButton(label, ImVec2(bw, 0))) m_s.showRenderDialog = true;
    ImGui::PopStyleVar();
    ui::Tooltip("Son render'ı dosyaya al (Ctrl+P)");

    ImGui::EndMainMenuBar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Application::drawStatusBar() {
    const ui::Palette& pal = ui::palette();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + 4.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14, 4));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##statusbar", vp, ImGuiDir_Down, h, flags)) {
        if (ImGui::BeginMenuBar()) {
            // Sol: durum mesajı.
            if (!m_s.statusText.empty()) {
                ImGui::TextColored(m_s.statusError ? pal.danger : pal.success,
                                   m_s.statusError ? ICON_TRIANGLE_ALERT : ICON_CHECK);
                ImGui::TextUnformatted(m_s.statusText.c_str());
            } else if (!m_jobs.empty()) {
                const char* spin = "|/-\\";
                ImGui::TextColored(pal.accent, "%c", spin[static_cast<int>(ImGui::GetTime() * 8.0) % 4]);
                ImGui::TextUnformatted(m_jobs.front().label.c_str());
            } else if (m_s.finalJob.active) {
                ImGui::TextColored(pal.accent, ICON_CLAPPERBOARD);
                if (m_s.finalJob.frames > 0)
                    ImGui::Text("Turntable: kare %d / %d", m_s.finalJob.frame.load(), m_s.finalJob.frames.load());
                else
                    ImGui::Text("Son render: %d / %d örnek", m_s.finalJob.spp.load(), m_s.finalJob.targetSpp.load());
            } else {
                ImGui::TextColored(pal.textFaint, ICON_MOUSE_POINTER_2);
                ImGui::TextColored(pal.textFaint, "Sol: döndür   Orta/Sağ: kaydır   Tekerlek: yakınlaş   Çift tık: pivot   F: odakla");
            }

            // Sağ: istatistikler.
            const ViewportStats st = m_s.viewport.stats();
            char buf[256];
            const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
            std::snprintf(buf, sizeof(buf), "%d × %d    %d%s örnek    %s    %u iş parçacığı    %.0f fps",
                          st.width, st.height, st.spp,
                          st.targetSpp > 0 ? (" / " + std::to_string(st.targetSpp)).c_str() : "",
                          ui::formatDuration(st.seconds).c_str(), threads, m_s.fps);
            const float tw = ImGui::CalcTextSize(buf).x;
            const float badgeW = denoiseAvailable() ? 64.0f : 0.0f;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - tw - badgeW - 24.0f);
            if (denoiseAvailable()) {
                ui::Badge(m_s.denoise ? "OIDN" : "OIDN kapalı", m_s.denoise ? pal.success : pal.textFaint);
                ImGui::SameLine();
            }
            ImGui::TextColored(pal.textDim, "%s", buf);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void Application::drawAboutWindows() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (m_s.showShortcuts) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
        if (ImGui::Begin(ICON_KEYBOARD "  Kısayollar###shortcuts", &m_s.showShortcuts,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
            struct Row { const char* key; const char* what; };
            const Row rows[] = {
                {"Sol fare", "Kamerayı döndür (orbit)"}, {"Orta / sağ fare", "Kaydır (pan)"},
                {"Tekerlek", "Yakınlaş / uzaklaş"}, {"Tık", "Parça seç"}, {"Çift tık", "Döndürme merkezini buraya al"},
                {"F", "Seçime odaklan"}, {"Shift+A", "Tümünü kadrajla"}, {"1 / 2 / 3 / 4", "Ön / yan / üst / dörtte üç"},
                {"Q / W / E / R", "Seç / taşı / döndür / ölçekle"}, {"G", "Seçimi zemine oturt"},
                {"T", "Turntable önizleme"}, {"Del", "Sil"}, {"Ctrl+D", "Çoğalt"},
                {"Ctrl+C / Ctrl+V", "Malzemeyi kopyala / yapıştır"}, {"Ctrl + tekerlek", "Odak uzaklığı (zoom lens)"},
                {"Ctrl+Z / Ctrl+Y", "Geri al / yinele"}, {"Ctrl+O", "Model içe aktar"},
                {"Ctrl+S", "Projeyi kaydet"}, {"Ctrl+P", "Render"}, {"Ctrl+E", "Viewport görüntüsünü kaydet"},
                {"F5", "Önizlemeyi yeniden başlat"},
            };
            if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                for (const Row& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushFont(ui::fonts().semibold, 0.0f);
                    ImGui::TextUnformatted(r.key);
                    ImGui::PopFont();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ui::palette().textDim, "%s", r.what);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    if (m_s.showAbout) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(ICON_APERTURE "  PhotonEngine hakkında###about", &m_s.showAbout,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(ui::fonts().semibold, ui::fonts().baseSize * 1.6f);
            ImGui::TextUnformatted("PhotonEngine");
            ImGui::PopFont();
            ImGui::TextColored(ui::palette().textDim, "Fiziksel tabanlı ürün görselleştirme");
            ImGui::Separator();
            ImGui::BulletText("Yol izleme (path tracing): NEE + MIS, Owen-Sobol örnekleme");
            ImGui::BulletText("Malzemeler: Disney Principled, GGX/VNDF cam");
            ImGui::BulletText("Gürültü giderme: Intel Open Image Denoise %s", denoiseAvailable() ? "(etkin)" : "(yok)");
            ImGui::BulletText("Ton eşleme: AgX, Khronos PBR Nötr, ACES");
            ImGui::Spacing();
            ImGui::TextColored(ui::palette().textFaint, "Üçüncü taraf lisansları: docs/THIRD_PARTY.md");
        }
        ImGui::End();
    }
}

} // namespace photon
