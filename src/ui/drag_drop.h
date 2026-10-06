// drag_drop.h — Sürükle-bırak yük (payload) türleri ve dosya uzantısı yardımcıları.
// Kütüphaneden viewport'a sürüklenen öğe, türünü bu adlarla taşır (ImGui drag & drop).
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace photon {

constexpr const char* kPayloadMaterial = "PHOTON_MATERIAL";
constexpr const char* kPayloadModel = "PHOTON_MODEL";
constexpr const char* kPayloadTexture = "PHOTON_TEXTURE";
constexpr const char* kPayloadHdr = "PHOTON_HDR";
constexpr const char* kPayloadStudio = "PHOTON_STUDIO";
constexpr const char* kPayloadLight = "PHOTON_LIGHT"; // "area" | "directional"
constexpr const char* kPayloadCamera = "PHOTON_CAMERA"; // preset id

inline std::string lowerExt(std::string pathOrExt) {
    auto dot = pathOrExt.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < pathOrExt.size() && pathOrExt[0] != '.')
        pathOrExt = pathOrExt.substr(dot);
    std::transform(pathOrExt.begin(), pathOrExt.end(), pathOrExt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return pathOrExt;
}

inline bool isHdrExtension(const std::string& path) {
    const std::string e = lowerExt(path);
    return e == ".hdr" || e == ".exr";
}

inline bool isTextureExtension(const std::string& path) {
    const std::string e = lowerExt(path);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" ||
           e == ".bmp" || e == ".tif" || e == ".tiff";
}

inline bool isModelExtension(const std::string& path) {
    const std::string e = lowerExt(path);
    // ponytail: OBJ + glTF 2.0 only; FBX needs Assimp (Faz 7 / v2)
    return e == ".obj" || e == ".gltf" || e == ".glb";
}

} // namespace photon
