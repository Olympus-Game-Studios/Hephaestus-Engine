---
name: hephaestus-verification
description: Load authoritative Hephaestus build, test, integration, lifecycle, and runtime verification rules. Use for implementation and bug fixes, especially CMake, assets, physics, scripting, UI, plugins, automation, or packaging.
paths:
  - "CMakeLists.txt"
  - "CMakePresets.json"
  - "engine/**"
  - "Plugins/**"
  - "tools/**"
---

Read `.agents/testing.md` when implementing or validating a change. Select the host and verification levels relevant to the behavior; do not claim more than the evidence establishes. The shared file is authoritative.
