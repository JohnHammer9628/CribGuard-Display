# Cry Detector (Baby Pi)

This service continuously monitors the microphone and can auto-play a lullaby
when sustained cry-like audio is detected. It runs independently of camera view/state.

## Install

On Baby Pi (Pi #2):

```bash
cd /path/to/CribGuard-Display
bash baby-pi/install-cry-detector.sh
```

## Service control

```bash
sudo systemctl status cribguard-cry-detector@<user>.service
sudo systemctl restart cribguard-cry-detector@<user>.service
journalctl -u cribguard-cry-detector@<user>.service -b -f
```

## Runtime config

Config file: `/etc/default/cribguard-cry-detector`

Common settings:

- `CRY_AUDIO_DEVICE` (default `plughw:2,0`)
- `CRY_ENABLE_AUTO_PLAY` (`1` or `0`)
- `CRY_PLAY_DEVICE` (default `default`)
- `CRY_LULLABY_COOLDOWN_SEC`
- `CRY_SUPPRESS_AFTER_PLAY_SEC` (mute detection briefly after auto-play starts)
- `CRY_SUPPRESS_WHILE_PLAYING` (`1` or `0`)
- `CRY_PLAYBACK_PEEK_SEC` (seconds detection is allowed during playback)
- `CRY_PLAYBACK_PEEK_INTERVAL_SEC` (peek cycle length)
- `CRY_PLAYBACK_DELTA_BOOST_DB` (stricter threshold during playback peeks)
- `CRY_MODEL_PATH` (optional path to `.joblib` cry model)
- `CRY_MODEL_THRESHOLD` (default `0.65`)
- `CRY_MODEL_USE_ONLY` (`1` = model decides cry/no-cry, `0` = require dB + model)
- `CRY_MODEL_WINDOW_SEC` (audio window for model features, default `1.0`)
- `CRY_MODEL_EVAL_INTERVAL_SEC` (how often model runs, default `0.25`)
- `CRY_MIN_DB`
- `CRY_DELTA_DB`
- `CRY_SUSTAIN_SEC`
- `CRY_RELEASE_SEC`

After edits:

```bash
sudo systemctl restart cribguard-cry-detector@<user>.service
```

## Status file

Current detector state is written to:

`/tmp/cribguard_cry_status.json`

When model mode is enabled, status also includes:

- `model_enabled`
- `model_prob_cry`
- `model_is_cry`

## Model mode (optional)

If you trained a model (for example `/home/jammin/cry-data/model/cry_logreg.joblib`):

```bash
sudo sed -i 's|^CRY_MODEL_PATH=.*|CRY_MODEL_PATH="/home/jammin/cry-data/model/cry_logreg.joblib"|' /etc/default/cribguard-cry-detector
sudo sed -i 's|^CRY_MODEL_USE_ONLY=.*|CRY_MODEL_USE_ONLY="1"|' /etc/default/cribguard-cry-detector
sudo sed -i 's|^CRY_MODEL_THRESHOLD=.*|CRY_MODEL_THRESHOLD="0.65"|' /etc/default/cribguard-cry-detector
sudo systemctl restart cribguard-cry-detector@<user>.service
```
