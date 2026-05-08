#!/usr/bin/env python3
"""
CribGuard cry detector (Baby Pi).

Captures mic audio from the reSpeaker Lite via `arecord` (stdlib + numpy — no
cv2, no gst-python), decides whether a real baby cry is happening using a
two-stage test (loudness + frequency-band ratio), and:
  - writes cry_alert / cry_clear / cry_status JSON lines to the shared events
    file the Flask /api/cry_status endpoint tails, and
  - auto-plays a lullaby (round-robin through ~/Lullabies) via the local
    Flask /api/play endpoint while cry state is latched, keeping it playing
    for POST_CRY_TAIL_SEC after the cry clears.

Frequency gate rationale: a baby cry's energy is concentrated between ~400 Hz
and ~3 kHz. Adult speech has a lot of energy below 300 Hz; TV / appliance
noise often spills above 5 kHz. Requiring a high cry-band/(voice+noise) ratio
makes the detector reject those non-cry sources even when they're loud.
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
from datetime import datetime, timezone

import numpy as np

# =============================================================================
# DETECTION TUNING KNOBS
# =============================================================================
# Tail the events file on the baby pi while testing:
#   tail -F /tmp/crib_monitor_events.jsonl
# Each cry_status line includes db + cry_ratio numbers so you can read them
# out of real audio and pick thresholds that land between "quiet room" and
# "actual cry" for your specific space.
# =============================================================================

# ALSA capture device for the reSpeaker Lite.
ALSA_DEVICE = os.environ.get("CG_CRY_ALSA_DEVICE", "plughw:2,0")
SAMPLE_RATE = int(os.environ.get("CG_CRY_SAMPLE_RATE", "16000"))
CHANNELS = 1

# Analysis window. Smaller = faster reaction, noisier numbers. 100 ms is a
# good default for voice-band analysis.
CHUNK_MS = 100

# Loudness gate (in dBFS — 0 = clipping, -60 = very quiet room tone).
#   Raise toward 0  -> fewer false triggers from soft noises
#   Lower toward -40 -> catches softer cries but more sensitive overall
# Tuned 2026-04 for showcase: real cries from a baby right next to the mic in
# the crib hit -10 to -22 dB. Showcase-room music reaches the mic at -25 to -40,
# even when audible to humans. -20 sits in the gap and rejects the kind of
# vocal-heavy music that is otherwise spectrally indistinguishable from a cry.
# If the mic is moved further from the crib, lower this back toward -28.
LOUDNESS_THRESHOLD_DB = -28.0

# Frequency bands (Hz) used to tell a cry apart from other loud sounds.
# Tuned to skip the overlap zone with adult voice:
#   Adult male fundamental  ~85-180 Hz
#   Adult female fundamental ~165-255 Hz
#   Their harmonics extend to ~400-700 Hz and can leak higher
#   Baby cry fundamental + strong harmonics ~600-2500 Hz
# Starting the cry band at 800 Hz excludes most adult-voice harmonic energy
# while still capturing where baby cries are loud.
CRY_BAND_LOW_HZ = 800
CRY_BAND_HIGH_HZ = 2800
VOICE_BAND_HIGH_HZ = 500       # adult-voice fundamentals + low harmonics live below this
NOISE_BAND_LOW_HZ = 4500       # broadband noise / sibilance lives above this

# Required cry_band_energy / (voice_band_energy + noise_band_energy).
#   Raise (e.g. 15.0) -> only clearly cry-shaped spectra trigger
#   Lower (e.g. 2.0) -> triggers on anything loud with high-freq content (voice consonants can pass)
# Tuned 2026-04-17 to 8: stricter values starve the score because most cry-
# audio chunks land in the 4-12 range with occasional 15-30+ peaks. The real
# yelling-rejection mechanism is the tonality gate below; ratio at 8 just
# filters out spectra dominated by voice-band fundamentals.
CRY_RATIO_MIN = 7.0

# Spectral flatness gate (Wiener entropy) measured INSIDE the cry band.
# Definition: geometric_mean(power) / arithmetic_mean(power), in [0, 1].
#   ~0.01-0.05 -> pure tone (single peak)
#   ~0.05-0.20 -> baby cry (fundamental + a few strong harmonics)
#   ~0.30-0.60 -> music with multiple instruments / vocals
#   ~0.80-1.00 -> white noise
# Sounds with flatness ABOVE this threshold are rejected as "too broadband to be a cry"
# even if they are loud and have lots of cry-band energy. This is what discriminates
# music and ambient room sound from a real cry.
#   Raise (e.g. 0.40) -> more permissive; some music can sneak through
#   Lower (e.g. 0.15) -> stricter; only clean tonal cries pass (may miss noisy/distant cries)
# Tuned 2026-04-17 from 0.30 to 0.15: adult yelling has noticeable breath
# turbulence and formant smearing (flatness ~0.20-0.40), while real cries
# stay extremely tonal (flatness 0.005-0.05 from your test data).
TONALITY_MAX = 0.10

# Score-based persistence, tolerant of the natural breath gaps inside a real
# baby cry (cry -> inhale -> cry -> inhale...). A cry-shaped chunk adds
# SCORE_UP; a non-cry chunk subtracts SCORE_DOWN. The score is clamped to
# [0, SCORE_MAX]. We latch "crying" when the score climbs above ENTER_SCORE
# and clear it when the score falls below EXIT_SCORE. At 10 chunks/sec:
#   100% cry -> +30/sec           (enters in ~1.3s)
#    75% cry -> +22.5/sec         (enters in ~1.8s, typical real cry)
#    50% cry -> +10/sec           (enters in ~4s)
#    25% cry or less -> 0 or drop (never enters — rejects random bumps)
# From fully-latched (score~100) at full silence, exits in ~9s.
#
# Tuning:
#   Raise SCORE_UP / lower SCORE_DOWN -> faster to latch, slower to clear
#   Raise ENTER_SCORE -> needs more accumulated evidence to trigger
#   Raise EXIT_SCORE  -> clears faster once the baby is quiet
SCORE_UP = 5.0                 # bumped 3 -> 5 (2026-04-17). Sparse / intermittent cry recordings only
                               # produce 2-4 cry-passing chunks per burst. With SCORE_UP=3 score barely
                               # crosses ENTER_SCORE=12 then drops back below EXIT_SCORE=10 → latch
                               # flickers. At 5, a 3-chunk burst pushes score to ~15-20, well clear of
                               # EXIT, and the grace window can hold the latch across the inter-burst gap.
SCORE_DOWN_SILENT = 4.0        # decay when room is actually silent (db below SILENT_DB) — clears fast after real cry ends
SCORE_DOWN_LATCHED = 0.3       # decay during breath gaps inside a real cry (audible background, just not cry-like right now)
SCORE_DOWN_IDLE = 0.8          # decay when NOT latched and not clearly silent. Kept moderate so the score can
                               # accumulate across the breath gaps inside a real cry, where the chunks between
                               # bursts are quieter than the loudness gate but still close in time. The loudness
                               # gate at -22 dB does the music-rejection work; this decay rate just shapes how
                               # forgiving the persistence model is to gaps within real crying.
SILENT_DB = -55.0              # below this, the room is "silent"; above it, there's still some audible activity.
                               # Lowered -50 -> -55 (2026-04-17): some baby cry recordings drop to -50 to -54
                               # between bursts (recording's own room ambient), and the previous threshold
                               # treated those as "silent" → fast decay → latch crashed in 0.4 sec.
SCORE_MAX = 35.0               # cap so post-cry drain doesn't take forever (was 100, 60, 25).
                               # Set high enough that GRACE_SEC of slow decay still leaves headroom
                               # above EXIT_SCORE, so brief mid-cry quiet stretches don't accidentally
                               # clear the latch.
GRACE_SEC = 5.0                # how many seconds we'll keep applying slow latched-decay after the
                               # last is_cry_now chunk. Past this, fast-decay kicks in even while
                               # latched. Bridges the breath-gap-vs-cry-ended ambiguity. Set wide
                               # enough to span quiet stretches in your cry recording (cry sound
                               # effects often cycle burst-silence-burst in 3-6 sec). Trade-off:
                               # this also delays the post-cry clear by GRACE_SEC after the cry
                               # actually ends.
ENTER_SCORE = 12.0             # cross this while idle -> latch "crying". Higher value = more sustained signal
                               # required, harder for brief music passages to trip a false alert.
                               # Lowered to 12 from 20 (2026-04-17): cry recordings have peaks every 3-9 sec
                               # with quiet between, so score routinely peaks at 8-14 then crashes before the
                               # next burst. Tonality gate at 0.10 + ratio gate at 7 still reject conversation /
                               # yelling / music — those rarely produce passing chunks at all.
EXIT_SCORE = 10.0              # fall below this while latched -> clear
# With these numbers, from SCORE_MAX after silence (db < -50):
#   decay at 15/sec -> reaches EXIT_SCORE in ~3 seconds.
# After cry_clear fires, the lullaby plays for POST_CRY_TAIL_SEC more before stopping.

# Seconds to keep the lullaby playing AFTER the cry clears.
#   0.0     -> stop the lullaby immediately when cry_clear fires
#   10.0    -> default: soothes the baby for another 10 s past the last cry
#   99999.0 -> effectively "play the whole track" (just don't stop)
POST_CRY_TAIL_SEC = 10.0

# Where things live.
EVENTS_FILE = os.environ.get("CG_EVENTS_FILE", "/tmp/crib_monitor_events.jsonl")
LULLABIES_DIR = os.path.expanduser("~/Lullabies")
# Optional allowlist for the UI to restrict round-robin. JSON array of filenames.
# Missing file / empty list = play everything in LULLABIES_DIR.
LULLABY_ALLOWLIST_FILE = os.environ.get("CG_LULLABY_ALLOWLIST", "/tmp/lullaby_allowlist.json")
FLASK_URL = os.environ.get("CG_FLASK_URL", "http://127.0.0.1:8000")
# ALSA device the Flask /api/play endpoint aplay's to (the reSpeaker's
# speaker output is the same card as its mic).
LULLABY_DEVICE = os.environ.get("CG_LULLABY_DEVICE", ALSA_DEVICE)

# One "status" line is appended every N analysis chunks (pure debug telemetry).
# Set CG_CRY_DEBUG_RAW=1 to also emit every chunk's features (very chatty, only
# for tuning sessions — disable in production).
STATUS_EVERY_N_CHUNKS = 10  # ~1 s at 100 ms chunks
DEBUG_RAW_CHUNKS = os.environ.get("CG_CRY_DEBUG_RAW", "0").strip().lower() not in ("0", "false", "no")

# =============================================================================

CHUNK_SAMPLES = int(SAMPLE_RATE * CHUNK_MS / 1000)
CHUNK_BYTES = CHUNK_SAMPLES * 2 * CHANNELS  # S16_LE = 2 bytes/sample

_stop_requested = False


def _on_signal(_signum, _frame):
    global _stop_requested
    _stop_requested = True


def iso_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def emit(event_type, state, reason, db=0.0, cry_ratio=0.0, score=0.0, flatness=0.0):
    rec = {
        "ts": iso_now(),
        "event_type": event_type,
        "state": state,
        "reason": reason,
        "db": float(round(db, 2)),
        "cry_ratio": float(round(cry_ratio, 3)),
        "flatness": float(round(flatness, 3)),
        "score": float(round(score, 1)),
    }
    line = json.dumps(rec)
    try:
        with open(EVENTS_FILE, "a") as f:
            f.write(line + "\n")
    except OSError as e:
        print(f"[ERR] write events: {e}", flush=True)
    print(line, flush=True)


def http_post(path, body=None, timeout=2.0):
    data = json.dumps(body or {}).encode("utf-8")
    req = urllib.request.Request(
        FLASK_URL + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read()
    except Exception as e:
        print(f"[ERR] POST {path}: {e}", flush=True)
        return None


def lullaby_candidates():
    """Return the list of .wav filenames eligible to be played.
    If LULLABY_ALLOWLIST_FILE is a JSON array, only those filenames are kept
    (filtered to ones that actually exist). Otherwise every .wav in
    LULLABIES_DIR is eligible."""
    allowlist = None
    try:
        with open(LULLABY_ALLOWLIST_FILE) as f:
            loaded = json.load(f)
        if isinstance(loaded, list) and loaded:
            allowlist = [str(x) for x in loaded]
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        pass
    try:
        all_files = sorted(
            f for f in os.listdir(LULLABIES_DIR)
            if f.lower().endswith(".wav")
        )
    except OSError:
        all_files = []
    if allowlist:
        return [f for f in allowlist if f in all_files]
    return all_files


def start_lullaby(index_state):
    files = lullaby_candidates()
    if not files:
        print("[WARN] no lullabies to play", flush=True)
        return
    fname = files[index_state["i"] % len(files)]
    index_state["i"] = (index_state["i"] + 1) % len(files)
    http_post("/api/play", {"file": fname, "device": LULLABY_DEVICE})
    print(f"[INFO] lullaby -> {fname}", flush=True)


def stop_lullaby():
    http_post("/api/play/stop")
    print("[INFO] lullaby stop", flush=True)


def rms_db(samples_f32):
    rms = float(np.sqrt(np.mean(samples_f32 * samples_f32)))
    if rms < 1e-9:
        return -120.0
    # 32768 = full-scale for int16, so dBFS = 20*log10(rms/fullscale).
    return 20.0 * np.log10(rms / 32768.0)


def cry_features(samples_f32):
    # Window to reduce spectral leakage, then real FFT for power spectrum.
    window = np.hanning(len(samples_f32))
    spectrum = np.abs(np.fft.rfft(samples_f32 * window))
    freqs = np.fft.rfftfreq(len(samples_f32), 1.0 / SAMPLE_RATE)
    power = spectrum * spectrum

    cry_mask = (freqs >= CRY_BAND_LOW_HZ) & (freqs <= CRY_BAND_HIGH_HZ)
    cry_power = power[cry_mask]

    cry = float(np.sum(cry_power))
    voice = float(np.sum(power[freqs < VOICE_BAND_HIGH_HZ]))
    noise = float(np.sum(power[freqs > NOISE_BAND_LOW_HZ]))
    denom = max(1.0, voice + noise)
    ratio = cry / denom

    # Spectral flatness inside the cry band. Tonal signal -> near 0; broadband -> near 1.
    # Floor the bins so log(0) doesn't blow up on a quiet band.
    cp = np.maximum(cry_power, 1e-10)
    arith = float(np.mean(cp))
    geo = float(np.exp(np.mean(np.log(cp))))
    flatness = geo / arith if arith > 0 else 1.0

    return ratio, flatness


def run():
    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)

    cmd = [
        "arecord", "-D", ALSA_DEVICE, "-q",
        "-f", "S16_LE", "-r", str(SAMPLE_RATE), "-c", str(CHANNELS),
        "-t", "raw",
    ]
    print(f"[INFO] spawning: {' '.join(cmd)}", flush=True)
    arecord = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    emit("cry_startup", "none", "service_start")

    latched = "none"          # "none" or "crying"
    score = 0.0               # cry evidence accumulator (0..SCORE_MAX)
    last_cry_chunk_time = -1e9  # monotonic time of the last is_cry_now chunk
    lullaby_stop_at = None    # scheduled stop time for the tail
    lullaby_index = {"i": 0}
    chunks = 0

    try:
        while not _stop_requested:
            raw = arecord.stdout.read(CHUNK_BYTES)
            if not raw or len(raw) < CHUNK_BYTES:
                # arecord died or we lost the stream; small backoff and retry.
                if arecord.poll() is not None:
                    emit("cry_error", latched, "arecord_exit")
                    break
                time.sleep(0.05)
                continue

            samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
            db = rms_db(samples)
            ratio, flatness = cry_features(samples)
            is_cry_now = (
                db >= LOUDNESS_THRESHOLD_DB
                and ratio >= CRY_RATIO_MIN
                and flatness <= TONALITY_MAX
            )
            if DEBUG_RAW_CHUNKS:
                emit("cry_chunk", latched, "raw" if not is_cry_now else "cry_passes",
                     db, ratio, score, flatness)

            now = time.monotonic()
            tonal = flatness <= TONALITY_MAX
            # Time since the most recent chunk that passed all cry gates. The
            # GRACE_SEC window is what bridges "cry breath gap" and "cry
            # actually ended" — within it we keep slow-decaying to protect
            # the latch; past it we fast-decay so the screen clears quickly.
            time_since_cry = now - last_cry_chunk_time

            if is_cry_now:
                last_cry_chunk_time = now
                score = min(SCORE_MAX, score + SCORE_UP)
            elif db < SILENT_DB:
                score = max(0.0, score - SCORE_DOWN_SILENT)
            elif latched == "crying" and time_since_cry < GRACE_SEC:
                # Inside the grace window after the last cry burst — could be a
                # breath gap. Slow decay protects the latch.
                score = max(0.0, score - SCORE_DOWN_LATCHED)
            elif latched == "crying":
                # Past the grace window with no fresh cry signal — treat as
                # "cry ended" and drain the score quickly.
                score = max(0.0, score - SCORE_DOWN_SILENT)
            elif not tonal:
                score = max(0.0, score - SCORE_DOWN_SILENT)
            else:
                score = max(0.0, score - SCORE_DOWN_IDLE)

            if latched == "none" and score >= ENTER_SCORE:
                latched = "crying"
                emit("cry_alert", "crying", "score_enter", db, ratio, score, flatness)
                start_lullaby(lullaby_index)
                lullaby_stop_at = None  # cancel any scheduled stop
            elif latched == "crying":
                if score < EXIT_SCORE:
                    latched = "none"
                    emit("cry_clear", "none", "score_exit", db, ratio, score, flatness)
                    lullaby_stop_at = now + POST_CRY_TAIL_SEC
                else:
                    # Still accumulating / still crying; cancel any pending stop.
                    lullaby_stop_at = None

            if lullaby_stop_at is not None and now >= lullaby_stop_at:
                stop_lullaby()
                lullaby_stop_at = None

            chunks += 1
            if chunks % STATUS_EVERY_N_CHUNKS == 0:
                # Report the current score alongside db/ratio/flatness so thresholds
                # can be tuned against real recorded data.
                if latched == "none":
                    reason = "building" if score > 0 else "stable"
                else:
                    reason = "tailing" if score < ENTER_SCORE else "stable"
                emit("cry_status", latched, reason, db, ratio, score, flatness)
    finally:
        try:
            arecord.terminate()
            arecord.wait(timeout=2)
        except Exception:
            try:
                arecord.kill()
            except Exception:
                pass
        if latched == "crying":
            stop_lullaby()
        emit("cry_shutdown", "none", "service_stop")


if __name__ == "__main__":
    try:
        run()
    except Exception as e:
        print(f"[FATAL] {e}", flush=True)
        sys.exit(1)
