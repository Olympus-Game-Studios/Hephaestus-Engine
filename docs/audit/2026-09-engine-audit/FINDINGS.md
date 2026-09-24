# Hephaestus Engine — Compiled Audit Findings

**Date:** 2026-09-24 · **Revision:** `26e3326` (main) · **Version stamp:** 2026.9.10
**Method:** Read-only audit. Builds, tests, the editor, and a packaged game were run to verify claims. See the full per-topic documents in this directory (`SYSTEM_AUDIT.md`, `FEATURE_GAPS.md`, `TECHNICAL_DEBT.md`, `ROADMAP.md`, `TEST_AND_BUILD_REPORT.md`, `ENGINE_STATE.md`).

> **Post-audit resolution (Stage 8A):** the **Runtime UI** gap below has been
> **resolved** — `HephaestusGame` now renders HUI through a Vulkan overlay backend
> (primitive geometry, text via an stb_truetype atlas, images, alpha, clipping,
> anchors, resize), verified by the relocated packaged-game portability gate and
> the headless `HUIRendererTests`. The historical finding is preserved below;
> see `Documentation/Features/HEPHAESTUS_UI_RUNTIME_GUIDE.md` for the current state.

---

## 1. What Hephaestus Is Today

An **early-production, Windows-first, Lua-scripted 3D game engine** with a Qt editor and a generic packaged player (`HephaestusGame`). It is well past "experimental renderer" but is not a mature general-purpose engine.

**Realistically buildable today:** a small Windows first-person/single-character 3D game with static + skinned meshes, PBR + directional CSM shadows + HDRI, Lua gameplay, character animation (clips/crossfade/two-slot controllers), prefabs, action-mapped keyboard/mouse input, 2D Windows audio, and editor-side UI. Packaging is portable and was verified end-to-end (a packaged game ran from a relocated directory, exit code 0, no engine-repo path leakage).

**Strongest systems (can be relied on):**
- Qt editor product — real Vulkan viewport, gizmos/snapping/ortho/shading, mature outliner + inspector (multi-select, batch edits, undo/redo), asset browser with transactional rename/trash and reference rewriting, build/package window, crash recovery.
- Prefab system — property-path override tracking, apply/revert/unpack/refresh, missing-asset stubs, dedicated Prefab Editor, strong tests.
- Skeletal animation & character stack — `.hpcharacter`, GLB + FBX (ufbx) import, GPU skinning (128 bones), multi-mesh characters, clip playback + gated crossfade, sockets, Character Editor, Lua controllers. Best-tested logic layer in the engine.
- Packaging / portability — `HephaestusBuild` CLI + editor Build window, project validation, transactional staging, portable path keys, no engine-absolute paths in packages.
- Asset operations — safe rename/move/trash with reference rewrite, rollback, character companion handling.
- Lua gameplay — broad API, `Expose.*` inspector variables, hot reload, per-script error isolation.

**Weakest systems (most likely to block production):**
- **Runtime UI**: the HUI framework (widgets, `.hui`, Lua API, editor QPainter overlay) never renders in packaged builds — `GameMain` collects draw commands and discards them (`GameMain.cpp:1609-1611`). No Vulkan UI pass, no text atlas, images are placeholder rects. **Shipped games have no menus/HUD/dialogue.**
- **Audio**: WinMM/MCI, Windows-only, 2D, no mixer/buses/streaming/pitch, no tests, silent on macOS/Linux, `.ogg` unsupported.
- **Physics**: no collision layers, no contact/trigger events, no joints, no true kinematic bodies, cylinder shape silently no-ops, parent-child transform sync broken, `debugDraw` flags are no-ops, no tests, and a documented Jolt crash during body settling. `CharacterMotor` is a velocity-driven motor, not a collide-and-slide controller.
- **Persistence**: no game save/load (editor scene serialization + crash recovery only).
- **Entity identity**: durable references are raw EnTT handles; prefab roots get fresh IDs on load; nested-prefab parenting is lost; `prefabGuid` is decorative.
- **No navigation/AI infrastructure**, no runtime level transitions, no trigger events to scripts, no particles/decals/fog/terrain.

---

## 2. System-by-System Status

| System | Status | Key evidence |
|---|---|---|
| Rendering (forward PBR + CSM + HDRI, shared editor/game) | **Functional** | `EngineFrameRenderer` shared by `QtEditorEngine` and `GameMain`; skinned passes for main/shadow/outline/ID |
| Materials | **Functional (bake-only)** | Material graph bakes to fixed PBR slots; generated GLSL never runs in the pipeline; `.hematerial` loader drops metallic textures (`EngineFrameRenderer.cpp:2851-2861`) |
| Scene/ECS/serialization | **Functional** | EnTT; `.hpscene` version never validated on load; `SerializeToString()` drops native ScriptComponent vs `Serialize()` |
| Prefabs | **Production Ready** | Overrides, apply/revert/unpack/refresh, missing stubs; nested prefabs unsupported |
| Skeletal animation / `.hpcharacter` | **Functional** | Strong logic tests; GPU & Qt-UI paths untested; no root motion, state machines, IK, retargeting |
| Physics (Jolt) | **Functional (simple use)** | Raycast is the strongest feature; layers/triggers/joints/kinematic missing; parenting broken |
| Input (`.hpinput`) | **Functional** | Solid actions/axes; gamepad disabled in editor Play; single controller; no contexts/remapping UI |
| Lua scripting | **Functional** | Broad API, `Expose.*` inspector, hot reload, isolation; unsandboxed; per-frame syscalls; `ScriptComponent` inert |
| Editor (Qt) | **Production Ready (core authoring)** | ~7,600-line `EditorMainWindow.cpp`; no log console; several "lying" settings; no asset-op undo; recovery not real autosave |
| Runtime UI (HUI) | **Partial — editor-only render** | Packaged game cannot display any UI |
| Audio | **Prototype** | WinMM/MCI 2D Windows-only |
| Asset pipeline | **Functional** | Runtime loads raw `.glb/.fbx/.png`; derived-data cache is write-only; auto-reimport disabled; packages copy entire `assets/` |
| Packaging / deployment | **Functional** | Windows x64 verified; absolute-in-project paths pass Release; portability test script broken as-written |
| Plugin System 1.0 | **Functional** | Versioned API, content mounting, example plugin; no crash isolation; no deps; settings lookup id-only; tests never load a real DLL |
| Gameplay framework | **Missing (game-specific Lua)** | Boomer-Shooter starter advertised in changelog is absent from the tree; only Cultist slice remains |
| Save/load | **Missing** | Editor serialization + crash recovery only |
| AI / navigation | **Missing** | Zero engine-level infra; `CultistAI.lua` is a straight-line timer AI |
| Debug tooling | **Stub/Partial** | No physics/nav viz, no editor console, no GPU profiling, no cheat commands |

**Prototypes disguised by functional demos:** the runtime UI framework (great editor-side, nothing in the shipped game); the audio system (demos work, no depth); the `CharacterMotor`/FPS controller (feels fine, not a real character controller); `ScriptComponent` (C++ scripting displayed in the inspector but never bound).

---

## 3. Production Readiness Assessment

- **What can be made today:** a small Windows first-person or single-arena 3D game with Lua gameplay, skeletal characters, prefabs, PBR/shadows/HDRI, packaged portably.
- **What cannot:** any shipped game with on-screen UI, save/load, spatial audio, trigger-driven gameplay, moving platforms/elevators/doors, NPC pathfinding, level transitions, controller menu navigation, or any non-Windows deployment (audio silent; runtimes unvalidated).
- **Non-developer usability blockers:** stale README/ROADMAP (ImGui era), absent sample project content, 7,600-line main window, no editor log console, lying settings, no onboarding materials.
- **Verdict:** one focused milestone (runtime UI backend) plus physics/save/audio hardening stands between Hephaestus and a production vertical slice.

---

## 4. Feature Gaps (by priority)

### Critical — blocks a complete game
1. **Runtime UI renderer** in packaged builds (Core).
2. **Runtime save/load** (Core).
3. **Trigger/contact events to gameplay** — sensors exist, no listener (Core).
4. **Collision layers/filtering** (Core).
5. **Kinematic bodies + parent-child physics sync** (Core).

### High — significantly restricts what can be built
- Portable 3D audio (Core) · Stable entity IDs / real prefab GUIDs (Core) · Runtime level/scene transitions (Core) · Alpha blending + sorting + MSAA (Core) · Frustum culling / instancing / LOD / pipeline cache (Core) · Robust character controller (Core) · Gamepad in editor + multi-controller + contexts + runtime remapping (Core) · Script sandboxing (Module) · Editor data-loss protections (Module) · Material shader authoring (Core) · `.hematerial` metallic texture fix (Core).

### Medium / Low
Navigation (Module), particles/decals/fog/post, animation graphs/IK/retargeting, point/spot shadows, editor QoL, nested prefabs, scene versioning, streaming, compiled runtime mesh format, templates/onboarding, plugin API 1.1 polish, HUI focus navigation, localization, and more — full list in `FEATURE_GAPS.md`.

### Deliberately NOT recommended yet
GI/Lumen-style, Nanite-style virtualized geometry, temporal upscaling, deferred/clustered lighting (before culling/instancing foundations), networking, consoles, C#, terrain (before streaming), a general gameplay framework (health/damage/inventory belong in game projects), full animation graphs (before runtime UI + physics foundations).

---

## 5. Top Technical Debt

1. **Raw EnTT handles as durable identity** — breaks saves/level transitions/nested prefabs. (High risk)
2. **HUI never rendered in packaged games** — whole framework is editor-only. (High)
3. **`GameMain.cpp` monolith** duplicates editor host logic; divergent mesh upload, duplicated shader loaders, CSM computed twice. (Medium-High)
4. **Per-editor viewport hosts duplicated** (Prefab/Character editors). (Medium)
5. **`EditorMainWindow.cpp` god-object** (~7,600 lines). (Medium)
6. **Physics parent-child sync broken** — blocks platforms/attached weapons. (High)
7. **`MeshCollisionSystem` dead code** + misleading inspector (Contacts/debugDraw). (Medium)
8. **Documented Jolt crash during body settling**, exceptions swallowed. (High)
9. **Variable-timestep, non-deterministic physics.** (Medium-High)
10. **Cylinder shape silent no-op.** (Medium)
11. **`.hpscene` version never validated.** (High over time)
12. **Serializer divergence** (file vs string path; editor state in levels). (Medium)
13. **Lua per-frame overhead + error spam** (no backoff). (Medium)
14. **Un-sandboxed Lua** (`io`/`os`/`package`/`debug` open). (High for mods)
15. **"Lying" settings** (shadow quality/HDR/exposure/gizmo size unwired). (Medium)
16. **Auto-reimport disabled but documented active.** (Medium)
17. **Derived-data cache write-only** — runtime re-parses raw sources. (Medium)
18. **Absolute-in-project paths pass Release validation.** (Medium)
19. **Portability test script broken as-written** (`--engine-root` = `build/Release`). (Low-Medium)
20. **Plugin settings lookup not owner-scoped.** (Low-Medium)
21. **No CI.** (High for reliability)
22. **Locale-sensitive JSON parsing.** (Low-Medium)
23. **Global state/singletons.** (Medium)
24. **No `VkPipelineCache`** (rebuild on every resize). (Low-Medium)
25. **Dead code** (`#if 0` build path, `RelationshipComponent`, `VulkanImGui.cpp` 0 bytes). (Low)
26. **Documentation drift.** (Medium)

Details, recommended actions, and timing for each are in `TECHNICAL_DEBT.md`.

---

## 6. Recommended Roadmap (evidence-based, dependency-aware)

**Phase 1 — Production Blockers:** runtime UI compositor (Vulkan pass + text atlas + images) · physics foundations (layers, trigger→Lua events, kinematic bodies, parent sync) · save/load API + stable entity identity + real prefab GUIDs · robust collide-and-slide character controller · portable 3D audio backend (replace MCI) · fix the Jolt settling crash + fixed-timestep + first physics test.

**Phase 2 — Core Maturity:** MSAA, alpha blending + sorting, pipeline cache, frustum culling, wire "lying" settings · unify editor/runtime hosts and mesh upload · scene versioning + serializer unification · auto-reimport + real DDC read path · packaging hardening (portable path rewrite, referenced-asset copy) · fix portability script + add CI · remove dead code · Lua sandbox decision + error backoff · documentation correctness pass.

**Phase 3 — Gameplay Production:** level/scene transitions + spawn points + trigger authoring · navigation (navmesh/pathfinding + debug draw) · animation state machines/graphs, root motion, then IK/retargeting · compiled runtime mesh format · gamepad/contexts/remapping · generic Lua component access · restore starter/template content.

**Phase 4 — Editor & Tooling:** real scene autosave, asset-op undo · log console, editor minidumps, shortcut reference · decompose `EditorMainWindow` + shared editor-engine base · template gallery · make the material graph's generated GLSL a real runtime path.

**Phase 5 — Rendering & Performance:** particles/VFX, decals, fog · point/spot shadows, SSAO, SSR, bloom, post stack · instancing + GPU-driven batching, LOD, GPU profiling · world streaming + real render-distance culling.

**Phase 6 — Advanced:** GI, virtualized geometry, temporal upscaling, GPU-driven rendering · retargeting/look-at/full IK · networking/multiplayer · consoles · Plugin API 2.0 (stable ABI, dependencies, crash isolation) · localization.

**Ownership rule:** keep game-specific gameplay (health/damage/inventory/weapons/teams/game modes/save schemas) in game projects on top of engine primitives; engine provides rendering, physics, animation core, UI compositor, audio, input, save primitives, navmesh runtime, stable identity; plugins own terrain/tile tools, custom importers/editors, panels; templates own the FPS controller, menu scripts, sample levels.

---

## 7. Build & Test Summary

- **Build:** all targets build cleanly on Windows (Editor, Game, Launcher, Build CLI). Editor boots with a live Vulkan viewport; packaged game ran relocated with exit code 0.
- **Tests:** 26/27 CTest pass. `ProjectCreationLayout` fails only because Qt DLLs aren't on CTest PATH (passes with PATH set — harness issue).
- **Coverage gaps:** rendering/GPU, physics simulation, editor Qt UI, audio, runtime UI rendering have no meaningful automated coverage. Portability end-to-end gate is a standalone script, unwired and currently red. **No CI.**

## 8. Documentation Accuracy

- **Accurate:** CHANGELOG, recent guides (AssetImporting, CharacterControllers, CharacterEditor, CharacterAssets, GameExportGuide, HeadlessBuilds, PrefabSystem), plugin docs, `docs/editor/UX_QOL_AUDIT.md`.
- **Outdated:** root `README.md` (ImGui era), `ROADMAP.md`, `QtEditorBuildNotes.md`, several `Scripting/*` docs, `IMGUIZMO_INTEGRATION.md`.
- **Known false claims:** "auto-reimport active" (disabled), "no hot-reload" (implemented), "multiple Lua scripts per entity" (1+1 slots), "C++ scripting removed" (inert but present), "editor/game don't share a frame loop" (now shared), "save/load runtime persistence complete" (no game saves), "Boomer-Shooter starter content" (absent from tree), "nightly CI" (none exists).

---

## 9. Final Answers to the Audit Questions

1. **What can it do?** Author + package a small Windows 3D game with Lua gameplay, skeletal characters, prefabs, PBR/CSM/HDRI, and portable packaging.
2. **What cannot?** Ship UI, save/load, spatial audio, triggers, moving platforms, NPC navigation, level transitions, non-Windows deployment.
3. **What partially works?** Physics, HUI (editor-only), audio (Windows 2D), gamepad (packaged only), input contexts/remapping, animation (crossfade only), material graph (bake-only).
4. **Genuinely production-ready?** Prefabs, asset operations, packaging, `Expose.*` inspector, character import/playback logic, core editor authoring.
5. **Prototypes disguised by demos?** Runtime UI, audio, CharacterMotor/FPS controller, C++ ScriptComponent.
6. **What prevents a complete game?** No runtime UI, no saves, no triggers, no spatial audio, fragile physics, single-scene runtime.
7. **What prevents non-developers using it?** Stale docs, absent sample content, god-object editor code, no log console, lying settings, no onboarding.
8. **What gets expensive later?** Raw-handle identity, editor/runtime divergence, hard caps (128 bones/256 descriptors/128 textures/1024 materials), Lua per-frame costs, physics determinism + Jolt crash, renderer without culling/instancing, HUI without a runtime backend.
9. **Build next:** runtime UI backend → physics foundations → save/load + stable identity → character controller → portable audio → Jolt crash fix.
10. **Don't build yet:** GI, virtualized geometry, temporal upscaling, terrain, VFX graphs, networking, consoles, C#, full animation graphs, general gameplay framework.
11. **Engine:** rendering, physics, animation core, UI compositor, audio, input, save primitives, streaming, navmesh, stable identity.
12. **Plugins:** terrain/tile tools, custom importers/editors, editor panels, custom asset types, rendering extensions (later).
13. **Games:** health/damage/inventory/weapons/projectiles/teams/game modes/save schemas — game-specific on engine primitives.
14. **How close to a vertical slice?** One focused milestone (runtime UI) plus physics/save/audio hardening.
15. **Best work sequence:** finish runtime UI + physics foundations, add save/load with stable identity, then portable audio, then renderer maturity; tackle structural debt (frame host, mesh upload, editor decomposition) in parallel refactors.