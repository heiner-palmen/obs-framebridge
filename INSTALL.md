# Installation

## Requirements

- OBS Studio **32.0+**
- Windows 10/11 x64 **or** Linux x64

---

## Windows

1. Download `obs-framebridge-win-x64-X.X.X.zip` from the [latest release](https://github.com/heiner-palmen/obs-framebridge/releases/latest)
2. Extract the archive
3. Copy files to your OBS installation:

```
obs-framebridge-win-x64-X.X.X/
  obs-plugins/
    64bit/
      obs-framebridge.dll  →  C:\Program Files\obs-studio\obs-plugins\64bit\
  data/
    obs-plugins/
      obs-framebridge/
        locale/            →  C:\Program Files\obs-studio\data\obs-plugins\obs-framebridge\
```

4. Restart OBS Studio
5. Load any Lua script that calls the `framebuffer_*` procedures

---

## Linux

1. Download `obs-framebridge-linux-x64-X.X.X.tar.gz` from the [latest release](https://github.com/heiner-palmen/obs-framebridge/releases/latest)
2. Extract the archive:

```bash
tar -xzf obs-framebridge-linux-x64-X.X.X.tar.gz
```

3. Copy files:

```bash
# Ubuntu / Debian (x86_64) — most common:
sudo cp obs-framebridge-linux-x64-X.X.X/obs-plugins/64bit/obs-framebridge.so \
    /usr/lib/x86_64-linux-gnu/obs-plugins/

# Arch / Fedora / other distros:
# sudo cp obs-framebridge-linux-x64-X.X.X/obs-plugins/64bit/obs-framebridge.so \
#     /usr/lib/obs-plugins/

sudo cp -r obs-framebridge-linux-x64-X.X.X/data/obs-plugins/obs-framebridge \
    /usr/share/obs/obs-plugins/
```

4. Restart OBS Studio

---

## Verify integrity

Each release includes `.sha256` files. To verify your download:

**Windows (PowerShell):**
```powershell
Get-FileHash obs-framebridge-win-x64-X.X.X.zip -Algorithm SHA256
```
Compare the output with the contents of `obs-framebridge-win-x64-X.X.X.zip.sha256`.

**Linux:**
```bash
sha256sum -c obs-framebridge-linux-x64-X.X.X.tar.gz.sha256
```

---

## Verify the plugin loaded

After restarting OBS, open the **Tools** menu — you should see **OBS FrameBridge** listed there.

Alternatively, load the test script from the `test/` folder:

```
test/test_obs-framebridge_loads.lua
```

It will print a confirmation to the OBS script log if the plugin is active.

---

## Uninstall

Simply delete the files you copied during installation and restart OBS.

---

## Building from source

See [README.md](README.md#building) for build instructions.
