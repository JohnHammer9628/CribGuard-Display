# Get CribGuard-Display onto the Raspberry Pi

Option A — Clone from your Git remote:

```bash
cd ~
git clone <your-repo-url> CribGuard-Display
cd ~/CribGuard-Display
```

Option B — Copy from your Windows machine via scp:

```powershell
# In PowerShell on your PC, replace <user> and <pi-host>
scp -r "C:\Users\johnh\School\CPE190\CribGuard-Display" <user>@<pi-host>:/home/<user>/
```

Then on the Pi:

```bash
cd /home/<user>/CribGuard-Display
```


