#!/usr/bin/env python3
"""
Lepton wetness detector (Baby Pi).

Reads raw Y16 (GRAY16_LE) 160x120 frames from a local UDP socket (fed by a
`gst-launch` pipeline that tees off the main RTP stream), runs a simple EMA
baseline vs. delta detector on the lower portion of the frame, and writes
wet_alert / wet_clear / status JSON lines to the events file that the Flask
/api/wet_status endpoint already tails.

Y16 vs. the old GRAY8 path: Y16 is the Lepton's raw radiometric output. It
does NOT auto-rescale per scene like the AGC'd 8-bit path did, so the deltas
reported here are stable across scene changes (lights, baby movement, etc.).
If the Lepton is in TLinear mode, pixel values are centikelvin and can be
converted to Celsius as: T_c = px * 0.01 - 273.15. If TLinear is off, values
are raw uncalibrated counts; the detector still works, just without °C units.
The status events log both raw mean and apparent °C so you can verify mode.

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
BYTES_PER_PIXEL = 2  # Y16 = uint16 little-endian
FRAME_SIZE = WIDTH * HEIGHT * BYTES_PER_PIXEL

EVENTS_FILE = os.environ.get("CG_EVENTS_FILE", "/tmp/crib_monitor_events.jsonl")
LISTEN_HOST = os.environ.get("CG_DETECTOR_LISTEN_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("CG_DETECTOR_LISTEN_PORT", "5556"))

# =============================================================================
# DETECTION TUNING KNOBS
# =============================================================================
# After editing any value below, redeploy:
#   1) PowerShell: scp this file to jammin@192.168.50.2:/home/jammin/lepton_detector.py
#   2) Baby pi:    sudo systemctl restart cribguard-camera
#   3) Parent pi:  sudo systemctl restart cribguard@pi5.service
#
# Watch the live behavior during tests with, on the baby pi:
#   tail -F /tmp/crib_monitor_events.jsonl
# Each status line prints cold_area / warm_area / ambient_c. Use those numbers
# to decide which knob to turn. Rule of thumb: find a knob whose threshold sits
# between your "wet" numbers and your "dry" numbers.
# =============================================================================

# Region-of-interest (ROI) crop inside the 160x120 frame. Only pixels inside
# this ROI are analysed for wetness; everything outside is ignored. Use this
# to exclude parts of the scene that aren't the diaper area (the baby's head,
# the crib bars, etc.).
#
# WET_X_START = fraction of the WIDTH (160 px) where the ROI begins, measured
# from the LEFT. 0.5 means "skip the left half, analyse the right half."
# WET_Y_START = fraction of the HEIGHT (120 px) where the ROI begins, measured
# from the TOP. 0.0 means "use the full vertical range."
#
# Examples:
#   WET_X_START=0.5, WET_Y_START=0.0 -> right 50% of the frame, full height (80x120)
#   WET_X_START=0.0, WET_Y_START=0.45 -> full width, bottom 55% (160x66)
#   WET_X_START=0.5, WET_Y_START=0.3  -> right half, bottom 70% (80x84)
#
#   - Raise WET_X_START -> tighter to the right edge
#   - Lower WET_X_START -> more of the frame included horizontally
WET_X_START = 0.5
WET_Y_START = 0.0

# How fast the "dry baseline" image adapts to the current scene, per frame,
# when no alert is latched. Exponential moving average: new = (1-alpha)*old + alpha*current.
#   - Higher (e.g. 0.05) -> baseline catches up faster; slow scene changes won't trigger
#   - Lower  (e.g. 0.005) -> baseline is more stable; small real wetness shows up more strongly
# If ambient temperature is drifting (e.g. room warming up) and causing false alerts,
# raise this. If short wet events are being "absorbed" into the baseline before they
# trigger, lower this.
BASELINE_ALPHA = 0.02

# Same EMA, but applied while an alert IS latched. Kept lower so the wet patch
# doesn't get learned as "normal" and accidentally clear the alert.
#   - Higher (e.g. 0.015) -> stuck alerts self-clear faster (good for demos)
#   - Lower  (e.g. 0.001) -> alerts hold longer even if the wet patch becomes baseline
# If the banner gets stuck ON after wetness is gone, raise this.
BASELINE_ALPHA_LATCHED = 0.005

# Per-pixel temperature delta to count a pixel as "cold" or "warm" vs. the
# baseline. Units are Y16 raw counts. If the Lepton is in TLinear mode, 1 unit
# = 0.01 Kelvin (= 0.01 °C of difference), so 100 units ~ 1°C.
#   cold patches -> COOLER pixels -> negative delta (COLD_DELTA)
#   warm patches -> WARMER pixels -> positive delta (WARM_DELTA)
# Wet-vs-dry diaper signature is typically a few hundred cK (~1-3°C).
#   - More false positives from ambient noise? Raise magnitudes (e.g. ±150).
#   - Real wetness not being detected? Lower magnitudes (e.g. ±60).
# NOTE: when this file ran on AGC'd GRAY8, ±12 was tuned to that 0-255 scale.
# Default below targets ~1°C in TLinear-cK. Re-tune after a few minutes of
# tail -F /tmp/crib_monitor_events.jsonl to see the actual baseline noise.
COLD_DELTA = -100.0
WARM_DELTA = 100.0

# How many pixels-over-threshold are required to LATCH a new alert.
# Together with ENTER_AREA, this is the primary "am I wet?" gate.
#   - Raise (e.g. 2500) -> harder to trigger, fewer false positives
#   - Lower (e.g. 1200) -> triggers on smaller wet patches
# If you can see wet_alert firing on a status line where cold_area is around N,
# set ENTER_AREA a bit above N to suppress that class of false alert.
ENTER_AREA = 1800

# How many pixels-over-threshold are required to STAY latched once alerting.
# Always <= ENTER_AREA (that's the hysteresis — easier to stay in state than to enter it).
#   - Raise (e.g. 1200) -> alerts clear sooner when wetness shrinks
#   - Lower (e.g. 500)  -> alerts hold even when the wet patch fades
EXIT_AREA = 900

# Dominance ratio: the winning channel (cold or warm) must be this much larger
# than the other to latch/hold an alert. This rejects "both went up" events
# which are usually caused by the Lepton's auto-gain rescaling the whole scene.
#   - Raise (e.g. 2.5) -> only clear, one-sided signals trigger (more precision, less recall)
#   - Lower (e.g. 1.2) -> almost any excess triggers (more recall, more false positives)
# If the banner flickers rapidly between cold and warm during a single scene change,
# raise this.
DOMINANCE = 1.5

# Seconds a new observation must persist before the latched state actually flips.
# This is the "don't trigger on a 1-frame blip" timer.
#   - Raise (e.g. 8) -> slower to alert and slower to clear; more stable
#   - Lower (e.g. 2) -> faster response, more jumpy
# For a baby monitor, 4-6 seconds is reasonable.
PERSIST_SEC = 4.0

# How often a "status" line is appended to the events file (pure debug output —
# the parent pi doesn't react to these, only to wet_alert / wet_clear).
# 18 frames at 9 fps = one status every ~2 seconds. Raise to make the log quieter.
STATUS_EVERY_N_FRAMES = 18


def iso_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def emit(event_type, state, reason, cold_area=0, warm_area=0, ambient_raw=0.0, ambient_c=0.0, confidence=0.0):
    rec = {
        "ts": iso_now(),
        "event_type": event_type,
        "state": state,
        "reason": reason,
        "cold_area": int(cold_area),
        "warm_area": int(warm_area),
        "ambient_raw": float(round(ambient_raw, 1)),
        "ambient_c": float(round(ambient_c, 3)),
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

            frame = np.frombuffer(frame_bytes, dtype=np.uint16).reshape((HEIGHT, WIDTH)).astype(np.float32)
            roi = frame[int(HEIGHT * WET_Y_START):, int(WIDTH * WET_X_START):]

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
            ambient_raw = float(np.mean(frame))
            # Apparent °C assuming TLinear (centikelvin). If TLinear is OFF this
            # number will look unphysical (negative hundreds, or very large) — that's
            # the signal to either enable TLinear or treat values as raw counts.
            ambient_c = ambient_raw * 0.01 - 273.15

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
                        emit("wet_clear", latched, "persist", cold_area, warm_area, ambient_raw, ambient_c, 1.0)
                    else:
                        emit("wet_alert", latched, "persist", cold_area, warm_area, ambient_raw, ambient_c, 1.0)
                    pending = latched
            else:
                pending = latched
                pending_since = now

            frames += 1
            if frames % STATUS_EVERY_N_FRAMES == 0:
                reason = "persistence_wait" if pending != latched else "stable"
                emit("status", latched, reason, cold_area, warm_area, ambient_raw, ambient_c, 0.0)


if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        emit("shutdown", "none", "service_stop")
