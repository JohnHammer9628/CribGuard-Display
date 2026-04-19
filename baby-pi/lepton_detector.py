#!/usr/bin/env python3
"""
Lepton wetness detector (Baby Pi).

Reads raw GRAY8 160x120 frames from a local UDP socket (fed by a `gst-launch`
pipeline that tees off the main RTP stream), runs a simple EMA baseline vs.
delta detector on the lower portion of the frame, and writes wet_alert /
wet_clear / status JSON lines to the events file that the Flask
/api/wet_status endpoint already tails.

Intentionally uses only the stdlib + numpy — no cv2, no gst-python bindings.
"""

import json
import os
import socket
import sys
import time
from datetime import datetime, timezone

import numpy as np

WIDTH, HEIGHT, FPS = 160, 120, 9
FRAME_SIZE = WIDTH * HEIGHT

EVENTS_FILE = os.environ.get("CG_EVENTS_FILE", "/tmp/crib_monitor_events.jsonl")
LISTEN_HOST = os.environ.get("CG_DETECTOR_LISTEN_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("CG_DETECTOR_LISTEN_PORT", "5556"))

WET_Y_START = 0.45
BASELINE_ALPHA = 0.02            # EMA rate when idle
BASELINE_ALPHA_LATCHED = 0.005   # slower drift while latched, so a mis-latch eventually clears
COLD_DELTA = -12.0
WARM_DELTA = 12.0
ENTER_AREA = 1800                # hard to latch: avoids colormap-rescale noise
EXIT_AREA = 900                  # clear faster once committed
DOMINANCE = 1.5                  # the "winning" channel must be this much bigger than the other
PERSIST_SEC = 4.0
STATUS_EVERY_N_FRAMES = 18       # ~2s at 9fps — more frequent status for debugging


def iso_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def emit(event_type, state, reason, cold_area=0, warm_area=0, ambient=0.0, confidence=0.0):
    rec = {
        "ts": iso_now(),
        "event_type": event_type,
        "state": state,
        "reason": reason,
        "cold_area": int(cold_area),
        "warm_area": int(warm_area),
        "ambient_c": float(round(ambient, 3)),
        "confidence": float(round(confidence, 3)),
    }
    line = json.dumps(rec)
    try:
        with open(EVENTS_FILE, "a") as f:
            f.write(line + "\n")
    except OSError as e:
        print(f"[ERR] write events: {e}", flush=True)
    print(line, flush=True)


def run():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Large receive buffer — a single GRAY8 160x120 frame is ~19 KB, but any
    # jitter means multiple frames can queue up before we drain.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind((LISTEN_HOST, LISTEN_PORT))
    sock.settimeout(2.0)

    print(f"[INFO] listening udp://{LISTEN_HOST}:{LISTEN_PORT} frame={FRAME_SIZE}B", flush=True)
    emit("startup", "none", "service_start")

    baseline = None
    latched = "none"
    pending = "none"
    pending_since = 0.0
    frames = 0
    buf = bytearray()

    while True:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            continue
        if not data:
            continue

        # Each gst udpsink packet is normally one whole buffer (one frame for
        # a raw GRAY8 pipeline on loopback). Handle both the nominal case and
        # the one where GStreamer fragmented the frame across packets.
        buf += data
        while len(buf) >= FRAME_SIZE:
            frame_bytes = bytes(buf[:FRAME_SIZE])
            del buf[:FRAME_SIZE]

            frame = np.frombuffer(frame_bytes, dtype=np.uint8).reshape((HEIGHT, WIDTH)).astype(np.float32)
            roi = frame[int(HEIGHT * WET_Y_START):, :]

            if baseline is None or baseline.shape != roi.shape:
                baseline = roi.copy()
            else:
                # Always adapt the baseline, just slower while latched. That
                # guarantees a mis-latched state eventually clears instead of
                # sticking forever.
                alpha = BASELINE_ALPHA_LATCHED if latched != "none" else BASELINE_ALPHA
                baseline = (1.0 - alpha) * baseline + alpha * roi

            delta = roi - baseline
            cold_area = int(np.sum(delta < COLD_DELTA))
            warm_area = int(np.sum(delta > WARM_DELTA))
            ambient = float(np.mean(frame))

            # Pick the dominant signal. A wet patch shows up strongly on ONE
            # side of the temperature delta, not both. The Lepton's colormap
            # rescales can briefly inflate both channels at once — those don't
            # count as a real alert.
            if cold_area >= warm_area * DOMINANCE and cold_area >= (EXIT_AREA if latched == "cold" else ENTER_AREA):
                observed = "cold"
            elif warm_area >= cold_area * DOMINANCE and warm_area >= (EXIT_AREA if latched == "warm" else ENTER_AREA):
                observed = "warm"
            else:
                observed = "none"

            now = time.monotonic()
            if observed != latched:
                if observed != pending:
                    pending = observed
                    pending_since = now
                elif now - pending_since >= PERSIST_SEC:
                    latched = observed
                    if latched == "none":
                        emit("wet_clear", latched, "persist", cold_area, warm_area, ambient, 1.0)
                    else:
                        emit("wet_alert", latched, "persist", cold_area, warm_area, ambient, 1.0)
                    pending = latched
            else:
                pending = latched
                pending_since = now

            frames += 1
            if frames % STATUS_EVERY_N_FRAMES == 0:
                reason = "persistence_wait" if pending != latched else "stable"
                emit("status", latched, reason, cold_area, warm_area, ambient, 0.0)


if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        emit("shutdown", "none", "service_stop")
