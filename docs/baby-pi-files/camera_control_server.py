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

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from Parent Pi

# Configure logging
logging.basicConfig(level=logging.INFO, format='[%(asctime)s] %(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

# Global state
streaming_process = None
parent_ip = "10.0.0.98"
parent_port = 5000

@app.route('/api/status', methods=['GET'])
def get_status():
    """Get current camera status"""
    global streaming_process
    
    is_streaming = streaming_process is not None and streaming_process.poll() is None
    
    return jsonify({
        'streaming': is_streaming,
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

