# Enter a VS amd64 developer shell at the photon-engine repo root.
# Safe from a normal PowerShell. Dot-source so the environment stays:
#   . .\scripts\devshell.ps1
# Running the file opens a nested shell that stays in the repo.

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

$invoked = $MyInvocation.InvocationName
$dotSourced = ($invoked -eq '.') -or ($invoked -eq '')
if (-not $dotSourced) {
    $me = $MyInvocation.MyCommand.Path
    & powershell.exe -NoLogo -NoExit -Command "Set-Location -LiteralPath '$repoRoot'; . '$me'"
    return
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere not found at $vswhere"
}

$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($install -is [array]) { $install = $install[0] }
$install = "$install".Trim()
if (-not $install) {
    throw 'No Visual Studio installation with the MSVC toolset was found.'
}

$launch = Join-Path $install 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path -LiteralPath $launch)) {
    throw "Launch-VsDevShell.ps1 not found at $launch"
}

# VsDevShell overwrites VCPKG_ROOT with the copy shipped inside Visual Studio.
# Keep the caller's install (User/Machine env on a normal PowerShell).
$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot) {
    $vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
}
if (-not $vcpkgRoot) {
    $vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'Machine')
}

. $launch -VsInstallationPath $install -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

if ($vcpkgRoot) {
    $env:VCPKG_ROOT = $vcpkgRoot
}

$toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
if (-not $env:VCPKG_ROOT -or -not (Test-Path -LiteralPath $toolchain)) {
    throw 'VCPKG_ROOT is not set to an existing vcpkg install.'
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw 'cmake was not found after launching the VS dev shell.'
}

Set-Location -LiteralPath $repoRoot
Write-Host "photon-engine dev shell"
Write-Host "  repo:       $repoRoot"
Write-Host "  cmake:      $($cmake.Source)"
Write-Host "  VCPKG_ROOT: $env:VCPKG_ROOT"
Write-Host "  configure:  cmake --preset release"
Write-Host "  build:      cmake --build --preset release"
