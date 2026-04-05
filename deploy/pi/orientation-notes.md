# Screen Orientation Options (Raspberry Pi 5 + Bookworm KMS/DRM)

## Quick (SDL-only)

If the panel is mounted in portrait but you want the app in landscape, add one of these to the run script:

```bash
export SDL_VIDEO_KMSDRM_ROTATION=90   # or 270
```

## OS-level rotation (DSI 7-inch)

Use a KMS overlay with rotation parameter in `/boot/firmware/config.txt`:

```
dtoverlay=vc4-kms-dsi-7inch,rotate=270
```

Check supported parameters:

```bash
dtoverlay -h vc4-kms-dsi-7inch
```

Reboot after changes.


