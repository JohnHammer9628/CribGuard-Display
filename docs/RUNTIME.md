# CribGuard Runtime (what runs where)

This document clarifies the *current working* runtime arrangement used for the demo/showcase so cleanup work doesn’t accidentally break the system.

## Network mapping (showcase)
- **Baby Pi (IR camera + control server)**: `10.0.0.153`
- **Parent Pi (UI/display)**: `10.0.0.98`

## Parent Pi (UI/display)
- **Binary**: `crib_guard_pi`
- **Source entrypoint**: `platforms/pi-sdl/main.cpp`
- **Config**: `settings.cfg`
  - `baby_pi_ip` / `baby_pi_port`: where the Baby Pi control server is reachable
  - `camera_stream_port`: UDP port used for the IR stream
  - `audio_record_seconds`: default record length used by UI
  - `audio_play_device`: ALSA device string sent to Baby Pi `/api/play`

## Baby Pi (IR camera + control server)
The canonical Baby Pi server implementation is:
- `baby-pi/camera_control_server.py`

Legacy compatibility entrypoint (kept runnable during cleanup):
- `jammin@10.0.0.153` (shim that executes the canonical server)

It provides (at minimum) these endpoints used by the UI:
- `GET /health`
- `GET /api/status`
- `POST /api/start`
- `POST /api/stop`
- `GET /api/lullabies`
- `POST /api/play`
- `POST /api/play/stop`
- `POST /api/rename`
- (optional) `POST /api/record` and `POST /api/record/stop`

### Important note for cleanup
There is also a reference copy under `docs/baby-pi-files/camera_control_server.py`. During cleanup we keep one canonical implementation (`baby-pi/`) and treat the `docs/` copy as documentation/reference only.

During cleanup we will:
- Move/rename the canonical Baby Pi server into a clear location (e.g. `baby-pi/camera_control_server.py`).\n
- Keep docs/reference copies clearly labeled so there is only one “source of truth.”

