#pragma once

/// @file path.h
/// @brief UTF-8 <-> std::filesystem::path.
///
/// Every path string in the engine is UTF-8 (GLFW drops, ImGui text, project
/// files). On Windows, std::filesystem::path(std::string) decodes with the ANSI
/// code page instead, so "Masaüstü/şişe.obj" would be mangled. Go through these
/// two helpers whenever a std::string path meets std::filesystem.

#include <filesystem>
#include <string>
#include <string_view>

namespace photon {

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace photon
