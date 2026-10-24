// ies.cpp — IES LM-63 ayrıştırıcı ve çift doğrusal profil okuması.
#include "lights/ies.h"
#include "core/platform/path.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace photon {

namespace {

bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
}

/// Artan dizide x için alt indeks ve kesir (kenetli).
void bracket(const std::vector<float>& a, float x, int& i0, int& i1, float& f) {
    if (a.size() == 1 || x <= a.front()) {
        i0 = i1 = 0;
        f = 0.0f;
        return;
    }
    if (x >= a.back()) {
        i0 = i1 = static_cast<int>(a.size()) - 1;
        f = 0.0f;
        return;
    }
    const auto it = std::upper_bound(a.begin(), a.end(), x);
    i1 = static_cast<int>(it - a.begin());
    i0 = i1 - 1;
    const float span = a[static_cast<size_t>(i1)] - a[static_cast<size_t>(i0)];
    f = span > 0.0f ? (x - a[static_cast<size_t>(i0)]) / span : 0.0f;
}

} // namespace

float IesProfile::evaluate(float gammaDeg, float phiDeg) const {
    if (vertical.empty() || horizontal.empty()) return 0.0f;
    if (gammaDeg < vertical.front() - 1e-3f || gammaDeg > vertical.back() + 1e-3f) return 0.0f;
    // Yatay simetri: açıyı tablonun kapsadığı aralığa katla.
    float phi = std::fmod(phiDeg, 360.0f);
    if (phi < 0.0f) phi += 360.0f;
    const float last = horizontal.back();
    if (horizontal.size() == 1 || last <= 0.0f) {
        phi = 0.0f;
    } else if (last <= 90.0f + 1e-3f) {
        if (phi > 180.0f) phi = 360.0f - phi;
        if (phi > 90.0f) phi = 180.0f - phi;
    } else if (last <= 180.0f + 1e-3f) {
        if (phi > 180.0f) phi = 360.0f - phi;
    }
    int h0, h1, v0, v1;
    float fh, fv;
    bracket(horizontal, phi, h0, h1, fh);
    bracket(vertical, gammaDeg, v0, v1, fv);
    const size_t nv = vertical.size();
    auto at = [&](int h, int v) { return values[static_cast<size_t>(h) * nv + static_cast<size_t>(v)]; };
    const float a = at(h0, v0) * (1.0f - fv) + at(h0, v1) * fv;
    const float b = at(h1, v0) * (1.0f - fv) + at(h1, v1) * fv;
    return std::max(0.0f, a * (1.0f - fh) + b * fh);
}

std::shared_ptr<const IesProfile> parseIes(const std::string& text, std::string* error) {
    // Başlık anahtar sözcüklerini (IESNA:, [TEST] ...) atla; veri TILT= satırından sonra.
    const size_t tilt = text.find("TILT=");
    if (tilt == std::string::npos) return fail(error, "IES: TILT satırı yok"), nullptr;
    size_t eol = text.find('\n', tilt);
    const std::string tiltValue = text.substr(tilt + 5, eol == std::string::npos ? std::string::npos : eol - tilt - 5);
    std::istringstream in(eol == std::string::npos ? std::string() : text.substr(eol + 1));
    in.imbue(std::locale::classic());
    if (tiltValue.rfind("INCLUDE", 0) == 0) {
        // Lamba-armatür geometrisi, çift sayısı, açılar ve çarpanlar: profili etkilemiyor.
        int geometry = 0, pairs = 0;
        in >> geometry >> pairs;
        float skip;
        for (int i = 0; i < 2 * pairs && in; ++i) in >> skip;
    }
    int lamps = 0, nV = 0, nH = 0, photometric = 0, units = 0;
    float lumens = 0, multiplier = 1, width = 0, length = 0, height = 0, ballast = 1, future = 0, watts = 0;
    in >> lamps >> lumens >> multiplier >> nV >> nH >> photometric >> units >> width >> length >> height;
    in >> ballast >> future >> watts;
    if (!in || nV <= 0 || nH <= 0 || nV > 4096 || nH > 4096) return fail(error, "IES: geçersiz başlık"), nullptr;
    auto profile = std::make_shared<IesProfile>();
    profile->vertical.resize(static_cast<size_t>(nV));
    profile->horizontal.resize(static_cast<size_t>(nH));
    profile->values.resize(static_cast<size_t>(nV) * static_cast<size_t>(nH));
    for (float& v : profile->vertical) in >> v;
    for (float& h : profile->horizontal) in >> h;
    for (float& c : profile->values) in >> c;
    if (!in) return fail(error, "IES: veri eksik"), nullptr;
    if (!std::is_sorted(profile->vertical.begin(), profile->vertical.end()) ||
        !std::is_sorted(profile->horizontal.begin(), profile->horizontal.end()))
        return fail(error, "IES: açılar sıralı değil"), nullptr;
    float peak = 0.0f;
    for (float& c : profile->values) {
        c = std::max(0.0f, c * multiplier);
        peak = std::max(peak, c);
    }
    if (!(peak > 0.0f)) return fail(error, "IES: tüm değerler sıfır"), nullptr;
    for (float& c : profile->values) c /= peak;
    profile->peakCandela = peak;
    return profile;
}

std::shared_ptr<const IesProfile> loadIes(const std::string& path, std::string* error) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) {
        if (error) *error = "IES dosyası açılamadı: " + path;
        return nullptr;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseIes(ss.str(), error);
}

} // namespace photon
