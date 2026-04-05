#!/usr/bin/env bash
set -euo pipefail

# Install cry detector script + systemd unit on Baby Pi.
#
# Usage:
#   bash baby-pi/install-cry-detector.sh
# Optional:
#   CRY_USER=jammin CRY_AUDIO_DEVICE=plughw:2,0 bash baby-pi/install-cry-detector.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CRY_USER="${CRY_USER:-${SUDO_USER:-$(id -un)}}"
if [[ "${CRY_USER}" == "root" ]]; then
  echo "ERROR: CRY_USER resolved to root. Set CRY_USER to your normal user." >&2
  exit 1
fi

CRY_AUDIO_DEVICE="${CRY_AUDIO_DEVICE:-plughw:2,0}"
CRY_API_BASE="${CRY_API_BASE:-http://127.0.0.1:8000}"
CRY_PLAY_DEVICE="${CRY_PLAY_DEVICE:-default}"
CRY_ENABLE_AUTO_PLAY="${CRY_ENABLE_AUTO_PLAY:-1}"
CRY_LULLABY_COOLDOWN_SEC="${CRY_LULLABY_COOLDOWN_SEC:-90}"
CRY_SUPPRESS_AFTER_PLAY_SEC="${CRY_SUPPRESS_AFTER_PLAY_SEC:-20}"
CRY_SUPPRESS_WHILE_PLAYING="${CRY_SUPPRESS_WHILE_PLAYING:-1}"
CRY_PLAYBACK_POLL_SEC="${CRY_PLAYBACK_POLL_SEC:-1.0}"
CRY_PLAYBACK_PEEK_SEC="${CRY_PLAYBACK_PEEK_SEC:-2.0}"
CRY_PLAYBACK_PEEK_INTERVAL_SEC="${CRY_PLAYBACK_PEEK_INTERVAL_SEC:-15.0}"
CRY_PLAYBACK_DELTA_BOOST_DB="${CRY_PLAYBACK_DELTA_BOOST_DB:-6}"
CRY_MODEL_PATH="${CRY_MODEL_PATH:-}"
CRY_MODEL_THRESHOLD="${CRY_MODEL_THRESHOLD:-0.65}"
CRY_MODEL_USE_ONLY="${CRY_MODEL_USE_ONLY:-1}"
CRY_MODEL_WINDOW_SEC="${CRY_MODEL_WINDOW_SEC:-1.0}"
CRY_MODEL_EVAL_INTERVAL_SEC="${CRY_MODEL_EVAL_INTERVAL_SEC:-0.25}"
CRY_MIN_DB="${CRY_MIN_DB:--42}"
CRY_DELTA_DB="${CRY_DELTA_DB:-12}"
CRY_SUSTAIN_SEC="${CRY_SUSTAIN_SEC:-2.5}"
CRY_RELEASE_SEC="${CRY_RELEASE_SEC:-4.0}"
CRY_CHUNK_MS="${CRY_CHUNK_MS:-250}"
CRY_SAMPLE_RATE="${CRY_SAMPLE_RATE:-16000}"
CRY_CHANNELS="${CRY_CHANNELS:-1}"
CRY_LULLABIES_DIR="${CRY_LULLABIES_DIR:-/home/${CRY_USER}/Lullabies}"

SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
  SUDO="sudo"
fi

echo "[cry] Installing runtime packages..."
${SUDO} apt-get update -y
${SUDO} apt-get install -y alsa-utils python3 python3-numpy python3-sklearn python3-joblib

echo "[cry] Installing script..."
${SUDO} install -m 0755 "${REPO_DIR}/baby-pi/cry_detector_service.py" /usr/local/bin/cribguard-cry-detector.py

echo "[cry] Installing service unit..."
${SUDO} install -m 0644 "${REPO_DIR}/baby-pi/cribguard-cry-detector@.service" /etc/systemd/system/cribguard-cry-detector@.service

echo "[cry] Writing /etc/default/cribguard-cry-detector ..."
TMP_ENV="$(mktemp)"
cat > "${TMP_ENV}" <<EOF
# CribGuard cry detector runtime config
CRY_AUDIO_DEVICE="${CRY_AUDIO_DEVICE}"
CRY_API_BASE="${CRY_API_BASE}"
CRY_PLAY_DEVICE="${CRY_PLAY_DEVICE}"
CRY_ENABLE_AUTO_PLAY="${CRY_ENABLE_AUTO_PLAY}"
CRY_LULLABY_COOLDOWN_SEC="${CRY_LULLABY_COOLDOWN_SEC}"
CRY_SUPPRESS_AFTER_PLAY_SEC="${CRY_SUPPRESS_AFTER_PLAY_SEC}"
CRY_SUPPRESS_WHILE_PLAYING="${CRY_SUPPRESS_WHILE_PLAYING}"
CRY_PLAYBACK_POLL_SEC="${CRY_PLAYBACK_POLL_SEC}"
CRY_PLAYBACK_PEEK_SEC="${CRY_PLAYBACK_PEEK_SEC}"
CRY_PLAYBACK_PEEK_INTERVAL_SEC="${CRY_PLAYBACK_PEEK_INTERVAL_SEC}"
CRY_PLAYBACK_DELTA_BOOST_DB="${CRY_PLAYBACK_DELTA_BOOST_DB}"
CRY_MODEL_PATH="${CRY_MODEL_PATH}"
CRY_MODEL_THRESHOLD="${CRY_MODEL_THRESHOLD}"
CRY_MODEL_USE_ONLY="${CRY_MODEL_USE_ONLY}"
CRY_MODEL_WINDOW_SEC="${CRY_MODEL_WINDOW_SEC}"
CRY_MODEL_EVAL_INTERVAL_SEC="${CRY_MODEL_EVAL_INTERVAL_SEC}"
CRY_MIN_DB="${CRY_MIN_DB}"
CRY_DELTA_DB="${CRY_DELTA_DB}"
CRY_SUSTAIN_SEC="${CRY_SUSTAIN_SEC}"
CRY_RELEASE_SEC="${CRY_RELEASE_SEC}"
CRY_CHUNK_MS="${CRY_CHUNK_MS}"
CRY_SAMPLE_RATE="${CRY_SAMPLE_RATE}"
CRY_CHANNELS="${CRY_CHANNELS}"
CRY_LULLABIES_DIR="${CRY_LULLABIES_DIR}"
CRY_STATUS_FILE="/tmp/cribguard_cry_status.json"
EOF
${SUDO} install -m 0644 "${TMP_ENV}" /etc/default/cribguard-cry-detector
rm -f "${TMP_ENV}"

echo "[cry] Enabling service for user ${CRY_USER} ..."
${SUDO} systemctl daemon-reload
${SUDO} systemctl enable --now "cribguard-cry-detector@${CRY_USER}.service"

echo "[cry] Installed."
echo "Status: sudo systemctl status cribguard-cry-detector@${CRY_USER}.service"
echo "Logs:   journalctl -u cribguard-cry-detector@${CRY_USER}.service -b -f"
