#!/usr/bin/env bash
set -euo pipefail

# Launch CribGuard UI on Raspberry Pi OS Lite using SDL KMS/DRM.
# Copy this to /usr/local/bin/cribguard-run.sh and chmod +x it.

APP_DIR="/home/pi/CribGuard-Display"
BIN="${APP_DIR}/build-pi/crib_guard_pi"

cd "${APP_DIR}"

export SDL_VIDEODRIVER=kmsdrm
# Use ALSA for audio in future streaming/audio work
export SDL_AUDIODRIVER=alsa

# If the panel is mounted portrait but you want landscape, uncomment one:
# export SDL_VIDEO_KMSDRM_ROTATION=90
# export SDL_VIDEO_KMSDRM_ROTATION=270

# Keep the console from suspending mid-demo (best-effort, harmless if unavailable)
echo 0 | sudo tee /sys/devices/platform/vconsole/console_suspend >/dev/null 2>&1 || true

exec "${BIN}" >> "${APP_DIR}/sim.log" 2>&1


