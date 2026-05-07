#!/bin/bash
# Thin wrapper. The Python streamer owns the v4l2 capture, the detector,
# and both gst-launch subprocesses (ingest + H264 output). One signal to
# this process tears the whole stack down.

set -u

PARENT_IP="${1:-192.168.50.1}"
PARENT_PORT="${2:-5000}"
DETECTOR_PORT="${CG_DETECTOR_LISTEN_PORT:-5556}"

# Wetness ROI box, raw pixels in the 160x120 Lepton frame. Edit and restart
# cribguard-camera to apply. The detector logs the resolved box at startup
# so you can verify in `journalctl -u cribguard-camera`.
export CG_WET_X_START="${CG_WET_X_START:-80}"
export CG_WET_Y_START="${CG_WET_Y_START:-0}"
export CG_WET_X_END="${CG_WET_X_END:-160}"
export CG_WET_Y_END="${CG_WET_Y_END:-120}"

exec python3 /home/jammin/lepton_streamer.py "$PARENT_IP" "$PARENT_PORT" "$DETECTOR_PORT"
