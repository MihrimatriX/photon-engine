// 3ds Max / Maya / Blender dosyalarını kurulu DCC ile açma testleri.
//   - Windows komut satırı tırnaklaması CommandLineToArgvW kurallarına uyar (her platformda);
//   - süreç çalıştırıcı kabuksuzdur: boşluk ve tırnak içeren argümanlar olduğu gibi geçer; çıkış
//     kodu ve çıktı toplanır; süre aşılınca süreç öldürülür (POSIX);
//   - sahte bir "blender" (test FBX'ini istenen çıktıya kopyalayan betik) ile uçtan uca: .blend
//     içe aktarılır, kaynak yol .blend kalır, ikinci açılış önbellekten gelir (araç yeniden
//     çalışmaz), kaynak değişince yeniden çevrilir; araç hata verirse çıktısı kullanıcıya ulaşır;
//     araç yoksa ne yapılacağını söyleyen bir hata döner.
#include <gtest/gtest.h>
#include "core/platform/process.h"
#include "scene/dcc_convert.h"
#include "scene/model_import.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace photon;
namespace fs = std::filesystem;

TEST(Process, WindowsQuotingFollowsCommandLineToArgvRules) {
    EXPECT_EQ(quoteWindowsArgument("blender.exe"), "blender.exe");
    EXPECT_EQ(quoteWindowsArgument(""), "\"\"");
    EXPECT_EQ(quoteWindowsArgument("C:\\Program Files\\x.exe"), "\"C:\\Program Files\\x.exe\"");
    EXPECT_EQ(quoteWindowsArgument("a\"b"), "\"a\\\"b\"");
    EXPECT_EQ(quoteWindowsArgument("C:\\dir with space\\"), "\"C:\\dir with space\\\\\""); // sondaki \ ikiye
    EXPECT_EQ(quoteWindowsArgument("x\\\\\"y"), "\"x\\\\\\\\\\\"y\"");                    // \\" → \\\\\"
    EXPECT_EQ(quoteWindowsArgument("no\\trailing\\quote\\"), "no\\trailing\\quote\\");     // boşluksuz: aynen
}

#if !defined(_WIN32)

namespace {

std::string readText(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int countLines(const fs::path& p) {
    std::ifstream in(p);
    int n = 0;
    for (std::string l; std::getline(in, l);) ++n;
    return n;
}

/// Ortam değişkenini test süresince değiştirir.
struct ScopedEnv {
    std::string name;
    std::string old;
    bool had = false;
    ScopedEnv(const char* n, const std::string& value) : name(n) {
        if (const char* v = std::getenv(n)) {
            had = true;
            old = v;
        }
        ::setenv(n, value.c_str(), 1);
    }
    ~ScopedEnv() {
        if (had) ::setenv(name.c_str(), old.c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};

} // namespace

TEST(Process, PassesArgumentsVerbatimAndReportsExitCode) {
    const ProcessResult r = runProcess({"/bin/sh", "-c", "printf '%s|' \"$@\"; exit 3", "sh", "a b", "c\"d", "e;rm -rf x"}, 10);
    ASSERT_TRUE(r.started);
    EXPECT_FALSE(r.timedOut);
    EXPECT_EQ(r.exitCode, 3);
    EXPECT_EQ(r.output, "a b|c\"d|e;rm -rf x|");
    const ProcessResult missing = runProcess({"/olmayan/program/yok"}, 5);
    EXPECT_FALSE(missing.started);
}

TEST(Process, KillsOnTimeout) {
    const auto t0 = std::chrono::steady_clock::now();
    const ProcessResult r = runProcess({"/bin/sh", "-c", "sleep 30"}, 1);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_TRUE(r.timedOut);
    EXPECT_LT(secs, 5.0);
}

TEST(DccConvert, BlendThroughFakeBlenderWithCache) {
    const fs::path root = fs::temp_directory_path() / "photon dcc test"; // boşluklu yol: tırnaklama sınanır
    fs::remove_all(root);
    fs::create_directories(root / "home");
    ScopedEnv home("HOME", (root / "home").string()); // önbellek kullanıcı klasörü buraya
    const fs::path fbx = fs::path(PHOTON_TEST_DATA) / "fbx" / "dcc_camera_lights_7400_ascii.fbx";
    const fs::path counter = root / "calls.txt";
    const fs::path tool = root / "fake blender";
    {
        // Gerçek blender gibi çağrılır: -b dosya --factory-startup --python betik -- çıktı
        std::ofstream t(tool);
        t << "#!/bin/sh\n"
             "[ \"$1\" = \"-b\" ] || { echo 'beklenen -b'; exit 2; }\n"
             "for a in \"$@\"; do last=\"$a\"; done\n"
             "echo \"$2\" >> '" << counter.string() << "'\n"
             "cp '" << fbx.string() << "' \"$last\"\n";
    }
    fs::permissions(tool, fs::perms::owner_all);
    ScopedEnv blender("PHOTON_BLENDER", tool.string());
    const fs::path blend = root / "Araba sahnesi.blend";
    std::ofstream(blend) << "BLENDER-v402 sahte içerik";

    ASSERT_TRUE(isSupportedModelFile(blend.string()));
    std::string err;
    ImportExtras extras;
    auto group = importModelFile(blend.string(), &err, {}, nullptr, &extras);
    ASSERT_NE(group, nullptr) << err;
    EXPECT_EQ(group->sourcePath, blend.string());
    EXPECT_EQ(group->name, "Araba sahnesi");
    EXPECT_FALSE(group->children.empty());
    EXPECT_EQ(extras.cameras.size(), 1u); // FBX'in kameraları ve ışıkları da gelir
    EXPECT_EQ(extras.lights.size(), 3u);
    EXPECT_EQ(countLines(counter), 1);
    EXPECT_EQ(readText(counter), blend.string() + "\n");

    // İkinci açılış: önbellekten, araç çalışmaz.
    ASSERT_NE(importModelFile(blend.string(), &err), nullptr) << err;
    EXPECT_EQ(countLines(counter), 1);

    // Kaynak değişti: yeniden çevrilir.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(blend, std::ios::app) << " değişti";
    fs::last_write_time(blend, fs::file_time_type::clock::now());
    ASSERT_NE(importModelFile(blend.string(), &err), nullptr) << err;
    EXPECT_EQ(countLines(counter), 2);
}

TEST(DccConvert, ReportsToolErrorsAndMissingTools) {
    const fs::path root = fs::temp_directory_path() / "photon_dcc_err";
    fs::remove_all(root);
    fs::create_directories(root / "home");
    ScopedEnv home("HOME", (root / "home").string());
    const fs::path tool = root / "bozuk_mayapy";
    std::ofstream(tool) << "#!/bin/sh\necho 'Error: line 1: Cannot find plug-in fbxmaya'\nexit 1\n";
    fs::permissions(tool, fs::perms::owner_all);
    const fs::path ma = root / "sahne.ma";
    std::ofstream(ma) << "//Maya ASCII";
    {
        ScopedEnv maya("PHOTON_MAYAPY", tool.string());
        std::string err;
        EXPECT_EQ(importModelFile(ma.string(), &err), nullptr);
        EXPECT_NE(err.find("fbxmaya"), std::string::npos) << err; // DCC'nin kendi hatası iletilir
        EXPECT_NE(err.find("çıkış kodu 1"), std::string::npos) << err;
    }
    {
        ScopedEnv maya("PHOTON_MAYAPY", "");
        ScopedEnv path("PATH", "");
        std::string err;
        EXPECT_EQ(importModelFile((root / "x.max").string(), &err), nullptr);
        std::ofstream(root / "x.max") << "max";
        EXPECT_EQ(importModelFile((root / "x.max").string(), &err), nullptr);
        EXPECT_NE(err.find("3ds Max bu bilgisayarda bulunamadı"), std::string::npos) << err;
        EXPECT_NE(err.find("FBX"), std::string::npos) << err; // alternatifi söyler
    }
}

#endif
