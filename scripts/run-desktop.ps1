# Launch the desktop build against retail Diablo data for play-testing.
#
# Usage:  pwsh -File scripts\run-desktop.ps1 [path-to-folder-with-DIABDAT.MPQ]
# Default data dir: E:\Games\Diablo
param(
    [string]$DataDir = "E:\Games\Diablo"
)
$ErrorActionPreference = "Stop"
$root    = (Resolve-Path "$PSScriptRoot\..").Path
$exeDir  = "$root\build\Release"
$exe     = "$exeDir\devilutionx.exe"

if (-not (Test-Path $exe)) { throw "Desktop build not found. Run scripts\build-desktop.ps1 first." }
if (-not (Test-Path "$DataDir\DIABDAT.MPQ")) { throw "DIABDAT.MPQ not found in $DataDir" }

# The VS multi-config build copies engine assets to build\assets, but the exe in
# build\Release looks for build\Release\assets. Mirror them if missing/stale.
if (-not (Test-Path "$exeDir\assets\ui_art\diablo.pal")) {
    Write-Host "==> Copying engine assets next to the exe..." -ForegroundColor Cyan
    Copy-Item -Recurse -Force "$root\build\assets" "$exeDir\assets"
}

Write-Host "==> Launching DevilutionX (data: $DataDir)" -ForegroundColor Green
Start-Process -FilePath $exe -ArgumentList '--data-dir', $DataDir -WorkingDirectory $exeDir
