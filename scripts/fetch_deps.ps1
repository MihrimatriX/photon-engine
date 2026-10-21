# fetch_deps.ps1 — Downloads the prebuilt Intel Open Image Denoise (OIDN) package
# into third_party/oidn. OIDN is not a vcpkg port; CMake finds it there.
# Optional: -Hdri2k replaces the bundled 1k HDRIs with 2k versions (Poly Haven, CC0).
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_deps.ps1 [-Hdri2k]
param([switch]$Hdri2k)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$version = '2.5.1'
$dest = Join-Path $repo 'third_party\oidn'

if (-not (Test-Path (Join-Path $dest 'lib\cmake'))) {
    $tmp = Join-Path $env:TEMP "oidn-$version.zip"
    $url = "https://github.com/RenderKit/oidn/releases/download/v$version/oidn-$version.x64.windows.zip"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $tmp
    $unz = Join-Path $env:TEMP "oidn-$version"
    Expand-Archive -Path $tmp -DestinationPath $unz -Force
    $src = Join-Path $unz "oidn-$version.x64.windows"
    New-Item -ItemType Directory -Force -Path (Join-Path $dest 'bin') | Out-Null
    Copy-Item -Recurse -Force (Join-Path $src 'include') $dest
    Copy-Item -Recurse -Force (Join-Path $src 'lib') $dest
    # CPU device only: the GTX 1080 (Pascal) is not supported by OIDN's GPU backends.
    foreach ($dll in 'OpenImageDenoise.dll', 'OpenImageDenoise_core.dll', 'OpenImageDenoise_device_cpu.dll', 'tbb12.dll') {
        Copy-Item -Force (Join-Path $src "bin\$dll") (Join-Path $dest 'bin')
    }
    Copy-Item -Force (Join-Path $src 'doc\LICENSE.txt') (Join-Path $dest 'LICENSE.txt')
    Write-Host "OIDN $version -> $dest"
} else {
    Write-Host "OIDN already present in $dest"
}

# Intel Embree 4 (Apache-2.0). vcpkg'den kaynaktan derlemek ~1 saat sürdüğü için
# resmi derlenmiş paket kullanılır. tbb12.dll OIDN'ninkiyle aynı ad; CMake OIDN'nin
# (daha yeni) sürümünü exe'nin yanına en son kopyalar.
$embreeVersion = '4.4.1'
$edest = Join-Path $repo 'third_party\embree'
if (-not (Test-Path (Join-Path $edest 'lib\cmake'))) {
    $tmp = Join-Path $env:TEMP "embree-$embreeVersion.zip"
    $url = "https://github.com/RenderKit/embree/releases/download/v$embreeVersion/embree-$embreeVersion.x64.windows.zip"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $tmp
    $unz = Join-Path $env:TEMP "embree-$embreeVersion"
    Expand-Archive -Path $tmp -DestinationPath $unz -Force
    $src = Join-Path $unz "embree-$embreeVersion.x64.windows"
    New-Item -ItemType Directory -Force -Path (Join-Path $edest 'bin') | Out-Null
    Copy-Item -Recurse -Force (Join-Path $src 'include') $edest
    Copy-Item -Recurse -Force (Join-Path $src 'lib') $edest
    foreach ($dll in 'embree4.dll', 'tbbmalloc.dll') {
        Copy-Item -Force (Join-Path $src "bin\$dll") (Join-Path $edest 'bin')
    }
    Copy-Item -Force (Join-Path $src 'share\doc\Embree_superbuild\LICENSE.txt') (Join-Path $edest 'LICENSE.txt')
    Write-Host "Embree $embreeVersion -> $edest"
} else {
    Write-Host "Embree already present in $edest"
}

if ($Hdri2k) {
    $envDir = Join-Path $repo 'assets\environments'
    foreach ($id in 'studio_small_09', 'brown_photostudio_02', 'photo_studio_01', 'kloppenheim_06', 'sunflowers_puresky') {
        $out = Join-Path $envDir "${id}_2k.hdr"
        if (-not (Test-Path $out)) {
            Invoke-WebRequest -Uri "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k/${id}_2k.hdr" -OutFile $out
            Write-Host "HDRI $out"
        }
    }
}
Write-Host "Reconfigure: cmake --preset release"
