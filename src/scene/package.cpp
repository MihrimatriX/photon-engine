// package.cpp — .photonpack yazma ve açma.
#include "scene/package.h"
#include "core/io/zip.h"
#include "core/platform/path.h"
#include "scene/mesh_binary.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace photon {

namespace fs = std::filesystem;

namespace {

constexpr const char* kProjectEntry = "project.photon";
constexpr const char* kCompleteMarker = ".photonpack-complete";

std::string lowerExt(const fs::path& p) {
    std::string e = pathToUtf8(p.extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

/// Görüntüler zaten sıkıştırılmıştır (PNG, JPEG, EXR); yeniden DEFLATE etmek zaman kaybı.
bool worthCompressing(const fs::path& p) {
    const std::string e = lowerExt(p);
    return e == ".ies" || e == ".txt" || e == ".json" || e == ".obj" || e == ".mtl" || e == ".bmp" || e == ".tga";
}

/// Dosya adını paket içi ad olarak güvenli kıl (ayırıcı ve denetim karakterleri '_').
std::string safeFileName(const fs::path& p) {
    std::string n = pathToUtf8(p.filename());
    for (char& c : n) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':') c = '_';
    }
    if (n.empty() || n == "." || n == "..") n = "dosya";
    return n;
}

uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull) {
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

bool isPackageFile(const std::string& path) { return lowerExt(pathFromUtf8(path)) == ".photonpack"; }

bool savePackage(const std::string& path, const SceneGraph& graph, const ProjectData& data, std::string* error,
                 std::vector<std::string>* warnings) {
    struct Asset {
        std::string name;
        fs::path file;
    };
    std::vector<Asset> assets;
    std::unordered_map<std::string, std::string> assetByPath;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> meshes;
    std::unordered_map<const TriangleMesh*, std::string> meshByPtr;

    ProjectWriteHooks hooks;
    hooks.mapAsset = [&](const std::string& p) -> std::string {
        std::error_code ec;
        const fs::path abs = fs::absolute(pathFromUtf8(p), ec).lexically_normal();
        const std::string key = pathToUtf8(abs);
        if (auto it = assetByPath.find(key); it != assetByPath.end()) return it->second;
        if (!fs::is_regular_file(abs, ec)) {
            if (warnings) warnings->push_back("Bulunamayan dosya pakete alınmadı: " + p);
            return p;
        }
        const std::string name = "assets/" + std::to_string(assets.size() + 1) + "_" + safeFileName(abs);
        assets.push_back({name, abs});
        assetByPath[key] = name;
        return name;
    };
    hooks.storeMesh = [&](const TriangleMesh& m) -> std::string {
        if (auto it = meshByPtr.find(&m); it != meshByPtr.end()) return it->second;
        const std::string name = "geometry/" + std::to_string(meshes.size() + 1) + ".mesh";
        meshes.emplace_back(name, encodeMeshBinary(m));
        meshByPtr[&m] = name;
        return name;
    };
    const std::string text = projectJsonText(graph, data, std::string(), &hooks);

    const fs::path file = pathFromUtf8(path);
    fs::path tmp = file;
    tmp += ".tmp";
    ZipWriter zip;
    if (!zip.open(tmp, error)) return false;
    bool ok = zip.add(kProjectEntry, text.data(), text.size(), true, error);
    for (const auto& [name, bytes] : meshes) ok = ok && zip.add(name, bytes.data(), bytes.size(), true, error);
    for (const Asset& a : assets) ok = ok && zip.addFile(a.name, a.file, worthCompressing(a.file), error);
    ok = zip.close(ok ? error : nullptr) && ok;
    std::error_code ec;
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    if (ec) {
        if (error) *error = "Paket taşınamadı: " + ec.message();
        return false;
    }
    return true;
}

std::string packageCacheDir(const std::string& path) {
    std::error_code ec;
    const fs::path abs = fs::absolute(pathFromUtf8(path), ec);
    const auto size = fs::file_size(abs, ec);
    const auto mtime = fs::last_write_time(abs, ec).time_since_epoch().count();
    const uint64_t h = fnv1a(std::to_string(size) + "|" + std::to_string(mtime), fnv1a(pathToUtf8(abs)));
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return pathToUtf8(userDataDir() / "packages" / hex);
}

bool loadPackage(const std::string& path, SceneGraph& graph, ProjectData& data, std::string* error,
                 const std::string& extractDir) {
    std::error_code ec;
    const bool useCache = extractDir.empty();
    const fs::path dir = pathFromUtf8(useCache ? packageCacheDir(path) : extractDir);
    const bool cached = useCache && fs::is_regular_file(dir / kCompleteMarker, ec);
    if (!cached) {
        ZipReader zip;
        if (!zip.open(pathFromUtf8(path), error)) return false;
        if (!zip.find(kProjectEntry)) {
            if (error) *error = "Paket bir PhotonEngine projesi içermiyor (project.photon yok)";
            return false;
        }
        // Önbellekte yarım çıkarma kalmasın: önce yan klasöre, bitince yerine.
        fs::path work = dir;
        if (useCache) {
            work += ".part";
            fs::remove_all(work, ec);
        }
        if (!zip.extractAll(work, error)) {
            if (useCache) fs::remove_all(work, ec);
            return false;
        }
        if (useCache) {
            std::ofstream(work / kCompleteMarker) << "ok\n";
            fs::remove_all(dir, ec);
            fs::rename(work, dir, ec);
            if (ec) {
                if (error) *error = "Paket önbelleği yazılamadı: " + ec.message();
                return false;
            }
        }
    }
    return loadProject(pathToUtf8(dir / kProjectEntry), graph, data, error);
}

} // namespace photon
