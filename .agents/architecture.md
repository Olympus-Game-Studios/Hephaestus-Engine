# Hephaestus architecture

Inspect current source, CMake target definitions, interfaces, callers, and tests before relying on architecture maps or older documentation as evidence of behavior.

## Ownership and boundaries

- The C++20 engine is primarily under `engine/`: Core, Gfx, Scene, Assets, Physics, Animation, Scripting, Audio, Input, Build, Plugins, Automation, and UI. `Plugins/` contains first-party independently loadable editor extensions and their content. Tests are under `engine/Tests` and registered in root CMake.
- Preserve the distinction between `HephaestusEngine` (editor) and `HephaestusGame` (packaged runtime). Shared source does not prove identical behavior: inspect host entry points, compile definitions, target sources, and runtime bootstrap.
- Keep Qt out of Qt-free engine libraries and packaged runtime paths. Qt 6 is optional and used by editor/launcher widgets. `engine/UI/HephaestusUI` is the runtime UI with a Vulkan backend, not an editor widget toolkit.
- Keep rendering implementation in its owning renderer subsystem and check editor-only GPU resources and compile-time branches before changing shared or packaged paths.
- Preserve existing service ownership, useful subsystem boundaries, and public plugin/API compatibility. Avoid circular dependencies and duplicate editor/runtime/plugin implementations.
- Put fundamental capabilities in their responsible core subsystem. Use a plugin when behavior is genuinely optional, independently deployable, or an editor extension; do not force a core feature into a plugin or invent a boundary for its own sake.
- Reuse existing path, asset, serialization, scene, input, build, package, and service mechanisms rather than creating parallel resolution or lifecycle systems.

## Cross-subsystem changes

Cross-subsystem changes are allowed when required for a correct complete feature. Identify owners and dependencies, trace affected consumers and host variants, and integrate through existing service or host boundaries where useful. Update affected targets, callers, tests, and documentation. Do not ask permission merely because a justified change crosses subsystem boundaries.

When useful, also read `.agents/rendering.md`, `.agents/editor.md`, `.agents/runtime.md`, `.agents/plugins.md`, and `.agents/testing.md` for the affected behavior.
