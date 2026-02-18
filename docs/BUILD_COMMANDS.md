# Build & Run Commands (Copy/Paste)

This file is intentionally short and copy/paste-friendly.

## Windows (PowerShell) - Configure + Build + Run (SDL)

```powershell
$vc="C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if(!(Test-Path $vc)){$vc="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"}

# Configure
& "$env:windir\System32\cmd.exe" /c "set PATH=%SystemRoot%\System32\WindowsPowerShell\v1.0;%PATH% && call ""$vc"" && cd /d ""%CD%"" && cmake --preset win-rel"

# Build
& "$env:windir\System32\cmd.exe" /c "set PATH=%SystemRoot%\System32\WindowsPowerShell\v1.0;%PATH% && call ""$vc"" && cd /d ""%CD%"" && cmake --build --preset win-rel -j"

# Run
& "$env:windir\System32\cmd.exe" /c "set PATH=%SystemRoot%\System32\WindowsPowerShell\v1.0;%PATH% && call ""$vc"" && cd /d ""%CD%"" && .\build-win\crib_guard_pi.exe"
```

If you see `The system cannot find the path specified`, the run path is wrong. The exe is emitted to `build-win\crib_guard_pi.exe`.
If you see `LNK1181: cannot open input file 'winmm.lib'`, install the Windows 10/11 SDK in Visual Studio Build Tools and re-run the commands.
