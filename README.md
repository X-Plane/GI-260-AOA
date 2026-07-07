# Garmin GI260 Popout

X-Plane 12 popout plugin that recreates the Garmin GI 260 AOA indicator.

## Repository Contents

- `src/plugin.cpp` - plugin implementation
- `Textures/` - runtime PNG textures
- `Resources/Beep.wav` - alert beep sample
- `config.ini` - runtime tuning and dataref configuration
- `CMakeLists.txt` - Windows build/deploy configuration

Generated binaries, local toolchains, rollback folders, PSD source files, and the X-Plane SDK are intentionally not tracked.

## X-Plane SDK

Install or unpack the X-Plane SDK locally, then point CMake at it:

```powershell
cmake -S . -B build -DXPLANE_SDK_DIR="C:/Path/To/XPlaneSDK"
cmake --build build --config Release
```

For local convenience, an ignored `SDK/` folder at the repo root is also supported.

## Deployment

By default, CMake deploys to:

```text
D:/SteamLibrary/steamapps/common/X-Plane 12/Resources/plugins/Garmin GI260 Popout
```

Override with:

```powershell
cmake -S . -B build -DXPLANE_PLUGIN_ROOT_DIR="D:/Path/To/X-Plane 12/Resources/plugins/Garmin GI260 Popout"
```

The final Windows plugin binary is `64/win.xpl`.
