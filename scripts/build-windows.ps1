# Configure, build and test on a Windows 10/11 x64 PC or self-hosted runner.
# Requirements (all free): Visual Studio 2022 Build Tools with the "Desktop development
# with C++" workload (MSVC v143 + Windows 10/11 SDK), CMake >= 3.25, Ninja, Git.
#
#   .\scripts\build-windows.ps1              # Release
#   .\scripts\build-windows.ps1 -Config Debug
#   .\scripts\build-windows.ps1 -VisualStudio  # generate a .sln instead of Ninja
#
# Run from a "x64 Native Tools Command Prompt for VS 2022", or let this script locate
# vcvars64.bat through vswhere.
param(
    [ValidateSet("Debug", "Release")] [string] $Config = "Release",
    [switch] $VisualStudio
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not $env:VSCMD_VER) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 Build Tools not found (vswhere missing)." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw "No Visual Studio installation with the C++ toolset was found." }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    Write-Host "Loading MSVC environment from $vcvars"
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") }
    }
}

git submodule update --init third_party/nlohmann_json third_party/doctest third_party/iPlug2 third_party/vst3sdk third_party/minimp3
git -C third_party/vst3sdk submodule update --init base pluginterfaces public.sdk cmake

if ($VisualStudio) {
    $preset = "windows-vs2022"
    cmake --preset $preset
    cmake --build --preset $preset --config $Config --parallel
    ctest --preset $preset -C $Config --output-on-failure
} else {
    $preset = if ($Config -eq "Debug") { "windows-x64-debug" } else { "windows-x64-release" }
    cmake --preset $preset
    cmake --build --preset $preset --parallel
    ctest --preset $preset --output-on-failure
}
Write-Host "Artifacts in build\$preset\bin"
