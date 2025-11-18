#!/usr/bin/env bash
set -euo pipefail

# Build CribGuard on the Raspberry Pi (Release, Ninja)
# Run from anywhere on the Pi.

REPO_DIR="/home/pi/CribGuard-Display"
BUILD_DIR="${REPO_DIR}/build-pi"

if [[ ! -d "${REPO_DIR}" ]]; then
  echo "Repo not found at ${REPO_DIR}. Clone or copy it first."
  exit 1
fi

cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "Build complete. Run with:"
echo "  SDL_VIDEODRIVER=kmsdrm ${BUILD_DIR}/crib_guard_pi"


