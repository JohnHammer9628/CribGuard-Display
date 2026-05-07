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
import signal
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
ROI_CONFIG_FILE = os.environ.get("CG_ROI_CONFIG_FILE", "/home/jammin/wet_roi.json")
PID_FILE = os.environ.get("CG_DETECTOR_PID_FILE", "/tmp/lepton_detector.pid")

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

# Region-of-interest (ROI) inside the 160x120 frame, in raw pixels. Only
# pixels inside this box are analysed for wetness; everything outside is
# ignored. The ROI is reload-able at runtime: send SIGHUP to the detector
# (Flask does this when the parent UI saves a new ROI) and the new box
# applies on the next frame without restarting the streamer.
#
# Resolution order (highest priority first):
#   1. JSON config file at ROI_CONFIG_FILE  (written by /api/wet_roi POST)
#   2. Env vars CG_WET_X_START / Y_START / X_END / Y_END
#   3. Hard-coded defaults below (right half of frame, full height)
#
# We keep the active ROI in a small mutable dict so the SIGHUP reload path
# can swap it atomically — the main loop reads roi_state["x_start"] etc.,
# gets a consistent box even mid-frame.
roi_state = {
    "x_start": 80,
    "y_start": 0,
    "x_end":   WIDTH,
    "y_end":   HEIGHT,
    "enter_area": 0,  # populated by _apply_roi below ENTER_PCT/EXIT_PCT defs
    "exit_area":  0,
}


def _clamp_int(v, lo, hi):
    try:
        v = int(v)
    except (TypeError, ValueError):
        return lo
    return max(lo, min(v, hi))


def _resolve_roi():
    """Pick the ROI from (config file | env vars | defaults), in that order."""
    x_s, y_s, x_e, y_e = 80, 0, WIDTH, HEIGHT
    # Env vars first (lowest priority above defaults).
    x_s = _clamp_int(os.environ.get("CG_WET_X_START", x_s), 0, WIDTH - 1)
    y_s = _clamp_int(os.environ.get("CG_WET_Y_START", y_s), 0, HEIGHT - 1)
    x_e = _clamp_int(os.environ.get("CG_WET_X_END",   x_e), 1, WIDTH)
    y_e = _clamp_int(os.environ.get("CG_WET_Y_END",   y_e), 1, HEIGHT)
    # Config file overrides env if present and parseable.
    try:
        with open(ROI_CONFIG_FILE, "r") as f:
            cfg = json.load(f)
        x_s = _clamp_int(cfg.get("x_start", x_s), 0, WIDTH - 1)
        y_s = _clamp_int(cfg.get("y_start", y_s), 0, HEIGHT - 1)
        x_e = _clamp_int(cfg.get("x_end",   x_e), 1, WIDTH)
        y_e = _clamp_int(cfg.get("y_end",   y_e), 1, HEIGHT)
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        pass
    # Guarantee a non-empty box.
    if x_e <= x_s:
        x_e = min(x_s + 1, WIDTH)
    if y_e <= y_s:
        y_e = min(y_s + 1, HEIGHT)
    return x_s, y_s, x_e, y_e


def _apply_roi(x_s, y_s, x_e, y_e):
    """Update roi_state. Computes absolute pixel thresholds from ENTER_PCT /
    EXIT_PCT so the area gate scales with the ROI's actual size."""
    area = max((x_e - x_s) * (y_e - y_s), 1)
    roi_state["x_start"] = x_s
    roi_state["y_start"] = y_s
    roi_state["x_end"]   = x_e
    roi_state["y_end"]   = y_e
    roi_state["enter_area"] = max(int(area * ENTER_PCT), 1)
    roi_state["exit_area"]  = max(int(area * EXIT_PCT),  1)


def reload_roi():
    """Re-read ROI from sources and rebuild the area thresholds. Safe to call
    from a signal handler — only mutates roi_state, no I/O on shared sockets."""
    x_s, y_s, x_e, y_e = _resolve_roi()
    old = (roi_state["x_start"], roi_state["y_start"], roi_state["x_end"], roi_state["y_end"])
    _apply_roi(x_s, y_s, x_e, y_e)
    print(f"[INFO] ROI reload: {old} -> ({x_s},{y_s},{x_e},{y_e}) "
          f"area={roi_state['x_end'] - roi_state['x_start']}x{roi_state['y_end'] - roi_state['y_start']} "
          f"enter={roi_state['enter_area']} exit={roi_state['exit_area']}", flush=True)


# How fast the "dry baseline" image adapts to the current scene, per frame.
# Only applied during fully idle periods (no pending signal, no latched
# alert). EMA: new = (1-alpha)*old + alpha*current.
#   - Higher (e.g. 0.05) -> baseline catches up faster; slow scene changes
#     are absorbed before they can trigger.
#   - Lower  (e.g. 0.005) -> baseline more stable; small real wetness shows
#     up more strongly against it.
# If ambient temperature drifts (room warming up) and that's causing false
# alerts, raise this. If short wet events are being "absorbed" into the
# baseline before they trigger, lower this.
BASELINE_ALPHA = 0.02

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

# Fraction of the ROI that must be over the delta threshold to LATCH a new
# alert. Stored as a percentage of ROI area so the same setting works across
# different ROI sizes (when the user resizes the ROI from the parent UI, the
# absolute pixel threshold rescales automatically). Combined with COLD_DELTA
# / WARM_DELTA, this is the primary "am I wet?" gate.
#   - Raise (e.g. 0.25) -> harder to trigger, fewer false positives
#   - Lower (e.g. 0.12) -> triggers on smaller wet patches
ENTER_PCT = 0.19

# Fraction of the ROI that must remain over threshold to STAY latched.
# Always <= ENTER_PCT (the hysteresis: easier to stay in state than to enter).
#   - Raise (e.g. 0.13) -> alerts clear sooner when wetness shrinks
#   - Lower (e.g. 0.05) -> alerts hold even when the wet patch fades
EXIT_PCT = 0.094

# Initial ROI load. Done here (after ENTER_PCT/EXIT_PCT are defined) so the
# enter_area/exit_area in roi_state are populated before run() starts.
_apply_roi(*_resolve_roi())

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

    # Pidfile so Flask can find this process and SIGHUP it for ROI reloads.
    try:
        with open(PID_FILE, "w") as f:
            f.write(str(os.getpid()))
    except OSError as e:
        print(f"[WARN] could not write pidfile {PID_FILE}: {e}", flush=True)

    # SIGHUP -> reload ROI from config file. Signal handlers run in the main
    # thread between bytecode boundaries, so this is safe alongside the loop.
    signal.signal(signal.SIGHUP, lambda *_: reload_roi())

    print(f"[INFO] listening udp://{LISTEN_HOST}:{LISTEN_PORT} frame={FRAME_SIZE}B", flush=True)
    print(f"[INFO] ROI px: x=[{roi_state['x_start']},{roi_state['x_end']}) "
          f"y=[{roi_state['y_start']},{roi_state['y_end']}) "
          f"size={roi_state['x_end'] - roi_state['x_start']}x{roi_state['y_end'] - roi_state['y_start']} "
          f"enter={roi_state['enter_area']} exit={roi_state['exit_area']}", flush=True)
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
        except InterruptedError:
            # SIGHUP (or similar) interrupted recvfrom. Handler already ran;
            # just loop and try again.
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
            # Snapshot ROI bounds for this frame so a SIGHUP mid-iteration
            # doesn't half-apply the new box (baseline shape would mismatch).
            x_s, y_s, x_e, y_e = roi_state["x_start"], roi_state["y_start"], roi_state["x_end"], roi_state["y_end"]
            enter_area = roi_state["enter_area"]
            exit_area  = roi_state["exit_area"]
            roi = frame[y_s:y_e, x_s:x_e]

            if baseline is None or baseline.shape != roi.shape:
                baseline = roi.copy()
            elif latched == "none" and pending == "none":
                # Only adapt the baseline during fully idle periods. Drifting
                # while a signal is pending or latched contaminates the
                # baseline with the suspect's own thermal signature, so when
                # the suspect leaves the delta inverts and we trigger a
                # phantom counter-state alert (warm cup removed -> false
                # cold). Freezing the baseline during latch eliminates that.
                baseline = (1.0 - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * roi

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
            if cold_area >= warm_area * DOMINANCE and cold_area >= (exit_area if latched == "cold" else enter_area):
                observed = "cold"
            elif warm_area >= cold_area * DOMINANCE and warm_area >= (exit_area if latched == "warm" else enter_area):
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
    finally:
        try:
            os.unlink(PID_FILE)
        except OSError:
            pass
