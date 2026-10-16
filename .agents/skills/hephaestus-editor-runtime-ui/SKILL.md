---
name: hephaestus-editor-runtime-ui
description: Route Hephaestus UI changes to the authoritative Qt editor/launcher and runtime UI rules. Use for editor widgets, Qt integration, runtime UI, or UI work crossing hosts.
paths:
  - "engine/UI/QtEditor/**"
  - "engine/UI/Launcher/**"
  - "engine/UI/Shared/**"
  - "engine/UI/HephaestusUI/**"
  - "Plugins/**/*.cpp"
---

Read `.agents/editor.md` for Qt editor/launcher work and `.agents/runtime.md` for HephaestusUI or packaged runtime work. Read both when behavior crosses hosts; also consult `.agents/architecture.md` and `.agents/testing.md` as applicable. The shared rule files, not this adapter, are authoritative.
