
# Hephaestus Engine

Lightweight learning renderer and engine project. This repository contains a small Vulkan-based rendering engine, editor scaffolding, ImGui integration, tools and example shaders used for experimentation and learning.

## Overview

This project demonstrates a minimal engine architecture with Vulkan rendering, ImGui-based UI, shader pipeline examples, and basic editor tooling. It is intended as a learning and experimentation codebase rather than a production engine.

Key parts:
- `engine/` — core engine code (graphics, scene, platforms, physics, scripting hooks).
- `editor/` — editor application and widgets.
- `extern/` — third-party libraries (glfw, imgui, ImGuizmo, glslang, etc.).
- `shaders/` — GLSL shader sources and precompiled SPIR-V (*.spv) outputs.
- `build/` — CMake/Visual Studio generated build files (ignored in clean repo). 

## Prerequisites (Windows)

- Visual Studio 2019 or 2022 with Desktop development for C++ workload.
- CMake (>= 3.16 recommended).
- Vulkan SDK (for headers, loader and validation tools). Set `VULKAN_SDK` environment variable via the SDK installer.
- glslangValidator (usually included with the Vulkan SDK) to compile GLSL to SPIR-V.
- Git (to clone repo and submodules if applicable).

Optional but useful:
- Python for small helper scripts.

If you don't have `glslangValidator` available on PATH, use the Vulkan SDK's `bin` folder (for example `%VULKAN_SDK%\Bin\glslangValidator.exe`).

## Build (PowerShell)

The repository uses an out-of-source CMake build. From a PowerShell prompt run:

```powershell
# from repo root
mkdir build; cd build
cmake -S .. -B . -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

This will generate a Visual Studio solution in `build/` and build the projects in Release mode. You can also open `build/HephaestusEngine.sln` in Visual Studio and build there.

If you already have a `build/` folder from earlier runs (this repo includes a sample `build/` tree), you can simply run the second command to compile:

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

## Project layout

- `CMakeLists.txt` — top-level CMake build script.
- `engine/` — engine subsystems (Gfx, Physics, Scene, Platform, Scripting, UI).
- `editor/` — editor app and widget code.
- `extern/` — third-party dependencies and libraries (GLFW, ImGui, ImGuizmo, VMA, etc.).
- `shaders/` — GLSL shaders and SPIR-V compiled outputs.
- `tools/` — small utilities and scripts.

## Notes & Troubleshooting

- Vulkan errors: ensure the Vulkan SDK is installed and the correct GPU drivers are present.
- Missing `glslangValidator`: install Vulkan SDK or add the binary to your PATH.
- CMake generator mismatch: if you use a different Visual Studio version specify the correct generator (`-G "Visual Studio 17 2022" -A x64`) or set the CMake GUI accordingly.
- If you hit build errors from external libraries, verify submodule contents or the `extern/` folder is complete.

## Contributing and Next Steps

- This repository currently doesn't include a license file — add `LICENSE` if you plan to make it public.
- Consider adding a CI pipeline to build on push (Windows) and run a smoke test.
- Small improvements: CONTRIBUTING.md, issue templates, and automated shader checks.

## References

- Vulkan SDK: https://vulkan.lunarg.com/
- glslangValidator (part of the Vulkan SDK)
- CMake: https://cmake.org/

---

If you'd like, I can also add a short CONTRIBUTING guide, a license file, or example run configurations for VS Code / tasks. What would you like next?
