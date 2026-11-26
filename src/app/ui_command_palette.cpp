// ui_command_palette.cpp — Stüdyo Komut Paleti (Ctrl+K): Türkçe bulanık arama ile
// 40+ anında eylem (cihaz değiştirme, GPU modu, görüntü modu, şekil/ışık ekleme,
// Path Guiding, Motion Blur, kamera açıları, dosya ve düzen işlemleri).
#include "app/application.h"
#include "engine/denoiser.h"
#include "engine/render_device.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace photon {

namespace {

/// Türkçe harfleri küçük ASCII karşılığına katlar (İ/I/ı→i, Ğ/ğ→g, Ü/ü→u, Ş/ş→s, Ö/ö→o, Ç/ç→c).
std::string foldTurkish(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c)));
            ++i;
        } else if (i + 1 < s.size()) {
            const unsigned char c2 = static_cast<unsigned char>(s[i + 1]);
            // UTF-8 2-baytlı Türkçe karakterler
            if (c == 0xC4 && (c2 == 0xB0 || c2 == 0xB1)) { out.push_back('i'); i += 2; }      // İ, ı
            else if (c == 0xC4 && (c2 == 0x9E || c2 == 0x9F)) { out.push_back('g'); i += 2; } // Ğ, ğ
            else if (c == 0xC3 && (c2 == 0x9C || c2 == 0xBC)) { out.push_back('u'); i += 2; } // Ü, ü
            else if (c == 0xC5 && (c2 == 0x9E || c2 == 0x9F)) { out.push_back('s'); i += 2; } // Ş, ş
            else if (c == 0xC3 && (c2 == 0x96 || c2 == 0xB6)) { out.push_back('o'); i += 2; } // Ö, ö
            else if (c == 0xC3 && (c2 == 0x87 || c2 == 0xA7)) { out.push_back('c'); i += 2; } // Ç, ç
            else { out.push_back(static_cast<char>(c)); ++i; }
        } else {
            ++i;
        }
    }
    return out;
}

/// Bulanık alt-dizi puanlaması: eşleşmezse -1, eşleşirse pozitif puan (ardışık ve kelime başı bonuslu).
int fuzzyScore(const std::string& queryFolded, const std::string& targetFolded) {
    if (queryFolded.empty()) return 100;
    size_t qi = 0;
    int score = 0;
    int streak = 0;
    for (size_t ti = 0; ti < targetFolded.size() && qi < queryFolded.size(); ++ti) {
        if (targetFolded[ti] == queryFolded[qi]) {
            score += 10 + streak * 6;
            if (ti == 0 || targetFolded[ti - 1] == ' ' || targetFolded[ti - 1] == ':' || targetFolded[ti - 1] == '/')
                score += 15;
            ++streak;
            ++qi;
        } else {
            streak = 0;
        }
    }
    if (qi < queryFolded.size()) return -1;
    // Kısa ve tam eşleşmelere hafif üstünlük
    score += std::max(0, 40 - static_cast<int>(targetFolded.size()));
    return score;
}

struct PaletteCommand {
    std::string category;
    std::string icon;
    std::string title;
    std::string keywords;
    std::string shortcut;
    std::function<void()> run;
};

} // namespace

void Application::drawCommandPalette() {
    if (!m_s.showCommandPalette) return;

    static std::string query;
    static int selected = 0;
    static bool justOpened = true;

    const ui::Palette& pal = ui::palette();
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Arka planı hafif karart (dışarı tıklanınca kapat)
    ImDrawList* bgDl = ImGui::GetForegroundDrawList();
    bgDl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 10, 14, 130));

    // Komut listesini mevcut sahne ve cihazlara göre kur
    std::vector<PaletteCommand> cmds;
    cmds.reserve(56);

    auto add = [&](const char* cat, const char* icon, std::string title, const char* kw, const char* sc,
                   std::function<void()> fn) {
        cmds.push_back({cat, icon, std::move(title), kw, sc, std::move(fn)});
    };

    // ── Cihaz ve GPU yürütme modları ──
    add("Cihaz", ICON_CPU, "Render Cihazı: Otomatik", "device auto gpu cpu", "", [this] {
        m_s.output.device = "auto";
        savePrefs();
        setStatus("Render cihazı: Otomatik");
    });
    for (const DeviceInfo& d : enumerateDevices()) {
        if (!d.usable) continue;
        const std::string id = d.id;
        const std::string label = std::string("Render Cihazı: ") + deviceKindName(d.kind) + " · " + d.name;
        add("Cihaz", d.kind == DeviceKind::Cpu ? ICON_CPU : ICON_GAUGE, label, "device gpu vulkan hybrid cpu", "",
            [this, id, label] {
                m_s.output.device = id;
                savePrefs();
                setStatus(label);
            });
    }
    add("GPU", ICON_SPARKLES, "GPU Modu: Otomatik (Donanım RT tercihli)", "gpu mode auto rt", "", [this] {
        m_s.settings.gpuMode = GpuMode::Auto;
        setStatus("GPU Modu: Otomatik");
    });
    add("GPU", ICON_SPARKLES, "GPU Modu: Donanım RT (VK_KHR_ray_query)", "gpu mode ray query hardware rt", "", [this] {
        m_s.settings.gpuMode = GpuMode::RayQuery;
        setStatus("GPU Modu: Donanım RT");
    });
    add("GPU", ICON_LAYERS, "GPU Modu: Wavefront (4 Aşamalı Kuyruk)", "gpu mode wavefront", "", [this] {
        m_s.settings.gpuMode = GpuMode::Wavefront;
        setStatus("GPU Modu: Wavefront");
    });
    add("GPU", ICON_GAUGE, "GPU Modu: Compute SAH BVH2 (Megakernel)", "gpu mode compute megakernel", "", [this] {
        m_s.settings.gpuMode = GpuMode::Megakernel;
        setStatus("GPU Modu: Compute");
    });

    // ── Görüntü modları ──
    add("Görünüm", ICON_SPARKLES, "Görünüm: Render (Işın İzleme)", "view path trace render", "Z",
        [this] { setViewMode(ViewMode::Render); });
    add("Görünüm", ICON_GAUGE, "Görünüm: Gerçek Zamanlı (OpenGL v2 LTC·GTAO·SSR)", "view realtime gpu ltc ssr", "Z",
        [this] { setViewMode(ViewMode::Realtime); });
    add("Görünüm", ICON_CIRCLE, "Görünüm: Kil (Clay)", "view clay mat", "Z",
        [this] { setViewMode(ViewMode::Clay); });
    add("Görünüm", ICON_CUBOID, "Görünüm: Katı (Solid)", "view solid", "Z",
        [this] { setViewMode(ViewMode::Solid); });
    add("Görünüm", ICON_GRID_3X3, "Görünüm: Tel Kafes (Wireframe)", "view wire wireframe", "Z",
        [this] { setViewMode(ViewMode::Wire); });
    add("Görünüm", ICON_CONTRAST, "Görünüm: Yüzey Normalleri", "view normals", "Z",
        [this] { setViewMode(ViewMode::Normals); });
    add("Görünüm", ICON_GRID_3X3, m_s.wireOverlay ? "Tel Kafes Bindirmeyi Kapat" : "Tel Kafes Bindirmeyi Aç",
        "wire overlay", "", [this] { m_s.wireOverlay = !m_s.wireOverlay; });

    // ── Şekil ve ışık ekleme ──
    add("Ekle", ICON_BOX, "Şekil Ekle: Küp", "add cube primitif", "", [this] { addPrimitive(PrimitiveKind::Cube); });
    add("Ekle", ICON_CIRCLE, "Şekil Ekle: Küre", "add sphere primitif", "", [this] { addPrimitive(PrimitiveKind::Sphere); });
    add("Ekle", ICON_CYLINDER, "Şekil Ekle: Silindir", "add cylinder primitif", "", [this] { addPrimitive(PrimitiveKind::Cylinder); });
    add("Ekle", ICON_CONE, "Şekil Ekle: Koni", "add cone primitif", "", [this] { addPrimitive(PrimitiveKind::Cone); });
    add("Ekle", ICON_TORUS, "Şekil Ekle: Halka (Torus)", "add torus simit", "", [this] { addPrimitive(PrimitiveKind::Torus); });
    add("Ekle", ICON_SQUARE, "Şekil Ekle: Düzlem", "add plane", "", [this] { addPrimitive(PrimitiveKind::Plane); });

    add("Işık", ICON_LAMP_CEILING, "Işık Ekle: Alan Işığı (Softbox)", "add area light softbox", "",
        [this] { addLight(LightDesc::Type::Area); });
    add("Işık", ICON_SUN, "Işık Ekle: Güneş (Yönlü Işık)", "add directional sun light", "",
        [this] { addLight(LightDesc::Type::Directional); });
    add("Işık", ICON_LIGHTBULB, "Işık Ekle: Nokta Işık", "add point light", "",
        [this] { addLight(LightDesc::Type::Point); });
    add("Işık", ICON_FLASHLIGHT, "Işık Ekle: Spot Işık (IES)", "add spot light ies", "",
        [this] { addLight(LightDesc::Type::Spot); });

    // ── Render ve fiziksel özellik anahtarları ──
    add("Render", ICON_CLAPPERBOARD, "Son Render Penceresini Aç", "final render output exr png", "Ctrl+P",
        [this] { m_s.showRenderDialog = true; });
    add("Render", ICON_WAND_SPARKLES,
        m_s.settings.pathGuiding ? "Path Guiding (SD-Tree): Kapat" : "Path Guiding (SD-Tree): Aç",
        "guiding sd-tree indirect variance", "", [this] {
            m_s.settings.pathGuiding = !m_s.settings.pathGuiding;
            setStatus(m_s.settings.pathGuiding ? "SD-Tree Path Guiding açıldı" : "Path Guiding kapatıldı");
        });
    add("Render", ICON_FILM,
        m_s.settings.motionBlur ? "Hareket Bulanıklığı (Motion Blur): Kapat" : "Hareket Bulanıklığı (Motion Blur): Aç",
        "motion blur shutter", "", [this] {
            m_s.settings.motionBlur = !m_s.settings.motionBlur;
            setStatus(m_s.settings.motionBlur ? "Hareket bulanıklığı açıldı" : "Hareket bulanıklığı kapatıldı");
        });
    add("Render", ICON_SPARKLES, m_s.denoise ? "Gürültü Giderme (OIDN): Kapat" : "Gürültü Giderme (OIDN): Aç",
        "denoise oidn", "", [this] { m_s.denoise = !m_s.denoise; });
    add("Render", ICON_SQUARE_DASHED, m_s.environment.groundEnabled ? "Zemini Kapat" : "Zemini Aç",
        "ground floor shadow catcher", "", [this] {
            pushUndo();
            m_s.environment.groundEnabled = !m_s.environment.groundEnabled;
            markDocumentChanged();
        });
    add("Render", ICON_REFRESH_CW, "Viewport Önizlemesini Yeniden Başlat", "restart preview", "F5",
        [this] { m_s.viewport.restart(false); });

    // ── Kamera ──
    add("Kamera", ICON_FOCUS, "Seçime Odaklan", "frame selection zoom", "F", [this] { frameSelection(); });
    add("Kamera", ICON_MAXIMIZE, "Tüm Sahneyi Kadrajla", "frame all fit", "Shift+A", [this] { frameAll(); });
    add("Kamera", ICON_FRAME, "2 Nokta Perspektif (Mimari)", "two point perspective shift", "",
        [this] { applyTwoPointPerspective(); });
    add("Kamera", ICON_ORBIT, m_s.camera.turntable ? "Turntable Önizlemeyi Durdur" : "Turntable Önizlemeyi Başlat",
        "turntable orbit spin", "T", [this] { m_s.camera.turntable = !m_s.camera.turntable; });
    add("Kamera", ICON_FILM, m_s.showTimeline ? "Zaman Çizelgesini Gizle" : "Zaman Çizelgesini Göster",
        "timeline animation keyframe", "Alt+T", [this] { m_s.showTimeline = !m_s.showTimeline; });
    add("Kamera", ICON_CAMERA, "Kamera Açısı: Ön", "camera front", "1", [this] { applyCameraPreset("front"); });
    add("Kamera", ICON_CAMERA, "Kamera Açısı: Yan", "camera side", "2", [this] { applyCameraPreset("side"); });
    add("Kamera", ICON_CAMERA, "Kamera Açısı: Üst", "camera top", "3", [this] { applyCameraPreset("top"); });
    add("Kamera", ICON_CAMERA, "Kamera Açısı: Dörtte Üç", "camera three quarter 3/4", "4",
        [this] { applyCameraPreset("three_quarter"); });

    // ── Düzen ve Dosya ──
    add("Düzen", ICON_UNDO_2, "Geri Al", "undo", "Ctrl+Z", [this] { undo(); });
    add("Düzen", ICON_REDO_2, "Yinele", "redo", "Ctrl+Y", [this] { redo(); });
    add("Düzen", ICON_COPY, "Seçimi Çoğalt", "duplicate", "Ctrl+D", [this] { duplicateSelection(); });
    add("Düzen", ICON_GROUP, "Seçimi Grupla", "group", "Ctrl+G", [this] { groupSelection(); });
    add("Düzen", ICON_MOVE, "Seçimi Zemine Oturt", "ground snap place", "G", [this] { placeSelectionOnGround(); });
    add("Düzen", ICON_EYE, "Tüm Nesneleri Göster", "show all unhide", "Alt+H", [this] { showAllNodes(); });
    add("Dosya", ICON_SAVE, "Projeyi Kaydet", "save project", "Ctrl+S", [this] { saveProject(); });
    add("Dosya", ICON_BOXES, "Paket Olarak Kaydet (.photonpack)", "package archive", "", [this] { savePackageAs(); });
    add("Dosya", ICON_FOLDER_OPEN, "Proje Aç…", "open project", "Ctrl+Shift+O", [this] { openProjectDialog(); });
    add("Dosya", ICON_BOX, "Model İçe Aktar…", "import fbx obj gltf", "Ctrl+O", [this] { openModelDialog(); });
    add("Dosya", ICON_IMAGE_DOWN, "Viewport Görüntüsünü Kaydet…", "export viewport png", "Ctrl+E",
        [this] { exportViewport(); });

    // Süz ve puanla
    const std::string qFold = foldTurkish(query);
    struct Scored { int score; size_t idx; };
    std::vector<Scored> matches;
    matches.reserve(cmds.size());
    for (size_t i = 0; i < cmds.size(); ++i) {
        const std::string hay = foldTurkish(cmds[i].category + " " + cmds[i].title + " " + cmds[i].keywords);
        const int sc = fuzzyScore(qFold, hay);
        if (sc >= 0) matches.push_back({sc, i});
    }
    if (!qFold.empty()) {
        std::stable_sort(matches.begin(), matches.end(), [](const Scored& a, const Scored& b) {
            return a.score > b.score;
        });
    }
    if (matches.empty()) selected = 0;
    else selected = std::clamp(selected, 0, static_cast<int>(matches.size()) - 1);

    const float modalW = std::min(580.0f, vp->Size.x - 40.0f);
    const float modalH = std::min(440.0f, vp->Size.y - 80.0f);
    const ImVec2 modalPos(vp->Pos.x + (vp->Size.x - modalW) * 0.5f, vp->Pos.y + std::max(56.0f, vp->Size.y * 0.14f));
    ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modalW, modalH), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.bg1);
    ImGui::PushStyleColor(ImGuiCol_Border, pal.accent);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    bool executed = false;
    bool closeNow = false;
    std::function<void()> pendingAction;

    if (ImGui::Begin("##CommandPalette", nullptr, flags)) {
        // Arama satırı
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal.accent, ICON_COMMAND);
        ImGui::SameLine(0, 8);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 54.0f);
        if (justOpened) {
            ImGui::SetKeyboardFocusHere();
            justOpened = false;
        }
        if (ImGui::InputTextWithHint("##cpq", "Komut ara… (ör. vulkan, guiding, küre, gerçek zamanlı)", &query)) {
            selected = 0;
        }
        ImGui::SameLine(0, 6);
        ImGui::TextColored(pal.textFaint, "Esc");

        // Klavye gezinimi
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) closeNow = true;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !matches.empty())
            selected = (selected + 1) % static_cast<int>(matches.size());
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !matches.empty())
            selected = (selected + static_cast<int>(matches.size()) - 1) % static_cast<int>(matches.size());
        if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && !matches.empty()) {
            pendingAction = cmds[matches[static_cast<size_t>(selected)].idx].run;
            executed = true;
        }

        // Dışarı tıklayınca kapat
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            closeNow = true;
        }

        ImGui::Separator();
        ImGui::BeginChild("##cplist", ImVec2(0, 0), ImGuiChildFlags_None);
        if (matches.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(pal.textFaint, "  Eşleşen komut bulunamadı.");
        } else {
            for (int row = 0; row < static_cast<int>(matches.size()); ++row) {
                const PaletteCommand& c = cmds[matches[static_cast<size_t>(row)].idx];
                const bool isSel = (row == selected);
                ImGui::PushID(row);
                ImGui::PushStyleColor(ImGuiCol_Header, pal.bg3);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pal.bg2);
                const float rowH = ImGui::GetFrameHeight() + 4.0f;
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##row", isSel, ImGuiSelectableFlags_None, ImVec2(0, rowH))) {
                    pendingAction = c.run;
                    executed = true;
                }
                if (isSel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
                    ImGui::SetScrollHereY(0.5f);
                ImGui::PopStyleColor(2);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float midY = p0.y + (rowH - ImGui::GetFontSize()) * 0.5f;
                // Kategori rozeti
                const ImVec2 catSz = ImGui::CalcTextSize(c.category.c_str());
                const float badgeW = 62.0f;
                dl->AddRectFilled(ImVec2(p0.x + 6.0f, p0.y + 3.0f), ImVec2(p0.x + 6.0f + badgeW, p0.y + rowH - 3.0f),
                                  ui::col(isSel ? pal.accent : pal.bg2, isSel ? 0.22f : 1.0f), 4.0f);
                dl->AddText(ImVec2(p0.x + 6.0f + (badgeW - catSz.x) * 0.5f, midY),
                            ui::col(isSel ? pal.accent : pal.textDim), c.category.c_str());

                // İkon + başlık
                const std::string line = c.icon + "  " + c.title;
                dl->AddText(ImVec2(p0.x + badgeW + 16.0f, midY), ui::col(isSel ? pal.text : pal.textDim), line.c_str());

                // Kısayol ipucu (sağa yaslı)
                if (!c.shortcut.empty()) {
                    const ImVec2 scSz = ImGui::CalcTextSize(c.shortcut.c_str());
                    const float rightX = p0.x + ImGui::GetContentRegionAvail().x - scSz.x - 10.0f;
                    dl->AddText(ImVec2(rightX, midY), ui::col(pal.textFaint), c.shortcut.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (executed || closeNow) {
        m_s.showCommandPalette = false;
        query.clear();
        selected = 0;
        justOpened = true;
    }
    if (executed && pendingAction) pendingAction();
}

} // namespace photon
