# Roadmap — Evidence-Based Phases

This roadmap is derived from the audit evidence (see `SYSTEM_AUDIT.md`, `FEATURE_GAPS.md`, `TECHNICAL_DEBT.md`). Ordering follows the prioritisation rules: broken core architecture and data-loss risks first, then packaging/portability blockers, missing production-critical systems, incomplete core systems, workflow/tooling, performance foundations, and finally advanced features. Rendering features are deliberately not placed ahead of production blockers.

Ownership labels: **Core** = Core Engine · **Module** = Built-in Engine Module · **Plugin** = Official Plugin · **Template** = Example/Template · **Game** = Game-specific.

---

## Phase 1 — Production Blockers (small complete game)

Goal: a developer can build, package, and ship a small complete game.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 1.1 | **Runtime UI renderer** — Vulkan 2D pass consuming HUI draw commands in `HephaestusGame`; text atlas + glyph rasterization; image/texture binding; keep `.hui`, widget tree, and Lua API unchanged | Core | Unblocks menus/HUD/dialogue in shipped builds (gap C1). The largest single production unlock. |
| 1.2 | **Physics foundations** — collision layers/masks; contact + sensor (trigger) events surfaced to Lua (`onTriggerEnter/Stay/Exit`); true kinematic bodies; fix parent-child world-transform sync | Core | Gaps C3/C4/C5. Prereq for doors, elevators, platforms, NPCs, projectiles-as-physics, and the documented trigger APIs. |
| 1.3 | **Save/load API** — minimal runtime persistence: Lua `Save.write/read` (JSON) + scene/level-load primitive, keyed by stable identity; save slots with atomic writes (reuse `RecoveryStore` patterns) | Core | Gap C2. Depends on 2.1 (stable identity) — do together. |
| 1.4 | **Stable entity identity + real prefab GUIDs** — `StableIdComponent`, GUID-based prefab lookup, fix nested-prefab parenting | Core | Debt #1; enables 1.3 and safe level transitions. |
| 1.5 | **Robust character controller** — replace the velocity-motor with a Jolt `CharacterVirtual`/collide-and-slide controller (step height, slope limits); keep the Lua API surface | Core | Gap H6. Required for reliable NPC locomotion and FPS feel. |
| 1.6 | **Portable audio backend** (e.g. miniaudio) — 3D positional + listener + mixer/buses + master volume; wav/ogg; replace WinMM/MCI | Core | Gaps H1. Non-Windows silent today. |
| 1.7 | **Reproduce/fix the Jolt settling crash** + add a physics simulation test; adopt fixed-timestep stepping | Core | Debt #8/#9. Do before relying on physics for gameplay. |

Milestone check: "walking-sim or single-arena FPS" vertical slice playable and shippable on Windows.

---

## Phase 2 — Core Engine Maturity

Goal: strengthen existing systems and workflows; remove silent-failure risks.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 2.1 | Renderer maturity — MSAA, true alpha blending + transparent sorting, `VkPipelineCache`, frustum culling, wire shadow-quality/HDR/exposure settings | Core | Gaps H4/H5; debt #15/#24. |
| 2.2 | Unify editor/runtime hosts — single mesh-upload path, shared shader loader, CSM computation once; extract a shared frame host for `QtEditorEngine`/`GameMain` | Core | Debt #3/#4. |
| 2.3 | `.hpscene` version validation + migration; unify `Serialize()`/`SerializeToString()`; stop serializing editor state into levels | Core | Debt #11/#12. |
| 2.4 | Wire `FileWatcher` auto-reimport (opt-in) and fix docs; make asset-database/DDC read path real | Module | Debt #16/#17 (partial). |
| 2.5 | Release packaging hardening — rewrite absolute-in-project paths to portable keys; referenced-asset packaging instead of full-directory copy (starts here, completes Phase 3) | Core | Debt #18. |
| 2.6 | Fix the portability test script; add GitHub Actions CI (build + ctest + portability script) | Module | Debt #19/#21. |
| 2.7 | Remove dead code (`MeshCollisionSystem` decision, `RelationshipComponent`, `#if 0` build path, inert `ScriptComponent`) and hide/implement lying inspector fields | Core/Module | Debt #7/#25. |
| 2.8 | Lua sandboxing decision + error backoff + mtime caching | Module | Debt #13/#14. |
| 2.9 | Documentation correctness pass per `TEST_AND_BUILD_REPORT.md` | — | Debt #26. |

---

## Phase 3 — Gameplay Production Features

Goal: increasingly complex games.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 3.1 | Scene/level transitions — runtime scene registry, additive/swap loading, spawn-point + trigger components authored in the editor | Core/Module | Depends on 1.2/1.4. |
| 3.2 | Navigation — runtime navmesh/pathfinding (bake + debug draw first), steering helpers; keep AI behaviour game-side | Module | Gap M1. |
| 3.3 | Animation state machines / animation graphs, layering, additive, root motion; then IK and retargeting | Module | Gap M3. |
| 3.4 | Compiled runtime mesh format + DDC as runtime loader; referenced-asset packaging completes | Core | Debt #17. |
| 3.5 | Gamepad: editor Play support, multiple controllers, input contexts, runtime remapping UI | Core | Gap H7. |
| 3.6 | Generic Lua component access; runtime material/light creation from Lua | Module | Gap M11. |
| 3.7 | Restore or replace the starter content (FPS template with scene, prefabs, scripts, HUD) as a Template | Template | The changelog-advertised starter is absent today. |

---

## Phase 4 — Editor and Tooling

Goal: authoring speed and usability.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 4.1 | Real scene autosave; undo for asset-browser ops | Module | Gap H9. |
| 4.2 | Editor log console dock; editor minidumps; keyboard-shortcut reference | Module | Gap M6. |
| 4.3 | Decompose `EditorMainWindow`; extract shared editor-engine base for document editors | Module | Debt #5/#4. |
| 4.4 | Template gallery + project templates; enable launcher Duplicate | Template | Gap M13. |
| 4.5 | Runtime material shader authoring — make generated GLSL a real, layout-compatible runtime path | Core | Gap H10/H11. |
| 4.6 | Editor world-space TRS, universal `enabled`, per-viewport settings | Module | Gap L5. |

---

## Phase 5 — Rendering and Performance

Goal: capability, scalability, profiling. Do not start until Phase 1/2 foundations are in.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 5.1 | Particles/VFX, decals, fog | Module | Gap M2. |
| 5.2 | Point/spot shadows, SSAO, screen-space reflections, bloom, post stack | Module | Gap M4. |
| 5.3 | Instancing + GPU-driven batching; LOD; GPU profiling; frame timing overlay | Core | Depends on culling (2.1). |
| 5.4 | World streaming / large-level partitioning; render-distance culling actually wired | Core | Gap M9. |

---

## Phase 6 — Advanced Engine Features

Goal: long-term, deliberately deferred.

| # | Work | Ownership | Notes |
|---|---|---|---|
| 6.1 | Global illumination (GI/Lumen-style), virtualized geometry (Nanite-style), temporal upscaling, GPU-driven rendering | Core | Only after renderer foundations mature (Phase 5). |
| 6.2 | Animation retargeting, look-at, full IK solvers | Module | Gap M3 tail. |
| 6.3 | Networking / multiplayer | Module | Requires platform/gamepad abstraction first. |
| 6.4 | Console platforms | Module | Out of scope until Windows/macOS/Linux shipping is proven. |
| 6.5 | Plugin API 2.0 — stable ABI story, dependency resolution, crash isolation, selection-aware context actions, typed viewport events | Plugin | Gaps L1; also fix settings owner-scoping (debt #20). |
| 6.6 | Localization/i18n | Module | Gap L6. |

---

## What should deliberately NOT be built yet

- GI / virtualized geometry / temporal upscaling (6.1)
- Networking, consoles, C# scripting
- A general gameplay framework (damage/inventory/weapons/teams/game modes) — **Game** ownership on top of engine primitives; do not expand `Components.h`
- Terrain/heightmaps before world streaming
- Full animation graphs before the runtime UI and physics foundations
- Aggressive renderer rewrites (deferred/clustered) before culling/instancing foundations