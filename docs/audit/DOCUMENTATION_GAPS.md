# Documentation gaps

Audit of **first-party docs vs implementation** (2026-09-02, `72a4dd7`). This file records discrepancies. Docs were **not** rewritten in this task.

---

## Accurate (trust these)

| Document | Why |
|----------|-----|
| `CHANGELOG.md` `[Unreleased]` 2026-08-28 … 2026-09-02 | Matches prefab, UX phases, Stage 6, Lua spawn APIs |
| `docs/editor/UX_QOL_AUDIT.md` | Honest about what Phase 2–4 did and deferred |
| `Documentation/Guides/GameExportGuide.md` | Stage 6 layout, `HephaestusGame`, notices |
| `Documentation/Guides/HeadlessBuilds.md` | CLI packaging |
| `Documentation/Guides/MATERIAL_EDITOR_COMPATIBILITY.md` | Legacy vs graph `.hematerial` |
| `Documentation/Features/HUI_FORMAT_REFERENCE.md` | Widget set roughly matches `HUIWidgetType` |
| `engine/Audio/AudioEngine.h` comments | WinMM vs stubs — more honest than README |

---

## Incomplete (true but missing the sharp edges)

| Topic | Gap |
|-------|-----|
| Material graph | Docs describe a node editor; they do **not** say Compile **bakes to `MaterialComponent`** and GPU stays `triangle.frag` |
| Physics | Jolt mentioned; not that layers/debug draw/cylinder/world-sync are incomplete |
| Lua variables | Spawn `vars` documented; Inspector cannot edit the map |
| FBX | “FBX import” without **ASCII-only** |
| Audio | WAV/MP3 vs importer ogg |
| Prefabs | Strong docs in changelog; nested instances/variants not called out as absent |

---

## Outdated / old architecture

| Document | Problem |
|----------|---------|
| **Root `README.md`** | Describes **ImGui editor**, ImGuizmo, ImNodes, `extern/imgui`. Product is **Qt**. Export described as hiding editor UI inside `HephaestusEngine.exe`; product is **generic `HephaestusGame`**. Feature list omits Stage 6, prefab overrides, HUI. |
| `ROADMAP.md` | ImNodes material editor; clustered lighting; C#; GPU particles; **save/load marked complete Nov 2025** (authoring serialize ≠ game saves); nightly CI not present |
| `Documentation/Roadmaps/ROADMAP_1-0.md` | Mentions Diligent renderer; UI folder `editor/`; 8–12 week 1.0 — historical |
| `Documentation/README.md` | Reorg notes; Reference/Tutorials empty |
| `Documentation/Architecture/ECS_*` | May predate prefab instance model |
| `engine/Scripting/README.md` | Mix of accurate API and stale “future UI bindings” / hot-reload confusion |
| `Documentation/Scripting/LUA_SCRIPTING_GUIDE.md` | Roadmap still lists Input/Scene as future; they exist. Hot-reload “Planned” vs mtime reload in code. Triggers documented as unimplemented (correct). |
| Comments in `EditorContext.h`, `PlayModeManager.h`, `AssetManager.h`, `GameMain.cpp` | “Legacy ImGui editor” |
| `Documentation/Integration/IMGUIZMO_INTEGRATION.md` | Gizmos are Vulkan `VulkanGizmo`, not ImGuizmo in the Qt path |
| `Documentation/Archive/C++_scripting_legacy/**` | Correctly archived, but live `ScriptEngine` still runs |
| `Documentation/Features/EXPORT_PACKAGING.md` | Changelog: marked historical vs BuildService |

---

## Claims features not actually usable

| Claim | Reality |
|-------|---------|
| ImGui-based editor | Qt Widgets |
| Clustered / tile-based light culling | Forward UBO 16+16 lights |
| Completed runtime save/load (`ROADMAP.md`) | No game save API |
| Material custom shaders / generated GPU shaders | Bake + diagnostics GLSL |
| C++ scripting removed | `ScriptEngine` still constructed |
| Binary FBX | Rejected by importer |
| Collision debug draw | Checkbox only |
| Render distance / HDRI yaw / exposure | Serialized, not shaded |
| Auto-reimport / FileWatcher | Off by default |
| Boomer-Shooter in-repo starter | Changelog only; **not in this tree** |

---

## Missing docs for major **existing** features

- Prefab instance workflow (apply/revert/unpack) — changelog-quality, no designer guide
- Stage 6 vs old export (partially in GameExportGuide)
- Lua `display.ui` / `destroy.ui`
- CharacterMotor / `Physics.move`
- AssetPathPolicy / safe asset operations
- Lit/Unlit/Wireframe and ortho views
- This audit set (`docs/audit/`) — new

---

## Suggested documentation pass (later, not this task)

1. Rewrite root README for Qt + `HephaestusGame` + Windows-first + honest feature list.
2. Retire or banner `ROADMAP.md` as vision, not status.
3. Stamp Lua guide with “verified against LuaScriptEngine”.
4. Material Editor: “Bake to PBR”.
5. Delete or redirect ImGui/Diligent/C++ scripting live links to Archive.
