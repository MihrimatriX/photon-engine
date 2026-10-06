// ui_common.h — Panellerin paylaştığı küçük yardımcılar: panel başlığı, küçük
// resim kartı, dönüşümü konum/dönüş/ölçeğe ayırma ve geri birleştirme.
#pragma once

#include "core/math/transform.h"
#include "core/math/vec.h"
#include "ui/theme.h"

#include <imgui.h>
#include <string>

namespace photon::ui {

/// Panelin üstünde ikon + başlık satırı ve ince ayraç.
void PanelHeader(const char* icon, const char* title, const char* subtitle = nullptr);

/// Küçük resim kartı (resim + altında ad). Tıklanınca true; çift tıklama için
/// ImGui::IsMouseDoubleClicked kullanılabilir. @p active: vurgu kenarlığı.
bool ThumbCard(const char* id, unsigned int texture, const char* label, const ImVec2& imageSize,
               bool active, const ImVec4& fallbackColor);

/// Konum (T), Euler dönüş derece (R, XYZ sırası: önce X sonra Y sonra Z) ve ölçek (S).
struct TRS {
    Vec3f t{0.0f};
    Vec3f rDeg{0.0f};
    Vec3f s{1.0f};
};

/// M = T · Rz · Ry · Rx · S ayrıştırması. Sütun uzunlukları ölçeği, normalize
/// sütunlar dönüş matrisini verir; Euler açıları bu matristen çıkarılır.
TRS decompose(const Transform& xf);
Transform compose(const TRS& trs);

} // namespace photon::ui
