#!/usr/bin/env python3
"""
Lepton thermal streamer + detector orchestrator (Baby Pi).

Replaces the previous gst-launch-only pipeline. We need this middleware because
GStreamer can't do calibrated radiometric mapping (fixed °F bounds -> 0-255)
or apply a true iron-bow thermal palette without a custom plugin. Both are
trivial in numpy.

Architecture:

  ingest gst-launch  (v4l2src GRAY16_LE -> fdsink stdout)
        |
        v   raw Y16 frames over a stdin pipe
  this script
        |   |
        |   +--> raw Y16 to detector at 127.0.0.1:DETECTOR_PORT (UDP)
        v
   AGC + iron-bow LUT in numpy  ->  RGB
        |
        v   raw RGB frames over a stdin pipe
  output gst-launch  (fdsrc -> videoparse -> openh264enc -> rtph264pay -> udpsink)
        |
        v
   Parent Pi UI

The detector (lepton_detector.py) is also launched here so a single SIGTERM
to this script tears everything down.

Knobs (env vars):
  CG_AGC_LO_F   lower bound of the displayed temperature range, °F (default 40)
  CG_AGC_HI_F   upper bound of the displayed temperature range, °F (default 120)
  CG_AGC_AUTO   "1" to ignore LO_F/HI_F and use per-frame 1st/99th percentile
                stretch (auto-AGC). Default "0" (fixed bounds).
"""

import atexit
import os
import signal
import socket
import subprocess
import sys

import numpy as np

WIDTH, HEIGHT, FPS = 160, 120, 9
FRAME_SIZE_Y16 = WIDTH * HEIGHT * 2
FRAME_SIZE_RGB = WIDTH * HEIGHT * 3

DETECTOR_SCRIPT = "/home/jammin/lepton_detector.py"


def make_iron_lut():
    """Iron-bow / inferno thermal palette as a 256x3 uint8 RGB lookup table.

    Linear interpolation between perceptual keyframes: black -> deep purple ->
    magenta -> red -> orange -> yellow -> near-white. Returns shape (256, 3).
    """
    keyframes = [
        (0.00, (  0,   0,   0)),
        (0.15, ( 32,  12,  80)),
        (0.30, (110,  30, 130)),
        (0.50, (200,  70,  80)),
        (0.65, (240, 130,  30)),
        (0.80, (255, 200,  50)),
        (1.00, (255, 255, 220)),
    ]
    lut = np.zeros((256, 3), dtype=np.uint8)
    for i in range(256):
        t = i / 255.0
        for j in range(len(keyframes) - 1):
            t0, c0 = keyframes[j]
            t1, c1 = keyframes[j + 1]
            if t <= t1:
                frac = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
                lut[i] = [int(c0[k] + (c1[k] - c0[k]) * frac) for k in range(3)]
                break
        else:
            lut[i] = keyframes[-1][1]
    return lut


def main():
    if len(sys.argv) < 4:
        print("usage: lepton_streamer.py PARENT_IP PARENT_PORT DETECTOR_PORT", file=sys.stderr)
        sys.exit(2)

    parent_ip = sys.argv[1]
    parent_port = sys.argv[2]
    detector_port = int(sys.argv[3])

    auto_agc = os.environ.get("CG_AGC_AUTO", "0") == "1"
    lo_f = float(os.environ.get("CG_AGC_LO_F", "60"))
    hi_f = float(os.environ.get("CG_AGC_HI_F", "100"))
    # cK = ((°F - 32) * 5/9 + 273.15) * 100
    lo_ck = ((lo_f - 32.0) * 5.0 / 9.0 + 273.15) * 100.0
    hi_ck = ((hi_f - 32.0) * 5.0 / 9.0 + 273.15) * 100.0
    span = max(hi_ck - lo_ck, 1.0)

    print(f"[streamer] PARENT={parent_ip}:{parent_port} DETECTOR_PORT={detector_port}", flush=True)
    print(f"[streamer] AGC mode={'auto' if auto_agc else 'fixed'} "
          f"range=[{lo_f}°F, {hi_f}°F] = [{lo_ck:.0f}, {hi_ck:.0f}] cK", flush=True)

    det_env = {**os.environ, "CG_DETECTOR_LISTEN_PORT": str(detector_port)}
    detector = subprocess.Popen(["python3", DETECTOR_SCRIPT], env=det_env)

    ingest_args = [
        "/usr/bin/gst-launch-1.0", "-q",
        "v4l2src", "device=/dev/video0", "!",
        f"video/x-raw,format=GRAY16_LE,width={WIDTH},height={HEIGHT},framerate={FPS}/1", "!",
        "fdsink", "fd=1",
    ]
    ingest = subprocess.Popen(ingest_args, stdout=subprocess.PIPE, bufsize=0)

    output_args = [
        "/usr/bin/gst-launch-1.0", "-q",
        "fdsrc", "fd=0", "!",
        "videoparse", "format=rgb", f"width={WIDTH}", f"height={HEIGHT}", f"framerate={FPS}/1", "!",
        "videoconvert", "!",
        "openh264enc", "bitrate=500000", "complexity=low", "!",
        "rtph264pay", "pt=96", "config-interval=1", "!",
        "udpsink", f"host={parent_ip}", f"port={parent_port}",
    ]
    output = subprocess.Popen(output_args, stdin=subprocess.PIPE, bufsize=0)

    det_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    lut = make_iron_lut()

    procs = [detector, ingest, output]
    cleaned_up = [False]

    def cleanup(*_args):
        if cleaned_up[0]:
            return
        cleaned_up[0] = True
        print("[streamer] shutting down", flush=True)
        for p in procs:
            try:
                p.terminate()
            except Exception:
                pass
        for p in procs:
            try:
                p.wait(timeout=2)
            except Exception:
                try:
                    p.kill()
                except Exception:
                    pass

    signal.signal(signal.SIGTERM, lambda *a: (cleanup(), sys.exit(0)))
    signal.signal(signal.SIGINT, lambda *a: (cleanup(), sys.exit(0)))
    atexit.register(cleanup)

    while True:
        # Drain a full frame from ingest. Pipes can return short reads, so loop.
        buf = bytearray()
        while len(buf) < FRAME_SIZE_Y16:
            chunk = ingest.stdout.read(FRAME_SIZE_Y16 - len(buf))
            if not chunk:
                print(f"[streamer] ingest EOF after {len(buf)} of {FRAME_SIZE_Y16} bytes", flush=True)
                cleanup()
                return
            buf.extend(chunk)

        frame_bytes = bytes(buf)

        try:
            det_sock.sendto(frame_bytes, ("127.0.0.1", detector_port))
        except OSError as e:
            print(f"[streamer] detector send failed: {e}", flush=True)

        y16 = np.frombuffer(frame_bytes, dtype=np.uint16).reshape((HEIGHT, WIDTH))
        if auto_agc:
            f = y16.astype(np.float32)
            lo = float(np.percentile(f, 1))
            hi = float(np.percentile(f, 99))
            local_span = max(hi - lo, 1.0)
            y8 = np.clip((f - lo) * (255.0 / local_span), 0, 255).astype(np.uint8)
        else:
            y8 = np.clip((y16.astype(np.float32) - lo_ck) * (255.0 / span), 0, 255).astype(np.uint8)

        rgb = lut[y8]  # (H, W, 3) uint8

        try:
            output.stdin.write(rgb.tobytes())
        except (BrokenPipeError, IOError) as e:
            print(f"[streamer] output stdin broken: {e}", flush=True)
            cleanup()
            return


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
