CribGuard Display — LVGL v9 + SDL2 Simulator (Windows)

## TL;DR — Quick Start (Raspberry Pi)

Pick ONE path and paste these blocks over SSH. Replace `<user>` and `<IP>` where shown. If your user is `jammin`, use that.

### Option 1: Desktop (shows over Pi homescreen) — easiest
Terminal: Raspberry Pi (SSH)
```bash
# On the Pi (SSH)
sudo apt update && sudo apt install -y git cmake build-essential ninja-build libsdl2-dev
sudo usermod -aG video,input $USER && sudo reboot
```
Terminal: Windows PowerShell (on your PC — any folder)
```powershell
# From your PC (PowerShell) — copy the project to the Pi
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" <user>@<IP>:/home/<user>/
```
Terminal: Raspberry Pi (SSH)
```bash
# Back on the Pi (SSH)
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"

# Make it auto-start on Desktop login
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard-run.sh /usr/local/bin/cribguard-run.sh
sudo sed -i 's|/home/pi|/home/<user>|g' /usr/local/bin/cribguard-run.sh
sudo sed -i '/^export SDL_VIDEODRIVER=/d;/^export SDL_VIDEO_KMSDRM_ROTATION=/d' /usr/local/bin/cribguard-run.sh
echo 'unset SDL_VIDEODRIVER' | sudo tee -a /usr/local/bin/cribguard-run.sh
echo 'export DISPLAY=:0'      | sudo tee -a /usr/local/bin/cribguard-run.sh
sudo chmod +x /usr/local/bin/cribguard-run.sh

mkdir -p /home/<user>/.config/autostart
cat > /home/<user>/.config/autostart/cribguard.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=CribGuard
Exec=/usr/local/bin/cribguard-run.sh
X-GNOME-Autostart-enabled=true
EOF
chown <user>:<user> /home/<user>/.config/autostart/cribguard.desktop
sudo reboot
```

### Option 2: Kiosk (no Desktop) — most robust for demos
Terminal: Raspberry Pi (SSH)
```bash
# On the Pi (SSH)
sudo apt update && sudo apt install -y git cmake build-essential ninja-build libsdl2-dev
sudo usermod -aG video,input $USER && sudo reboot
```
Terminal: Windows PowerShell (on your PC — any folder)
```powershell
# From your PC (PowerShell) — copy the project to the Pi
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" <user>@<IP>:/home/<user>/
```
Terminal: Raspberry Pi (SSH)
```bash
# Back on the Pi (SSH)
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"

# Install launcher
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard-run.sh /usr/local/bin/cribguard-run.sh
sudo sed -i 's|/home/pi|/home/<user>|g' /usr/local/bin/cribguard-run.sh
sudo sed -i '/^export DISPLAY=/d;/^unset SDL_VIDEODRIVER/d' /usr/local/bin/cribguard-run.sh
echo 'export SDL_VIDEODRIVER=kmsdrm'       | sudo tee -a /usr/local/bin/cribguard-run.sh
echo 'export SDL_VIDEO_KMSDRM_ROTATION=90' | sudo tee -a /usr/local/bin/cribguard-run.sh
sudo chmod +x /usr/local/bin/cribguard-run.sh

# Install service
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard.service /etc/systemd/system/cribguard.service
sudo sed -i 's|/home/pi|/home/<user>|g' /etc/systemd/system/cribguard.service
sudo sed -i 's/^User=.*/User=<user>/'    /etc/systemd/system/cribguard.service
sudo sed -i 's/^Group=.*/Group=<user>/'  /etc/systemd/system/cribguard.service
sudo systemctl daemon-reload
sudo systemctl enable --now cribguard.service
```



## Build & Run (Windows) — start here

This is the minimal, copy‑pasteable flow to get a simulator window on screen.

### A) One‑time setup

Terminal: Windows PowerShell (on your PC — any folder)
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

Terminal: Windows PowerShell (on your PC — project directory)
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
Terminal: Windows PowerShell (on your PC — project directory)
```powershell
Copy-Item .\build-win\SDL2.dll .\build-win-vcpkg\ -Force
.\build-win-vcpkg\crib_guard_pi.exe
```

Diagnostics:
Terminal: Windows PowerShell (on your PC — project directory)
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

## Beginner Step‑by‑Step (Desktop path)

Follow these numbered steps exactly. Do each step before moving on.

1) On the Pi, enable SSH (so you can control from your PC)
Terminal: Raspberry Pi (Terminal or SSH)
```bash
sudo systemctl enable --now ssh
```

2) On the Pi, find its IP (note the first address, e.g., 10.0.0.98)
Terminal: Raspberry Pi (Terminal or SSH)
```bash
hostname -I
```

3) On your PC, copy this project to the Pi (replace `<user>` and `<IP>`)
Terminal: Windows PowerShell (on your PC — any folder)
```powershell
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" <user>@<IP>:/home/<user>/
```

4) On the Pi, install packages (one line)
Terminal: Raspberry Pi (SSH)
```bash
sudo apt update && sudo apt install -y git cmake build-essential ninja-build libsdl2-dev
```

5) On the Pi, allow display/touch groups and reboot
Terminal: Raspberry Pi (SSH)
```bash
sudo usermod -aG video,input $USER
sudo reboot
```

6) On the Pi (after reboot), configure and build
Terminal: Raspberry Pi (SSH)
```bash
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"
```

7) Test run (should open on the homescreen)
Terminal: Raspberry Pi (SSH)
```bash
DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi
```

8) Make it auto‑start on login (Desktop autostart)
Terminal: Raspberry Pi (SSH)
```bash
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard-run.sh /usr/local/bin/cribguard-run.sh
sudo sed -i 's|/home/pi|/home/<user>|g' /usr/local/bin/cribguard-run.sh
sudo sed -i '/^export SDL_VIDEODRIVER=/d;/^export SDL_VIDEO_KMSDRM_ROTATION=/d' /usr/local/bin/cribguard-run.sh
echo 'unset SDL_VIDEODRIVER' | sudo tee -a /usr/local/bin/cribguard-run.sh
echo 'export DISPLAY=:0'      | sudo tee -a /usr/local/bin/cribguard-run.sh
sudo chmod +x /usr/local/bin/cribguard-run.sh
```

9) Create the autostart entry
Terminal: Raspberry Pi (SSH)
```bash
mkdir -p /home/<user>/.config/autostart
cat > /home/<user>/.config/autostart/cribguard.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=CribGuard
Exec=/usr/local/bin/cribguard-run.sh
X-GNOME-Autostart-enabled=true
EOF
chown <user>:<user> /home/<user>/.config/autostart/cribguard.desktop
```

10) Reboot (the UI should appear automatically)
Terminal: Raspberry Pi (SSH)
```bash
sudo reboot
```

11) Control cheatsheet (from SSH)
Terminal: Raspberry Pi (SSH)
```bash
# Stop current UI instance (Desktop autostart stays in place)
pkill -f crib_guard_pi
# Manual run on Desktop
DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi
# App log
tail -n 100 /home/<user>/CribGuard-Display/sim.log
```

12) Switch to Kiosk later (most robust demo) — see “Raspberry Pi 4 + 7" Touch — E) Run (Kiosk mode)” above.

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

## Raspberry Pi 4 + 7" Touch (Bookworm) — end‑to‑end

This project uses LVGL v9 with the SDL backend. You can run the UI on a Raspberry Pi either:

- As a Desktop app (auto-starts when the user logs in to the GUI) — easiest for beginners
- As a Kiosk service with KMS/DRM (no Desktop needed; most robust for demos)

The code already:
- Auto-detects the active display mode and normalizes to landscape (falls back to 1280×720)
- Uses fullscreen by default (`config/lv_conf.h`: `LV_SDL_FULLSCREEN=1`)
- Disables LVGL’s ASM-optimized paths to ensure stable builds on Pi

### A) One-time OS packages (Pi)
Terminal: Raspberry Pi (SSH)

```bash
sudo apt update
sudo apt install -y git cmake build-essential ninja-build libsdl2-dev
# Optional now; useful later for camera/audio streaming work:
sudo apt install -y gstreamer1.0-tools gstreamer1.0-plugins-{base,good,bad,ugly} gstreamer1.0-gl
# Some images don’t have libav/ffmpeg packages; that’s OK to skip.
sudo usermod -aG video,input $USER
sudo reboot
```

Tip: all Pi-side commands can be run over SSH (recommended) so you don’t need a Pi keyboard/mouse.

### B) Get the project onto the Pi

- From your Windows PC (PowerShell), copy the folder to the Pi (replace IP/username):

Terminal: Windows PowerShell (on your PC — any folder)
```powershell
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" jammin@10.0.0.98:/home/jammin/
```

Or clone your repo directly on the Pi into `/home/<user>/CribGuard-Display`.

### C) Build on the Pi
Terminal: Raspberry Pi (SSH)

```bash
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"
```

If the network blocks CMake’s `FetchContent` for LVGL:
Terminal: Raspberry Pi (SSH)

```bash
# Download LVGL v9.2.0 as a tarball and point CMake at it
mkdir -p /home/<user>/CribGuard-Display/extern/lvgl
wget -O /home/<user>/CribGuard-Display/extern/lvgl-9.2.0.tar.gz https://github.com/lvgl/lvgl/archive/refs/tags/v9.2.0.tar.gz
tar -xzf /home/<user>/CribGuard-Display/extern/lvgl-9.2.0.tar.gz -C /home/<user>/CribGuard-Display/extern/lvgl --strip-components=1
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_LVGL=/home/<user>/CribGuard-Display/extern/lvgl
cmake --build /home/<user>/CribGuard-Display/build-pi -j1
```

If you ever see assembler errors like “unknown mnemonic ‘typedef’” from `*.S` files, this repo already disables those ASM sources, but as a last resort you can move them aside:
Terminal: Raspberry Pi (SSH)

```bash
cd /home/<user>/CribGuard-Display/extern/lvgl
mkdir -p _asm.disabled
mv -v src/draw/sw/blend/helium _asm.disabled/ 2>/dev/null || true
mv -v src/draw/sw/blend/neon   _asm.disabled/ 2>/dev/null || true
```

### D) Run (Desktop mode — shows over the homescreen)

Manual test from SSH (renders onto the Pi’s screen):

Terminal: Raspberry Pi (SSH)
```bash
DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi
```

Auto-start on user login (recommended for beginners):
Terminal: Raspberry Pi (SSH)

```bash
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard-run.sh /usr/local/bin/cribguard-run.sh
sudo sed -i 's|/home/pi|/home/<user>|g' /usr/local/bin/cribguard-run.sh
sudo sed -i '/^export SDL_VIDEODRIVER=/d;/^export SDL_VIDEO_KMSDRM_ROTATION=/d' /usr/local/bin/cribguard-run.sh
echo 'unset SDL_VIDEODRIVER' | sudo tee -a /usr/local/bin/cribguard-run.sh
echo 'export DISPLAY=:0'      | sudo tee -a /usr/local/bin/cribguard-run.sh
sudo chmod +x /usr/local/bin/cribguard-run.sh

mkdir -p /home/<user>/.config/autostart
cat > /home/<user>/.config/autostart/cribguard.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=CribGuard
Exec=/usr/local/bin/cribguard-run.sh
X-GNOME-Autostart-enabled=true
EOF
chown <user>:<user> /home/<user>/.config/autostart/cribguard.desktop
```

Reboot to verify it comes up automatically:
Terminal: Raspberry Pi (SSH)

```bash
sudo reboot
```

### E) Run (Kiosk mode — no Desktop, best for demos)

Use this when you want the most robust, fast-boot experience (e.g., power‑bank demo). It runs on KMS/DRM and doesn’t require a GUI session.

Terminal: Raspberry Pi (SSH)
```bash
sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard-run.sh /usr/local/bin/cribguard-run.sh
sudo sed -i 's|/home/pi|/home/<user>|g' /usr/local/bin/cribguard-run.sh
sudo sed -i '/^export DISPLAY=/d;/^unset SDL_VIDEODRIVER/d' /usr/local/bin/cribguard-run.sh
echo 'export SDL_VIDEODRIVER=kmsdrm'       | sudo tee -a /usr/local/bin/cribguard-run.sh
# Touch Display 2 is portrait by default; rotate to landscape if needed:
echo 'export SDL_VIDEO_KMSDRM_ROTATION=90' | sudo tee -a /usr/local/bin/cribguard-run.sh
sudo chmod +x /usr/local/bin/cribguard-run.sh

sudo cp /home/<user>/CribGuard-Display/deploy/pi/cribguard.service /etc/systemd/system/cribguard.service
sudo sed -i 's|/home/pi|/home/<user>|g' /etc/systemd/system/cribguard.service
sudo sed -i 's/^User=.*/User=<user>/'    /etc/systemd/system/cribguard.service
sudo sed -i 's/^Group=.*/Group=<user>/'  /etc/systemd/system/cribguard.service
sudo systemctl daemon-reload
sudo systemctl enable --now cribguard.service
```

Optional: prevent console blanking on Lite
Terminal: Raspberry Pi (SSH)

```bash
echo 'consoleblank=0' | sudo tee -a /boot/firmware/cmdline.txt
sudo reboot
```

Optional: rotate the boot console/TTY permanently (7" DSI)
Terminal: Raspberry Pi (SSH)

```bash
echo 'dtoverlay=vc4-kms-dsi-7inch,rotate=270' | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

### F) SSH control (so you don’t need a Pi keyboard)

- Stop service: `sudo systemctl stop cribguard.service`
- Start service: `sudo systemctl start cribguard.service`
- Disable/enable autostart: `sudo systemctl disable|enable cribguard.service`
- Live logs: `journalctl -u cribguard.service -b -f`
- Manual Desktop run: `DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi`
- Manual KMS/DRM run: `SDL_VIDEODRIVER=kmsdrm SDL_VIDEO_KMSDRM_ROTATION=90 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi`

### G) Switching quickly between Desktop and Kiosk

- To Desktop autostart:
  - `sudo systemctl disable --now cribguard.service`
  - Edit `/usr/local/bin/cribguard-run.sh` to have:
    - `unset SDL_VIDEODRIVER`
    - `export DISPLAY=:0`
  - Create `~/.config/autostart/cribguard.desktop` as shown above and reboot.

- To Kiosk service:
  - `rm -f ~/.config/autostart/cribguard.desktop`
  - Edit `/usr/local/bin/cribguard-run.sh` to have:
    - `export SDL_VIDEODRIVER=kmsdrm`
    - `export SDL_VIDEO_KMSDRM_ROTATION=90` (if needed)
  - `sudo systemctl enable --now cribguard.service`

### H) Power off / reboot the Pi safely

Terminal: Raspberry Pi (SSH)
```bash
# Stop UI first (choose the one you use)
sudo systemctl stop cribguard.service  # if using Kiosk/service
pkill -f crib_guard_pi                 # if you started it manually on Desktop

# Safe shutdown (wait for screen off / LED activity to stop, then unplug)
sudo poweroff
```

Terminal: Raspberry Pi (SSH)
```bash
# Reboot
sudo reboot
```

## How to keep improving the UI (step‑by‑step)

Recommended fastest loop: edit on Windows (simulator), then deploy to Pi.

1) Open the UI source
- Main UI code: `platforms/pi-sdl/main.cpp`
- LVGL config: `config/lv_conf.h`
- Logs (Windows): `sim.log` in project root after running the sim

2) Run the Windows simulator (fast iteration)
Terminal: Windows PowerShell (on your PC — project directory)
```powershell
# From a dev PowerShell (see “Build & Run (Windows)” above)
cmake --build build-win-vcpkg -j
.\build-win-vcpkg\crib_guard_pi.exe
```
What to edit:
- Look for `build_ui()` to add buttons, labels, layouts.
- Style tokens live near the top (`namespace theme`).
- Event handlers are named `on_...` (e.g., `on_btn_play`, `on_top_quiet_click`).
- The dashboard and modals have `build_*` helpers (library, lullabies, camera, settings).

3) Common edits (where to make them)
- Add a new button: inside `build_ui()`, find the row where similar buttons are created; duplicate a `lv_btn_create(...)` block and hook an `lv_obj_add_event_cb(...)`.
- Add a modal/screen: create a `build_mything_dialog(parent)` function following `build_library_dialog` or `build_camera_dialog` as a pattern, then trigger it from a button.
- Update theme/colors/sizes: edit constants in `namespace theme` and rebuild.
- Update status/tiles: see `update_dashboard()` and the `g_stat_*` labels.

4) Rebuild after changes (Windows or Pi)
- Windows:
Terminal: Windows PowerShell (on your PC — project directory)
```powershell
cmake --build build-win-vcpkg -j
.\build-win-vcpkg\crib_guard_pi.exe
```
- Pi (Desktop run):
Terminal: Raspberry Pi (SSH)
```bash
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"
DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi
```
- Pi (Kiosk service):
Terminal: Raspberry Pi (SSH)
```bash
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"
sudo systemctl restart cribguard.service
```

5) Logs (find issues fast)
Terminal: Raspberry Pi (SSH)
```bash
# App log on Pi
tail -n 100 /home/<user>/CribGuard-Display/sim.log
# Service logs on Pi (Kiosk)
journalctl -u cribguard.service -b -f
```

6) Assets
- Put images/fonts in `assets/` (wire-up to LVGL when you start using them).
- Keep resolutions reasonable for the target display.

7) Source control
- Commit small UI changes frequently. Include screenshots of the UI for PRs.

## Roadmap / optional improvements

- Near-term
  - GStreamer test pipeline (videotestsrc → appsink) rendered into the camera video surface; Play/Pause/Stop control the pipeline.
  - Two‑Pi demo: RTP/UDP H.264 receiver on Display Pi (udpsrc → rtph264depay → avdec_h264 → convert/scale → appsink); Camera Pi sender (libcamera + x264enc + rtph264pay + udpsink).
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
