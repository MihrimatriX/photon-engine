// ui_import.cpp — İçe aktarma penceresi: bırakılan ya da seçilen model dosyaları için birim,
// yukarı eksen ve Alembic karesi. KeyShot'un "Import" penceresine karşılık gelir; ayarlar
// hatırlanır ve "bir daha sorma" ile pencere atlanabilir (Dosya menüsünden geri açılır).
#include "app/application.h"
#include "app/ui_common.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "core/platform/path.h"

#include <algorithm>

namespace photon {

namespace {

std::string lowerExt(const std::string& path) {
    std::string e = pathToUtf8(pathFromUtf8(path).extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

/// Biçimin kısa açıklaması: kullanıcı neyin otomatik geldiğini görsün.
const char* formatNote(const std::string& ext) {
    if (ext == ".fbx") return "FBX: birim ve eksen dosyadan; hiyerarşi, adlar, malzemeler, kameralar (animasyonuyla) ve ışıklar";
    if (ext == ".abc") return "Alembic: hiyerarşi ve yüz kümeleri (malzeme dilimleri); birim aşağıdan";
    if (ext == ".gltf" || ext == ".glb") return "glTF: metre, Y yukarı (standart)";
    if (ext == ".obj") return "OBJ: birim bilgisi yok, aşağıdan seçin";
    return "";
}

} // namespace

void Application::drawImportDialog() {
    if (!m_s.showImportDialog) return;
    if (m_s.importQueue.empty()) {
        m_s.showImportDialog = false;
        return;
    }
    const ui::Palette& pal = ui::palette();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::Begin(ICON_BOX "  İçe aktar###importdlg", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    AppState::ImportPrefs& ip = m_s.importPrefs;

    // Dosyalar ve biçim notları.
    bool anyUnitless = false, anyAlembic = false, anyFbx = false;
    for (const auto& p : m_s.importQueue) {
        const std::string ext = lowerExt(p);
        anyUnitless = anyUnitless || ext == ".obj" || ext == ".abc";
        anyAlembic = anyAlembic || ext == ".abc";
        anyFbx = anyFbx || ext == ".fbx";
        ImGui::PushFont(ui::fonts().semibold, 0.0f);
        ImGui::TextUnformatted((std::string(ICON_BOX "  ") + ui::fileName(p)).c_str());
        ImGui::PopFont();
        ImGui::TextColored(pal.textDim, "%s", formatNote(ext));
    }
    if (m_s.importHasPoint) ImGui::TextColored(pal.accent, ICON_CROSSHAIR "  Bırakılan noktaya yerleştirilecek");
    ImGui::Spacing();

    if (ui::BeginProps("imp", 0.36f)) {
        ui::Prop("Birim", "Dosyadaki 1 birimin karşılığı. Otomatik: OBJ'de metre, Alembic'te yazan uygulamadan "
                          "tahmin (Maya, Cinema 4D: santimetre). FBX ve glTF kendi birimini bildirir.");
        ImGui::BeginDisabled(!anyUnitless);
        const char* units[] = {"Otomatik", "mm", "cm", "m", "inç"};
        ui::Segmented("unit", &ip.unit, units, 5);
        ImGui::EndDisabled();
        ui::Prop("Yukarı eksen", "CAD ve bazı dışa aktarıcılar Z'yi yukarı alır; model yatık geliyorsa Z seçin");
        int up = ip.zUp ? 1 : 0;
        const char* axes[] = {"Y yukarı", "Z yukarı"};
        if (ui::Segmented("up", &up, axes, 2)) ip.zUp = up == 1;
        if (anyAlembic) {
            ui::Prop("Alembic karesi", "Animasyonlu önbellekten okunacak kare. 0 = ilk kare");
            ui::SliderI("##abcf", &ip.alembicFrame, 0, 1000);
            ui::Prop("Kare hızı (fps)");
            ui::SliderF("##abcfps", &ip.alembicFps, 1.0f, 120.0f, "%.0f");
        }
        if (anyFbx) {
            ui::Prop("Işıkları al", "3ds Max / Maya / Blender ışıkları (nokta, spot, yönlü, alan). Şiddetler "
                                    "uygulamalar arasında standart değil: oranlar korunur, düzey ölçeklenir");
            ui::Toggle("implights", &ip.importLights);
            ui::Prop("Kameraları al", "Sabit kameralar Kayıtlı kameralara, animasyonlu kameralar zaman çizelgesine "
                                      "eklenir. Boş sahnede görünüm ilk kameraya geçer");
            ui::Toggle("impcams", &ip.importCameras);
        }
        ui::Prop("Bir daha sorma", "Sonraki içe aktarmalar bu ayarlarla doğrudan yapılır. Geri açmak için: "
                                   "Dosya → İçe aktarma ayarları");
        ui::Toggle("dontask", &ip.dontAsk);
        ui::EndProps();
    }
    ImGui::Spacing();
    const float bw = 140.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2 * bw - 8.0f);
    if (ui::GhostButton("Vazgeç", ImVec2(bw, 34))) open = false;
    ImGui::SameLine(0, 8);
    const std::string label = m_s.importQueue.size() > 1
                                  ? std::string(ICON_UPLOAD "  ") + std::to_string(m_s.importQueue.size()) + " dosyayı aktar"
                                  : std::string(ICON_UPLOAD "  İçe aktar");
    if (ui::PrimaryButton(label.c_str(), ImVec2(bw, 34)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        savePrefs();
        const Vec3f point = m_s.importPoint;
        for (const auto& p : m_s.importQueue)
            importWithOptions(p, importOptionsFor(p), true, m_s.importHasPoint ? &point : nullptr);
        m_s.importQueue.clear();
        open = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) open = false;
    ImGui::End();
    if (!open) {
        m_s.importQueue.clear();
        m_s.showImportDialog = false;
    }
}

} // namespace photon
