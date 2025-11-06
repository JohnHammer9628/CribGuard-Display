CribGuard Display — LVGL v9 + SDL2 Simulator (Windows)

Run the CribGuard UI locally on Windows using LVGL v9 with the SDL2 backend.
Build system: CMake + Ninja + vcpkg. IDE: VS Code (recommended).

Current status: Simulator builds and runs in portrait 720×1280, shows the top status bar, state label, Calm/Cry/Motion, Play/Stop, and a volume slider.

Next step (optional): Replace platforms/pi-sdl/main.cpp with the v2 file that tightens layout and adds keyboard shortcuts. (You have not applied this yet.)

📋 Table of Contents

Quick Start

Initial Setup

# CribGuard Display

Run the CribGuard UI locally on Windows using LVGL v9 with the SDL2 backend (simulator).

Build system: CMake + Ninja + vcpkg. Recommended IDE: Visual Studio Code.

Status
- Simulator builds and runs on Windows in portrait (720×1280).
- Shows top status bar, state label, Calm/Cry/Motion indicators, Play/Stop controls and a volume slider.

Table of contents
- Quick start
- Prerequisites
- Build & run
# CribGuard Display

Run the CribGuard UI locally on Windows using LVGL v9 with the SDL2 backend (simulator).

Build system: CMake + Ninja + vcpkg. Recommended IDE: Visual Studio Code.

Status
- Simulator builds and runs on Windows in portrait (720×1280).
- Shows a top status bar, state label, Calm/Cry/Motion indicators, Play/Stop controls and a volume slider.

## Table of contents
- Quick start
- Prerequisites
- Build & run
- One-liner (PowerShell)
- Project structure
- VS Code integration
- Configuration reference
- Testing & diagnostics
- Troubleshooting
- Roadmap
- Contributing
- License

---

## Quick start

After initial setup, the normal workflow is:

```powershell
cmake --preset win-rel
cmake --build --preset win-rel
.\build-win\crib_guard_pi.exe
```

You should see a portrait 720×1280 window with the CribGuard UI. The process runs until you close the window.

## Prerequisites (Windows 10 / 11)

- Visual Studio Build Tools (MSVC C++ toolset + Windows SDK)
- CMake (on PATH)
- Ninja (on PATH)
- vcpkg (recommended path: `C:\dev\vcpkg`)
- Git
- Visual Studio Code (recommended) with the C/C++ and CMake Tools extensions

vcpkg quick setup (example):

```powershell
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
C:\dev\vcpkg\vcpkg integrate install
```

Clone this repository:

```powershell
git clone https://github.com/<your-org>/CribGuard-Display.git
cd CribGuard-Display
code .
```

## Build & run (detailed)

1) Configure

```powershell
cmake --preset win-rel
```

2) Build

```powershell
cmake --build --preset win-rel
```

3) Run

```powershell
.\build-win\crib_guard_pi.exe
```

Clean rebuild (if needed):

```powershell
Remove-Item -Recurse -Force .\build-win -ErrorAction SilentlyContinue
cmake --preset win-rel
cmake --build --preset win-rel
.\build-win\crib_guard_pi.exe
```

Expected console messages

```
[INFO] LVGL 9.2.0 init ok
[INFO] HAL init ok (SDL window 720x1280)
[INFO] Entering main loop...
```

## One-liner (PowerShell)

This script configures, builds and runs the simulator (invokes the Visual Studio dev command script):

```powershell
Remove-Item -Recurse -Force build-win -ErrorAction SilentlyContinue

$env:VCPKG_ROOT = "C:\dev\vcpkg"
$env:CMAKE_TOOLCHAIN_FILE = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
$env:CMAKE_PREFIX_PATH   = "$env:VCPKG_ROOT\installed\x64-windows"

$vs = (Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\Common7\Tools\VsDevCmd.bat' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1).FullName

cmd /c "`\"$vs`\" -arch=amd64 && cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=%CMAKE_TOOLCHAIN_FILE% -DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH% -DCMAKE_BUILD_TYPE=Release && ninja -C build-win && build-win\crib_guard_pi.exe"
```

If you want to only build (no auto-run), remove the final `&& build-win\crib_guard_pi.exe`.

## Project structure (high level)

- `CMakeLists.txt` — top-level CMake
- `CMakePresets.json` — configure/build presets (includes `win-rel`)
- `platforms/pi-sdl/main.cpp` — simulator entry (SDL backend)
- `ui/ui.cpp`, `ui/ui.h` — UI implementation
- `config/lv_conf.h` — LVGL configuration
- `build-win/` — generated build output (Ninja files, executable)

Executable: `build-win/crib_guard_pi.exe`

Note: there is an alternate `main.cpp` (v2) available that tightens layout and adds keyboard shortcuts — not applied by default.

## VS Code integration

Create `.vscode/tasks.json` and `.vscode/launch.json` for one-click build/run and debugging. Example `tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    { "label": "Configure (win-rel)", "type": "shell", "command": "cmake --preset win-rel" },
    { "label": "Build (win-rel)", "type": "shell", "command": "cmake --build --preset win-rel", "dependsOn": "Configure (win-rel)", "problemMatcher": "$msCompile" },
    { "label": "Run simulator", "type": "shell", "command": ".\\build-win\\crib_guard_pi.exe" },
    { "label": "Build & Run (win-rel)", "type": "shell", "dependsOn": ["Build (win-rel)"], "command": ".\\build-win\\crib_guard_pi.exe" }
  ]
}
```

Example `launch.json` for the Microsoft C++ debugger:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug LVGL Simulator (win-rel)",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}\\build-win\\crib_guard_pi.exe",
      "cwd": "${workspaceFolder}",
      "preLaunchTask": "Build (win-rel)"
    }
  ]
}
```

Usage: `Ctrl+Shift+B` → Build & Run (win-rel). Press `F5` to debug.

## Configuration reference

`CMakePresets.json` includes a `win-rel` preset used by the instructions above. Important fields:

- `CMAKE_TOOLCHAIN_FILE` — point to `C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake` when using vcpkg
- `VCPKG_TARGET_TRIPLET` — typically `x64-windows`
- `jobs` in buildPresets — set to `0` to let Ninja/CMake pick cores automatically

If you see an error like `jobs expected an integer, got: max`, set `jobs` to `0`.

## Testing & diagnostics

Run the simulator and observe stdout/stderr:

```powershell
.\build-win\crib_guard_pi.exe
```

If enabled, view the sim log:

```powershell
cd build-win
Get-Content .\sim.log -Tail 200 -Wait
```

Check executable DLL dependencies (example using dumpbin):

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\<version>\bin\Hostx64\x64\dumpbin.exe" /DEPENDENTS .\build-win\crib_guard_pi.exe
```

You should see `SDL2.dll` and MSVC runtime DLLs listed.

## Troubleshooting

- Simulator closes immediately: run from PowerShell to view errors:

```powershell
.\build-win\crib_guard_pi.exe
```

- If it still exits, perform a clean rebuild (see 'Clean rebuild' above).
- "SDL headers not found": ensure `LV_USE_SDL` is enabled in `config/lv_conf.h` and reconfigure with the vcpkg toolchain file set.

## Roadmap / optional improvements

- Replace `platforms/pi-sdl/main.cpp` with a v2 that adds keyboard shortcuts and tighter layout.
  - Suggested shortcuts: `1`=Calm, `2`=Cry, `3`=Motion, `P`=Play, `S`=Stop, `←/→` volume, `Esc` quit.
- Add SquareLine Studio workflow to allow visual UI authoring and safe `ui/` module overwrites.
- Add simulated data feeds (fake Calm/Cry/Motion messages) for faster UI iteration.
- Target hardware ports: ESP32-S3 panel, Raspberry Pi Touch Display.

## Contributing

Contributions welcome. Please open issues or pull requests for bug fixes, improvements and platform ports.

## License

See `LICENSE` (if present) or add your preferred license.

---

If you'd like, I can also:

- add a short badge/header with build status or platform notes
- commit a `tasks.json` / `launch.json` example into `.vscode/`
- provide the v2 `main.cpp` and apply it behind a feature branch

— end of README