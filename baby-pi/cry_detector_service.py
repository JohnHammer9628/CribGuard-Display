#!/usr/bin/env python3
"""
CribGuard cry detector service (Baby Pi).

This runs independently from camera streaming so cry detection is always active.
It monitors mic audio continuously, detects sustained loud/cry-like periods, and
optionally triggers lullaby playback through the local camera control API.
"""

from __future__ import annotations

from array import array
from dataclasses import dataclass
from pathlib import Path
from urllib import error, request
import json
import logging
import math
import os
import signal
import subprocess
import sys
import time

try:
    import joblib  # type: ignore
except Exception:
    joblib = None

try:
    import numpy as np  # type: ignore
except Exception:
    np = None


LOG = logging.getLogger("cry-detector")
STOP_REQUESTED = False


def on_signal(_signum, _frame):
    global STOP_REQUESTED
    STOP_REQUESTED = True


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        return int(raw)
    except ValueError:
        LOG.warning("Invalid int for %s=%r, using default=%d", name, raw, default)
        return default


def env_float(name: str, default: float) -> float:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError:
        LOG.warning("Invalid float for %s=%r, using default=%s", name, raw, default)
        return default


def env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name, "").strip().lower()
    if not raw:
        return default
    return raw in {"1", "true", "yes", "on"}


@dataclass
class Config:
    audio_device: str
    sample_rate: int
    channels: int
    chunk_ms: int
    min_db: float
    delta_db: float
    sustain_sec: float
    release_sec: float
    cooldown_sec: float
    suppress_after_play_sec: float
    suppress_while_playing: bool
    playback_poll_sec: float
    playback_peek_sec: float
    playback_peek_interval_sec: float
    playback_delta_boost_db: float
    pause_capture_during_suppress: bool
    model_path: str
    model_threshold: float
    model_use_only: bool
    model_window_sec: float
    model_eval_interval_sec: float
    api_base: str
    lullabies_dir: Path
    play_device: str
    enable_auto_play: bool
    status_file: Path
    status_write_interval_sec: float
    reconnect_delay_sec: float
    log_interval_sec: float

    @staticmethod
    def from_env() -> "Config":
        return Config(
            audio_device=os.environ.get("CRY_AUDIO_DEVICE", "plughw:2,0").strip() or "plughw:2,0",
            sample_rate=env_int("CRY_SAMPLE_RATE", 16000),
            channels=max(1, env_int("CRY_CHANNELS", 1)),
            chunk_ms=max(50, env_int("CRY_CHUNK_MS", 250)),
            min_db=env_float("CRY_MIN_DB", -42.0),
            delta_db=env_float("CRY_DELTA_DB", 12.0),
            sustain_sec=max(0.5, env_float("CRY_SUSTAIN_SEC", 2.5)),
            release_sec=max(0.5, env_float("CRY_RELEASE_SEC", 4.0)),
            cooldown_sec=max(1.0, env_float("CRY_LULLABY_COOLDOWN_SEC", 90.0)),
            suppress_after_play_sec=max(0.0, env_float("CRY_SUPPRESS_AFTER_PLAY_SEC", 20.0)),
            suppress_while_playing=env_bool("CRY_SUPPRESS_WHILE_PLAYING", True),
            playback_poll_sec=max(0.2, env_float("CRY_PLAYBACK_POLL_SEC", 1.0)),
            playback_peek_sec=max(0.0, env_float("CRY_PLAYBACK_PEEK_SEC", 2.0)),
            playback_peek_interval_sec=max(1.0, env_float("CRY_PLAYBACK_PEEK_INTERVAL_SEC", 15.0)),
            playback_delta_boost_db=max(0.0, env_float("CRY_PLAYBACK_DELTA_BOOST_DB", 6.0)),
            pause_capture_during_suppress=env_bool("CRY_PAUSE_CAPTURE_DURING_SUPPRESS", True),
            model_path=os.environ.get("CRY_MODEL_PATH", "").strip(),
            model_threshold=min(1.0, max(0.0, env_float("CRY_MODEL_THRESHOLD", 0.65))),
            model_use_only=env_bool("CRY_MODEL_USE_ONLY", True),
            model_window_sec=max(0.5, env_float("CRY_MODEL_WINDOW_SEC", 1.0)),
            model_eval_interval_sec=max(0.1, env_float("CRY_MODEL_EVAL_INTERVAL_SEC", 0.25)),
            api_base=os.environ.get("CRY_API_BASE", "http://127.0.0.1:8000").strip().rstrip("/"),
            lullabies_dir=Path(os.path.expanduser(os.environ.get("CRY_LULLABIES_DIR", "~/Lullabies"))),
            play_device=os.environ.get("CRY_PLAY_DEVICE", "default").strip() or "default",
            enable_auto_play=env_bool("CRY_ENABLE_AUTO_PLAY", True),
            status_file=Path(os.path.expanduser(os.environ.get("CRY_STATUS_FILE", "/tmp/cribguard_cry_status.json"))),
            status_write_interval_sec=max(0.2, env_float("CRY_STATUS_WRITE_INTERVAL_SEC", 1.0)),
            reconnect_delay_sec=max(0.2, env_float("CRY_RECONNECT_DELAY_SEC", 1.0)),
            log_interval_sec=max(1.0, env_float("CRY_LOG_INTERVAL_SEC", 5.0)),
        )


class CryDetector:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.chunk_sec = cfg.chunk_ms / 1000.0
        self.sustain_chunks = max(1, int(round(cfg.sustain_sec / self.chunk_sec)))
        self.release_chunks = max(1, int(round(cfg.release_sec / self.chunk_sec)))

        self.baseline_db: float | None = None
        self.threshold_db: float = cfg.min_db
        self.last_db: float = -96.0

        self.high_chunks = 0
        self.quiet_chunks = 0
        self.crying = False

        self.last_change_ts = 0.0
        self.last_play_ts = 0.0
        self.last_error = ""
        self.last_log_ts = 0.0
        self.last_status_write_ts = 0.0
        self.playback_active = False
        self.last_playback_poll_ts = 0.0
        self.last_playback_state_change_ts = 0.0
        self.last_suppressed = False
        self.suppression_reason = ""
        self.model_enabled = False
        self.model_error = ""
        self.model_prob_cry = 0.0
        self.model_is_cry = False
        self.model_cry_class_idx: int | None = None
        self.model = None
        self.model_buffer = bytearray()
        self.model_window_bytes = max(2, int(cfg.sample_rate * cfg.model_window_sec * cfg.channels * 2))
        self.last_model_eval_ts = 0.0

        self._load_model_if_configured()

    def _load_model_if_configured(self):
        if not self.cfg.model_path:
            return

        if joblib is None or np is None:
            self.model_error = "numpy/joblib missing; install python3-numpy python3-joblib python3-sklearn"
            LOG.error("ML model disabled: %s", self.model_error)
            return

        path = Path(os.path.expanduser(self.cfg.model_path))
        if not path.is_file():
            self.model_error = f"model file not found: {path}"
            LOG.error("ML model disabled: %s", self.model_error)
            return

        try:
            self.model = joblib.load(path)
            classes = list(getattr(self.model, "classes_", []))
            if "cry" in classes:
                self.model_cry_class_idx = classes.index("cry")
            elif 1 in classes:
                self.model_cry_class_idx = classes.index(1)
            elif True in classes:
                self.model_cry_class_idx = classes.index(True)
            self.model_enabled = True
            self.model_error = ""
            LOG.info(
                "ML model enabled (path=%s threshold=%.2f mode=%s)",
                path,
                self.cfg.model_threshold,
                "model-only" if self.cfg.model_use_only else "db+model",
            )
        except Exception as exc:
            self.model_error = f"model load failed: {exc}"
            LOG.error("ML model disabled: %s", self.model_error)

    def _extract_model_features(self, pcm_chunk: bytes) -> list[float] | None:
        if np is None:
            return None
        # Match train_cry_model.py feature extraction exactly:
        # [db, std, zcr, spectral_centroid, ber_300_900, ber_900_3000]
        # Model was trained on 16kHz mono chunks. If stereo is present, average channels.
        samples = np.frombuffer(pcm_chunk, dtype="<i2").astype(np.float32)
        if samples.size < 64:
            return None
        if self.cfg.channels > 1:
            frame_count = samples.size // self.cfg.channels
            if frame_count == 0:
                return None
            samples = samples[: frame_count * self.cfg.channels]
            samples = samples.reshape((frame_count, self.cfg.channels)).mean(axis=1)
        x = samples / 32768.0

        eps = 1e-9
        if x.size < 256:
            return None

        rms = float(np.sqrt(np.mean(x * x)) + eps)
        db = float(20.0 * np.log10(rms + eps))
        std = float(np.std(x))

        # zcr(x): mean(abs(diff(signbit(x))))
        z = np.signbit(x)
        zcr = float(np.mean(np.abs(np.diff(z.astype(np.int8)))))

        # spectral_centroid(x, sr): sum(freq * mag) / sum(mag)
        X = np.abs(np.fft.rfft(x))
        freqs = np.fft.rfftfreq(len(x), d=1.0 / float(self.cfg.sample_rate))
        mag_sum = float(np.sum(X) + eps)
        centroid = float(np.sum(freqs * X) / mag_sum)

        # band_energy_ratio(x, sr, f1, f2)
        P = np.abs(np.fft.rfft(x)) ** 2
        total = float(np.sum(P) + eps)
        ber_300_900 = float(np.sum(P[(freqs >= 300.0) & (freqs <= 900.0)]) / total)
        ber_900_3000 = float(np.sum(P[(freqs >= 900.0) & (freqs <= 3000.0)]) / total)

        return [db, std, zcr, centroid, ber_300_900, ber_900_3000]

    def _evaluate_model(self, now_ts: float):
        if not self.model_enabled:
            return
        if len(self.model_buffer) < self.model_window_bytes:
            return
        if (now_ts - self.last_model_eval_ts) < self.cfg.model_eval_interval_sec:
            return
        self.last_model_eval_ts = now_ts

        window = bytes(self.model_buffer[-self.model_window_bytes:])
        features = self._extract_model_features(window)
        if not features:
            return

        try:
            if hasattr(self.model, "predict_proba"):
                probs = self.model.predict_proba([features])[0]
                if self.model_cry_class_idx is not None and self.model_cry_class_idx < len(probs):
                    prob_cry = float(probs[self.model_cry_class_idx])
                elif len(probs) > 1:
                    prob_cry = float(probs[-1])
                else:
                    prob_cry = float(probs[0])
            else:
                pred = self.model.predict([features])[0]
                prob_cry = 1.0 if str(pred) == "cry" or pred == 1 or pred is True else 0.0

            self.model_prob_cry = prob_cry
            self.model_is_cry = prob_cry >= self.cfg.model_threshold
            self.model_error = ""
        except Exception as exc:
            self.model_error = f"inference failed: {exc}"
            self.model_enabled = False
            LOG.error("ML model disabled after inference error: %s", self.model_error)

    def _post_json(self, path: str, payload: dict) -> tuple[bool, str]:
        url = f"{self.cfg.api_base}{path}"
        body = json.dumps(payload).encode("utf-8")
        req = request.Request(url, data=body, headers={"Content-Type": "application/json"}, method="POST")
        try:
            with request.urlopen(req, timeout=3) as resp:
                return True, resp.read().decode("utf-8", errors="replace")
        except error.URLError as exc:
            return False, str(exc)

    def _get_json(self, path: str) -> tuple[bool, dict | str]:
        url = f"{self.cfg.api_base}{path}"
        try:
            with request.urlopen(url, timeout=3) as resp:
                text = resp.read().decode("utf-8", errors="replace")
            return True, json.loads(text)
        except Exception as exc:
            return False, str(exc)

    def _refresh_playback_state(self, now_ts: float, force: bool = False):
        if not force and (now_ts - self.last_playback_poll_ts) < self.cfg.playback_poll_sec:
            return
        self.last_playback_poll_ts = now_ts

        ok, payload = self._get_json("/api/status")
        if not ok:
            self.last_error = f"status poll failed: {payload}"
            return

        playing = bool(payload.get("playing", False)) if isinstance(payload, dict) else False
        if playing != self.playback_active:
            self.playback_active = playing
            self.last_playback_state_change_ts = now_ts
            LOG.info("Playback state changed: %s", "playing" if playing else "stopped")

    def _is_detection_suppressed(self, now_ts: float) -> tuple[bool, str]:
        if (now_ts - self.last_play_ts) < self.cfg.suppress_after_play_sec:
            remaining = self.cfg.suppress_after_play_sec - (now_ts - self.last_play_ts)
            return True, f"post_play_window({remaining:.1f}s)"

        if self.cfg.suppress_while_playing and self.playback_active:
            if self.cfg.playback_peek_sec <= 0:
                return True, "playback_active(no_peek)"

            elapsed = max(0.0, now_ts - self.last_playback_state_change_ts)
            phase = elapsed % self.cfg.playback_peek_interval_sec
            if phase >= self.cfg.playback_peek_sec:
                return True, "playback_active(suppressed)"
            return False, "playback_peek_window"

        return False, ""

    def _refresh_suppression_state(self, now_ts: float) -> bool:
        self._refresh_playback_state(now_ts)
        suppressed, reason = self._is_detection_suppressed(now_ts)
        self.last_suppressed = suppressed
        self.suppression_reason = reason

        if suppressed:
            self.high_chunks = 0
            self.quiet_chunks = 0
            if self.crying:
                self.crying = False
                self.last_change_ts = now_ts
                LOG.info("Cry state cleared (suppressed: %s)", reason)
        return suppressed

    def _pick_lullaby(self) -> str | None:
        if not self.cfg.lullabies_dir.is_dir():
            return None
        candidates = [
            p for p in self.cfg.lullabies_dir.iterdir()
            if p.is_file() and p.suffix.lower() in {".wav", ".mp3", ".flac"}
        ]
        if not candidates:
            return None
        candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        return candidates[0].name

    def _trigger_lullaby_if_needed(self, now_ts: float):
        if not self.cfg.enable_auto_play or not self.crying:
            return
        if (now_ts - self.last_play_ts) < self.cfg.cooldown_sec:
            return

        lullaby_name = self._pick_lullaby()
        if not lullaby_name:
            self.last_error = f"no lullaby files in {self.cfg.lullabies_dir}"
            LOG.warning("Cry detected but no lullaby found in %s", self.cfg.lullabies_dir)
            return

        ok, msg = self._post_json(
            "/api/play",
            {"file": lullaby_name, "device": self.cfg.play_device}
        )
        if ok:
            self.last_play_ts = now_ts
            self.last_error = ""
            LOG.info("Cry detected -> requested lullaby playback: %s", lullaby_name)
        else:
            self.last_error = f"play failed: {msg}"
            LOG.error("Cry detected but playback request failed: %s", msg)

    def _write_status(self, force: bool = False):
        now_ts = time.time()
        if not force and (now_ts - self.last_status_write_ts) < self.cfg.status_write_interval_sec:
            return

        status = {
            "crying": self.crying,
            "suppressed": self.last_suppressed,
            "suppression_reason": self.suppression_reason,
            "playback_active": self.playback_active,
            "model_enabled": self.model_enabled,
            "model_prob_cry": round(self.model_prob_cry, 3),
            "model_is_cry": self.model_is_cry,
            "last_db": round(self.last_db, 2),
            "baseline_db": round(self.baseline_db if self.baseline_db is not None else self.cfg.min_db, 2),
            "threshold_db": round(self.threshold_db, 2),
            "high_chunks": self.high_chunks,
            "quiet_chunks": self.quiet_chunks,
            "last_change_epoch": self.last_change_ts,
            "last_play_epoch": self.last_play_ts,
            "last_error": self.last_error or self.model_error,
            "updated_epoch": now_ts,
        }

        self.cfg.status_file.parent.mkdir(parents=True, exist_ok=True)
        tmp_path = self.cfg.status_file.with_suffix(self.cfg.status_file.suffix + ".tmp")
        tmp_path.write_text(json.dumps(status), encoding="utf-8")
        os.replace(tmp_path, self.cfg.status_file)
        self.last_status_write_ts = now_ts

    def _maybe_log(self):
        now_ts = time.time()
        if (now_ts - self.last_log_ts) < self.cfg.log_interval_sec:
            return
        LOG.info(
            "state=%s suppressed=%s playback=%s model=%s p=%.2f db=%.1f baseline=%.1f threshold=%.1f high=%d quiet=%d",
            "cry" if self.crying else "calm",
            self.suppression_reason if self.last_suppressed else "no",
            "on" if self.playback_active else "off",
            "on" if self.model_enabled else "off",
            self.model_prob_cry,
            self.last_db,
            self.baseline_db if self.baseline_db is not None else self.cfg.min_db,
            self.threshold_db,
            self.high_chunks,
            self.quiet_chunks,
        )
        self.last_log_ts = now_ts

    def process_audio_chunk(self, pcm_chunk: bytes, db_level: float):
        now_ts = time.time()
        self.last_db = db_level
        self.model_buffer.extend(pcm_chunk)
        if len(self.model_buffer) > self.model_window_bytes:
            del self.model_buffer[:-self.model_window_bytes]
        self._evaluate_model(now_ts)

        suppressed = self._refresh_suppression_state(now_ts)
        if suppressed:
            self._write_status()
            self._maybe_log()
            return

        if self.baseline_db is None:
            self.baseline_db = db_level
        else:
            # Slow adaptation when quiet, very slow when loud.
            alpha = 0.03 if not self.crying and db_level < self.threshold_db + 6 else 0.005
            self.baseline_db = (1.0 - alpha) * self.baseline_db + alpha * db_level

        effective_delta = self.cfg.delta_db + (self.cfg.playback_delta_boost_db if self.playback_active else 0.0)
        self.threshold_db = max(self.cfg.min_db, self.baseline_db + effective_delta)

        db_high = db_level >= self.threshold_db
        model_ready = self.model_enabled and self.last_model_eval_ts > 0.0
        if self.model_enabled and model_ready:
            high_now = self.model_is_cry if self.cfg.model_use_only else (db_high and self.model_is_cry)
        else:
            high_now = db_high

        if high_now:
            self.high_chunks += 1
            self.quiet_chunks = 0
        else:
            self.quiet_chunks += 1
            # Require contiguous "loud" chunks to enter cry state.
            self.high_chunks = 0

        if not self.crying and self.high_chunks >= self.sustain_chunks:
            self.crying = True
            self.last_change_ts = now_ts
            # Reset release counter when entering cry to avoid immediate clear.
            self.quiet_chunks = 0
            LOG.warning("Cry state entered")

        elif self.crying and self.quiet_chunks >= self.release_chunks:
            self.crying = False
            self.last_change_ts = now_ts
            # Reset enter counter when returning to calm.
            self.high_chunks = 0
            LOG.info("Cry state cleared")

        self._trigger_lullaby_if_needed(now_ts)
        self._write_status()
        self._maybe_log()


def start_arecord(cfg: Config) -> subprocess.Popen:
    cmd = [
        "arecord",
        "-q",
        "-D",
        cfg.audio_device,
        "-f",
        "S16_LE",
        "-r",
        str(cfg.sample_rate),
        "-c",
        str(cfg.channels),
        "-t",
        "raw",
    ]
    LOG.info("Starting mic capture: %s", " ".join(cmd))
    return subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid,
    )


def compute_db_from_pcm16le(data: bytes, channels: int) -> float:
    if not data:
        return -96.0
    samples = array("h")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()

    if len(samples) == 0:
        return -96.0

    if channels > 1:
        mono = samples[0::channels]
    else:
        mono = samples

    if len(mono) == 0:
        return -96.0

    sum_sq = 0.0
    for s in mono:
        sum_sq += float(s) * float(s)
    rms = math.sqrt(sum_sq / float(len(mono)))
    if rms <= 0.0:
        return -96.0
    return 20.0 * math.log10(rms / 32768.0)


def stop_process_group(proc: subprocess.Popen | None):
    if not proc:
        return
    try:
        if proc.poll() is None:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                proc.wait(timeout=2)
    except Exception:
        pass


def run():
    cfg = Config.from_env()
    logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(levelname)s: %(message)s")
    LOG.info("Cry detector starting (device=%s rate=%d ch=%d)", cfg.audio_device, cfg.sample_rate, cfg.channels)

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    detector = CryDetector(cfg)
    detector._refresh_playback_state(time.time(), force=True)
    detector._write_status(force=True)

    chunk_frames = max(1, int(cfg.sample_rate * cfg.chunk_ms / 1000))
    chunk_bytes = chunk_frames * cfg.channels * 2  # S16_LE

    proc: subprocess.Popen | None = None
    capture_paused = False
    while not STOP_REQUESTED:
        now_ts = time.time()
        if cfg.pause_capture_during_suppress and detector._refresh_suppression_state(now_ts):
            if proc is not None:
                stop_process_group(proc)
                proc = None
            if not capture_paused:
                LOG.info("Pausing mic capture while suppressed (%s)", detector.suppression_reason)
                capture_paused = True
            detector.last_error = ""
            detector._write_status()
            detector._maybe_log()
            time.sleep(min(cfg.reconnect_delay_sec, 0.5))
            continue
        if capture_paused:
            LOG.info("Resuming mic capture after suppression")
            capture_paused = False

        if proc is None or proc.poll() is not None:
            stop_process_group(proc)
            proc = None
            try:
                proc = start_arecord(cfg)
            except Exception as exc:
                detector.last_error = f"arecord start failed: {exc}"
                LOG.error("Failed to start arecord: %s", exc)
                detector._write_status(force=True)
                time.sleep(cfg.reconnect_delay_sec)
                continue

        assert proc.stdout is not None
        data = proc.stdout.read(chunk_bytes)
        if not data or len(data) < chunk_bytes:
            now_ts = time.time()
            if cfg.pause_capture_during_suppress and detector._refresh_suppression_state(now_ts):
                detector.last_error = ""
                detector._write_status(force=True)
                stop_process_group(proc)
                proc = None
                time.sleep(min(cfg.reconnect_delay_sec, 0.5))
                continue

            detector.last_error = "audio read short/empty; restarting arecord"
            LOG.warning("Audio read short/empty (%d bytes), restarting capture", len(data) if data else 0)
            detector._write_status(force=True)
            stop_process_group(proc)
            proc = None
            time.sleep(cfg.reconnect_delay_sec)
            continue

        detector.last_error = ""
        db_level = compute_db_from_pcm16le(data, cfg.channels)
        detector.process_audio_chunk(data, db_level)

    LOG.info("Cry detector stopping")
    stop_process_group(proc)
    detector._write_status(force=True)


if __name__ == "__main__":
    run()
