# Building on Windows (Desktop + Nintendo Switch)

This fork builds for two targets from the same source tree:

| Target  | Output                            | Toolchain            |
|---------|-----------------------------------|----------------------|
| Desktop | `build/Release/devilutionx.exe`   | MSVC + vcpkg         |
| Switch  | `build-switch/devilutionx.nro`    | devkitPro (devkitA64)|

Helper scripts live in [`scripts/`](scripts/). Run them from PowerShell.

## Desktop (Windows x64)

**One-time setup**
1. Install **Visual Studio Build Tools** with *Desktop development with C++*.
2. `scoop install cmake ninja` (or install CMake/Ninja any way you like).
3. Clone + bootstrap **vcpkg**:
   ```powershell
   git clone https://github.com/microsoft/vcpkg "$env:USERPROFILE\vcpkg"
   & "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
   ```
   If you cloned vcpkg shallow, also fetch the baseline commit pinned in `vcpkg.json`:
   ```powershell
   git -C "$env:USERPROFILE\vcpkg" fetch --depth 1 origin <baseline-sha-from-vcpkg.json>
   ```

**Build**
```powershell
pwsh -File scripts\build-desktop.ps1
```
vcpkg compiles the dependencies (SDL2, libsodium, fmt, lua, bzip2, ...) on the first
run, so expect 20-40 min initially; subsequent builds are fast.

## Nintendo Switch (.nro)

**One-time setup**
1. Install **devkitPro** to `C:\devkitPro` (graphical installer from <https://devkitpro.org>).
2. Install the toolchain packages with devkitPro's pacman:
   ```powershell
   $pac = "C:\devkitPro\msys2\usr\bin\pacman"
   & $pac -S --needed - < Packaging\switch\packages.txt
   & $pac -S --needed switch-cmake switch-sdl2_image cmake ninja make
   ```

**Build**
```powershell
pwsh -File scripts\build-switch.ps1
```
Copy the resulting `build-switch\devilutionx.nro` to `/switch/devilutionx/` on your SD
card, alongside `diabdat.mpq`. (Core UI assets are bundled into the .nro via RomFS.)

### Windows + devkitPro gotchas (already handled by the script)
- Use the **MSYS2 CMake** (`C:\devkitPro\msys2\usr\bin\cmake.exe`), not native CMake —
  the devkitPro toolchain hard-rejects `CMAKE_HOST_WIN32`.
- Drive it from **PowerShell**, not Git Bash (Git Bash loads a conflicting
  `msys-2.0.dll` and the MSYS2 cmake silently fails with exit 127).
- Set `MSYS2_ARG_CONV_EXCL=-D` so `-D...=/path` flags aren't mangled, while source
  paths handed to the native gcc are still auto-converted.
- `SDL_image` 2.0.5 is pre-extracted (minus its macOS `Xcode/` framework symlinks,
  which CMake's tar can't create on Windows) and fed via `FETCHCONTENT_SOURCE_DIR_SDL_IMAGE`.

## Multiplayer note
Switch-to-Switch (and cross-platform) multiplayer requires **all players to run the
same build/version**. When you start adding mod features, rebuild and deploy the `.nro`
to every console.
