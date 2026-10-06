# package.ps1 — Release derlemesinden taşınabilir (kurulum gerektirmeyen) bir paket üretir:
#   dist\PhotonEngine-<sürüm>-win64\  ve  dist\PhotonEngine-<sürüm>-win64.zip
# İçerik: iki exe, DLL'ler, VC++ çalışma zamanı (uygulama yanına, kurulum gerekmez),
# assets\, örnek proje, kullanım kılavuzu ve bağımlılık lisansları.
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1
param([string]$Build = 'build\release')

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $repo $Build
$app = Join-Path $build 'src\app'

$version = ([regex]'project\(PhotonEngine VERSION ([\d.]+)').Match((Get-Content -Raw (Join-Path $repo 'CMakeLists.txt'))).Groups[1].Value
$name = "PhotonEngine-$version-win64"
$out = Join-Path $repo "dist\$name"
foreach ($f in (Join-Path $app 'photon_app.exe'), (Join-Path $build 'photon_render.exe')) {
    if (-not (Test-Path $f)) { throw "$f yok — önce: cmake --build --preset release" }
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out, "$out\ornekler", "$out\lisanslar" | Out-Null

# Programlar ve DLL'ler (photon_app'in yanındakiler: Embree, OIDN, TBB, GLFW).
Copy-Item (Join-Path $app 'photon_app.exe'), (Join-Path $build 'photon_render.exe') $out
Copy-Item (Join-Path $app '*.dll') $out
Copy-Item -Recurse (Join-Path $app 'assets') $out

# VC++ çalışma zamanı: Microsoft "app-local" dağıtıma izin verir; böylece hedef
# makinede vc_redist kurulu olmasa (ya da eski sürüm olsa) da çalışır.
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT' -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'VC++ Redist klasörü bulunamadı (Visual Studio kurulu mu?)' }
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-Item (Join-Path $crt.FullName $dll) $out }

# Örnek proje: bench sahnesi, yolları paketteki assets\'e göre düzeltilmiş.
# (.NET ile okunur/yazılır: Windows PowerShell 5.1'in varsayılan ANSI kodlaması Türkçe düğüm adlarını bozar.)
$proj = [IO.File]::ReadAllText((Join-Path $repo 'bench\scenes\showcase.photon')).Replace('../../assets/', '../assets/')
[IO.File]::WriteAllText("$out\ornekler\vitrin.photon", $proj, (New-Object Text.UTF8Encoding $false))

Copy-Item (Join-Path $repo 'packaging\KULLANIM.txt') $out

# Lisanslar: dağıttığımız her bileşenin metni.
Copy-Item (Join-Path $repo 'docs\THIRD_PARTY.md') "$out\lisanslar"
Copy-Item (Join-Path $repo 'third_party\oidn\LICENSE.txt') "$out\lisanslar\oidn.txt"
Copy-Item (Join-Path $repo 'third_party\embree\LICENSE.txt') "$out\lisanslar\embree.txt"
foreach ($p in 'glfw3', 'imgui', 'imguizmo', 'nlohmann-json') {
    Copy-Item (Join-Path $build "vcpkg_installed\x64-windows\share\$p\copyright") "$out\lisanslar\$p.txt"
}
foreach ($p in 'cgltf', 'stb', 'tinyobjloader') {
    $lic = Get-ChildItem (Join-Path $build "_deps\$p-src") -File | Where-Object Name -match '^(LICENSE|COPYING)' | Select-Object -First 1
    if ($lic) { Copy-Item $lic.FullName "$out\lisanslar\$p.txt" } else { Write-Warning "$p lisans dosyası yok" }
}
# tinyexr'in BSD-3 metni ayrı dosyada değil, başlığın en üstündeki yorum bloğunda.
$h = Get-Content (Join-Path $build '_deps\tinyexr-src\tinyexr.h') -TotalCount 120
$end = 3; while ($end -lt $h.Count -and $h[$end] -notmatch '^\s*#') { $end++ }
$h[2..($end - 1)] | Set-Content "$out\lisanslar\tinyexr.txt"

$zip = "$out.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip -CompressionLevel Optimal
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "$zip ($mb MB)"
