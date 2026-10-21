# Collects Windows release artifacts into a portable ZIP (no installer required).
param([Parameter(Mandatory)] [string] $Build, [Parameter(Mandatory)] [string] $Dist)
$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot ".."
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'VERSION ([0-9.]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
$stage = Join-Path $Dist "RaDiff-Voice-Processor-$version-Windows-x64"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item (Join-Path $Build "bin\RaDiffVoiceCLI.exe") $stage
foreach ($name in @("RaDiffVoiceProcessor.exe")) {
    $p = Join-Path $Build "bin\$name"; if (Test-Path $p) { Copy-Item $p $stage }
}
$vst3 = Join-Path $Build "bin\RaDiff Voice Processor.vst3"
if (Test-Path $vst3) { Copy-Item $vst3 $stage -Recurse }
Copy-Item (Join-Path $root "presets") (Join-Path $stage "Presets") -Recurse
$readme = Join-Path $root "docs\INSTALL-Windows.md"; if (Test-Path $readme) { Copy-Item $readme (Join-Path $stage "README.md") }
Compress-Archive -Path $stage -DestinationPath "$stage.zip" -Force
Write-Host "Packaged $stage.zip"
