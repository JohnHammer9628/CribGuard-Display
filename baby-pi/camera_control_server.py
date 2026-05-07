#!/usr/bin/env python3
"""
Camera Control Server for Baby Pi
Provides REST API to start/stop MLX90640 IR camera streaming, plus lullaby playback/rename helpers.

This file is the canonical Baby Pi server implementation going forward.
"""

from flask import Flask, jsonify, request
from flask_cors import CORS
import subprocess
import signal
import os
import json
import logging
import shutil
import shlex
import threading
import time

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from Parent Pi

# Configure logging
logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

# Global state
streaming_process = None
recording_process = None
playback_process = None
listen_process = None
cry_detector_process = None
CRY_DETECTOR_CMD = os.environ.get("CG_CRY_DETECTOR_CMD", "/usr/bin/python3 /home/jammin/cry_detector.py").split()
AUTO_START_CRY = os.environ.get("CG_AUTOSTART_CRY", "1").strip().lower() not in ("0", "false", "no")

# Live mic streaming ("listen mode"): baby pi -> parent pi, Opus over RTP.
LISTEN_ALSA_DEVICE = os.environ.get("CG_LISTEN_ALSA_DEVICE", "plughw:2,0")
LISTEN_PORT = int(os.environ.get("CG_LISTEN_PORT", "5001"))
LISTEN_BITRATE = int(os.environ.get("CG_LISTEN_BITRATE_BPS", "24000"))
LISTEN_SAMPLE_RATE = int(os.environ.get("CG_LISTEN_SAMPLE_RATE", "16000"))
parent_ip = os.environ.get("CG_PARENT_IP", "192.168.50.1").strip()
parent_port = int(os.environ.get("CG_PARENT_PORT", "5000"))
AUTO_START_STREAM = os.environ.get("CG_AUTOSTART_CAMERA", "1").strip().lower() not in ("0", "false", "no")
AUTO_START_DELAY_SEC = float(os.environ.get("CG_AUTOSTART_DELAY_SEC", "2.0"))
AUTO_START_RETRIES = int(os.environ.get("CG_AUTOSTART_RETRIES", "20"))
AUTO_START_RETRY_SEC = float(os.environ.get("CG_AUTOSTART_RETRY_SEC", "1.5"))
LULLABIES_DIR = os.path.expanduser("~/Lullabies")
os.makedirs(LULLABIES_DIR, exist_ok=True)
EVENTS_FILE = os.path.expanduser(os.environ.get("CG_EVENTS_FILE", "~/crib_monitor_events.jsonl"))

# Camera backend selection:
# - mlx90640 (default): ./mlx90640_streaming <parent_ip> <parent_port>
# - lepton_usb / lepton3.5: ./lepton_streaming <parent_ip> <parent_port>
# - custom: provide camera_stream_template
camera_backend = os.environ.get("CG_CAMERA_BACKEND", "mlx90640").strip().lower()
camera_stream_template = os.environ.get("CG_CAMERA_STREAM_CMD", "").strip()

CAMERA_BACKEND_DEFAULT_TEMPLATES = {
    "mlx90640": "./mlx90640_streaming {parent_ip} {parent_port}",
    "lepton_usb": "./lepton_streaming {parent_ip} {parent_port}",
    "lepton3.5": "./lepton_streaming {parent_ip} {parent_port}",
}


def resolve_stream_command(target_ip, target_port, backend_override=None, template_override=None):
    """Resolve camera stream command into argv list."""
    backend = (backend_override or camera_backend or "mlx90640").strip().lower()
    template = (template_override or "").strip()

    if not template:
        if camera_stream_template:
            template = camera_stream_template
        else:
            template = CAMERA_BACKEND_DEFAULT_TEMPLATES.get(
                backend, "./mlx90640_streaming {parent_ip} {parent_port}"
            )

    rendered = template.format(parent_ip=target_ip, parent_port=target_port)
    cmd = shlex.split(rendered)
    if not cmd:
        raise ValueError("camera stream command template resolved to empty command")
    return backend, template, cmd


def stream_executable_exists(cmd):
    """Check whether command executable exists/is runnable."""
    exe = cmd[0]
    if os.path.isabs(exe) or exe.startswith("."):
        return os.path.isfile(exe) and os.access(exe, os.X_OK)
    return shutil.which(exe) is not None


def stop_camera_stream():
    """Stop stream process if running."""
    global streaming_process

    if not streaming_process or streaming_process.poll() is not None:
        logger.warning("Camera not streaming")
        return False, {'success': False, 'error': 'Not streaming'}, 400

    try:
        logger.info(f"Stopping camera stream (PID: {streaming_process.pid})")
        os.killpg(os.getpgid(streaming_process.pid), signal.SIGTERM)
        try:
            streaming_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.warning("Process didn't exit gracefully, sending SIGKILL")
            os.killpg(os.getpgid(streaming_process.pid), signal.SIGKILL)
            streaming_process.wait()
        streaming_process = None
        logger.info("Camera streaming stopped")
        return True, {'success': True}, 200
    except Exception as e:
        logger.error(f"Failed to stop camera: {e}")
        return False, {'success': False, 'error': str(e)}, 500


def start_camera_stream(
    target_ip,
    target_port,
    backend_override=None,
    template_override=None,
    allow_already=False
):
    """Start camera stream process (idempotent when allow_already=True)."""
    global streaming_process, parent_ip, parent_port

    if streaming_process and streaming_process.poll() is None:
        payload = {
            'success': True if allow_already else False,
            'error': None if allow_already else 'Already streaming',
            'already_streaming': True,
            'pid': streaming_process.pid,
            'parent_ip': parent_ip,
            'parent_port': parent_port,
            'camera_backend': camera_backend,
            'camera_stream_template': camera_stream_template or CAMERA_BACKEND_DEFAULT_TEMPLATES.get(camera_backend, "")
        }
        status = 200 if allow_already else 400
        if allow_already:
            logger.info("Camera already streaming; treating as success")
        else:
            logger.warning("Camera already streaming")
        return allow_already, payload, status

    parent_ip = str(target_ip)
    parent_port = int(target_port)

    try:
        backend, template, cmd = resolve_stream_command(
            parent_ip,
            parent_port,
            backend_override=backend_override,
            template_override=template_override
        )

        if not stream_executable_exists(cmd):
            logger.error(f"Camera backend '{backend}' command not found/executable: {cmd[0]}")
            return False, {
                'success': False,
                'error': f"stream command not found or not executable: {cmd[0]}",
                'camera_backend': backend,
                'camera_stream_template': template
            }, 500

        logger.info(f"Starting camera stream: {' '.join(cmd)}")

        streaming_process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=os.setsid  # Create new process group for clean shutdown
        )

        logger.info(f"Camera streaming started (PID: {streaming_process.pid})")
        payload = {
            'success': True,
            'pid': streaming_process.pid,
            'parent_ip': parent_ip,
            'parent_port': parent_port,
            'camera_backend': backend,
            'camera_stream_template': template,
            'stream_cmd': cmd
        }
        return True, payload, 200
    except Exception as e:
        logger.error(f"Failed to start camera: {e}")
        return False, {'success': False, 'error': str(e)}, 500


def start_camera_stream_autostart():
    """Background autostart loop for always-on showcase mode."""
    if not AUTO_START_STREAM:
        logger.info("Camera autostart disabled (CG_AUTOSTART_CAMERA=0)")
        return

    def worker():
        if AUTO_START_DELAY_SEC > 0:
            time.sleep(AUTO_START_DELAY_SEC)

        for attempt in range(1, AUTO_START_RETRIES + 1):
            ok, payload, _ = start_camera_stream(parent_ip, parent_port, allow_already=True)
            if ok:
                logger.info(f"Camera autostart ready (attempt {attempt}/{AUTO_START_RETRIES})")
                return
            logger.warning(
                f"Camera autostart attempt {attempt}/{AUTO_START_RETRIES} failed: {payload.get('error', 'unknown')}"
            )
            time.sleep(AUTO_START_RETRY_SEC)

        logger.error("Camera autostart exhausted retries; service stays up for remote /api/start attempts")

    threading.Thread(target=worker, daemon=True, name="camera-autostart").start()


@app.route('/api/status', methods=['GET'])
def get_status():
    """Get current camera status"""
    global streaming_process

    is_streaming = streaming_process is not None and streaming_process.poll() is None

    return jsonify({
        'streaming': is_streaming,
        'recording': (recording_process is not None and recording_process.poll() is None),
        'playing': (playback_process is not None and playback_process.poll() is None),
        'parent_ip': parent_ip,
        'parent_port': parent_port,
        'camera_backend': camera_backend,
        'camera_stream_template': camera_stream_template or CAMERA_BACKEND_DEFAULT_TEMPLATES.get(camera_backend, ""),
        'pid': streaming_process.pid if is_streaming else None
    })


@app.route('/api/start', methods=['POST'])
def start_camera():
    """Start IR camera streaming"""

    # Get optional parameters
    data = request.get_json() or {}
    target_ip = data.get('parent_ip', parent_ip)
    target_port = data.get('parent_port', parent_port)
    req_backend = data.get('camera_backend')
    req_template = data.get('camera_stream_template')

    ok, payload, status = start_camera_stream(
        target_ip,
        target_port,
        backend_override=req_backend,
        template_override=req_template,
        allow_already=True
    )
    return jsonify(payload), status


@app.route('/api/stop', methods=['POST'])
def stop_camera():
    """Stop IR camera streaming"""
    ok, payload, status = stop_camera_stream()
    return jsonify(payload), status


@app.route('/api/record', methods=['POST'])
def record_audio():
    """Record audio from USB microphone and save to Lullabies directory.
       JSON body: { "seconds": 30, "device": "default", "filename": "optional.wav" }"""
    global recording_process

    # Reject if already recording
    if recording_process and recording_process.poll() is None:
        logger.warning("Audio already recording")
        return jsonify({'success': False, 'error': 'Already recording'}), 400

    data = request.get_json() or {}
    seconds = int(data.get('seconds', 30))
    device = str(data.get('device', 'default'))
    filename = data.get('filename')

    # Generate filename with timestamp if not provided
    if not filename:
        from datetime import datetime
        filename = f"lullaby_{datetime.now().strftime('%Y%m%d_%H%M%S')}.wav"
    # Sanitize filename
    filename = os.path.basename(filename)
    filepath = os.path.join(LULLABIES_DIR, filename)

    # Ensure arecord exists
    if not shutil.which('arecord'):
        logger.error("arecord not found; install with: sudo apt install -y alsa-utils")
        return jsonify({'success': False, 'error': 'arecord not found; install alsa-utils'}), 500

    # Build arecord command: CD quality mono WAV for N seconds
    # Use plug device for broader compatibility
    dev_arg = device if device else 'default'
    cmd = ['arecord', '-D', dev_arg, '-f', 'S16_LE', '-r', '44100', '-c', '1', '-d', str(seconds), '-t', 'wav', filepath]

    try:
        logger.info(f"Starting audio record: {' '.join(cmd)}")
        recording_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=os.setsid)
        # We do not block; client can poll or rely on seconds duration
        return jsonify({'success': True, 'file': filepath, 'seconds': seconds})
    except Exception as e:
        logger.error(f"Failed to start recording: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/record/stop', methods=['POST'])
def record_stop():
    """Stop ongoing audio recording early."""
    global recording_process
    if not recording_process or recording_process.poll() is not None:
        logger.warning("No active recording")
        return jsonify({'success': False, 'error': 'Not recording'}), 400
    try:
        logger.info(f"Stopping audio record (PID: {recording_process.pid})")
        os.killpg(os.getpgid(recording_process.pid), signal.SIGTERM)
        try:
            recording_process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(recording_process.pid), signal.SIGKILL)
            recording_process.wait()
        recording_process = None
        return jsonify({'success': True})
    except Exception as e:
        logger.error(f"Failed to stop recording: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/lullabies', methods=['GET'])
def list_lullabies():
    """List recorded lullaby files from LULLABIES_DIR."""
    files = []
    try:
        for name in os.listdir(LULLABIES_DIR):
            if not name.lower().endswith(('.wav', '.mp3', '.flac')):
                continue
            path = os.path.join(LULLABIES_DIR, name)
            try:
                st = os.stat(path)
                files.append({
                    'name': name,
                    'size': st.st_size,
                    'mtime': int(st.st_mtime)
                })
            except FileNotFoundError:
                continue
        # sort by mtime desc
        files.sort(key=lambda x: x['mtime'], reverse=True)
        return jsonify({'success': True, 'files': files})
    except Exception as e:
        logger.error(f"Failed to list lullabies: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/play', methods=['POST'])
def play_lullaby():
    """Play a lullaby file on Baby Pi speakers via aplay."""
    global playback_process
    data = request.get_json() or {}
    name = data.get('file')
    device = data.get('device', 'default')
    if not name:
        return jsonify({'success': False, 'error': 'file is required'}), 400
    # Ensure within LULLABIES_DIR
    name = os.path.basename(name)
    path = os.path.join(LULLABIES_DIR, name)
    if not os.path.isfile(path):
        return jsonify({'success': False, 'error': 'file not found'}), 404

    # Stop previous playback if any
    try:
        if playback_process and playback_process.poll() is None:
            os.killpg(os.getpgid(playback_process.pid), signal.SIGTERM)
            try:
                playback_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(playback_process.pid), signal.SIGKILL)
                playback_process.wait()
    except Exception:
        pass

    if not shutil.which('aplay'):
        logger.error("aplay not found; install alsa-utils")
        return jsonify({'success': False, 'error': 'aplay not found; install alsa-utils'}), 500

    cmd = ['aplay', '-q', '-D', device, path]
    try:
        logger.info(f"Playing lullaby: {' '.join(cmd)}")
        playback_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=os.setsid)
        return jsonify({'success': True, 'file': name})
    except Exception as e:
        logger.error(f"Failed to play: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/play/stop', methods=['POST'])
def stop_playback():
    """Stop current audio playback."""
    global playback_process
    if not playback_process or playback_process.poll() is not None:
        return jsonify({'success': False, 'error': 'not playing'}), 400
    try:
        os.killpg(os.getpgid(playback_process.pid), signal.SIGTERM)
        try:
            playback_process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(playback_process.pid), signal.SIGKILL)
            playback_process.wait()
        playback_process = None
        return jsonify({'success': True})
    except Exception as e:
        logger.error(f"Failed to stop playback: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/rename', methods=['POST'])
def rename_lullaby():
    """Rename an existing .wav file in the LULLABIES_DIR.
       JSON body: { "old": "oldname.wav", "new": "newname.wav" }
       Only .wav files are allowed to be renamed via this endpoint.
    """
    data = request.get_json() or {}
    old_name = data.get('old')
    new_name = data.get('new')
    if not old_name or not new_name:
        return jsonify({'success': False, 'error': 'old and new are required'}), 400
    # Sanitize: base names only
    old_base = os.path.basename(old_name)
    new_base = os.path.basename(new_name)
    # Force .wav extension on destination; require source .wav
    if not old_base.lower().endswith('.wav'):
        return jsonify({'success': False, 'error': 'only .wav files may be renamed'}), 400
    if not new_base.lower().endswith('.wav'):
        new_base = f"{new_base}.wav"
    old_path = os.path.join(LULLABIES_DIR, old_base)
    new_path = os.path.join(LULLABIES_DIR, new_base)
    if not os.path.isfile(old_path):
        return jsonify({'success': False, 'error': 'source file not found'}), 404
    if os.path.abspath(os.path.dirname(old_path)) != os.path.abspath(LULLABIES_DIR) \
       or os.path.abspath(os.path.dirname(new_path)) != os.path.abspath(LULLABIES_DIR):
        return jsonify({'success': False, 'error': 'invalid path'}), 400
    if os.path.exists(new_path):
        return jsonify({'success': False, 'error': 'destination exists'}), 409
    try:
        logger.info(f"Renaming lullaby: {old_base} -> {new_base}")
        os.rename(old_path, new_path)
        return jsonify({'success': True, 'old': old_base, 'new': new_base})
    except Exception as e:
        logger.error(f"Failed to rename: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/config', methods=['POST'])
def update_config():
    """Update configuration (parent IP/port)"""
    global parent_ip, parent_port, camera_backend, camera_stream_template

    data = request.get_json() or {}

    if 'parent_ip' in data:
        parent_ip = data['parent_ip']
        logger.info(f"Updated parent_ip to {parent_ip}")

    if 'parent_port' in data:
        parent_port = int(data['parent_port'])
        logger.info(f"Updated parent_port to {parent_port}")

    if 'camera_backend' in data:
        camera_backend = str(data['camera_backend']).strip().lower()
        logger.info(f"Updated camera_backend to {camera_backend}")

    if 'camera_stream_template' in data:
        camera_stream_template = str(data['camera_stream_template']).strip()
        logger.info(f"Updated camera_stream_template to: {camera_stream_template}")

    return jsonify({
        'success': True,
        'parent_ip': parent_ip,
        'parent_port': parent_port,
        'camera_backend': camera_backend,
        'camera_stream_template': camera_stream_template or CAMERA_BACKEND_DEFAULT_TEMPLATES.get(camera_backend, "")
    })


def _start_cry_detector():
    """Spawn the cry detector as a managed subprocess (idempotent)."""
    global cry_detector_process
    if cry_detector_process and cry_detector_process.poll() is None:
        return
    try:
        logger.info(f"Starting cry detector: {' '.join(CRY_DETECTOR_CMD)}")
        cry_detector_process = subprocess.Popen(
            CRY_DETECTOR_CMD,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=os.setsid,
        )
    except Exception as e:
        logger.error(f"Failed to start cry detector: {e}")
        cry_detector_process = None


def _stop_cry_detector():
    """Kill the cry detector (idempotent)."""
    global cry_detector_process
    if not cry_detector_process or cry_detector_process.poll() is not None:
        cry_detector_process = None
        return
    try:
        logger.info(f"Stopping cry detector (PID: {cry_detector_process.pid})")
        os.killpg(os.getpgid(cry_detector_process.pid), signal.SIGTERM)
        try:
            cry_detector_process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(cry_detector_process.pid), signal.SIGKILL)
            cry_detector_process.wait()
    except Exception as e:
        logger.error(f"Failed to stop cry detector: {e}")
    cry_detector_process = None


def read_last_cry_state():
    """Tail the shared events file and return the most recent cry state.
    Only cry_* events are considered; the wetness detector's events are
    skipped. Returns 'crying' | 'none'."""
    result = {'state': 'none', 'event_type': '', 'ts': '', 'db': 0.0, 'cry_ratio': 0.0, 'source': EVENTS_FILE}
    try:
        with open(EVENTS_FILE, 'rb') as f:
            f.seek(0, 2)
            size = f.tell()
            chunk = min(size, 16384)
            f.seek(size - chunk, 0)
            tail = f.read().decode('utf-8', errors='ignore')
        for line in reversed([ln for ln in tail.splitlines() if ln.strip()]):
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            et = ev.get('event_type', '')
            if not et.startswith('cry_'):
                continue
            st = ev.get('state')
            if st in ('none', 'crying'):
                result['state'] = st
                result['event_type'] = et
                result['ts'] = ev.get('ts', '')
                result['db'] = ev.get('db', 0.0)
                result['cry_ratio'] = ev.get('cry_ratio', 0.0)
                return result
    except FileNotFoundError:
        pass
    except Exception as e:
        logger.warning(f"cry_status read error: {e}")
    return result


def read_last_wet_state():
    """Tail the lepton monitor's JSONL events file and return the most recent
    wet state ('none' | 'cold' | 'warm'). Falls back to 'none' if the file is
    missing, empty, or unreadable."""
    result = {'state': 'none', 'event_type': '', 'ts': '', 'source': EVENTS_FILE}
    try:
        with open(EVENTS_FILE, 'rb') as f:
            f.seek(0, 2)
            size = f.tell()
            chunk = min(size, 8192)
            f.seek(size - chunk, 0)
            tail = f.read().decode('utf-8', errors='ignore')
        for line in reversed([ln for ln in tail.splitlines() if ln.strip()]):
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue
            # Cry events share the events file and also use state="none";
            # without this filter the wet endpoint mirrors cry_status.
            if ev.get('event_type', '').startswith('cry_'):
                continue
            st = ev.get('state')
            if st in ('none', 'cold', 'warm'):
                result['state'] = st
                result['event_type'] = ev.get('event_type', '')
                result['ts'] = ev.get('ts', '')
                return result
    except FileNotFoundError:
        pass
    except Exception as e:
        logger.warning(f"wet_status read error: {e}")
    return result


@app.route('/api/wet_status', methods=['GET'])
def wet_status():
    """Return latest wet state read from the lepton monitor events log."""
    return jsonify(read_last_wet_state())


# ROI configuration shared with lepton_detector.py. The detector reads this
# file at startup and on SIGHUP; the parent UI's "Set ROI" tool POSTs here
# to update the box without restarting the streamer.
WET_ROI_CONFIG_FILE = '/home/jammin/wet_roi.json'
WET_ROI_PID_FILE = '/tmp/lepton_detector.pid'
WET_ROI_DEFAULTS = {'x_start': 80, 'y_start': 0, 'x_end': 160, 'y_end': 120}
WET_ROI_FRAME_W = 160
WET_ROI_FRAME_H = 120


def _read_wet_roi():
    roi = dict(WET_ROI_DEFAULTS)
    try:
        with open(WET_ROI_CONFIG_FILE, 'r') as f:
            cfg = json.load(f)
        for k in ('x_start', 'y_start', 'x_end', 'y_end'):
            if k in cfg:
                roi[k] = int(cfg[k])
    except (FileNotFoundError, json.JSONDecodeError, OSError, ValueError, TypeError):
        pass
    return roi


@app.route('/api/wet_roi', methods=['GET'])
def get_wet_roi():
    """Return the persisted wet-detector ROI box (raw pixels in 160x120 frame)."""
    return jsonify(_read_wet_roi())


@app.route('/api/wet_roi', methods=['POST'])
def set_wet_roi():
    """Persist a new ROI box and SIGHUP the detector so it reloads.

    Body: {"x_start": int, "y_start": int, "x_end": int, "y_end": int}
    All four must satisfy 0 <= start < end <= frame_max."""
    body = request.get_json(silent=True) or {}
    try:
        x_s = int(body['x_start'])
        y_s = int(body['y_start'])
        x_e = int(body['x_end'])
        y_e = int(body['y_end'])
    except (KeyError, ValueError, TypeError):
        return jsonify({'success': False, 'error': 'expected ints x_start/y_start/x_end/y_end'}), 400

    # Clamp to frame bounds.
    x_s = max(0, min(x_s, WET_ROI_FRAME_W - 1))
    y_s = max(0, min(y_s, WET_ROI_FRAME_H - 1))
    x_e = max(1, min(x_e, WET_ROI_FRAME_W))
    y_e = max(1, min(y_e, WET_ROI_FRAME_H))
    if x_e <= x_s or y_e <= y_s:
        return jsonify({'success': False, 'error': 'end must be greater than start'}), 400

    roi = {'x_start': x_s, 'y_start': y_s, 'x_end': x_e, 'y_end': y_e}
    try:
        with open(WET_ROI_CONFIG_FILE, 'w') as f:
            json.dump(roi, f)
            f.write('\n')
    except OSError as e:
        return jsonify({'success': False, 'error': f'write failed: {e}'}), 500

    # SIGHUP the detector so it reloads without a streamer restart. If the
    # pidfile is missing or stale we silently skip — the next detector start
    # will pick up the new config from the file anyway.
    try:
        with open(WET_ROI_PID_FILE, 'r') as f:
            pid = int(f.read().strip())
        os.kill(pid, signal.SIGHUP)
        signaled = True
    except (FileNotFoundError, ValueError, ProcessLookupError, PermissionError, OSError):
        signaled = False

    return jsonify({'success': True, 'roi': roi, 'reloaded': signaled})


@app.route('/api/cry_status', methods=['GET'])
def cry_status():
    """Return latest cry state read from the shared events log."""
    return jsonify(read_last_cry_state())


def build_listen_pipeline(target_ip, target_port):
    """gst-launch pipeline that captures from the reSpeaker Lite, encodes Opus
    at voice bitrate, and sends RTP to the parent pi."""
    return [
        "/usr/bin/gst-launch-1.0", "-q",
        "alsasrc", f"device={LISTEN_ALSA_DEVICE}", "!",
        "audioconvert", "!",
        "audioresample", "!",
        f"audio/x-raw,rate={LISTEN_SAMPLE_RATE},channels=1", "!",
        "opusenc", f"bitrate={LISTEN_BITRATE}", "audio-type=voice", "!",
        "rtpopuspay", "pt=97", "!",
        "udpsink", f"host={target_ip}", f"port={target_port}",
        "sync=false", "async=false",
    ]


@app.route('/api/listen/start', methods=['POST'])
def listen_start():
    """Start streaming mic audio to the parent pi (idempotent)."""
    global listen_process

    data = request.get_json(silent=True) or {}
    target_ip = data.get('parent_ip', parent_ip)
    target_port = int(data.get('parent_port', LISTEN_PORT))

    if listen_process and listen_process.poll() is None:
        return jsonify({
            'success': True,
            'already_listening': True,
            'pid': listen_process.pid,
            'parent_ip': target_ip,
            'parent_port': target_port,
        })

    # Cry detector holds the mic; free it before we start streaming.
    _stop_cry_detector()

    cmd = build_listen_pipeline(target_ip, target_port)
    try:
        logger.info(f"Starting listen stream: {' '.join(cmd)}")
        listen_process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=os.setsid,
        )
        return jsonify({
            'success': True,
            'pid': listen_process.pid,
            'parent_ip': target_ip,
            'parent_port': target_port,
            'alsa_device': LISTEN_ALSA_DEVICE,
        })
    except Exception as e:
        logger.error(f"Failed to start listen stream: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/listen/stop', methods=['POST'])
def listen_stop():
    """Stop the mic audio stream."""
    global listen_process
    if not listen_process or listen_process.poll() is not None:
        return jsonify({'success': False, 'error': 'Not listening'}), 400
    try:
        logger.info(f"Stopping listen stream (PID: {listen_process.pid})")
        os.killpg(os.getpgid(listen_process.pid), signal.SIGTERM)
        try:
            listen_process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(listen_process.pid), signal.SIGKILL)
            listen_process.wait()
        listen_process = None
        # Resume cry detection now that the mic is free.
        if AUTO_START_CRY:
            _start_cry_detector()
        return jsonify({'success': True})
    except Exception as e:
        logger.error(f"Failed to stop listen stream: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/listen/status', methods=['GET'])
def listen_status():
    """Whether the mic is currently streaming."""
    global listen_process
    active = listen_process is not None and listen_process.poll() is None
    return jsonify({
        'listening': active,
        'pid': listen_process.pid if active else None,
        'alsa_device': LISTEN_ALSA_DEVICE,
        'port': LISTEN_PORT,
    })


@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint"""
    return jsonify({'status': 'ok'})


if __name__ == '__main__':
    # Run on all interfaces, port 8000
    logger.info("Starting camera control server on port 8000")
    start_camera_stream_autostart()
    if AUTO_START_CRY:
        _start_cry_detector()
    app.run(host='0.0.0.0', port=8000, debug=False)
