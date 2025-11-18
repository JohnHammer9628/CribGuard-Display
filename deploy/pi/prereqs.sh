#!/usr/bin/env bash
set -euo pipefail

# CribGuard Pi prerequisites installer (Bookworm Lite)
# Run this on the Raspberry Pi (not on your PC).

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "Please run with sudo: sudo $0"
  exit 1
fi

apt update
apt install -y \
  git cmake build-essential ninja-build libsdl2-dev \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-gl

# Ensure the default user has access to KMS and input devices.
DEFAULT_USER="${SUDO_USER:-pi}"
if id "${DEFAULT_USER}" >/dev/null 2>&1; then
  usermod -aG video,input "${DEFAULT_USER}"
  echo "Added ${DEFAULT_USER} to groups: video,input (reboot or re-login required)."
fi

echo "Prerequisites installed. Reboot recommended."


