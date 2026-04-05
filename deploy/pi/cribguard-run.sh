#!/usr/bin/env bash
set -euo pipefail

# Launch CribGuard UI on Raspberry Pi OS Bookworm (Pi 5) using SDL KMS/DRM.
#
# Runtime overrides (from systemd EnvironmentFile or shell):
# - CRIBGUARD_USER               user account that owns the repo
# - CRIBGUARD_HOME               home directory for CRIBGUARD_USER
# - CRIBGUARD_REPO_DIR           repository root (default: <home>/CribGuard-Display)
# - CRIBGUARD_BUILD_DIR          build dir (default: <repo>/build-pi)
# - SDL_VIDEODRIVER              default kmsdrm
# - SDL_AUDIODRIVER              default alsa
# - SDL_VIDEO_KMSDRM_ROTATION    default 90 (landscape on official 7" DSI)

RUN_USER="${CRIBGUARD_USER:-$(id -un)}"
if [[ -z "${CRIBGUARD_HOME:-}" ]]; then
  CRIBGUARD_HOME="$(getent passwd "${RUN_USER}" | cut -d: -f6 || true)"
fi
if [[ -z "${CRIBGUARD_HOME:-}" ]]; then
  CRIBGUARD_HOME="${HOME:-/home/${RUN_USER}}"
fi

REPO_DIR="${CRIBGUARD_REPO_DIR:-${CRIBGUARD_HOME}/CribGuard-Display}"
BUILD_DIR="${CRIBGUARD_BUILD_DIR:-${REPO_DIR}/build-pi}"
BIN="${CRIBGUARD_BIN:-${BUILD_DIR}/crib_guard_pi}"
LOG_FILE="${CRIBGUARD_LOG_FILE:-${REPO_DIR}/sim.log}"

if [[ ! -x "${BIN}" ]]; then
  echo "[CribGuard] binary not found: ${BIN}" >&2
  echo "[CribGuard] build first (cmake --preset pi5-rel && cmake --build --preset pi5-rel)" >&2
  exit 1
fi

mkdir -p "$(dirname "${LOG_FILE}")"
cd "${REPO_DIR}"

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_VIDEO_KMSDRM_ROTATION="${SDL_VIDEO_KMSDRM_ROTATION:-90}"

if [[ -w /sys/devices/platform/vconsole/console_suspend ]]; then
  echo 0 > /sys/devices/platform/vconsole/console_suspend || true
fi

exec "${BIN}" >> "${LOG_FILE}" 2>&1

