# Collects Windows release artifacts into a portable ZIP (no installer required).
param([Parameter(Mandatory)] [string] $Build, [Parameter(Mandatory)] [string] $Dist)
$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot ".."
# The project version is the indented "VERSION x.y.z" line inside project(...), not the
# cmake_minimum_required(VERSION 3.25) line.
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern '^\s+VERSION ([0-9.]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$stage = Join-Path $Dist "RaDiff-Voice-Processor-$version-Windows-x64"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item (Join-Path $Build "bin\RaDiffVoiceCLI.exe") $stage
# Standalone app: iPlug2 puts RaDiffVoice.exe in <build>\out with its font and factory presets
# next to it (see src/standalone/CMakeLists.txt). Ship that folder as a unit.
$appExe = Join-Path $Build "out\RaDiffVoice.exe"
if (Test-Path $appExe) {
    $appDir = Join-Path $stage "RaDiffVoice"
    New-Item -ItemType Directory -Path $appDir | Out-Null
    Copy-Item $appExe $appDir
    Get-ChildItem (Join-Path $Build "out") -Filter *.ttf | Copy-Item -Destination $appDir
    $bundled = Join-Path $Build "out\presets"
    if (Test-Path $bundled) { Copy-Item $bundled (Join-Path $appDir "presets") -Recurse }
} else {
    Write-Warning "Standalone app not found at $appExe; packaging CLI only"
}
$vst3 = Join-Path $Build "bin\RaDiff Voice Processor.vst3"
if (Test-Path $vst3) { Copy-Item $vst3 $stage -Recurse }
Copy-Item (Join-Path $root "presets") (Join-Path $stage "Presets") -Recurse
$readme = Join-Path $root "docs\INSTALL-Windows.md"; if (Test-Path $readme) { Copy-Item $readme (Join-Path $stage "README.md") }
Compress-Archive -Path $stage -DestinationPath "$stage.zip" -Force
Write-Host "Packaged $stage.zip"
