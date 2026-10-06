// ui_render.cpp — "Render" penceresi: çıktı çözünürlüğü, kalite, biçim, dosya yolu,
// canlı ilerleme önizlemesi ve turntable kare dizisi.
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "ui/file_dialog.h"
#include "engine/denoiser.h"
#include "core/color/transfer.h"
#include "core/platform/path.h"

#include <GLFW/glfw3.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace photon {

namespace {

struct ResPreset { const char* name; int w; int h; };
const ResPreset kPresets[] = {
    {"HD 1280 × 720", 1280, 720},       {"Full HD 1920 × 1080", 1920, 1080}, {"QHD 2560 × 1440", 2560, 1440},
    {"4K UHD 3840 × 2160", 3840, 2160}, {"Kare 2048 × 2048", 2048, 2048},    {"Dikey 1080 × 1350", 1080, 1350},
    {"A4 300 dpi 3508 × 2480", 3508, 2480},
};

// Önizleme dokusu (UI thread'inde yaşar).
unsigned int gPreviewTex = 0;
int gPreviewW = 0, gPreviewH = 0;

void uploadPreview(const Image& img, ToneMapOperator tmo, float ev) {
    // Büyük çıktıyı önizleme için en fazla 1024 piksele küçült (en yakın komşu).
    const int maxSide = 1024;
    const float s = std::min(1.0f, static_cast<float>(maxSide) / static_cast<float>(std::max(img.width(), img.height())));
    const int w = std::max(1, static_cast<int>(static_cast<float>(img.width()) * s));
    const int h = std::max(1, static_cast<int>(static_cast<float>(img.height()) * s));
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(img.width() - 1, static_cast<int>(static_cast<float>(x) / s));
            const int sy = std::min(img.height() - 1, static_cast<int>(static_cast<float>(y) / s));
            const Color3f d = toneMap(img.getPixel(sx, sy), tmo, ev);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            rgba[i] = quantizeUnorm8(d.r);
            rgba[i + 1] = quantizeUnorm8(d.g);
            rgba[i + 2] = quantizeUnorm8(d.b);
            rgba[i + 3] = 255;
        }
    if (!gPreviewTex) {
        glGenTextures(1, &gPreviewTex);
        glBindTexture(GL_TEXTURE_2D, gPreviewTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, gPreviewTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    gPreviewW = w;
    gPreviewH = h;
}

} // namespace

void Application::drawRenderDialog() {
    FinalRenderJob& job = m_s.finalJob;
    // Biten işin son karesini her durumda yükle (pencere kapalı olsa bile).
    {
        std::shared_ptr<const Image> prev;
        {
            std::lock_guard<std::mutex> lk(job.mutex);
            if (job.previewNew) {
                prev = job.preview;
                job.previewNew = false;
            }
        }
        if (prev) uploadPreview(*prev, m_s.settings.tmo, m_s.settings.exposure);
    }
    if (!m_s.showRenderDialog) return;

    const ui::Palette& pal = ui::palette();
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(980, 600), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.bg1);
    const bool open = ImGui::Begin(ICON_CLAPPERBOARD "  Render###renderdlg", &m_s.showRenderDialog,
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (!open) {
        ImGui::End();
        return;
    }
    const bool busy = job.active.load();
    OutputSettings& out = m_s.output;

    // ── Sol sütun: ayarlar ──
    ImGui::BeginChild("settings", ImVec2(360, 0), ImGuiChildFlags_None);
    ImGui::BeginDisabled(busy);
    if (ui::Section(ICON_RATIO, "Çözünürlük")) {
        int current = -1;
        for (int i = 0; i < static_cast<int>(std::size(kPresets)); ++i)
            if (kPresets[i].w == out.width && kPresets[i].h == out.height) current = i;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##res", current >= 0 ? kPresets[current].name : "Özel")) {
            for (int i = 0; i < static_cast<int>(std::size(kPresets)); ++i)
                if (ImGui::Selectable(kPresets[i].name, i == current)) {
                    out.width = kPresets[i].w;
                    out.height = kPresets[i].h;
                }
            ImGui::Separator();
            if (ImGui::Selectable("Viewport oranı") && m_s.viewportPxH > 0) {
                out.height = static_cast<int>(std::lround(out.width * static_cast<double>(m_s.viewportPxH) / m_s.viewportPxW));
            }
            ImGui::EndCombo();
        }
        if (ui::BeginProps("res")) {
            ui::Prop("Genişlik");
            ImGui::InputInt("##w", &out.width, 16, 256);
            ui::Prop("Yükseklik");
            ImGui::InputInt("##h", &out.height, 16, 256);
            out.width = std::clamp(out.width, 16, 16384);
            out.height = std::clamp(out.height, 16, 16384);
            ui::EndProps();
        }
        const float vpAspect = m_s.viewportPxH > 0 ? static_cast<float>(m_s.viewportPxW) / m_s.viewportPxH : 1.0f;
        const float outAspect = static_cast<float>(out.width) / static_cast<float>(out.height);
        if (std::abs(vpAspect - outAspect) > 0.02f)
            ui::Hint("Çıktı oranı viewport'tan farklı: kadraj dikey görüş açısına göre korunur, yanlar değişir.");
    }
    if (ui::Section(ICON_GAUGE, "Kalite")) {
        if (ui::BeginProps("q")) {
            ui::Prop("Örnek sayısı", "Piksel başına ışın sayısı. Daha çok = daha az gürültü, daha uzun süre");
            ui::SliderI("##spp", &out.spp, 8, 8192, "%d", ImGuiSliderFlags_Logarithmic);
            ui::Prop("Süre sınırı", "0 = yok. Dolunca o ana dek olan örneklerle bitirir");
            ui::SliderF("##tl", &out.maxSeconds, 0.0f, 3600.0f, out.maxSeconds > 0 ? "%.0f sn" : "yok",
                               ImGuiSliderFlags_Logarithmic);
            ui::Prop("Gürültü giderme");
            ImGui::BeginDisabled(!denoiseAvailable());
            ui::Toggle("odn", &out.denoise);
            ImGui::EndDisabled();
            ui::Prop("Sekme sayısı");
            ui::SliderI("##mb", &m_s.settings.maxBounces, 1, 32);
            ui::EndProps();
        }
        const double mp = static_cast<double>(out.width) * out.height / 1e6;
        const ViewportStats st = m_s.viewport.stats();
        if (st.passMs > 0 && st.width > 0 && !st.interactive) {
            // Tahmin: viewport'un ölçülen pass süresinden piksel oranıyla.
            const double perPassMs = st.passMs * (static_cast<double>(out.width) * out.height) /
                                     (static_cast<double>(st.width) * st.height);
            double secs = perPassMs * out.spp / 1000.0;
            if (out.maxSeconds > 0) secs = std::min(secs, static_cast<double>(out.maxSeconds));
            ImGui::TextColored(pal.textDim, ICON_TIMER "  Tahmini süre: ~%s  (%.1f MP)", ui::formatDuration(secs).c_str(), mp);
        }
    }
    if (ui::Section(ICON_SAVE, "Çıktı")) {
        int fmt = out.format;
        const char* fmts[] = {"PNG", "JPEG", "EXR"};
        if (ui::Segmented("fmt", &fmt, fmts, 3)) {
            out.format = fmt;
            if (!out.path.empty()) {
                std::filesystem::path p = pathFromUtf8(out.path);
                p.replace_extension(fmt == 1 ? ".jpg" : fmt == 2 ? ".exr" : ".png");
                out.path = pathToUtf8(p);
            }
        }
        ImGui::Spacing();
        bool transparent = m_s.environment.background.mode == Background::Mode::Transparent;
        if (ui::BeginProps("o")) {
            ui::Prop("Şeffaf arka plan", "Yalnız PNG: arka plan alfa = 0");
            ImGui::BeginDisabled(out.format != 0);
            if (ui::Toggle("tr", &transparent)) {
                pushUndo();
                m_s.environment.background.mode = transparent ? Background::Mode::Transparent : Background::Mode::Environment;
                markDocumentChanged();
            }
            ImGui::EndDisabled();
            ui::EndProps();
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6);
        ImGui::InputTextWithHint("##path", "Resimler/PhotonEngine/render_….png", &out.path);
        ImGui::SameLine(0, 6);
        if (ui::IconButton(ICON_FOLDER_OPEN, "Gözat…")) {
            std::string p = out.path.empty() ? "render.png" : out.path;
            FileDialogFilter f[] = {{"PNG", "*.png"}, {"JPEG", "*.jpg"}, {"OpenEXR", "*.exr"}};
            FileDialogFilter ordered[3] = {f[out.format], f[(out.format + 1) % 3], f[(out.format + 2) % 3]};
            if (showFileDialog(p, FileDialogMode::Save, "Render çıktısı", ordered, 3)) out.path = p;
        }
        ui::Hint("Boş bırakılırsa tarih-saatli bir adla Resimler/PhotonEngine klasörüne kaydedilir.");
    }
    if (ui::Section(ICON_ORBIT, "Turntable", false)) {
        if (ui::BeginProps("tt")) {
            ui::Prop("Kare sayısı");
            ui::SliderI("##ttf", &out.turntableFrames, 8, 360);
            ui::Prop("Örnek / kare");
            ui::SliderI("##tts", &out.turntableSpp, 4, 512, "%d", ImGuiSliderFlags_Logarithmic);
            ui::EndProps();
        }
        if (ui::GhostButton(ICON_FILM "  Turntable kareleri üret", ImVec2(-FLT_MIN, 0))) startTurntable();
        ui::Hint("Kareler PNG olarak bir klasöre yazılır. Video: ffmpeg -framerate 30 -i frame_%04d.png out.mp4");
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine(0, 18);

    // ── Sağ sütun: önizleme ve ilerleme ──
    ImGui::BeginGroup();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float footer = ImGui::GetFrameHeight() * 2.0f + 50.0f;
    const ImVec2 box(avail.x, std::max(120.0f, avail.y - footer));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + box.x, p.y + box.y), ui::col(pal.bg0), 10.0f);
    const float aspect = static_cast<float>(out.width) / static_cast<float>(out.height);
    float iw = box.x - 24.0f, ih = iw / aspect;
    if (ih > box.y - 24.0f) {
        ih = box.y - 24.0f;
        iw = ih * aspect;
    }
    const ImVec2 i0(p.x + (box.x - iw) * 0.5f, p.y + (box.y - ih) * 0.5f);
    if (gPreviewTex && (busy || job.succeeded || job.spp > 0)) {
        dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(gPreviewTex)), i0, ImVec2(i0.x + iw, i0.y + ih));
    } else {
        dl->AddRect(i0, ImVec2(i0.x + iw, i0.y + ih), ui::col(pal.border), 6.0f, 0, 1.0f);
        const char* msg = "Render başlatıldığında önizleme burada görünür";
        const ImVec2 ms = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p.x + (box.x - ms.x) * 0.5f, p.y + box.y * 0.5f - ms.y * 0.5f), ui::col(pal.textFaint), msg);
    }
    ImGui::Dummy(box);
    ImGui::Spacing();

    // İlerleme çubuğu + durum.
    std::string status;
    std::string outPath;
    double seconds = 0.0;
    bool succeeded = false;
    {
        std::lock_guard<std::mutex> lk(job.mutex);
        status = job.status;
        outPath = job.outputPath;
        seconds = job.seconds;
        succeeded = job.succeeded;
    }
    float frac = 0.0f;
    char overlay[128] = "";
    if (job.frames > 0) {
        frac = static_cast<float>(job.frame.load()) / static_cast<float>(std::max(1, job.frames.load()));
        std::snprintf(overlay, sizeof(overlay), "Kare %d / %d", job.frame.load(), job.frames.load());
    } else if (job.targetSpp > 0) {
        frac = static_cast<float>(job.spp.load()) / static_cast<float>(std::max(1, job.targetSpp.load()));
        std::snprintf(overlay, sizeof(overlay), "%d / %d örnek", job.spp.load(), job.targetSpp.load());
    }
    if (busy && frac > 0.01f && seconds > 0.5) {
        const double eta = seconds * (1.0 - frac) / frac;
        std::snprintf(overlay + std::strlen(overlay), sizeof(overlay) - std::strlen(overlay), "   •   kalan ~%s",
                      ui::formatDuration(eta).c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, succeeded && !busy ? pal.success : pal.accent);
    ImGui::ProgressBar(busy || succeeded ? frac : 0.0f, ImVec2(-FLT_MIN, 0), overlay);
    ImGui::PopStyleColor();
    ImGui::TextColored(pal.textDim, "%s", status.empty() ? "Hazır" : status.c_str());

    const float bw = 170.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail.x - bw * 2.0f - 8.0f);
    if (!busy) {
        if (succeeded && !outPath.empty()) {
            if (ui::GhostButton(ICON_EXTERNAL_LINK "  Klasörde göster", ImVec2(bw, 34))) openInFileBrowser(outPath);
        } else {
            ImGui::Dummy(ImVec2(bw, 34));
        }
        ImGui::SameLine(0, 8);
        if (ui::PrimaryButton(ICON_PLAY "  Render başlat", ImVec2(bw, 34))) startFinalRender();
    } else {
        ImGui::Dummy(ImVec2(bw, 34));
        ImGui::SameLine(0, 8);
        ImGui::PushStyleColor(ImGuiCol_Button, pal.danger);
        if (ImGui::Button(ICON_CIRCLE_STOP "  Durdur ve kaydet", ImVec2(bw, 34))) cancelFinalRender();
        ImGui::PopStyleColor();
        ui::Tooltip("Şu ana kadarki örneklerle bitirir ve kaydeder");
    }
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace photon
