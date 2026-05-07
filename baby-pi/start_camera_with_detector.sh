#!/bin/bash
# Thin wrapper. The Python streamer owns the v4l2 capture, the detector,
# and both gst-launch subprocesses (ingest + H264 output). One signal to
# this process tears the whole stack down.

set -u

PARENT_IP="${1:-192.168.50.1}"
PARENT_PORT="${2:-5000}"
DETECTOR_PORT="${CG_DETECTOR_LISTEN_PORT:-5556}"

exec python3 /home/jammin/lepton_streamer.py "$PARENT_IP" "$PARENT_PORT" "$DETECTOR_PORT"
