# Build DevilutionX for Nintendo Switch (.nro) on Windows using devkitPro.
#
# Prerequisites (one-time):
#   - devkitPro installed to C:\devkitPro (graphical installer from devkitpro.org)
#   - Switch packages via devkitPro pacman:
#       C:\devkitPro\msys2\usr\bin\pacman -S --needed - < Packaging\switch\packages.txt
#       C:\devkitPro\msys2\usr\bin\pacman -S --needed switch-cmake switch-sdl2_image cmake ninja make
#
# Why this script is fiddly (lessons learned — do not "simplify" these away):
#   * The devkitPro CMake toolchain rejects native Windows CMake (`if(CMAKE_HOST_WIN32)`),
#     so we MUST use the MSYS2 cmake at C:\devkitPro\msys2\usr\bin\cmake.exe.
#   * That cmake fails to run from inside Git Bash (wrong msys-2.0.dll loaded) but runs
#     fine from PowerShell with msys2\usr\bin on PATH -- which is what this script does.
#   * MSYS2_ARG_CONV_EXCL=-D stops the msys runtime from mangling -D path values, while
#     STILL auto-converting the unix source paths it hands to the native devkitA64 gcc.
#   * Source/build dirs are passed as msys paths (/e/...), converted via cygpath.
#   * SDL_image 2.0.5's tarball contains macOS framework symlinks that CMake's tar can't
#     extract on Windows, so we pre-extract it (minus Xcode/) and feed it via
#     FETCHCONTENT_SOURCE_DIR_SDL_IMAGE.
#
# Usage:  pwsh -File scripts\build-switch.ps1
$ErrorActionPreference = "Stop"

$dkp     = "C:\devkitPro"
$msysBin = "$dkp\msys2\usr\bin"
$cmake   = "$msysBin\cmake.exe"
$cygpath = "$msysBin\cygpath.exe"
$ninja   = "/opt/devkitpro/msys2/usr/bin/ninja.exe"

if (-not (Test-Path $cmake))   { throw "MSYS2 cmake not found. Install with: $msysBin\pacman -S cmake ninja make" }
if (-not (Test-Path "$dkp\cmake\Switch.cmake")) { throw "Switch.cmake missing. Install: $msysBin\pacman -S switch-cmake" }

$winRoot  = (Resolve-Path "$PSScriptRoot\..").Path
$msysRoot = (& $cygpath -u $winRoot).Trim()

# --- Pre-extract SDL_image (skips macOS framework symlinks that break tar on Windows) ---
$prefetch = "$winRoot\.deps-prefetch\sdl_image"
if (-not (Test-Path "$prefetch\IMG_png.c")) {
    Write-Host "==> Pre-fetching SDL_image source..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $prefetch | Out-Null
    $tgz = "$winRoot\.deps-prefetch\sdlimg.tar.gz"
    curl.exe -L -s -o $tgz "https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-2.0.5.tar.gz"
    & "$msysBin\tar.exe" -xzf (& $cygpath -u $tgz) -C (& $cygpath -u $prefetch) --strip-components=1 --exclude='*/Xcode'
}
$prefetchMsys = (& $cygpath -u $prefetch).Trim()

$env:DEVKITPRO          = "/opt/devkitpro"
$env:MSYS2_ARG_CONV_EXCL = "-D"
$env:Path               = "$msysBin;$dkp\tools\bin;" + $env:Path

Write-Host "==> Configuring Switch build..." -ForegroundColor Cyan
& $cmake -S $msysRoot -B "$msysRoot/build-switch" -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DBUILD_ASSETS_MPQ=OFF" `
    "-DBUILD_TESTING=OFF" `
    "-DFETCHCONTENT_SOURCE_DIR_SDL_IMAGE=$prefetchMsys"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

Write-Host "==> Compiling for aarch64 (this takes a few minutes)..." -ForegroundColor Cyan
& $cmake --build "$msysRoot/build-switch" -j
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "==> Done: $winRoot\build-switch\devilutionx.nro" -ForegroundColor Green
