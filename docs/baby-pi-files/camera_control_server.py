#!/usr/bin/env python3
"""
Camera Control Server for Baby Pi
Provides REST API to start/stop MLX90640 IR camera streaming
"""

from flask import Flask, jsonify, request
from flask_cors import CORS
import subprocess
import signal
import os
import logging
import shutil

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from Parent Pi

# Configure logging
logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

# Global state
streaming_process = None
recording_process = None
playback_process = None
parent_ip = "10.0.0.98"
parent_port = 5000
LULLABIES_DIR = os.path.expanduser("~/Lullabies")
os.makedirs(LULLABIES_DIR, exist_ok=True)

@app.route('/api/status', methods=['GET'])
def get_status():
    """Get current camera status"""
    global streaming_process
    
    is_streaming = streaming_process is not None and streaming_process.poll() is None
    
    return jsonify({
        'streaming': is_streaming,
        'recording': (recording_process is not None and recording_process.poll() is None),
        'parent_ip': parent_ip,
        'parent_port': parent_port,
        'pid': streaming_process.pid if is_streaming else None
    })

@app.route('/api/start', methods=['POST'])
def start_camera():
    """Start IR camera streaming"""
    global streaming_process, parent_ip, parent_port
    
    # Check if already streaming
    if streaming_process and streaming_process.poll() is None:
        logger.warning("Camera already streaming")
        return jsonify({'success': False, 'error': 'Already streaming'}), 400
    
    # Get optional parameters
    data = request.get_json() or {}
    target_ip = data.get('parent_ip', parent_ip)
    target_port = data.get('parent_port', parent_port)
    
    # Update global config
    parent_ip = target_ip
    parent_port = target_port
    
    # Start streaming process
    try:
        cmd = ['./mlx90640_streaming', parent_ip, str(parent_port)]
        logger.info(f"Starting camera stream: {' '.join(cmd)}")
        
        streaming_process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=os.setsid  # Create new process group for clean shutdown
        )
        
        logger.info(f"Camera streaming started (PID: {streaming_process.pid})")
        return jsonify({
            'success': True,
            'pid': streaming_process.pid,
            'parent_ip': parent_ip,
            'parent_port': parent_port
        })
        
    except Exception as e:
        logger.error(f"Failed to start camera: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/stop', methods=['POST'])
def stop_camera():
    """Stop IR camera streaming"""
    global streaming_process
    
    if not streaming_process or streaming_process.poll() is not None:
        logger.warning("Camera not streaming")
        return jsonify({'success': False, 'error': 'Not streaming'}), 400
    
    try:
        # Send SIGTERM to process group
        logger.info(f"Stopping camera stream (PID: {streaming_process.pid})")
        os.killpg(os.getpgid(streaming_process.pid), signal.SIGTERM)
        
        # Wait for process to exit (with timeout)
        try:
            streaming_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.warning("Process didn't exit gracefully, sending SIGKILL")
            os.killpg(os.getpgid(streaming_process.pid), signal.SIGKILL)
            streaming_process.wait()
        
        streaming_process = None
        logger.info("Camera streaming stopped")
        return jsonify({'success': True})
        
    except Exception as e:
        logger.error(f"Failed to stop camera: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500

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
    device  = str(data.get('device', 'default'))
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
    global parent_ip, parent_port
    
    data = request.get_json() or {}
    
    if 'parent_ip' in data:
        parent_ip = data['parent_ip']
        logger.info(f"Updated parent_ip to {parent_ip}")
    
    if 'parent_port' in data:
        parent_port = int(data['parent_port'])
        logger.info(f"Updated parent_port to {parent_port}")
    
    return jsonify({
        'success': True,
        'parent_ip': parent_ip,
        'parent_port': parent_port
    })

@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint"""
    return jsonify({'status': 'ok'})

if __name__ == '__main__':
    # Run on all interfaces, port 8000
    logger.info("Starting camera control server on port 8000")
    app.run(host='0.0.0.0', port=8000, debug=False)

