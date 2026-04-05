# Build & Run Commands (Copy/Paste)

This file is intentionally short and copy/paste-friendly.

## Windows (PowerShell) - Configure + Build + Run (SDL)

Note: In PowerShell, you must use the wrapper below so MSVC/SDK paths are set.

```powershell
$vc="C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if(!(Test-Path $vc)){$vc="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"}

# Configure + Build + Run (single cmd so env is preserved)
& "$env:windir\System32\cmd.exe" /v:on /c "call ""$vc"" && cd /d ""%CD%"" && cmake --preset win-rel && cmake --build --preset win-rel -j && .\build-win\crib_guard_pi.exe"
```

If you see `The system cannot find the path specified`, the run path is wrong. The exe is emitted to `build-win\crib_guard_pi.exe`.
If you see `LNK1181: cannot open input file 'winmm.lib'`, install the Windows 10/11 SDK in Visual Studio Build Tools and re-run the commands.

## Raspberry Pi 5 (Bookworm 64-bit + 7" DSI) - One-command Kiosk Install

Terminal: Raspberry Pi (SSH)

```bash
cd /home/<user>/CribGuard-Display
bash deploy/pi/install-kiosk.sh
```

Service and logs:

```bash
sudo systemctl status cribguard@<user>.service
journalctl -u cribguard@<user>.service -b -f
```
