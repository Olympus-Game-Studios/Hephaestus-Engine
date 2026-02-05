# Qt Editor UI Build Notes

This document describes how to build Hephaestus Engine with the Qt-based editor UI.

## Overview

The Qt-based editor UI is an alternative to the existing ImGui-based editor. It provides a native Qt Widgets interface with dockable panels and a placeholder viewport. The Qt editor is **UI-only** and does not yet integrate with the Vulkan renderer.

## Requirements

- **Qt 6.x** (6.5+ recommended) with the following modules:
  - Qt6Core
  - Qt6Gui
  - Qt6Widgets
- **CMake 3.24 or higher**

## Installation

### Windows

1. Download and install Qt from https://www.qt.io/download
2. Use the Qt Online Installer and select:
   - Qt 6.x.x → MSVC 2019/2022 64-bit
   - Qt 6.x.x → Qt 5 Compatibility Module (optional)

Default installation path: `C:\Qt`

## CMake Configuration

### Option 1: Using HEPHAESTUS_QT_ROOT (Recommended)

If Qt is installed in `C:\Qt`, the build system will auto-detect it:

```powershell
cmake -S . -B build -DHEPHAESTUS_USE_QT=ON
cmake --build build --config Release
```

For a custom Qt installation path:

```powershell
cmake -S . -B build -DHEPHAESTUS_USE_QT=ON -DHEPHAESTUS_QT_ROOT="D:/CustomQt"
cmake --build build --config Release
```

### Option 2: Using Qt6_DIR

Explicitly specify the Qt6 CMake config directory:

```powershell
cmake -S . -B build -DHEPHAESTUS_USE_QT=ON -DQt6_DIR="C:/Qt/6.7.0/msvc2019_64/lib/cmake/Qt6"
cmake --build build --config Release
```

### Option 3: Using CMAKE_PREFIX_PATH

Add Qt to the CMake prefix path:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
cmake --build build --config Release
```

## Build Options

| Option               | Default | Description                              |
|----------------------|---------|------------------------------------------|
| `HEPHAESTUS_USE_QT`  | ON      | Enable Qt-based editor UI                |
| `HEPHAESTUS_QT_ROOT` | C:/Qt   | Root path to Qt installation             |

## Usage

### Launch Qt Editor

```bash
HephaestusEngine.exe --editor
```

This launches the Qt Widgets editor with:
- Menu bar (File, Edit, View, Help)
- Dockable panels (Outliner, Details, Asset Browser)
- Placeholder viewport (no rendering)

### Launch Legacy ImGui Editor

```bash
HephaestusEngine.exe --editor-legacy
```

Or simply run without arguments (default behavior):

```bash
HephaestusEngine.exe
```

### Launch Runtime Mode

```bash
HephaestusEngine.exe --runtime
```

## File Structure

```
engine/UI/QtEditor/
├── EditorQtApp.h         # RunQtEditor() declaration
├── EditorQtApp.cpp       # Qt application entry point
├── EditorMainWindow.h    # Main window declaration
└── EditorMainWindow.cpp  # Main window implementation
```

## Build Verification

After building, verify the Qt editor works:

1. **Without arguments** - Should launch the legacy ImGui editor with Vulkan rendering
2. **With `--editor`** - Should launch the Qt editor (no Vulkan, responsive UI)
3. **With `--runtime`** - Should launch runtime mode

## Troubleshooting

### Qt6 Not Found

If you see "Qt6 Widgets not found" during CMake configure:

1. Verify Qt6 is installed
2. Check that MSVC 2019/2022 64-bit kit is installed
3. Set Qt6_DIR explicitly:
   ```
   -DQt6_DIR="C:/Qt/6.7.0/msvc2019_64/lib/cmake/Qt6"
   ```

### Missing DLLs at Runtime

The build system automatically copies Qt DLLs to the output directory. If you see DLL errors:

1. Ensure the following files exist in the build output directory:
   - Qt6Core.dll
   - Qt6Gui.dll
   - Qt6Widgets.dll
   - platforms/qwindows.dll (platform plugin)

2. If missing, manually copy from your Qt installation's `bin/` directory

### Qt Platform Plugin Error

If you see "This application failed to start because no Qt platform plugin could be initialized":

1. Ensure `platforms/qwindows.dll` exists next to the executable
2. Or set the QT_PLUGIN_PATH environment variable:
   ```
   set QT_PLUGIN_PATH=C:\Qt\6.7.0\msvc2019_64\plugins
   ```

## Limitations

The Qt editor is currently **UI-only**:

- No Vulkan rendering
- No 3D viewport (placeholder label only)
- No scene editing
- No asset operations

This is intentional for this milestone. Renderer integration will be added in a future milestone.

## Reverting Changes

To revert to a Qt-free build:

1. Set `HEPHAESTUS_USE_QT=OFF` in CMake:
   ```
   cmake -S . -B build -DHEPHAESTUS_USE_QT=OFF
   ```

2. The Qt editor files will still exist but won't be compiled

3. `--editor` flag will print an error and fall back to legacy editor
