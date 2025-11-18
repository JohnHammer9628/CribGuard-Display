#!/usr/bin/env bash
set -euo pipefail

# Prevent console blanking on Raspberry Pi OS Lite.
# Appends consoleblank=0 to /boot/firmware/cmdline.txt if not already present.

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
  echo "Please run with sudo: sudo $0"
  exit 1
fi

CMDLINE="/boot/firmware/cmdline.txt"
if ! grep -q "consoleblank=" "${CMDLINE}"; then
  sed -i 's/$/ consoleblank=0/' "${CMDLINE}"
  echo "Added consoleblank=0. Reboot required."
else
  echo "consoleblank already set. No changes made."
fi


