# build_usd.ps1 — OpenUSD'yi (Pixar, TOST-1.0 / Apache-2.0 tabanlı) kaynaktan third_party\usd altına,
# oneTBB'yi (Apache-2.0) third_party\tbb altına kurar. CMake ikisini bulunca OpenUSD içe aktarma
# (.usd, .usda, .usdc, .usdz) açılır; bulunmazsa motor USD'siz derlenir.
#
#   MSVC geliştirici kabuğunda (scripts\devshell.ps1 ya da "x64 Native Tools"):
#   powershell -ExecutionPolicy Bypass -File scripts\build_usd.ps1 [-Work C:\pxu] [-Jobs 8]
#
# Yalnız çekirdek kitaplıklar: tek DLL (usd_ms.dll, lib\ altına kurulur), Python, görüntüleme
# (Hydra), araçlar ve testler kapalı. USD'nin derlendiği TBB başlıkları motorun bağlandığı TBB ile
# aynı sürüm olmalı (tbb::detail::dN ad alanı ABI'nin parçası): oneTBB burada sabitlenir, sürümü
# OIDN'nin paketlediği tbb12.dll ile aynıdır (2023.1.0). Derleme klasörü kısa bir yolda tutulur
# (Windows MAX_PATH: USD'nin nesne dosyası yolları uzun). CI bu betiğin özetiyle önbellekler.
param(
    [string]$Work = (Join-Path $env:SystemDrive 'pxu'),
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$usdVersion = 'v26.11.1'
$tbbVersion = '2023.1.0'
$tbbSha256 = 'cf6ee0c600fcb5c3a9b65e3e6e4781669d06f1bb1e37970d145fcde08eed8da9'
$tbbDest = Join-Path $repo 'third_party\tbb'
$usdDest = Join-Path $repo 'third_party\usd'
New-Item -ItemType Directory -Force -Path $Work | Out-Null

function Invoke-Native([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe çıkış kodu $LASTEXITCODE" }
}

# 1. oneTBB (derlenmiş paket; CMake yapılandırması redist\ ve lib\ yollarını kendisi bulur)
if (-not (Test-Path (Join-Path $tbbDest 'lib\cmake\tbb\TBBConfig.cmake'))) {
    $zip = Join-Path $Work "oneapi-tbb-$tbbVersion-win.zip"
    $url = "https://github.com/oneapi-src/oneTBB/releases/download/v$tbbVersion/oneapi-tbb-$tbbVersion-win.zip"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zip
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $tbbSha256) { throw "oneTBB SHA-256 uyuşmuyor: $hash" }
    $unz = Join-Path $Work 'tbb-x'
    if (Test-Path $unz) { Remove-Item -Recurse -Force $unz }
    Expand-Archive -Path $zip -DestinationPath $unz -Force
    if (Test-Path $tbbDest) { Remove-Item -Recurse -Force $tbbDest }
    Move-Item (Join-Path $unz "oneapi-tbb-$tbbVersion") $tbbDest
    Write-Host "oneTBB $tbbVersion -> $tbbDest"
} else {
    Write-Host "oneTBB already present in $tbbDest"
}

if (Test-Path (Join-Path $usdDest 'pxrConfig.cmake')) {
    Write-Host "OpenUSD already present in $usdDest"
    exit 0
}
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    throw 'cl.exe bulunamadı: betiği MSVC geliştirici kabuğunda çalıştırın (scripts\devshell.ps1).'
}

# 2. OpenUSD kaynağı (sabit etiket)
$src = Join-Path $Work 'src'
if (-not (Test-Path (Join-Path $src '.git'))) {
    Invoke-Native git @('clone', '--depth', '1', '--branch', $usdVersion, 'https://github.com/PixarAnimationStudios/OpenUSD.git', $src)
}

# 3. Yapılandır, derle, kur
$bld = Join-Path $Work 'b'
$tbbCmake = (Join-Path $tbbDest 'lib\cmake\tbb') -replace '\\', '/'
Invoke-Native cmake @('-S', $src, '-B', $bld, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_INSTALL_PREFIX=$($usdDest -replace '\\', '/')",
    "-DTBB_DIR=$tbbCmake",
    '-DBUILD_SHARED_LIBS=ON',
    '-DPXR_BUILD_MONOLITHIC=ON',
    '-DPXR_ENABLE_PYTHON_SUPPORT=OFF',
    '-DPXR_BUILD_IMAGING=OFF',
    '-DPXR_BUILD_USD_IMAGING=OFF',
    '-DPXR_BUILD_USDVIEW=OFF',
    '-DPXR_BUILD_USD_TOOLS=OFF',
    '-DPXR_BUILD_USD_VALIDATION=OFF',
    '-DPXR_BUILD_EXEC=OFF',
    '-DPXR_BUILD_TESTS=OFF',
    '-DPXR_BUILD_EXAMPLES=OFF',
    '-DPXR_BUILD_TUTORIALS=OFF',
    '-DPXR_BUILD_DOCUMENTATION=OFF',
    '-DPXR_BUILD_HTML_DOCUMENTATION=OFF',
    '-DPXR_ENABLE_GL_SUPPORT=OFF',
    '-DPXR_ENABLE_MATERIALX_SUPPORT=OFF',
    '-DPXR_ENABLE_OPENVDB_SUPPORT=OFF',
    '-DPXR_ENABLE_PTEX_SUPPORT=OFF',
    '-DPXR_ENABLE_OSL_SUPPORT=OFF',
    '-DPXR_BUILD_ALEMBIC_PLUGIN=OFF',
    '-DPXR_BUILD_DRACO_PLUGIN=OFF')
Invoke-Native cmake @('--build', $bld, '--target', 'install', '-j', "$Jobs")
Copy-Item -Force (Join-Path $src 'LICENSE.txt') (Join-Path $usdDest 'LICENSE.txt')
if (-not (Test-Path (Join-Path $usdDest 'lib\usd_ms.dll'))) { throw 'usd_ms.dll kurulmadı' }
Write-Host "OpenUSD $usdVersion -> $usdDest"
Write-Host "Reconfigure: cmake --preset fetch-release"
