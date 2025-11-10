CribGuard Display — LVGL v9 + SDL2 Simulator (Windows)

## Build & Run (Windows) — start here

This is the minimal, copy‑pasteable flow to get a simulator window on screen.

### A) One‑time setup

```powershell
# 1) Install MSVC toolchain + Ninja
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install -e --id Ninja-build.Ninja

# 2) Install vcpkg
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg 2>$null
C:\dev\vcpkg\bootstrap-vcpkg.bat

# 3) (If not already installed) GStreamer SDK via vcpkg (takes a while)
C:\dev\vcpkg\vcpkg.exe install gstreamer[core,plugins-base,plugins-good,plugins-bad,plugins-ugly]:x64-windows
```

### B) Build & run in THIS terminal (every session)

```powershell
# 1) Load MSVC into this shell
$vs = "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if (!(Test-Path $vs)) { $vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" }
cmd /c """$vs"" -arch=x64 -host_arch=x64 && set" | % { if ($_ -match "^(.*?)=(.*)$") { Set-Item env:$($matches[1]) $matches[2] } }

# 2) Configure (fresh dir) and build
cd C:\Users\johnh\School\CPE190\CribGuard-Display
if (Test-Path .\build-win-vcpkg) { Remove-Item .\build-win-vcpkg -Recurse -Force }
cmake -S . -B build-win-vcpkg -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-win-vcpkg -j

# 3) Run (make sure DLLs are found and window is visible)
$env:PATH += ";C:\dev\vcpkg\installed\x64-windows\bin"
$env:SDL_VIDEO_CENTERED = "1"; $env:SDL_VIDEO_WINDOW_POS = "100,100"
.\build-win-vcpkg\crib_guard_pi.exe
```

If SDL2.dll is missing, copy the one already included in `build-win`:

```powershell
Copy-Item .\build-win\SDL2.dll .\build-win-vcpkg\ -Force
.\build-win-vcpkg\crib_guard_pi.exe
```

Diagnostics:

```powershell
Get-Content .\sim.log -Tail 100
```

## Current UI (what you should see)

- Top bar: brand label plus quick-action “Quiet” and “Connected” pills; buttons for Library, Lullabies, Cam, Settings.
- Dashboard card: hero status ring (Calm/Cry/Motion color) and stats tiles (Connection, Volume, Quiet Hours).
- Camera modal: header actions (Snapshot, Fullscreen, Close), video surface placeholder, Play/Pause/Stop, Mute, spinner shown briefly on Play.
- Library modal: full-screen sheet with tabs (All, Photos, Recordings), placeholder content.
- Lullabies modal: full-screen sheet, placeholder content.
- Settings modal: dark-themed sheet (Default Volume slider, Quiet Hours enable, start/end hours).

Log lines in `sim.log` include exact camera window and render-area sizes on open/fullscreen toggle.

### Simulator input

- Touch-first: tap/click to interact; modals close via Close button or backdrop tap (no keyboard shortcuts).
- Mouse wheel is enabled for scrolling tests (e.g., Library/Lullabies lists).

## Repository structure (high level)

- `platforms/pi-sdl/main.cpp` — LVGL v9 + SDL simulator entry and all current UI.
- `config/lv_conf.h` — LVGL configuration (ensure `LV_USE_SDL 1`).
- `build-win-vcpkg/` — generated build output (after configure).
- `assets/` — images/fonts (if any).
- `.gitignore` — ignores build outputs and logs.

## Common tasks (cheat sheet)

```powershell
# Reconfigure in a fresh dir
if (Test-Path .\build-win-vcpkg) { Remove-Item .\build-win-vcpkg -Recurse -Force }
cmake -S . -B build-win-vcpkg -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows

# Build + run
cmake --build build-win-vcpkg -j
$env:PATH += ";C:\dev\vcpkg\installed\x64-windows\bin"
$env:SDL_VIDEO_CENTERED = "1"; $env:SDL_VIDEO_WINDOW_POS = "100,100"
.\build-win-vcpkg\crib_guard_pi.exe

# Tail logs
Get-Content .\sim.log -Tail 100 -Wait
```

## Troubleshooting (quick)

- No window shows up: set `SDL_VIDEO_CENTERED=1` and `SDL_VIDEO_WINDOW_POS=100,100` (see commands above).
- Missing SDL2.dll: copy from `build-win/SDL2.dll` to `build-win-vcpkg/`.
- “Compiler not found”: load MSVC env with `VsDevCmd.bat` as shown in Build & Run.

## Raspberry Pi (Bookworm) quick reference

On each Pi:

```bash
sudo apt update
sudo apt install -y gstreamer1.0-tools gstreamer1.0-plugins-{base,good,bad,ugly} gstreamer1.0-libav gstreamer1.0-gl
# Camera Pi (libcamera source)
sudo apt install -y libcamera-apps gstreamer1.0-libcamera || true
```

We’ll add receiver (Display Pi) and sender (Camera Pi) commands once streaming is wired in this repo.

## Roadmap / optional improvements

- Near-term
  - GStreamer test pipeline (videotestsrc → appsink) rendered into the camera video surface; Play/Pause/Stop control the pipeline.
  - RTP/UDP H.264 receiver (udpsrc → rtph264depay → avdec_h264 → convert/scale → appsink); auto-fit to live `video_surface` size.
  - Library: wire thumbnails grid, detail viewer, basic delete/export; storage path and quota in Settings.
  - Lullabies: preset list (play/stop/loop, per-track volume), optional sleep timer.
  - Settings: stream host/port and latency; recording/photo quality; storage quota and auto-delete policy.

- Longer-term
  - Recording controls (REC toggle, timer overlay), snapshots; write media to disk and surface in Library.
  - Basic analytics and health overlay (FPS, drops, CPU).
  - Optional keyboard shortcuts for simulator.

## Contributing

Contributions welcome. Please open issues or pull requests for bug fixes, improvements and platform ports.

## License

See `LICENSE` (if present) or add your preferred license.