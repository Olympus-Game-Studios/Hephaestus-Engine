
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
- `editor/` — Editor application and widgets
- `extern/` — Third-party libraries (glfw, imgui, ImGuizmo, EnTT, VMA)
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
- **SelectionComponent** - Editor selection state
- **CameraComponent** - Camera properties

### 🛠️ Editor Tools
- Scene hierarchy window
- Entity properties inspector
- ImGuizmo transform manipulation (Translate/Rotate/Scale)
- Ray-based object picking
- Real-time component editing 

## Prerequisites (Windows)

- Visual Studio 2019 or 2022 with Desktop development for C++ workload.
- CMake (>= 3.16 recommended).
- Vulkan SDK (for headers, loader and validation tools). Set `VULKAN_SDK` environment variable via the SDK installer.
- glslangValidator (usually included with the Vulkan SDK) to compile GLSL to SPIR-V.
- Git (to clone repo and submodules if applicable).

Optional but useful:
- Python for small helper scripts.

If you don't have `glslangValidator` available on PATH, use the Vulkan SDK's `bin` folder (for example `%VULKAN_SDK%\Bin\glslangValidator.exe`).

## Quick Start

### 1. Setup EnTT (First time only)

Run the setup script to download EnTT:

```powershell
.\setup_entt.ps1
```

This downloads EnTT v3.13.2 and extracts it to `extern/entt/`.

### 2. Build (PowerShell)

The repository uses an out-of-source CMake build. From a PowerShell prompt run:

```powershell
# from repo root
mkdir build; cd build
cmake -S .. -B . -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

This will generate a Visual Studio solution in `build/` and build the projects in Release mode. You can also open `build/HephaestusEngine.sln` in Visual Studio and build there.

If you already have a `build/` folder from earlier runs, you can simply run:

```powershell
cmake --build build --config Release
```

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
│   │   └── Transform.h      # Legacy transform (kept for reference)
│   ├── Physics/             # Physics hooks
│   └── Platform/            # Platform abstraction
├── editor/                  # Editor widgets
├── extern/
│   ├── entt/                # ✨ EnTT ECS library (NEW!)
│   ├── glfw/                # Window & input
│   ├── imgui/               # UI library
│   ├── ImGuizmo/            # Transform gizmos
│   └── VMA/                 # Vulkan Memory Allocator
├── shaders/                 # GLSL shaders and SPIR-V
├── build/                   # CMake build output
└── Documentation:
    ├── ECS_GUIDE.md                    # ✨ Comprehensive ECS guide
    ├── ECS_IMPLEMENTATION_SUMMARY.md   # ✨ What was added
    ├── CUSTOM_COMPONENTS_GUIDE.md      # ✨ How to extend
    ├── RAY_PICKING.md                  # Ray picking documentation
    └── IMGUIZMO_INTEGRATION.md         # ImGuizmo docs
```

## Documentation

### New ECS System Documentation

- **[ECS_GUIDE.md](ECS_GUIDE.md)** - Complete guide to the Entity-Component System
- **[ECS_IMPLEMENTATION_SUMMARY.md](ECS_IMPLEMENTATION_SUMMARY.md)** - Implementation details and migration guide
- **[CUSTOM_COMPONENTS_GUIDE.md](CUSTOM_COMPONENTS_GUIDE.md)** - Examples for creating custom components

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

See the guides above for complete examples and best practices.

## Controls

- **Right Mouse Button** - Hold to look around and move (Unreal-style)
- **WASD** - Move camera (when RMB held)
- **Q/E** - Move up/down (when RMB held)
- **Mouse Wheel** - Adjust camera speed
- **Left Click** - Select entity in viewport
- **T** - Translate gizmo mode
- **R** - Rotate gizmo mode
- **S** - Scale gizmo mode

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
- **EnTT** - Entity-Component-System (header-only)
- **VMA** - Vulkan Memory Allocator

## Next Steps & Roadmap

### Immediate
- [ ] Add more component types (RigidBody, Light, Camera)
- [ ] Implement system patterns (Physics, Animation)
- [ ] Scene serialization (Save/Load)

### Future
- [ ] Asset management system
- [ ] Material system
- [ ] Lighting & shadows
- [ ] Scripting integration (Lua/C#)
- [ ] Multi-threading support

## Contributing

Contributions welcome! This is a learning project demonstrating modern engine architecture patterns.
- Small improvements: CONTRIBUTING.md, issue templates, and automated shader checks.

## References

- Vulkan SDK: https://vulkan.lunarg.com/
- glslangValidator (part of the Vulkan SDK)
- CMake: https://cmake.org/

---

If you'd like, I can also add a short CONTRIBUTING guide, a license file, or example run configurations for VS Code / tasks. What would you like next?
