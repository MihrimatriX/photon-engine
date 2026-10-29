// dcc_convert.cpp — DCC araçlarını bulma, dışa aktarma betikleri ve önbellek.
#include "scene/dcc_convert.h"
#include "core/platform/path.h"
#include "core/platform/process.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace photon {

namespace fs = std::filesystem;

namespace {

std::string lowerExt(const std::string& path) {
    std::string e = pathToUtf8(pathFromUtf8(path).extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

std::string envPath(const char* name) {
#ifdef _WIN32
    std::wstring w(name, name + std::strlen(name));
    if (const wchar_t* v = _wgetenv(w.c_str())) return pathToUtf8(fs::path(v));
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

/// @p parent altında @p prefix ile başlayan klasörlerden ada göre en büyüğünde (en yeni sürüm)
/// @p rel dosyası varsa onun yolu.
std::string newestUnder(const fs::path& parent, const std::string& prefix, const fs::path& rel) {
    std::error_code ec;
    std::vector<fs::path> hits;
    for (const auto& e : fs::directory_iterator(parent, ec)) {
        const std::string n = pathToUtf8(e.path().filename());
        if (n.rfind(prefix, 0) == 0 && fs::is_regular_file(e.path() / rel, ec)) hits.push_back(e.path() / rel);
    }
    if (hits.empty()) return {};
    std::sort(hits.begin(), hits.end());
    return pathToUtf8(hits.back());
}

std::string inPath(const std::string& exe) {
    const std::string path = envPath("PATH");
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(sep, start);
        const std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::error_code ec;
        if (!dir.empty() && fs::is_regular_file(pathFromUtf8(dir) / exe, ec)) return pathToUtf8(pathFromUtf8(dir) / exe);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// ── Dışa aktarma betikleri (DCC eklentileriyle aynı ayarlar) ──

constexpr const char* kBlenderScript = R"PY(import sys
import bpy
out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.export_scene.fbx(
    filepath=out, use_selection=False,
    object_types={"MESH", "CURVE", "SURFACE", "META", "FONT", "CAMERA", "LIGHT", "EMPTY", "OTHER"},
    apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", axis_forward="-Z", axis_up="Y",
    use_mesh_modifiers=True, mesh_smooth_type="FACE", use_tspace=True, add_leaf_bones=False,
    path_mode="COPY", embed_textures=True, bake_anim=True, bake_anim_use_all_actions=False,
    bake_anim_use_nla_strips=False, bake_anim_step=1.0, bake_anim_simplify_factor=0.0)
)PY";

constexpr const char* kMayaScript = R"PY(import sys
import maya.standalone
maya.standalone.initialize(name="python")
import maya.cmds as cmds
import maya.mel as mel
src, out = sys.argv[1], sys.argv[2]
cmds.loadPlugin("fbxmaya", quiet=True)
cmds.file(src, open=True, force=True, ignoreVersion=True, prompt=False)
start = int(cmds.playbackOptions(q=True, minTime=True))
end = int(cmds.playbackOptions(q=True, maxTime=True))
for c in ("FBXResetExport", "FBXExportEmbeddedTextures -v true", "FBXExportCameras -v true",
          "FBXExportLights -v true", "FBXExportSmoothingGroups -v true", "FBXExportTangents -v true",
          "FBXExportUpAxis y", "FBXExportInAscii -v false", "FBXExportBakeComplexAnimation -v true",
          "FBXExportBakeComplexStart -v %d" % start, "FBXExportBakeComplexEnd -v %d" % end,
          "FBXExportBakeComplexStep -v 1"):
    mel.eval(c)
mel.eval('FBXExport -f "%s"' % out.replace("\\", "/"))
maya.standalone.uninitialize()
)PY";

constexpr const char* kMaxScript = R"MS(local out = maxOps.mxsCmdLineArgs[#out]
FBXExporterSetParam "ResetExport"
FBXExporterSetParam "EmbedTextures" true
FBXExporterSetParam "Cameras" true
FBXExporterSetParam "Lights" true
FBXExporterSetParam "Animation" true
FBXExporterSetParam "BakeAnimation" true
FBXExporterSetParam "SmoothingGroups" true
FBXExporterSetParam "TangentSpaceExport" true
FBXExporterSetParam "ASCII" false
FBXExporterSetParam "UpAxis" "Y"
exportFile out #noPrompt using:FBXEXP
)MS";

std::string tail(const std::string& s, size_t n) { return s.size() <= n ? s : "…" + s.substr(s.size() - n); }

} // namespace

const char* dccAppName(DccApp app) {
    switch (app) {
        case DccApp::Blender: return "Blender";
        case DccApp::Maya: return "Maya";
        case DccApp::Max: return "3ds Max";
    }
    return "?";
}

std::optional<DccApp> dccForFile(const std::string& path) {
    const std::string e = lowerExt(path);
    if (e == ".blend") return DccApp::Blender;
    if (e == ".ma" || e == ".mb") return DccApp::Maya;
    if (e == ".max") return DccApp::Max;
    return std::nullopt;
}

bool isDccNativeFile(const std::string& path) { return dccForFile(path).has_value(); }

std::string findDccExecutable(DccApp app) {
    const char* env = app == DccApp::Blender ? "PHOTON_BLENDER" : app == DccApp::Maya ? "PHOTON_MAYAPY" : "PHOTON_3DSMAXBATCH";
    std::error_code ec;
    if (const std::string e = envPath(env); !e.empty() && fs::is_regular_file(pathFromUtf8(e), ec)) return e;
#ifdef _WIN32
    const fs::path pf = pathFromUtf8(envPath("ProgramFiles").empty() ? std::string("C:\\Program Files") : envPath("ProgramFiles"));
    switch (app) {
        case DccApp::Blender: {
            std::string p = newestUnder(pf / "Blender Foundation", "Blender", "blender.exe");
            return p.empty() ? inPath("blender.exe") : p;
        }
        case DccApp::Maya: return newestUnder(pf / "Autodesk", "Maya", fs::path("bin") / "mayapy.exe");
        case DccApp::Max: return newestUnder(pf / "Autodesk", "3ds Max", "3dsmaxbatch.exe");
    }
#elif defined(__APPLE__)
    switch (app) {
        case DccApp::Blender:
            if (fs::is_regular_file("/Applications/Blender.app/Contents/MacOS/Blender", ec))
                return "/Applications/Blender.app/Contents/MacOS/Blender";
            return inPath("blender");
        case DccApp::Maya:
            return newestUnder("/Applications/Autodesk", "maya", fs::path("Maya.app") / "Contents" / "bin" / "mayapy");
        case DccApp::Max: return {}; // 3ds Max yalnız Windows
    }
#else
    switch (app) {
        case DccApp::Blender: return inPath("blender");
        case DccApp::Maya: {
            std::string p = newestUnder("/usr/autodesk", "maya", fs::path("bin") / "mayapy");
            return p.empty() ? inPath("mayapy") : p;
        }
        case DccApp::Max: return {}; // 3ds Max yalnız Windows
    }
#endif
    return {};
}

std::string convertDccToFbx(const std::string& path, std::string* error, int timeoutSeconds) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return std::string();
    };
    const auto app = dccForFile(path);
    if (!app) return fail("DCC dosyası değil: " + path);
    std::error_code ec;
    const fs::path src = fs::absolute(pathFromUtf8(path), ec);
    if (!fs::is_regular_file(src, ec)) return fail("Dosya bulunamadı: " + path);
    const std::string exe = findDccExecutable(*app);
    if (exe.empty()) {
        const std::string howto = *app == DccApp::Max ? "3ds Max'te File → Export → FBX ya da PhotonEngine eklentisi"
                                : *app == DccApp::Maya ? "Maya'da File → Export All → FBX ya da PhotonEngine rafı"
                                                       : "Blender'da File → Export → FBX ya da PhotonEngine eklentisi";
        return fail(std::string(dccAppName(*app)) + " bu bilgisayarda bulunamadı; dosyayı açmak için kurulu olması "
                    "gerekir. Alternatif: " + howto + " ile FBX gönderin.");
    }

    // Önbellek: kaynak değişmediyse önceki çeviri kullanılır.
    const auto size = fs::file_size(src, ec);
    const auto mtime = fs::last_write_time(src, ec).time_since_epoch().count();
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a(pathToUtf8(src) + "|" + std::to_string(size) + "|" + std::to_string(mtime))));
    const fs::path cacheDir = userDataDir() / "dcc_cache";
    fs::create_directories(cacheDir, ec);
    const fs::path out = cacheDir / (std::string(hex) + ".fbx");
    if (fs::is_regular_file(out, ec) && fs::file_size(out, ec) > 0) return pathToUtf8(out);

    const fs::path work = cacheDir / (std::string(hex) + ".work");
    fs::create_directories(work, ec);
    const fs::path tmpOut = work / "out.fbx";
    fs::remove(tmpOut, ec);
    std::vector<std::string> argv;
    switch (*app) {
        case DccApp::Blender: {
            const fs::path script = work / "photon_export.py";
            std::ofstream(script) << kBlenderScript;
            argv = {exe, "-b", pathToUtf8(src), "--factory-startup", "--python", pathToUtf8(script), "--", pathToUtf8(tmpOut)};
            break;
        }
        case DccApp::Maya: {
            const fs::path script = work / "photon_export.py";
            std::ofstream(script) << kMayaScript;
            argv = {exe, pathToUtf8(script), pathToUtf8(src), pathToUtf8(tmpOut)};
            break;
        }
        case DccApp::Max: {
            const fs::path script = work / "photon_export.ms";
            std::ofstream(script) << kMaxScript;
            argv = {exe, pathToUtf8(script), "-sceneFile", pathToUtf8(src), "-mxsString", "out:" + pathToUtf8(tmpOut)};
            break;
        }
    }
    const ProcessResult r = runProcess(argv, timeoutSeconds);
    if (!r.started) {
        fs::remove_all(work, ec);
        return fail(std::string(dccAppName(*app)) + " başlatılamadı: " + exe);
    }
    if (r.timedOut) {
        fs::remove_all(work, ec);
        return fail(std::string(dccAppName(*app)) + " " + std::to_string(timeoutSeconds) + " saniyede bitmedi (dosya çok büyük ya da bir pencere yanıt bekliyor)");
    }
    if (!fs::is_regular_file(tmpOut, ec) || fs::file_size(tmpOut, ec) == 0) {
        const std::string log = tail(r.output, 600);
        fs::remove_all(work, ec);
        return fail(std::string(dccAppName(*app)) + " FBX üretmedi (çıkış kodu " + std::to_string(r.exitCode) + ")" +
                    (log.empty() ? "" : ":\n" + log));
    }
    fs::rename(tmpOut, out, ec);
    if (ec) fs::copy_file(tmpOut, out, fs::copy_options::overwrite_existing, ec);
    fs::remove_all(work, ec);
    if (ec && !fs::is_regular_file(out)) return fail("Çevrilen dosya önbelleğe yazılamadı: " + ec.message());
    return pathToUtf8(out);
}

} // namespace photon
