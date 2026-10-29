// animation.cpp — Kamera animasyonu: zaman çizelgesi, oynatma ve kare dizisi render'ı.
//
// Zaman çizelgesi viewport'un altında yüzen bir şerittir (Marmoset/D5 gibi): iz seçimi,
// oynat/duraklat, döngü, anahtar ekle/sil, kayıtlı kameralardan tur oluşturma ve animasyonu
// render etme. Şeritte sürüklemek zamanı değiştirir ve kamerayı o ana getirir; oynatırken
// viewport etkileşimli modda (düşük çözünürlük + anlık gürültü giderme) izler, durunca temizlenir.
// Render, her kare için Final modlu bir RenderSession ile PNG dizisi yazar (turntable ile aynı yol).
#include "app/application.h"
#include "app/camera_keys.h"
#include "core/image/imager.h"
#include "core/image/image_io.h"
#include "core/platform/path.h"
#include "engine/denoiser.h"
#include "engine/render_session.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace photon {

namespace fs = std::filesystem;

namespace {

/// Şeridin gösterdiği süre: izin sonundan biraz fazlası (sona yeni anahtar eklenebilsin).
float timelineLength(const CameraTrack* t) {
    const float d = t ? t->duration() : 0.0f;
    return std::max({4.0f, d * 1.25f, d + 1.0f});
}

} // namespace

void Application::applyTrackTime(float t) {
    if (m_s.activeTrack < 0 || m_s.activeTrack >= static_cast<int>(m_s.cameraTracks.size())) return;
    const CameraTrack& track = m_s.cameraTracks[static_cast<size_t>(m_s.activeTrack)];
    if (track.empty()) return;
    m_s.camera = orbitFromKey(track.evaluate(t), m_s.camera);
}

void Application::tickAnimation(float dt) {
    if (!m_s.animPlaying) return;
    if (m_s.activeTrack < 0 || m_s.activeTrack >= static_cast<int>(m_s.cameraTracks.size())) {
        m_s.animPlaying = false;
        return;
    }
    const float d = m_s.cameraTracks[static_cast<size_t>(m_s.activeTrack)].duration();
    m_s.animTime += dt;
    if (m_s.animTime > d) {
        if (m_s.animLoop && d > 0.0f) {
            m_s.animTime = std::fmod(m_s.animTime, d);
        } else {
            m_s.animTime = d;
            m_s.animPlaying = false;
        }
    }
    applyTrackTime(m_s.animTime);
}

void Application::buildTrackFromSavedCameras() {
    if (m_s.savedCameras.size() < 2) {
        setStatus("Tur için en az iki kayıtlı kamera gerekir (Kamera sekmesi → Mevcut görünümü kaydet)", true);
        return;
    }
    CameraTrack t;
    t.name = "Kamera turu " + std::to_string(m_s.cameraTracks.size() + 1);
    float time = 0.0f;
    for (const auto& sc : m_s.savedCameras) {
        t.setKey(keyFromOrbit(sc.cam, time));
        time += 2.0f; // kamera başına 2 sn
    }
    m_s.cameraTracks.push_back(std::move(t));
    m_s.activeTrack = static_cast<int>(m_s.cameraTracks.size()) - 1;
    m_s.animTime = 0.0f;
    m_s.showTimeline = true;
    m_s.documentDirty = true;
    applyTrackTime(0.0f);
    setStatus("Kayıtlı " + std::to_string(m_s.savedCameras.size()) + " kameradan tur oluşturuldu");
}

void Application::drawTimeline(const ImVec2& origin, const ImVec2& size) {
    if (!m_s.showTimeline) return;
    const ui::Palette& pal = ui::palette();
    if (m_s.activeTrack >= static_cast<int>(m_s.cameraTracks.size())) m_s.activeTrack = -1;
    if (m_s.activeTrack < 0 && !m_s.cameraTracks.empty()) m_s.activeTrack = 0;
    CameraTrack* track = m_s.activeTrack >= 0 ? &m_s.cameraTracks[static_cast<size_t>(m_s.activeTrack)] : nullptr;

    const float h = 78.0f;
    const ImVec2 pos(origin.x + 10.0f, origin.y + size.y - h - 10.0f);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(pal.bg1.x, pal.bg1.y, pal.bg1.z, 0.94f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (ImGui::BeginChild("##timeline", ImVec2(size.x - 20.0f, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // ── Üst satır: iz, oynatma, anahtarlar, render ──
        ImGui::SetNextItemWidth(170.0f);
        const char* preview = track ? track->name.c_str() : "İz yok";
        if (ImGui::BeginCombo("##track", preview)) {
            for (int i = 0; i < static_cast<int>(m_s.cameraTracks.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(m_s.cameraTracks[static_cast<size_t>(i)].name.c_str(), i == m_s.activeTrack)) {
                    m_s.activeTrack = i;
                    m_s.animTime = 0.0f;
                    applyTrackTime(0.0f);
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::Selectable(ICON_PLUS "  Yeni animasyon (bu görünümden)")) {
                CameraTrack t;
                t.name = "Kamera animasyonu " + std::to_string(m_s.cameraTracks.size() + 1);
                t.setKey(keyFromOrbit(m_s.camera, 0.0f));
                m_s.cameraTracks.push_back(std::move(t));
                m_s.activeTrack = static_cast<int>(m_s.cameraTracks.size()) - 1;
                m_s.animTime = 0.0f;
                m_s.documentDirty = true;
            }
            if (ImGui::Selectable(ICON_FILM "  Kayıtlı kameralardan tur")) buildTrackFromSavedCameras();
            if (track && ImGui::Selectable(ICON_TRASH_2 "  Bu izi sil")) {
                m_s.cameraTracks.erase(m_s.cameraTracks.begin() + m_s.activeTrack);
                m_s.activeTrack = m_s.cameraTracks.empty() ? -1 : 0;
                m_s.animPlaying = false;
                m_s.documentDirty = true;
            }
            ImGui::EndCombo();
        }
        track = m_s.activeTrack >= 0 ? &m_s.cameraTracks[static_cast<size_t>(m_s.activeTrack)] : nullptr;
        ImGui::SameLine();
        ImGui::BeginDisabled(!track || track->keys.size() < 2);
        if (ui::IconButton(m_s.animPlaying ? ICON_PAUSE : ICON_PLAY, m_s.animPlaying ? "Duraklat (Boşluk)" : "Oynat (Boşluk)")) {
            m_s.animPlaying = !m_s.animPlaying;
            if (m_s.animPlaying && track && m_s.animTime >= track->duration()) m_s.animTime = 0.0f;
        }
        ImGui::SameLine();
        if (ui::IconButton(ICON_REFRESH_CW, "Döngü", m_s.animLoop)) m_s.animLoop = !m_s.animLoop;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!track);
        if (ui::GhostButton(ICON_PLUS "  Anahtar")) {
            track->setKey(keyFromOrbit(m_s.camera, m_s.animTime));
            m_s.documentDirty = true;
            setStatus("Anahtar eklendi: " + std::to_string(m_s.animTime).substr(0, 4) + " sn");
        }
        ui::Tooltip("Şu anki görünümü oynatma çizgisindeki zamana anahtar olarak ekler (aynı zamandakini değiştirir)");
        ImGui::SameLine();
        if (ui::GhostButton(ICON_MINUS "  Sil")) {
            if (track->removeKeyAt(m_s.animTime)) m_s.documentDirty = true;
            else setStatus("Oynatma çizgisinde anahtar yok (anahtarın üstüne getirin)", true);
        }
        ImGui::SameLine();
        if (track) {
            bool linear = !track->smooth;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Sabit hız");
            ui::Tooltip("Kapalı: anahtarlardan yumuşak geçen eğri, başta ve sonda yavaşlar. Açık: doğrusal, sabit hız");
            ImGui::SameLine();
            if (ui::Toggle("##linear", &linear)) {
                track->smooth = !linear;
                m_s.documentDirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragFloat("##fps", &track->fps, 0.2f, 1.0f, 120.0f, "%.0f fps")) m_s.documentDirty = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (track)
            ImGui::TextColored(pal.textDim, "%.2f / %.2f sn  ·  %d anahtar  ·  %d kare", m_s.animTime, track->duration(),
                               static_cast<int>(track->keys.size()), track->frameCount());
        const float rightW = 200.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - rightW));
        ImGui::BeginDisabled(!track || track->keys.size() < 2 || m_s.finalJob.active.load());
        if (ui::PrimaryButton(ICON_CLAPPERBOARD "  Animasyonu render et")) startAnimationRender();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::IconButton(ICON_X, "Zaman çizelgesini kapat")) {
            m_s.showTimeline = false;
            m_s.animPlaying = false;
        }

        // ── Alt satır: zaman şeridi (anahtar elmasları, oynatma çizgisi) ──
        ImGui::Spacing();
        const ImVec2 bar0 = ImGui::GetCursorScreenPos();
        const float barW = ImGui::GetContentRegionAvail().x, barH = 22.0f;
        ImGui::InvisibleButton("##scrub", ImVec2(barW, barH));
        const float length = timelineLength(track);
        if (track && ImGui::IsItemActive()) {
            const float u = std::clamp((ImGui::GetIO().MousePos.x - bar0.x) / barW, 0.0f, 1.0f);
            m_s.animTime = u * length;
            // Anahtara yakınsa (6 px) ona yapış: silme ve değiştirme kesin zamanla çalışır.
            for (const CameraKey& k : track->keys)
                if (std::abs((k.time - m_s.animTime) / length * barW) < 6.0f) m_s.animTime = k.time;
            m_s.animPlaying = false;
            applyTrackTime(m_s.animTime);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 bar1(bar0.x + barW, bar0.y + barH);
        dl->AddRectFilled(bar0, bar1, ui::col(pal.bg2), 5.0f);
        if (track && track->duration() > 0.0f)
            dl->AddRectFilled(bar0, ImVec2(bar0.x + track->duration() / length * barW, bar1.y), ui::col(pal.accentSoft), 5.0f);
        // Saniye çizgileri.
        for (int s = 1; s < static_cast<int>(length); ++s) {
            const float x = bar0.x + static_cast<float>(s) / length * barW;
            dl->AddLine(ImVec2(x, bar1.y - 6.0f), ImVec2(x, bar1.y), ui::col(pal.textFaint));
        }
        if (track) {
            // Sık anahtarlar (DCC'den her karede örneklenmiş animasyon) elmas yerine ince çizgi.
            const bool dense = track->keys.size() > 1 &&
                               track->duration() / length * barW / static_cast<float>(track->keys.size() - 1) < 12.0f;
            for (const CameraKey& k : track->keys) {
                const float x = bar0.x + k.time / length * barW, y = (bar0.y + bar1.y) * 0.5f;
                const bool atHead = std::abs(k.time - m_s.animTime) < 1e-3f;
                const ImU32 c = ui::col(atHead ? pal.accent : pal.text, dense ? 0.55f : 1.0f);
                if (dense) dl->AddLine(ImVec2(x, y - 5), ImVec2(x, y + 5), c, 1.0f);
                else dl->AddQuadFilled(ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6), ImVec2(x - 6, y), c);
            }
        }
        const float hx = bar0.x + std::clamp(m_s.animTime / length, 0.0f, 1.0f) * barW;
        dl->AddLine(ImVec2(hx, bar0.y - 2.0f), ImVec2(hx, bar1.y + 2.0f), ui::col(pal.accent), 2.0f);
        if (!track) {
            const char* hint = "Animasyon yok: soldaki listeden \"Yeni animasyon\" ya da \"Kayıtlı kameralardan tur\" seçin";
            dl->AddText(ImVec2(bar0.x + 8.0f, bar0.y + 3.0f), ui::col(pal.textDim), hint);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Kare dizisi: her kare için iz değerlendirilir, Final modlu oturumla render edilir, PNG yazılır.
// Videoya çevirmek için (çıktı klasöründe):
//   ffmpeg -framerate 30 -i frame_%04d.png -c:v libx264 -pix_fmt yuv420p -crf 16 animasyon.mp4
void Application::startAnimationRender() {
    if (m_s.activeTrack < 0 || m_s.activeTrack >= static_cast<int>(m_s.cameraTracks.size())) return;
    FinalRenderJob& job = m_s.finalJob;
    if (job.active) return;
    joinFinalRender();
    const CameraTrack track = m_s.cameraTracks[static_cast<size_t>(m_s.activeTrack)];
    if (track.keys.size() < 2) return;

    fs::path base = m_s.projectPath.empty() ? fs::path(userDataDir()) / "renders" : pathFromUtf8(m_s.projectPath).parent_path();
    std::string name = track.name;
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    const fs::path dir = base / (name + "_kareler");
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto scene = buildScene(true);
    RenderSettings rs = m_s.settings;
    rs.width = std::clamp(m_s.output.width, 16, 8192);
    rs.height = std::clamp(m_s.output.height, 16, 8192);
    rs.samplesPerPixel = std::max(1, m_s.output.turntableSpp);
    const bool denoise = m_s.output.denoise;
    const std::string deviceId = m_s.output.device;
    const ToneMapOperator tmo = m_s.settings.tmo;
    const float ev = m_s.settings.exposure;
    const ImagerSettings imager = m_s.imager;
    const OrbitCamera baseCam = m_s.camera;
    const int frames = track.frameCount();

    job.cancel = false;
    job.spp = 0;
    job.targetSpp = rs.samplesPerPixel;
    job.frame = 0;
    job.frames = frames;
    {
        std::lock_guard<std::mutex> lk(job.mutex);
        job.status = "Animasyon render ediliyor…";
        job.outputPath = pathToUtf8(dir);
        job.preview.reset();
        job.succeeded = false;
    }
    m_s.showRenderDialog = true;
    job.active = true;
    job.thread = std::thread([this, scene, rs, frames, track, denoise, deviceId, tmo, ev, imager, baseCam, dir]() {
        FinalRenderJob& j = m_s.finalJob;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            std::string deviceError;
            std::unique_ptr<RenderDevice> device = createDevice(deviceId, &deviceError);
            if (!device) throw std::runtime_error(deviceError);
            device->setScene(scene); // sahne bir kez yüklenir, kareler yalnız kamerayı değiştirir
            RenderSession session(*device);
            SessionConfig sc;
            sc.mode = RenderMode::Final;
            sc.targetSpp = rs.samplesPerPixel;
            sc.aovs = denoise;
            Denoiser denoiser;
            const float aspect = static_cast<float>(rs.width) / static_cast<float>(rs.height);
            for (int i = 0; i < frames && !j.cancel; ++i) {
                const float t = static_cast<float>(i) / std::max(1.0f, track.fps);
                const OrbitCamera cam = orbitFromKey(track.evaluate(t), baseCam);
                session.begin(makeCamera(cameraParamsFor(cam), aspect), rs, sc);
                while (session.advance(&j.cancel)) {
                }
                if (j.cancel) break;
                Image img = denoise ? session.resolveDenoised(denoiser, Denoiser::Quality::High) : session.resolve();
                applyImager(img, imager, ev);
                char fname[32];
                std::snprintf(fname, sizeof(fname), "frame_%04d.png", i);
                saveImagePNG(img, pathToUtf8(dir / fname), tmo, ev);
                j.frame = i + 1;
                auto prev = std::make_shared<Image>(std::move(img));
                std::lock_guard<std::mutex> lk(j.mutex);
                j.preview = prev;
                j.previewNew = true;
                j.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            }
            std::lock_guard<std::mutex> lk(j.mutex);
            j.succeeded = !j.cancel;
            j.status = j.cancel ? "Animasyon durduruldu (" + std::to_string(j.frame.load()) + " kare)"
                                : "Animasyon tamamlandı: " + std::to_string(frames) + " kare (" +
                                      std::to_string(static_cast<int>(track.fps)) + " fps)";
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(j.mutex);
            j.status = std::string("Hata: ") + e.what();
        }
        j.active = false;
    });
}

} // namespace photon
