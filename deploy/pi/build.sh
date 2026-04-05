#!/usr/bin/env bash
set -euo pipefail

# Build CribGuard on Raspberry Pi (Release, Ninja).
# Run from anywhere.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR_DEFAULT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
REPO_DIR="${CRIBGUARD_REPO_DIR:-${REPO_DIR_DEFAULT}}"
BUILD_DIR="${CRIBGUARD_BUILD_DIR:-${REPO_DIR}/build-pi}"

if [[ ! -d "${REPO_DIR}" ]]; then
  echo "Repo not found at ${REPO_DIR}. Clone or copy it first."
  exit 1
fi

if (cd "${REPO_DIR}" && cmake --list-presets 2>/dev/null | grep -q "\"pi5-rel\""); then
  (
    cd "${REPO_DIR}"
    cmake --preset pi5-rel
    cmake --build --preset pi5-rel -j"$(nproc)"
  )
else
  cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

echo "Build complete. Run with:"
echo "  SDL_VIDEODRIVER=kmsdrm ${BUILD_DIR}/crib_guard_pi"
echo
echo "Native Lepton monitor target (if dependencies found):"
echo "  cmake --build ${BUILD_DIR} -j\"$(nproc)\" --target lepton_monitor_service"
echo "  ${BUILD_DIR}/lepton_monitor_service --config ${REPO_DIR}/baby-pi/lepton_monitor_config.yaml --headless"
