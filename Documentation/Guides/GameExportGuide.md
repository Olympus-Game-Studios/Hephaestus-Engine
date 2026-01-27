# Hephaestus Engine - Game Runtime and Export Guide

This document explains the Editor vs Game Runtime separation and how to export standalone games.

## Architecture Overview

The Hephaestus Engine now has two separate build targets:

1. **HephaestusEngine.exe** (Editor)
   - Full editor with UI, asset browser, inspector, outliner
   - Uses ImGui, ImGuizmo, ImNodes for editor interface
   - Uses Ultralight for HTML-based panels (optional)
   - Project manager, material editor, etc.

2. **HephaestusGame.exe** (Game Runtime)
   - Minimal standalone runtime for shipped games
   - No editor UI - just game execution
   - No Ultralight dependency (by default)
   - Smaller executable size
   - Includes crash handling and logging

## Building Both Targets

```bash
# Configure with both targets enabled (default)
cmake -B build -DHEPHAESTUS_BUILD_EDITOR=ON -DHEPHAESTUS_BUILD_GAME=ON

# Build both
cmake --build build --config Release

# Build only the game runtime
cmake --build build --config Release --target HephaestusGame

# Build only the editor
cmake --build build --config Release --target HephaestusEngine
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `HEPHAESTUS_BUILD_EDITOR` | ON | Build the editor (HephaestusEngine.exe) |
| `HEPHAESTUS_BUILD_GAME` | ON | Build the game runtime (HephaestusGame.exe) |
| `HEPHAESTUS_GAME_SCRIPTING` | ON | Enable Lua scripting in game runtime |
| `HEPHAESTUS_GAME_USE_ULTRALIGHT` | OFF | Enable Ultralight UI in game runtime (rare) |
| `HEPHAESTUS_USE_ULTRALIGHT` | ON | Enable Ultralight for the editor |

## Exporting a Game

### Option 1: From the Editor

1. Open your project in the editor
2. Load the scene you want to export
3. Click the **Export** button in the toolbar
4. The export will be created in `YourProject/dist/YourProject_export/`

### Option 2: Using the Export Script

```powershell
# From the engine root directory
.\tools\export_game.ps1 -ProjectPath "path/to/your/project" -ProjectName "MyGame"

# With options
.\tools\export_game.ps1 `
    -ProjectPath "." `
    -ProjectName "MyAwesomeGame" `
    -OutputPath "C:\Builds\MyGame" `
    -Configuration "Release" `
    -CreateZip
```

## Exported Package Structure

```
MyGame/
├── MyGame.exe              # Renamed game runtime
├── lua.dll                 # Lua scripting (if enabled)
├── game.ini                # Game configuration
├── RunGame.bat             # Launcher script
├── export_manifest.txt     # Package contents list
├── assets/
│   ├── scenes/
│   │   └── MainLevel.hpscene
│   ├── meshes/
│   └── textures/
├── scripts/
│   └── *.lua               # Game scripts
├── shaders/
│   └── *.spv               # Compiled shaders
├── logs/                   # Created at runtime
│   └── runtime.log
└── crashes/                # Created if crash occurs
    ├── crash_YYYYMMDD_HHMMSS.dmp
    └── crash_YYYYMMDD_HHMMSS.txt
```

## Game Configuration (game.ini)

The exported game reads settings from `game.ini`:

```ini
[Game]
project_name = MyGame
window_title = My Awesome Game
scene = assets/scenes/MainLevel.hpscene

[Display]
window_width = 1920
window_height = 1080
fullscreen = false
vsync = true
```

## Command Line Arguments

The game runtime accepts these command-line arguments:

| Argument | Description |
|----------|-------------|
| `--scene <path>` | Override the scene to load |
| `--width <pixels>` | Override window width |
| `--height <pixels>` | Override window height |
| `--fullscreen` | Start in fullscreen mode |
| `--windowed` | Start in windowed mode |
| `--help` | Show help message |

Example:
```bash
MyGame.exe --scene "assets/scenes/Level2.hpscene" --fullscreen
```

## Crash Handling

The game runtime includes automatic crash handling:

1. **Crash Detection**: Catches unhandled exceptions
2. **Minidump Generation**: Creates `.dmp` files for debugging
3. **Crash Logs**: Human-readable crash reports with:
   - Exception code and address
   - Register dump
   - Timestamp and initialization stage
4. **User Notification**: Shows message box with crash info

Crash files are saved to the `crashes/` folder next to the executable.

## Troubleshooting

### "lua.dll not found"
Make sure Lua is installed via vcpkg or the DLL is copied to the game folder:
```bash
vcpkg install lua:x64-windows
```

### "Failed to create Vulkan instance"
- Ensure graphics drivers are up to date
- Verify Vulkan SDK is installed or `vulkan-1.dll` is available

### Game crashes immediately
1. Check `logs/runtime.log` for startup progress
2. Check `crashes/` folder for crash reports
3. Verify all required DLLs are present
4. Ensure shaders are compiled (`.spv` files in `shaders/`)

### Scene not loading
- Verify the scene file exists in `assets/scenes/`
- Check the path in `game.ini` is correct (use forward slashes)
- Look for errors in `logs/runtime.log`

## Why Two Executables?

**Problem**: The original design used a single executable with runtime flags to toggle editor features. This caused:
- Ultralight DLLs were always required (even for games)
- Editor code was still linked, increasing size
- Initialization order issues caused crashes
- Complex conditional paths at runtime

**Solution**: Compile-time separation:
- Game runtime doesn't link Ultralight or ImGui (by default)
- Smaller executable size (~50% reduction)
- No editor code paths to accidentally trigger
- Cleaner dependency management
- Proper crash handling without editor overhead

## Requirements for End Users

- Windows 10/11 (64-bit)
- Vulkan-compatible GPU
- Visual C++ Redistributable 2019 or later
- (Optionally) Install Vulkan Runtime from LunarG if not bundled with drivers
