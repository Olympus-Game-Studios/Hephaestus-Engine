---
name: hephaestus-architecture
description: Load the authoritative Hephaestus subsystem and host-boundary rules. Use for engine architecture, shared services, build targets, rendering, assets, scenes, physics, scripting, automation, UI, or feature placement.
paths:
  - "engine/**"
  - "Plugins/**"
  - "CMakeLists.txt"
  - "CMakePresets.json"
  - "docs/plugins/**"
---

Read `.agents/architecture.md` before implementation. For `engine/Gfx` work, also read `.agents/rendering.md`; read `.agents/testing.md` for changes that cross targets or hosts. These shared rule files are mandatory; this skill is only a discovery shortcut and does not duplicate their content.
