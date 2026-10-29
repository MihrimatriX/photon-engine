// fbx_loader.h — Autodesk FBX (ikili ve ASCII, 2009–2020+) yükleyicisi; ufbx (MIT) üzerine.
//
// Maya, 3ds Max, Blender, Cinema 4D, SolidWorks Visualize ve oyun motorlarının ortak değişim
// biçimi. Dosyanın kendi eksen ve birim tanımı okunur; sahne metreye ve Y-yukarıya çevrilir.
// Düğüm hiyerarşisi, adlar ve örnekleme (aynı mesh'i paylaşan düğümler) korunur; çok malzemeli
// mesh'ler malzeme başına parçalara bölünür. Malzemeler (Phong/Lambert, Arnold Standard Surface,
// 3ds Max Physical, Stingray PBS) ufbx'in ortak PBR eşlemesinden Principled'a çevrilir; dokular
// dosya yanında aranır, gömülüyse doku önbelleğine çıkarılır.
#pragma once

#include "io/imported_model.h"

#include <string>

namespace photon {

class FbxLoader {
public:
    /// Başarısızsa boş model ve @p error dolar.
    static ImportedModel load(const std::string& path, std::string* error = nullptr);
};

} // namespace photon
