#!/usr/bin/env bash
# build_usd.sh — OpenUSD'yi (Pixar, TOST-1.0 / Apache-2.0 tabanlı) kaynaktan third_party/usd altına,
# oneTBB'yi (Apache-2.0) third_party/tbb altına kurar. Linux/macOS geliştirme makineleri içindir;
# Windows karşılığı scripts/build_usd.ps1 (CI onu kullanır).
#
#   scripts/build_usd.sh            # ~10–15 dk (4 çekirdek), bir kez
#   JOBS=16 scripts/build_usd.sh
#
# Yalnız çekirdek kitaplıklar derlenir: tek paylaşılan kitaplık (usd_ms), Python, görüntüleme (Hydra),
# araçlar ve testler kapalı. Yükleyici yalnız sahne birleştirme, UsdGeom/UsdShade/UsdLux okumaya ihtiyaç
# duyar. USD'nin TBB başlıkları ile motorun bağlandığı TBB aynı sürüm olmalı (tbb::detail::dN ad alanı
# ABI'nin parçası): bu yüzden oneTBB burada sabitlenir ve USD ona karşı derlenir. Sürüm, OIDN'nin
# paketlediğiyle aynıdır (2023.1.0, libtbb 12.19); Embree 4.4'ün istediği 12.17'den yenidir.
set -euo pipefail

USD_VERSION="v26.11.1"
TBB_VERSION="2023.1.0"
TBB_SHA256_LINUX="349d0e8b08cae4a5ab2668d54ff4e90b0fa012a332de6fb156961ddc119cd617"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${PHOTON_DEPS_WORK:-$ROOT/build/deps}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TBB_DEST="$ROOT/third_party/tbb"
USD_DEST="$ROOT/third_party/usd"
mkdir -p "$WORK"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Bu betik Linux içindir; macOS'ta oneTBB'yi Homebrew'dan kurup TBB_DIR'i ona yönlendirin." >&2
fi

# 1. oneTBB (derlenmiş paket, CMake yapılandırma dosyalarıyla)
if [[ ! -f "$TBB_DEST/lib/cmake/tbb/TBBConfig.cmake" ]]; then
    tgz="$WORK/oneapi-tbb-$TBB_VERSION-lin.tgz"
    curl -fsSL -o "$tgz" "https://github.com/oneapi-src/oneTBB/releases/download/v$TBB_VERSION/oneapi-tbb-$TBB_VERSION-lin.tgz"
    echo "$TBB_SHA256_LINUX  $tgz" | sha256sum -c -
    rm -rf "$WORK/tbb-x" && mkdir -p "$WORK/tbb-x"
    tar -xzf "$tgz" -C "$WORK/tbb-x"
    rm -rf "$TBB_DEST"
    mv "$WORK/tbb-x/oneapi-tbb-$TBB_VERSION" "$TBB_DEST"
    echo "oneTBB $TBB_VERSION -> $TBB_DEST"
else
    echo "oneTBB zaten var: $TBB_DEST"
fi

# 2. OpenUSD kaynağı (sabit etiket)
if [[ ! -d "$WORK/usd-src/.git" ]]; then
    git clone --depth 1 --branch "$USD_VERSION" https://github.com/PixarAnimationStudios/OpenUSD.git "$WORK/usd-src"
fi

# 3. Yapılandır, derle, kur
cmake -S "$WORK/usd-src" -B "$WORK/usd-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$USD_DEST" \
    -DTBB_DIR="$TBB_DEST/lib/cmake/tbb" \
    -DBUILD_SHARED_LIBS=ON \
    -DPXR_BUILD_MONOLITHIC=ON \
    -DPXR_ENABLE_PYTHON_SUPPORT=OFF \
    -DPXR_BUILD_IMAGING=OFF \
    -DPXR_BUILD_USD_IMAGING=OFF \
    -DPXR_BUILD_USDVIEW=OFF \
    -DPXR_BUILD_USD_TOOLS=OFF \
    -DPXR_BUILD_USD_VALIDATION=OFF \
    -DPXR_BUILD_EXEC=OFF \
    -DPXR_BUILD_TESTS=OFF \
    -DPXR_BUILD_EXAMPLES=OFF \
    -DPXR_BUILD_TUTORIALS=OFF \
    -DPXR_BUILD_DOCUMENTATION=OFF \
    -DPXR_BUILD_HTML_DOCUMENTATION=OFF \
    -DPXR_ENABLE_GL_SUPPORT=OFF \
    -DPXR_ENABLE_MATERIALX_SUPPORT=OFF \
    -DPXR_ENABLE_OPENVDB_SUPPORT=OFF \
    -DPXR_ENABLE_PTEX_SUPPORT=OFF \
    -DPXR_ENABLE_OSL_SUPPORT=OFF \
    -DPXR_BUILD_ALEMBIC_PLUGIN=OFF \
    -DPXR_BUILD_DRACO_PLUGIN=OFF
cmake --build "$WORK/usd-build" --target install -j "$JOBS"
cp "$WORK/usd-src/LICENSE.txt" "$USD_DEST/LICENSE.txt"
echo "OpenUSD $USD_VERSION -> $USD_DEST"
echo "Yeniden yapılandırın: cmake --preset <ön ayar>  (OpenUSD: found ... satırı görünmeli)"
