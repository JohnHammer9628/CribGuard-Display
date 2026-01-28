# CribGuard Smoke Test (Pi demo-critical)

Use this checklist after *every* cleanup change on the `cleanup` branch to ensure we didn’t break the working demo.

## Known-good demo network mapping (showcase)
- **Baby Pi (IR camera + control server)**: `10.0.0.153`
- **Parent Pi (UI/display)**: `10.0.0.98`

## Prereqs
- `settings.cfg` on Parent Pi points at the Baby Pi:
  - `baby_pi_ip=10.0.0.153`
  - `baby_pi_port=8000`
  - `camera_stream_port=5000`
  - `audio_record_seconds=30`
  - `audio_play_device=plughw:1,0`

## 1) Baby Pi: start the control server (camera + lullabies)
On the Baby Pi:
- Confirm the server file exists:
  - Canonical: `baby-pi/camera_control_server.py`
  - Legacy shim: `jammin@10.0.0.153`
- Start it (exact command depends on where it lives on the Baby Pi):

```bash
# Preferred
python3 baby-pi/camera_control_server.py

# Legacy (kept working during cleanup)
./jammin@10.0.0.153
```

Sanity checks:

```bash
curl http://localhost:8000/health
curl http://localhost:8000/api/status
curl http://localhost:8000/api/lullabies
```

Expected:
- `/health` returns `{"status":"ok"}`
- `/api/status` returns JSON (streaming may be false initially)
- `/api/lullabies` returns `{"success": true, "files": [...]}` (may be empty)

## 2) Parent Pi: build and run the UI
On the Parent Pi:

```bash
cmake -S /home/<user>/CribGuard-Display -B /home/<user>/CribGuard-Display/build-pi -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/<user>/CribGuard-Display/build-pi -j"$(nproc)"
DISPLAY=:0 /home/<user>/CribGuard-Display/build-pi/crib_guard_pi
```

Expected:
- UI launches and is responsive.

## 3) Camera flow (UI + Baby Pi)
From the UI:
- Open **Cam** modal.
- Press **Play** (or equivalent) to start camera.

Expected:
- Baby Pi starts streaming (Baby Pi `/api/status` shows `streaming: true`).
- Parent Pi shows frames (or at minimum transitions to “playing” state without errors).

Stop:
- Press **Stop**.

Expected:
- Baby Pi `/api/status` shows `streaming: false`.

## 4) Lullabies flow (UI + Baby Pi)
From the UI:
- Open **Lullabies** modal.
- Confirm it fetches a list (non-empty if lullabies were uploaded/copied onto Baby Pi).
- Tap a lullaby to **play**.
- Use **Stop Play** to stop.

Expected:
- Audio plays via ALSA device `audio_play_device` (default: `plughw:1,0`).
- Stop halts playback reliably.

## 5) Lullaby rename flow (UI + Baby Pi)
From the UI:
- Long-press a lullaby (or use the rename UI action).
- Rename to a new value.

Expected:
- UI reflects updated name after refresh.
- Baby Pi server responds success on rename.

## 6) Audio record flow (optional if demo uses it)
From the UI:
- Trigger a record action (uses `audio_record_seconds`).

Expected:
- Baby Pi returns success and a file path.

