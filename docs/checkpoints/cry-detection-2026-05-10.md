# Cry Detection Checkpoint - 2026-05-10

Base commit: `2a7bcdc`

This checkpoint captures the current in-progress cry detection work before
more tuning. The matching patch file is:

```text
docs/checkpoints/cry-detection-2026-05-10.patch
```

Current modified files:

- `baby-pi/cry_detector.py`
- `baby-pi/camera_control_server.py`

Main changes captured:

- Added baby-range pitch estimation to `cry_detector.py`.
- Cry chunks now require loudness, cry-band spectral shape, and either
  baby-range pitch or an especially strong cry-shaped spectrum.
- Cry telemetry now emits `pitch_hz`, `pitch_conf`, `flatness`, and `score`
  for tuning normal talking vs. baby crying.
- `camera_control_server.py` now points at the same default event log as the
  detector, exposes extra cry telemetry, logs detector output to a file, and
  handles microphone ownership for listen/record modes.

Restore this checkpoint from the repo root:

```bash
git apply docs/checkpoints/cry-detection-2026-05-10.patch
```

To inspect the patch first:

```bash
git apply --check docs/checkpoints/cry-detection-2026-05-10.patch
```

Note: `.tmp_mlx_lib/` was already untracked and is not part of this checkpoint.
