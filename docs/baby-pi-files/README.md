# Baby Pi Files

This directory contains all the files needed for the Baby Pi (the Pi with the IR camera attached).

## Files

1. **mlx90640_streaming.cpp** - C++ program that captures IR camera frames and streams them via GStreamer
2. **camera_control_server.py** - Python Flask server that provides REST API to control the camera
3. **Makefile.addition** - Lines to add to the mlx90640-library Makefile
4. **cribguard-camera.service** - systemd service file for auto-starting the control server

## Installation on Baby Pi

### 1. Copy files to Baby Pi

Copy these files to `~/mlx90640-library/` on your Baby Pi:
- `mlx90640_streaming.cpp`
- `camera_control_server.py`

```bash
# On your development machine
scp mlx90640_streaming.cpp pi@10.0.0.153:~/mlx90640-library/
scp camera_control_server.py pi@10.0.0.153:~/mlx90640-library/

# Make the Python script executable
ssh pi@10.0.0.153
cd ~/mlx90640-library
chmod +x camera_control_server.py
```

### 2. Update Makefile

On Baby Pi, edit `~/mlx90640-library/Makefile` and add the line from `Makefile.addition`:

```bash
cd ~/mlx90640-library
nano Makefile
```

Add at the end:
```makefile
mlx90640_streaming: mlx90640_streaming.cpp functions/MLX90640_API.o functions/MLX90640_LINUX_I2C_Driver.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) `pkg-config --cflags --libs opencv4`
```

### 3. Build

```bash
cd ~/mlx90640-library
make mlx90640_streaming
```

### 4. Test manually

```bash
# Terminal 1: Start control server
./camera_control_server.py

# Terminal 2: Test API
curl http://localhost:8000/health
curl -X POST http://localhost:8000/api/start
curl http://localhost:8000/api/status
curl -X POST http://localhost:8000/api/stop
```

### 5. Set up auto-start (optional)

```bash
sudo cp cribguard-camera.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable cribguard-camera.service
sudo systemctl start cribguard-camera.service
sudo systemctl status cribguard-camera.service
```

## Configuration

### Change Parent Pi IP

Edit `camera_control_server.py` and change:
```python
parent_ip = "10.0.0.98"  # Your Parent Pi IP
```

Or pass it via API when starting:
```bash
curl -X POST http://localhost:8000/api/start \
  -H "Content-Type: application/json" \
  -d '{"parent_ip": "10.0.0.98", "parent_port": 5000}'
```

### Adjust video quality

Edit `mlx90640_streaming.cpp`:
```cpp
const int OUT_WIDTH = 640;   // Resolution
const int OUT_HEIGHT = 480;
const int FPS = 8;           // Frame rate

// In GStreamer pipeline:
"x264enc tune=zerolatency bitrate=800 ..."  // Bitrate in Kbps
```

Lower values = less bandwidth, lower quality.
Higher values = more bandwidth, better quality.

Rebuild after changes:
```bash
make mlx90640_streaming
```

## Troubleshooting

### Sensor not detected
```bash
i2cdetect -y 1
```
Should show `33`. If not, check I2C is enabled and sensor connections.

### Build errors
```bash
# Missing OpenCV
sudo apt install -y libopencv-dev

# Missing GStreamer
sudo apt install -y gstreamer1.0-tools gstreamer1.0-plugins-{base,good,bad,ugly} gstreamer1.0-libav
```

### Control server won't start
```bash
# Missing Flask
sudo pip3 install flask flask-cors

# Check if port 8000 is already in use
sudo netstat -tulpn | grep 8000
```

### No video on Parent Pi

Check streaming is running:
```bash
curl http://localhost:8000/api/status
```

Check process:
```bash
ps aux | grep mlx90640_streaming
```

View logs:
```bash
sudo journalctl -u cribguard-camera.service -f
```

## API Reference

### GET /health
Health check.

**Response:** `{"status": "ok"}`

### GET /api/status
Get streaming status.

**Response:**
```json
{
  "streaming": true,
  "parent_ip": "10.0.0.98",
  "parent_port": 5000,
  "pid": 12345
}
```

### POST /api/start
Start camera streaming.

**Request (optional):**
```json
{
  "parent_ip": "10.0.0.98",
  "parent_port": 5000
}
```

**Response:**
```json
{
  "success": true,
  "pid": 12345,
  "parent_ip": "10.0.0.98",
  "parent_port": 5000
}
```

### POST /api/stop
Stop camera streaming.

**Response:**
```json
{
  "success": true
}
```

### POST /api/config
Update configuration without starting stream.

**Request:**
```json
{
  "parent_ip": "10.0.0.98",
  "parent_port": 5000
}
```

## Network Requirements

- Baby Pi and Parent Pi must be on same local network
- UDP port 5000 must be open for video streaming
- TCP port 8000 must be open for control API
- Minimum 1 Mbps bandwidth recommended

## See Also

- `../baby-pi-setup.md` - Complete setup guide
- `../parent-pi-integration.md` - Parent Pi setup
- `../QUICKSTART.md` - Quick start guide

