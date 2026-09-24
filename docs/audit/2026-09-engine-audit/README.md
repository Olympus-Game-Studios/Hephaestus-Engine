# Hephaestus Engine — Full State Audit (2026-09)

**Date:** 2026-09-24
**Repository revision:** `26e3326d19c993b18e18c170ad9c98a27f01d8de` (`26e3326`)
**Branch:** `main`
**Version stamp in CMake:** `2026.9.10`
**Scope:** Read-only audit. No engine behaviour was changed. Builds and tests were run; the editor and a packaged game were launched to verify runtime behaviour.

This audit supersedes the previous capability audit in `docs/audit/` (dated 2026-09-02, revision `72a4dd7`). The older audit is retained as historical evidence. Where the old audit's claims contradict current code, the current code wins; discrepancies are called out throughout.

---

## Current State

Hephaestus is best described today as an **early-production, Windows-first, Lua-scripted 3D game engine with a Qt editor and a generic packaged player (`HephaestusGame`)**. It is substantially ahead of the "experimental renderer" stage, but it is not yet a mature general-purpose engine and cannot yet ship a complete game of meaningful scope.

A small, **static-mesh or single-character Windows 3D game** can plausibly be made with it today: the editor can author scenes, materials, prefabs, characters and UI; the runtime can load a scene, run Lua gameplay, simulate physics, play animations, render PBR + shadows + HDRI, and package itself into a portable build that runs on another machine (verified end-to-end during this audit).

The systems that are genuinely strong and can be relied on:

1. **Qt editor product** — real Vulkan viewport, gizmos/snapping/ortho/shading modes, mature outliner + inspector (multi-select, batch edits, clipboard, undo/redo), asset browser with transactional rename/trash and reference rewriting, prefab authoring with overrides, build/package window, crash recovery. This is the engine's best surface.
2. **Prefab system** — property-path override tracking, apply/revert/unpack/refresh, missing-asset stubs, Create From Selection, dedicated Prefab Editor, strong tests.
3. **Skeletal animation & character stack** (new since the last audit) — `.hpcharacter` asset, GLB + FBX (ufbx) skeletal import, GPU skinning (128 bones, 4 influences), multi-mesh characters, clip playback with crossfade, sockets, Character Editor with timeline/skeleton overlay, two-slot Lua controllers (gameplay + character). Well-tested logic layer.
4. **Packaging / portability** — `HephaestusBuild` CLI + editor Build window, project validation with actionable diagnostics, transactional staging, portable keys (`assets/`, `.engine/`, `scripts/`, `builtin://`), no engine-repo path leakage into packages. Verified: a packaged game runs from a relocated directory and never touches the engine source tree.
5. **Asset operations** — safe rename/move/trash with reference rewrite, rollback, character companion handling, recovery store.
6. **Lua gameplay** — broad API (scene spawn/destroy, physics raycast/motor, input actions, audio, HUI, animator), static `Expose.*` variable scanning with inspector editing and prefab override integration, hot reload, per-script error isolation.

The systems most likely to block real production:

1. **Runtime UI does not render in packaged games.** The HUI widget tree, `.hui` format, Lua UI API, and editor-side QPainter overlay all work — but the packaged `HephaestusGame` collects draw commands and never consumes them (`GameMain.cpp:1609-1611`). There is no Vulkan UI pass. **Any shipped game has no HUD, menus, or dialogue UI.** This is the single most serious production gap.
2. **Audio is a Windows-only WinMM/MCI 2D player.** No 3D positioning, no mixer/buses, no streaming, no pitch, no tests, silent on macOS/Linux.
3. **Physics is functional but immature.** No collision layers/filtering, no joints/constraints, no contact/trigger events delivered to gameplay, no true kinematic bodies, cylinder shape silently no-ops, parented-body transform sync is broken, no debug visualization, no tests, and a documented Jolt crash during body settling. The `CharacterMotor` is a velocity-driven dynamic-body motor, not a robust collide-and-slide controller.
4. **No save-game system.** Only editor scene serialization and editor crash recovery exist. No runtime persistence API, no save slots, no Lua save/load.
5. **Entity identity is fragile.** Durable references are raw EnTT handles; prefab-instance roots get fresh IDs on load; nested-prefab parenting is lost across save/load; `prefabGuid` is decorative (lookup is path-based).
6. **No navigation/AI infrastructure** at engine level; no scene streaming/level transitions at runtime; no triggers delivered to scripts; no particles; no decals; no terrain.
7. **A "lying" settings surface.** Several editor settings (shadow quality, HDR, exposure, gizmo size) are stored but do not feed the renderer.

---

## Production Readiness

**What can be built with Hephaestus today:**

A small first-person or single-character 3D game on **Windows x64**, with:
- Lua-authored gameplay (movement, raycast interaction, spawning prefabs, timers, damage via `entity:Call`)
- Static meshes, PBR materials, directional CSM shadows, HDRI lighting
- Skeletal characters with clip playback + crossfade and two-slot Lua controllers (the Cultist slice demonstrates the pattern)
- Input via `.hpinput` action/axis maps (keyboard/mouse; gamepad in the packaged game only)
- A packaged, portable Windows build with validation and a CLI
- Editor-side HUI menus that work **in editor Play** only

**What cannot be built today:** any game that needs on-screen UI in the shipped build (menus, HUD, dialogue, inventory), positional/spatialized audio, trigger volumes that fire gameplay events, moving platforms or kinematic objects, multiple floors/NPC pathfinding, runtime save/load, level transitions, controller navigation in menus, or any cross-platform deployment (Linux/macOS runtimes are untested and audio is silent there).

**Someone other than the engine developer** would need to contend with: stale documentation (root README/ROADMAP describe an ImGui-era engine), no onboarding sample project in the tree (the changelog-advertised "Boomer-Shooter starter content" is absent), a 7,600-line `EditorMainWindow.cpp`, several editor settings that do nothing, no editor log console, no GUI inspector for runtime state, and the need to read source to learn the two-slot Lua model.

---

## Major Missing Capabilities (summary)

| Capability | Status | Production impact |
|---|---|---|
| Runtime UI rendering in packaged builds | **Missing** (editor-only today) | Blocks menus/HUD/dialogue/inventory in any shipped game |
| Game save/load | **Missing** | Blocks persistence for almost every game |
| Physics layers, triggers→Lua, joints, kinematic bodies | **Missing/Partial** | Blocks NPCs, doors, elevators, moving platforms, gameplay-relevant collisions |
| Audio 3D / mixer / cross-platform | **Missing/Prototype** | Blocks spatial audio; non-Windows silent |
| Scene/level transitions at runtime | **Missing** | Single-scene games only |
| Navigation/AI engine infrastructure | **Missing** | NPCs must be hacked in Lua |
| Particles, decals, fog | **Missing** | Limits visual polish |
| Stable entity IDs / GUIDs | **Missing** | Fragile persistence and prefab nesting |
| Runtime shader material graph | **Missing** (graph bakes to fixed PBR slots) | No custom GPU shaders for artists |
| Transparency (alpha blend), MSAA, post-processing | **Missing/Partial** | Limits material and image quality |

---

## Recommended Immediate Focus (top 5–10, dependency-aware)

See `ROADMAP.md` for the full phased plan. The immediate focus is finishing the production loop before adding features:

1. **Runtime UI backend** — a Vulkan compositor/2D pass that consumes the existing HUI command list in `HephaestusGame`, plus text rasterization and image rendering. This unblocks menus/HUD in shipped builds. (Phase 1)
2. **Physics foundation** — collision layers/masks, contact & sensor (trigger) events surfaced to Lua, true kinematic bodies, fix parent-child world-transform sync. These are prerequisites for doors/elevators/platforms/NPCs and unblock the trigger systems the Lua docs already advertise. (Phase 1)
3. **Runtime save/load API** (Lua `Save.*` + scene/level-load primitive) keyed by stable entity identity — addresses persistence and level transitions together. (Phase 1)
4. **Stable entity identity / prefab GUID resolution** — replace raw-handle references and make `prefabGuid` real, so nested prefabs, overrides in packaged builds, and saves survive reload. (Phase 2)
5. **Audio backend upgrade** — replace WinMM/MCI with a portable 3D backend (e.g. miniaudio) and add positional/listener/mixer support. (Phase 2)
6. **Renderer maturity** — MSAA, true alpha blending with sorting, pipeline cache, frustum culling, and wiring the "lying" settings (shadow quality, exposure). (Phase 2)
7. **Unify duplicated runtime/editor code paths** — single mesh-upload path, single shader-loader, shared render-loop host for editor/character/prefab editors. (Phase 2, debt)
8. **Editor hardening** — real scene autosave, undo for asset operations, FileWatcher auto-reimport, in-editor log console, decompose `EditorMainWindow`. (Phase 4)
9. **Navigation** (runtime navmesh/pathfinding + debug draw) only when a title needs it; a minimal engine trigger/event helper first. (Phase 3)
10. **Deliberately wait:** particles/VFX, terrain, SSR/SSAO/bloom, GPU-driven rendering, GI (Nanite/Lumen-style), networking, consoles, C#, and a full animation graph/IK/retargeting stack until the foundations above are solid.

---

## Longer-Term Direction

After the production-critical foundation is complete, the next horizon is engine maturity: animation state machines/IK/retargeting, world streaming and large levels, particles/decals/post, material shader authoring, a stable public PluginAPI (2.0), editor templates and onboarding content, and cross-platform verification. Advanced rendering (GI, virtualized geometry, GPU-driven rendering) and multiplayer remain firmly out of scope until then.

---

## Document Map

| Document | Contents |
|---|---|
| [ENGINE_STATE.md](ENGINE_STATE.md) | Repository/module inventory, architecture audit, engine/runtime/editor separation, engine completeness matrix |
| [SYSTEM_AUDIT.md](SYSTEM_AUDIT.md) | Per-system status classifications with evidence (rendering, scene/ECS/prefabs, animation, physics, input, scripting, editor, HUI, audio, assets/packaging, plugins, AI/nav, gameplay, save/load) |
| [FEATURE_GAPS.md](FEATURE_GAPS.md) | Missing functionality grouped Critical / High / Medium / Low |
| [TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) | Debt items with impact, risk, affected systems, recommended action, timing |
| [ROADMAP.md](ROADMAP.md) | Phased, evidence-based roadmap with ownership classification (Core / Module / Plugin / Template / Game) |
| [TEST_AND_BUILD_REPORT.md](TEST_AND_BUILD_REPORT.md) | Build status, test results, coverage table, documentation accuracy table |

---

## Final Answers to the Audit Questions

1. **What can Hephaestus do today?** Author and package a small Windows first-person/single-character 3D game with static + skinned meshes, PBR + CSM shadows + HDRI, Lua gameplay, character animation, prefabs, action-mapped keyboard/mouse input, 2D Windows audio, and editor-side UI. Packaging is portable and verified.
2. **What cannot it do?** Ship any on-screen UI, save/load, spatial audio, level transitions, gameplay-relevant triggers, kinematic/moving-platform physics, NPC navigation, particles/decals/fog, or run meaningfully on non-Windows platforms.
3. **What only partially works?** Physics (no layers/joints/triggers/kinematics; parenting broken), HUI (editor-only), audio (Windows 2D only), gamepad (packaged game only, single controller), input contexts/remapping (absent), crossfade-based animation (no state machines/layering/IK), material graph (bake-only, no custom shaders).
4. **Which systems are genuinely production-ready?** Prefab system; asset operations/reference rewriting; packaging/portability pipeline; `Expose.*` inspector variables; the `.hpcharacter` asset + skeletal import + playback logic layer; the Qt editor's core authoring workflows (outliner/inspector/gizmos/asset browser/undo).
5. **Which systems are prototypes disguised by functional demos?** The runtime UI system (impressive editor-side framework that never renders in the packaged game); the audio system (working demos, MCI backend, no depth); the `CharacterMotor`/FPS controller (feels fine in a demo, not a real character controller); `ScriptComponent` (C++ scripting — exists, is displayed in the inspector, but is never bound in play mode).
6. **What prevents a complete game today?** No runtime UI, no save system, no trigger events, no spatial audio, fragile physics for non-trivial scenarios, single-scene runtime.
7. **What prevents someone other than the engine developer from using it?** Stale documentation, absent sample project content, 7,600-line main window, no editor log console, lying settings, no onboarding materials, macOS/Linux untested.
8. **What architectural problems will become expensive later?** Raw-EnTT-handle entity identity; editor/runtime divergence (GameMain monolith vs QtEditorEngine vs per-editor windows); the 128-bone / 256-descriptor / 128-texture / 1024-material hard caps; per-frame stat syscalls and GC churn in Lua; physics step determinism and the documented Jolt crash; renderer without culling/instancing/pipeline cache; HUI with no runtime backend.
9. **What should be built next?** Runtime UI backend; physics foundations (layers/triggers/kinematics/parenting); save/load + stable identity; portable audio; renderer maturity (MSAA/transparency/culling/pipeline cache).
10. **What should deliberately NOT be built yet?** GI, virtualized geometry, GPU-driven rendering, temporal upscaling, terrain, VFX graphs, networking, consoles, C# scripting, full animation graphs/IK/retargeting, and a general gameplay framework (damage/inventory/weapons belong in game projects).
11. **Which features belong in the engine?** Rendering, physics, animation core, runtime UI compositor, audio, input, save/load primitives, scene streaming, navmesh runtime, stable entity identity.
12. **Which belong in plugins?** Terrain tools, atlas/tile tools, custom importers/asset editors, editor panels (already supported), custom asset types, rendering extensions (later).
13. **Which belong in individual games?** Health/damage/inventory/weapons/projectiles/teams/game modes/save schemas — these are game-specific and should use engine primitives (trigger events, save API, `entity:Call`).
14. **How close is Hephaestus to a production vertical slice?** One focused milestone away in UI (runtime HUI rendering) plus physics/save hardening. A genuinely complete small game additionally needs save/load, trigger events, and portable audio.
15. **What sequence of work gives the largest usability improvement without rewrites?** Finish the runtime UI backend and physics foundations first (they unblock the most gameplay), add save/load with stable identity, then a portable audio backend, then renderer maturity; tackle the structural debt (single frame-loop host, mesh-upload unification, editor decomposition) in parallel as refactors while shipping the features above.