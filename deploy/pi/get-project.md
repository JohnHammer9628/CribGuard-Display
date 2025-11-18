# Get CribGuard-Display onto the Raspberry Pi

Option A — Clone from your Git remote:

```bash
cd ~
git clone <your-repo-url> CribGuard-Display
cd ~/CribGuard-Display
```

Option B — Copy from your Windows machine via scp:

```powershell
# In PowerShell on your PC, replace <pi-host> with your Pi's hostname or IP
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" pi@<pi-host>:/home/pi/CribGuard-Display
```

Then on the Pi:

```bash
cd ~/CribGuard-Display
```


