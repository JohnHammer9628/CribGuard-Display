#!/bin/bash
# Launch the gst-launch pipeline (RTP to parent + raw GRAY8 tee to localhost)
# and the Python detector that consumes the raw tee, as children of one
# process group so Flask's start/stop signals reach both.

set -u

PARENT_IP="${1:-192.168.50.1}"
PARENT_PORT="${2:-5000}"
DETECTOR_PORT="${CG_DETECTOR_LISTEN_PORT:-5556}"

# Detector first so the UDP socket is listening before gst starts sending.
python3 /home/jammin/lepton_detector.py &
DETECTOR_PID=$!

cleanup() {
    kill "$DETECTOR_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Foreground the streaming pipeline. `exec` replaces this shell so the PID
# Flask tracks is the gstreamer process, and the trap still fires on SIGTERM
# because the shell owns the process group.
exec /usr/bin/gst-launch-1.0 -q \
    v4l2src device=/dev/video0 ! \
    video/x-raw,format=UYVY,width=160,height=120,framerate=9/1 ! \
    tee name=t \
    t. ! queue leaky=downstream max-size-buffers=4 ! videoconvert ! \
         openh264enc bitrate=500000 complexity=low ! \
         rtph264pay pt=96 config-interval=1 ! \
         udpsink host="$PARENT_IP" port="$PARENT_PORT" \
    t. ! queue leaky=downstream max-size-buffers=2 ! videoconvert ! \
         video/x-raw,format=GRAY8 ! \
         udpsink host=127.0.0.1 port="$DETECTOR_PORT" sync=false
