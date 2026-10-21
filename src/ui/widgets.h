// widgets.h — Uygulamaya özel, tutarlı görünümlü ImGui bileşenleri.
//
// Paneller ham ImGui çağrıları yerine bunları kullanır; böylece her yerde aynı
// hizalama (solda etiket, sağda kontrol), aynı boşluk ve aynı vurgu dili olur.
#pragma once

#include <imgui.h>
#include <string>

namespace photon::ui {

// ── Özellik tablosu ──────────────────────────────────────────────────────
// Kullanım:
//   if (ui::BeginProps("kamera")) {
//       ui::Prop("Odak uzaklığı");  ImGui::SliderFloat("##fl", &v, 12, 200);
//       ui::EndProps();
//   }
// Prop() bir tablo satırı açar, etiketi sol sütuna yazar ve sağ sütuna geçip
// sonraki kontrolün genişliğini sütuna yayar.
bool BeginProps(const char* id, float labelFraction = 0.40f);
void Prop(const char* label, const char* tooltip = nullptr);
void EndProps();

/// Katlanabilir bölüm başlığı (ikon + kalın metin + ince ayraç).
bool Section(const char* icon, const char* label, bool defaultOpen = true);

/// Kare ikon düğmesi. @p active: basılı/seçili görünüm.
bool IconButton(const char* icon, const char* tooltip, bool active = false, float size = 0.0f);

/// Vurgu renkli ana eylem düğmesi.
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// Sınırsız (ghost) ikincil düğme.
bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0));

/// Yan yana seçenekler; tıklanınca *current değişir.
bool Segmented(const char* id, int* current, const char* const* items, int count, float width = -1.0f);

/// "Doldurmalı" kaydırıcılar: değer çubuğun soldan dolan kısmıyla gösterilir,
/// sayı ortada okunur (ImGui'nin tutamacı metnin üstüne binmez). İmzalar
/// ImGui::SliderFloat/SliderInt ile aynı; Ctrl+tık ile değer yazılabilir.
bool SliderF(const char* id, float* v, float vMin, float vMax, const char* fmt = "%.2f", ImGuiSliderFlags flags = 0);
bool SliderI(const char* id, int* v, int vMin, int vMax, const char* fmt = "%d", ImGuiSliderFlags flags = 0);

/// Açık/kapalı anahtarı (iOS tarzı).
bool Toggle(const char* id, bool* value);

/// Metnin üzerine gelince açıklama gösteren soru işareti.
void Help(const char* text);

/// Gri, küçük açıklama metni.
void Hint(const char* text);

/// Küçük renkli rozet (ör. "OIDN", "4K").
void Badge(const char* text, const ImVec4& color);

/// Gecikmeli araç ipucu (son eklenen öğe için).
void Tooltip(const char* text);

/// Bir dosya yolunun yalnız dosya adı.
std::string fileName(const std::string& path);

/// Saniyeyi "1 dk 05 sn" biçiminde yazar.
std::string formatDuration(double seconds);
/// Büyük sayıyı kısa yazar: 950, "12,4 bin", "1,2 milyon" (Türkçe ondalık virgülü).
std::string formatCount(size_t n);

} // namespace photon::ui
