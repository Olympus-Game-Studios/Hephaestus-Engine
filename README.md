
# Hephaestus Engine

A modern, lightweight Vulkan-based game engine with Entity-Component-System (ECS) architecture. This repository contains a rendering engine, scene graph, ImGui-based editor, and tools for learning and experimentation.

## Overview

This project demonstrates a modern engine architecture with:
- **Vulkan rendering pipeline**
- **Entity-Component-System (ECS)** using EnTT
- **Scene graph** with hierarchical transforms
- **ImGui-based editor** with ImGuizmo integration
- **Ray picking** for object selection
- **Unreal-style camera controls**

Key parts:
- `engine/` — Core engine code (graphics, scene/ECS, platform, physics hooks)
  - `Core/` — Application entry point and math utilities
  - `Gfx/` — Vulkan rendering abstraction
  - `Scene/` — **NEW!** ECS system (Entity, Scene, Components)
  - `UI/` — Editor panels including material graph editor
  - `Assets/` — Asset loading and management (models, textures)
- `editor/` — Editor application and widgets
- `extern/` — Third-party libraries (glfw, imgui, ImGuizmo, ImNodes, EnTT, VMA, stb_image)
- `shaders/` — GLSL shader sources and compiled SPIR-V
- `build/` — CMake/Visual Studio generated build files

## Features

### ✨ Entity-Component-System (ECS)
- Modern, data-oriented architecture using **EnTT**
- Compose entities from reusable components
- Efficient component queries and iteration
- Type-safe component access
- Scene graph with parent-child hierarchy

### 🎮 Built-in Components
- **TagComponent** - Entity naming
- **TransformComponent** - Position, rotation, scale with hierarchy
- **MeshRendererComponent** - Rendering data and bounding boxes
- **MaterialComponent** - Full PBR material properties with texture support
- **SelectionComponent** - Editor selection state
- **CameraComponent** - Camera properties

### 🎨 Material System
- **PBR Workflow** - Physically-based rendering with metallic/roughness workflow
- **Texture Support** - Albedo, Normal Maps, Roughness Maps, Ambient Occlusion
- **Node-Based Editor** - Visual material graph editor with real-time preview
- **Material Assets** - Save and reuse materials across multiple entities
- **Simple & Advanced Modes** - UI properties editor and visual node graph

### 🛠️ Editor Tools
- Scene hierarchy window
- Entity properties inspector
- **Material Editor** - Visual node-based material authoring
- ImGuizmo transform manipulation (Translate/Rotate/Scale)
- Ray-based object picking
- Real-time component editing
- **Export & Packaging** - One-click scene export with automatic asset collection

### 📦 Export & Distribution
- **One-Click Export** - Export button in editor toolbar
- **Asset Collection** - Automatically gathers all referenced assets (meshes, textures, materials, scripts)
- **Packaging Script** - PowerShell tool for validation and manifest generation
- **Reproducible Builds** - Create distributable runtime packages
- **Pure Runtime Binary** - Exported `HephaestusEngine.exe` auto-detects packaged builds, hides the editor UI, and boots straight into gameplay (use `--editor` to override)
- **See:** `Documentation/Features/EXPORT_PACKAGING.md` for complete guide

### 📝 Scripting System (Lua)
- **Lua Scripts** - Interpreted, rapid iteration and the primary supported scripting language
  - No recompilation needed
  - Fast prototyping and iteration
  - Attach scripts via the Editor Details Panel
  - Unified lifecycle callbacks (onAwake, onUpdate, onLateUpdate, onDestroy)

**Note:** C++-based scripting is deprecated in this repository. Use Lua for game scripting; keep C++ for engine internals and performance-critical systems.

**Quick Start**: See `Documentation/Scripting/SCRIPTING_QUICK_START.md` for immediate examples.

## Prerequisites (Windows)

- Visual Studio 2019 or 2022 with Desktop development for C++ workload.
- CMake (>= 3.16 recommended).
- Vulkan SDK (for headers, loader and validation tools). Set `VULKAN_SDK` environment variable via the SDK installer.
- glslangValidator (usually included with the Vulkan SDK) to compile GLSL to SPIR-V.
- Git (to clone the repo and submodules, including `extern/ufbx`). Use `git clone --recurse-submodules` or `git submodule update --init --recursive` after a plain clone.

Optional but useful:
- Python for small helper scripts.

If you don't have `glslangValidator` available on PATH, use the Vulkan SDK's `bin` folder (for example `%VULKAN_SDK%\Bin\glslangValidator.exe`).

## Prerequisites (macOS)

- macOS 12 or newer (Apple Silicon or Intel) with Metal/Vulkan support.
- Xcode Command Line Tools (`xcode-select --install`).
- CMake (>= 3.16). Install with Homebrew: `brew install cmake`.
- Vulkan SDK for macOS (includes MoltenVK). Run the SDK's `setup-env.sh` or export `VULKAN_SDK` so headers and dylibs resolve.
- glslangValidator (install via `brew install glslang` or use the SDK build).
- Qt 6 and Lua: `brew install qt lua`. Packaged games carry their own copy, so players do not need Homebrew or the Vulkan SDK.
- Git (preinstalled with the command line tools).

Optional but useful:
- Ninja generator (`brew install ninja`) for faster local builds.
- PowerShell Core (`brew install powershell`) to reuse the existing setup scripts.

## Quick Start

### 1. Setup EnTT (First time only)

Run the setup script to download EnTT:

```powershell
.\setup_entt.ps1
```

This downloads EnTT v3.13.2 and extracts it to `extern/entt/`.

On macOS, run the script with PowerShell Core (`pwsh ./setup_entt.ps1`) or manually copy EnTT into `extern/entt/` if you prefer staying in a POSIX shell.

### 2. Build

#### Windows (PowerShell)

The repository uses an out-of-source CMake build. From a PowerShell prompt run:

```powershell
# from repo root
git clone --recurse-submodules <repo-url>
mkdir build; cd build
cmake -S .. -B . -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

This will generate a Visual Studio solution in `build/` and build the projects in Release mode. You can also open `build/HephaestusEngine.sln` in Visual Studio and build there.

If you already have a `build/` folder from earlier runs, you can simply run:

```powershell
cmake --build build --config Release
```

#### macOS (Terminal)

Choose your preferred generator (Ninja recommended for command-line workflows, Xcode if you want an IDE project):

```bash
# from repo root
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

#### Platform profiles (CMake presets)

The repository uses one source tree for all supported desktop platforms. Use the
platform preset on that platform; no platform-specific branch is required:

```bash
cmake --preset macos-release    # macOS + Ninja
cmake --build --preset macos-release
cmake --preset linux-release    # Linux + Ninja
cmake --build --preset linux-release
```

On Windows, use a Developer PowerShell for Visual Studio:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

The presets expect the Vulkan SDK and Qt 6 to be installed on the selected
platform. Pass `-DQt6_DIR=...` to CMake when Qt is not in its standard location.

On macOS the preset builds `HephaestusEngine.app`, `HephaestusLauncher.app` and
`HephaestusGame.app` in `build/macos-release/Release/`, plus the tests and engine
plugins (no `build.bat` step is needed). In the build tree the apps read assets,
shaders and plugins from the folder beside them, like the Windows build. Packaging a
project (from the editor, or `Release/tools/HephaestusBuild --project <path>`) produces
`<Product>.app` with the game's content in `Contents/Resources` and Lua, the Vulkan
loader and MoltenVK in `Contents/Frameworks`; it is ad-hoc signed, not notarized, and
writes its logs to `~/Library/Logs/<Product>`. MoltenVK has no ray queries, so
ray-traced shadows and probe GI fall back to shadow maps and baked probes.

To make a distributable editor, run `tools/create_release_package_macos.sh` after a
`macos-release` build. It writes `dist/macos/Hephaestus Engine.app` (which opens the
launcher, with the editor, Qt, plugins and the game player inside) and a `.dmg`. Without
options it is ad-hoc signed for local use. To ship it, sign with your Developer ID and
notarize with a profile saved once via `xcrun notarytool store-credentials`:

```bash
tools/create_release_package_macos.sh --sign "Developer ID Application: Your Name (TEAMID)" --notary-profile hephaestus-notary
```

If you generate an Xcode project instead, use `cmake -S . -B build -G "Xcode"` and build with `cmake --build build --config Release` or open `build/HephaestusEngine.xcodeproj`.

If you previously configured `build/`, rerun the single-line build command:

```bash
cmake --build build
```

> Note: When launching binaries outside the build tree you may need to export `DYLD_LIBRARY_PATH=$VULKAN_SDK/lib` (or source the Vulkan SDK `setup-env.sh`) so MoltenVK is discoverable.

## Compile shaders

Shaders live in the `shaders/` folder. You can compile the GLSL source to SPIR-V using `glslangValidator` (PowerShell):

```powershell
cd shaders
glslangValidator -V triangle.vert -o triangle.vert.spv
glslangValidator -V triangle.frag -o triangle.frag.spv
cd ..
```

If `glslangValidator` is not on your PATH, run it from the Vulkan SDK bin folder, for example:

```powershell
& "$env:VULKAN_SDK\Bin\glslangValidator.exe" -V shaders\triangle.vert -o shaders\triangle.vert.spv
```

The project includes precompiled SPIR-V files (`*.spv`) used by the sample application. Recompile shaders whenever you change GLSL sources.

## Run

After a successful build the main executable(s) will be in `build/` under the selected configuration (for example `build/Release/`). Run the executable directly or launch from Visual Studio.

Example (PowerShell):

```powershell
.
# If the binary is in build\Release
& .\build\Release\HephaestusEngine.exe
```

Paths and executable names may vary depending on generator and configuration.

## Project Layout

```
Hephaestus-Engine/
├── CMakeLists.txt           # Main build configuration
├── setup_entt.ps1           # EnTT setup script
├── engine/
│   ├── Core/                # Application entry and math
│   ├── Gfx/                 # Vulkan rendering abstraction
│   ├── Scene/               # ✨ ECS system (NEW!)
│   │   ├── Entity.h/cpp     # Entity wrapper
│   │   ├── Scene.h/cpp      # Scene manager
│   │   ├── Components.h     # All component definitions
│   │   └── SceneSerializer.cpp # Scene save/load
│   ├── UI/                  # ✨ Editor UI panels
│   │   └── MaterialEditor/  # Node-based material graph
│   ├── Assets/              # Asset loading (models, textures)
│   ├── Physics/             # Physics hooks
│   └── Platform/            # Platform abstraction
├── editor/                  # Editor widgets
├── extern/
│   ├── entt/                # ✨ EnTT ECS library (NEW!)
│   ├── glfw/                # Window & input
│   ├── imgui/               # UI library
│   ├── ImGuizmo/            # Transform gizmos
│   ├── imnodes/             # ✨ Node graph editor (NEW!)
│   ├── stb_image.h          # Image loading
│   └── VMA/                 # Vulkan Memory Allocator
├── shaders/                 # GLSL shaders and SPIR-V
├── build/                   # CMake build output
└── Documentation:
    ├── ECS_GUIDE.md                    # ✨ Comprehensive ECS guide
    ├── ECS_IMPLEMENTATION_SUMMARY.md   # ✨ What was added
    ├── CUSTOM_COMPONENTS_GUIDE.md      # ✨ How to extend
    ├── LIGHTING_AND_MATERIALS_GUIDE.md # ✨ PBR material system
    ├── TEXTURE_RENDERING_GUIDE.md      # ✨ Texture integration
    ├── RAY_PICKING.md                  # Ray picking documentation
    └── IMGUIZMO_INTEGRATION.md         # ImGuizmo docs
```

## Documentation

### Markdown Reference

| File | Summary |
| --- | --- |
| [`README.md`](README.md) | Project overview, build instructions, controls, and feature tour. |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Contribution workflow, coding standards, and pull request checklist. |
| [`ROADMAP.md`](ROADMAP.md) | Long-term roadmap, milestones, and active developer to-do list. |
| [`Documentation/Guides/CUSTOM_COMPONENTS_GUIDE.md`](Documentation/Guides/CUSTOM_COMPONENTS_GUIDE.md) | Step-by-step tutorial for extending the ECS with custom components and systems. |
| [`Documentation/Architecture/ECS_ARCHITECTURE_DIAGRAM.md`](Documentation/Architecture/ECS_ARCHITECTURE_DIAGRAM.md) | ASCII architecture diagram showing scene, entity, component, and system relationships. |
| [`Documentation/Guides/ECS_GUIDE.md`](Documentation/Guides/ECS_GUIDE.md) | Comprehensive deep dive into the ECS API, scene management, and usage patterns. |
| [`Documentation/Architecture/ECS_IMPLEMENTATION_SUMMARY.md`](Documentation/Architecture/ECS_IMPLEMENTATION_SUMMARY.md) | Summary of files, features, and integration work added during the ECS migration. |
| [`Documentation/Architecture/ECS_SUCCESS.md`](Documentation/Architecture/ECS_SUCCESS.md) | Release-style recap of ECS deliverables, editor features, and setup steps. |
| [`Documentation/Archive/ICON_INTEGRATION.md`](Documentation/Archive/ICON_INTEGRATION.md) | Reserved for icon pipeline documentation (currently a placeholder). |
| [`Documentation/Archive/IMGUIZMO_INTEGRATION.md`](Documentation/Archive/IMGUIZMO_INTEGRATION.md) | Notes on integrating ImGuizmo with the editor, including math helpers and UI wiring. |
| [`Documentation/Guides/LIGHTING_AND_MATERIALS_GUIDE.md`](Documentation/Guides/LIGHTING_AND_MATERIALS_GUIDE.md) | Overview of the PBR material system, light types, and usage examples. |
| [`Documentation/Lighting/LIGHTING_ADVANCED_EXAMPLES.md`](Documentation/Lighting/LIGHTING_ADVANCED_EXAMPLES.md) | Expanded code examples for advanced materials, presets, and lighting scenarios. |
| [`Documentation/Lighting/LIGHTING_VISUAL_GUIDE.md`](Documentation/Lighting/LIGHTING_VISUAL_GUIDE.md) | Visual reference describing demo scene lighting setups and material appearances. |
| [`Documentation/Archive/TEXTURE_DROPDOWN_FEATURE.md`](Documentation/Archive/TEXTURE_DROPDOWN_FEATURE.md) | Implementation details for the texture selection UI in the material editor. |
| [`Documentation/Archive/RAY_PICKING.md`](Documentation/Archive/RAY_PICKING.md) | Detailed explanation of the ray picking implementation and input handling. |
| [`Documentation/Archive/RAY_PICKING_REFERENCE.md`](Documentation/Archive/RAY_PICKING_REFERENCE.md) | Quick reference for ray picking code changes and helper APIs. |
| [`Documentation/Roadmaps/ROADMAP_1-0.md`](Documentation/Roadmaps/ROADMAP_1-0.md) | 1.0 release roadmap, project status checklist, and acceptance criteria. |
| [`Documentation/Roadmaps/ROADMAP_1-0_issues_plan.md`](Documentation/Roadmaps/ROADMAP_1-0_issues_plan.md) | Suggested 10-week issues plan and schedule for a single developer. |

Third-party dependencies include their own documentation inside `extern/`; refer to those folders when working with vendor libraries.

Issue drafts & helper script
----------------------------
We've added a set of draft GitHub issues and a helper script to make converting roadmap items into issues easy. The drafts live under `.github/ISSUE_DRAFTS/` and the script to create issues using the GitHub CLI is at `tools/create_github_issues.ps1`.

To create issues locally (requires GitHub CLI `gh` and repo permissions):

```powershell
gh auth login
.\tools\create_github_issues.ps1
```

The script reads each draft markdown file (skipping `README.md`) and creates an issue with the first markdown heading as the title and the file body as the issue description.

### New ECS System Documentation

- **[ECS_GUIDE.md](Documentation/ECS_GUIDE.md)** - Complete guide to the Entity-Component System
- **[ECS_IMPLEMENTATION_SUMMARY.md](Documentation/ECS_IMPLEMENTATION_SUMMARY.md)** - Implementation details and migration guide
- **[CUSTOM_COMPONENTS_GUIDE.md](Documentation/CUSTOM_COMPONENTS_GUIDE.md)** - Examples for creating custom components

### Usage Examples

#### Creating Entities
```cpp
Scene scene("My Scene");

Entity cube = scene.CreateEntity("Cube");
cube.AddComponent<TransformComponent>(Vec3(0, 0, 0));
cube.AddComponent<MeshRendererComponent>();
```

#### Querying Entities
```cpp
auto view = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent>();
for (auto handle : view) {
    Entity entity(handle, &scene);
    auto& transform = entity.GetComponent<TransformComponent>();
    // Process entity...
}
```

#### Hierarchical Transforms
```cpp
Entity parent = scene.CreateEntity("Parent");
Entity child = scene.CreateEntity("Child");
scene.SetParent(child, parent);
```

#### Creating Materials
```cpp
// Simple material with colors
Entity cube = scene.CreateEntity("Cube");
auto& material = cube.AddComponent<MaterialComponent>();
material.baseColor = {1.0f, 0.0f, 0.0f}; // Red
material.metallic = 0.0f;
material.roughness = 0.5f;

// Material with textures
material.useAlbedoTexture = true;
material.albedoTexturePath = "assets/textures/brick_albedo.png";
material.useNormalTexture = true;
material.normalTexturePath = "assets/textures/brick_normal.png";
material.useRoughnessTexture = true;
material.roughnessTexturePath = "assets/textures/brick_roughness.png";
```

#### Using the Material Graph Editor
1. Open the Material Editor panel
2. Right-click in the graph canvas
3. Add texture nodes: **Textures** → **Albedo/Normal/Roughness/AO Texture**
4. Select a texture node and choose a texture from the dropdown
5. Connect texture outputs to the **Material Output** node inputs
6. Click **Apply To Selected Entity** to assign the material

See the guides above for complete examples and best practices.

## Controls

- **Right Mouse Button** - Hold to look around and move (Unreal-style)
- **WASD** - Move camera (when RMB held)
- **Q/E** - Move up/down (when RMB held)
- **Mouse Wheel** - Adjust camera speed
- **Left Click** - Select entity in viewport
- **T** - Translate gizmo mode
- **R** - Rotate gizmo mode
- **Y** - Scale gizmo mode

## Notes & Troubleshooting

- **Vulkan errors**: Ensure the Vulkan SDK is installed and correct GPU drivers are present
- **Missing `glslangValidator`**: Install Vulkan SDK or add the binary to PATH
- **EnTT not found**: Run `.\setup_entt.ps1` to download EnTT
- **CMake generator mismatch**: Specify correct generator (`-G "Visual Studio 17 2022" -A x64`)
- **Build errors from external libraries**: Verify `extern/` folder is complete

## Dependencies

- **Vulkan SDK** - Graphics API
- **GLFW** - Window/input management
- **ImGui** - Editor UI
- **ImGuizmo** - Transform gizmos
- **ImNodes** - Node-based graph editor for materials
- **EnTT** - Entity-Component-System (header-only)
- **stb_image** - Image loading for textures
- **VMA** - Vulkan Memory Allocator

## Next Steps & Roadmap

### Recently Completed ✅
- [x] Entity-Component-System (EnTT)
- [x] Scene serialization (Save/Load)
- [x] Material system with PBR workflow
- [x] Texture support (Albedo, Normal, Roughness, AO)
- [x] Node-based material graph editor
- [x] Material asset save/load system

### Immediate
- [ ] Add more component types (RigidBody, Light)
- [ ] Implement system patterns (Physics, Animation)
- [ ] Enhanced asset browser with thumbnails
- [ ] Material library and presets

### Future
- [ ] Advanced lighting (Point, Spot, Directional lights)
- [ ] Shadow mapping
- [ ] Post-processing effects
- [ ] Scripting integration (Lua/C#)
- [ ] Multi-threading support
- [ ] GPU profiling tools

## Contributing

Contributions welcome! This is a learning project demonstrating modern engine architecture patterns. Review [`CONTRIBUTING.md`](CONTRIBUTING.md) for setup steps, coding guidelines, and the pull request checklist.
- Small improvements: documentation edits, issue templates, and automated shader checks are great starting points.

## References

- Vulkan SDK: https://vulkan.lunarg.com/
- glslangValidator (part of the Vulkan SDK)
- CMake: https://cmake.org/

---

