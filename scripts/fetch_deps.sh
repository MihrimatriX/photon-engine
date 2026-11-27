#!/usr/bin/env bash
# fetch_deps.sh — Linux için fetch_deps.ps1 karşılığı: Intel Embree 4 ve OIDN 2'nin resmi
# derlenmiş paketlerini third_party/ altına indirir (ikisi de Apache-2.0, CMake orada bulur).
#   bash scripts/fetch_deps.sh
#   cmake --preset fetch-release -DGLFW_BUILD_WAYLAND=OFF && cmake --build --preset fetch-release
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
oidn_version=2.5.1
embree_version=4.4.1
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

if [ ! -d "$repo/third_party/oidn/lib/cmake" ]; then
    url="https://github.com/RenderKit/oidn/releases/download/v$oidn_version/oidn-$oidn_version.x86_64.linux.tar.gz"
    echo "Downloading $url"
    curl -fsSL "$url" -o "$tmp/oidn.tgz"
    tar xzf "$tmp/oidn.tgz" -C "$tmp"
    mkdir -p "$repo/third_party/oidn"
    cp -r "$tmp/oidn-$oidn_version.x86_64.linux/." "$repo/third_party/oidn/"
    echo "OIDN $oidn_version -> third_party/oidn"
else
    echo "OIDN already present"
fi

if [ ! -d "$repo/third_party/embree/lib64/cmake" ]; then
    url="https://github.com/RenderKit/embree/releases/download/v$embree_version/embree-$embree_version.x86_64.linux.tar.gz"
    echo "Downloading $url"
    curl -fsSL "$url" -o "$tmp/embree.tgz"
    mkdir -p "$repo/third_party/embree"
    tar xzf "$tmp/embree.tgz" -C "$repo/third_party/embree"
    echo "Embree $embree_version -> third_party/embree"
else
    echo "Embree already present"
fi
# Linux paketi kitaplıkları lib64'e koyar; CMake'in config araması <prefix>/lib*/cmake'i her
# platformda taramadığı için lib → lib64 bağlantısı (find_package(embree 4 CONFIG) bulsun).
[ -e "$repo/third_party/embree/lib" ] || ln -s lib64 "$repo/third_party/embree/lib"
