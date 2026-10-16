# Native plugins

Before placing functionality in a plugin, decide whether it is genuinely optional, independently deployable, or an editor extension. Fundamental engine behavior belongs in its responsible core subsystem; do not force it into a plugin, invent a plugin boundary, or duplicate a core capability.

- Before changing the native contract, inspect `engine/PluginAPI/PluginAPI.h`, `docs/plugins/plugin-api.md`, and a nearby first-party plugin. Preserve compatibility where possible; justify breaking API changes and update migration guidance and tests.
- Use supplied capability services and registration flows. Avoid editor singletons or internal renderer, ECS, physics, and allocator objects when no public service exposes them. Add a narrow service capability only when the feature requires it and the boundary is reusable.
- Respect manifest/version checks, `OnLoad` failure behavior, callback/user-data lifetime, descriptor copying, host-created Qt panel ownership, reverse load/unload order, and failure isolation.
- Native editor plugins are not a stable cross-toolchain ABI. The current Qt panel boundary requires compatible compiler family, architecture, Qt major, and C++ runtime. Avoid exposing STL types across public descriptors.
- Separate plugin binaries from plugin content mounts. Current game packaging stages enabled plugin content; it does not imply packaged games load editor plugin DLLs. Inspect build, package, and runtime-loader code before changing or describing this behavior.
- When plugin behavior crosses into core services, scenes, assets, build staging, or runtime hosts, also read `.agents/architecture.md` and `.agents/testing.md` and update affected consumers.
