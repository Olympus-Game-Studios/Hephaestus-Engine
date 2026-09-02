# Hephaestus Engine Capability & Maturity Audit

**Date:** 2026-09-02  
**Repository revision:** `72a4dd7130b9026a44f3dfa51991ab4acb1f0fff` (`72a4dd7`)  
**Branch:** `main`  
**Version stamp in CMake:** `2026.9.0`  
**Scope:** Read-only audit of the current repository. No engine behaviour was changed.

This is the entry point. Subsystem detail lives in the linked files. Scores are conservative and based on implementation, wiring, editor workflow, Lua exposure, and tests — not on the existence of a class or menu item.

---

## Scope

Audited:

- Runtime (`engine/`, `shaders/`, `HephaestusGame`)
- Editor (`engine/UI/QtEditor`, launcher, material/HUI/prefab editors)
- Assets, packaging, and tests
- First-party documentation versus current code

Not treated as engine features:

- Vendored samples inside `extern/` (Jolt samples, EnTT tests, GLFW tests)
- Marketing or roadmap claims unless verified in code

---

## Audit methodology

1. Inventory modules, CMake targets, shaders, components, and tests.
2. Trace each capability along: asset → serialization → runtime → renderer/subsystem → editor → Lua → tests.
3. Rate using the maturity scale below. A technically working but poorly integrated feature is not scored as Strong.
4. Treat inspector fields, enums, and filenames as **claims** until a runtime path consumes them.
5. Use changelog, `docs/editor/UX_QOL_AUDIT.md`, and guides only as supporting context.

### Maturity scale

| Level | Name | Meaning |
|------:|------|---------|
| 0 | Missing | No meaningful implementation |
| 1 | Foundation | Types, prototypes, or UI exist; not a usable workflow |
| 2 | Functional | Works in limited scenarios; major gaps |
| 3 | Usable | Practical for real development; notable gaps remain |
| 4 | Strong | Integrated, reasonably tested, suitable within Hephaestus' current scope |
| 5 | Advanced | Unusually complete for this stage of the engine |

Status labels used in the [capability matrix](ENGINE_CAPABILITY_MATRIX.md): Solid / Functional but incomplete / Prototype / Unwired / Stub / Legacy / Missing / Unknown.

---

## Subsystem audits

| Document | Overall | One-line verdict |
|----------|--------:|------------------|
| [rendering.md](rendering.md) | 3 | Forward Vulkan PBR + directional CSM + HDRI; dual editor/game stacks |
| [materials.md](materials.md) | 3 | Graph editor bakes to a fixed PBR slot model; no custom GPU shaders |
| [scene_ecs.md](scene_ecs.md) | 3 | EnTT scene graph + serialize/play; no streaming or stable GUIDs |
| [prefabs.md](prefabs.md) | 4 | Strongest content system: overrides, apply/revert/unpack, tests |
| [physics.md](physics.md) | 3 | Jolt rigid bodies + FPS motor; local-only sync, no layers/joints |
| [scripting.md](scripting.md) | 3 | Lua can drive small games; no sandbox, no Lua tests, vestigial C++ scripts |
| [ui.md](ui.md) | 3 | `.hui` menus/HUD work; limited widgets, mouse-centric |
| [audio.md](audio.md) | 2 | Windows 2D WinMM MCI; no 3D, no cross-platform backend |
| [animation.md](animation.md) | 0 | Static meshes only; no clips, skinning, or controllers |
| [input.md](input.md) | 2 | Raw keyboard/mouse poll; no action maps or gamepad |
| [assets_import.md](assets_import.md) | 3 | Path-based pipeline + safe ops; DDC/auto-reimport unfinished |
| [build_runtime.md](build_runtime.md) | 4 | Stage 6 Windows packaging is the most mature shipping slice |
| [editor_tooling.md](editor_tooling.md) | 4 | Qt editor after UX Phases 1–4 is the engine's strongest product surface |
| [world_gameplay.md](world_gameplay.md) | 2 | ECS + Lua hooks; no game-mode, save, nav, or AI layer |
| [performance_profiling.md](performance_profiling.md) | 1 | FPS/draw/RAM overlay only |
| [testing_quality.md](testing_quality.md) | 3 | Strong assets/prefab/build tests; renderer/physics/Lua untested |
| [navigation.md](navigation.md) | 0 | No navmesh/pathfinding |
| [particles_vfx.md](particles_vfx.md) | 0 | No particle/VFX system |
| [terrain.md](terrain.md) | 0 | No terrain/heightmap |
| [networking.md](networking.md) | 0 | No networking |
| [save_system.md](save_system.md) | 0 | No save-game API (editor recovery is not a game save) |
| [localization.md](localization.md) | 0 | No i18n |
| [platform_support.md](platform_support.md) | 2 | Windows x64 is real; Linux/macOS/consoles are not shipping targets |

Synthesis:

- [ENGINE_CAPABILITY_MATRIX.md](ENGINE_CAPABILITY_MATRIX.md)
- [ENGINE_PRIORITY_ROADMAP.md](ENGINE_PRIORITY_ROADMAP.md)
- [ENGINE_ARCHITECTURE_RISKS.md](ENGINE_ARCHITECTURE_RISKS.md)
- [DOCUMENTATION_GAPS.md](DOCUMENTATION_GAPS.md)
- [GENRE_READINESS.md](GENRE_READINESS.md)

---

## Executive summary

Hephaestus is a **Windows-first, Lua-scripted, Qt-authored 3D engine** that can already assemble, play, and package small static-mesh games. It is **not** a general-purpose Unreal/Unity competitor. The last two months of work (prefabs, inspector/scene authoring, asset operations, Stage 6 packaging) made the **editor-to-package loop** substantially more real than the public README or `ROADMAP.md` suggest.

**Overall maturity: Level 3 (Usable) for small Windows 3D projects that do not need skeletal animation, networking, or large worlds. Level 2 as a general game engine.**

What exists and is worth relying on:

- Qt editor (scene, inspector, outliner, asset browser, prefab/material/HUI editors, build window)
- Prefab instances with overrides
- Forward PBR rendering with directional cascaded shadows and HDRI skybox
- Lua gameplay (transform, input poll, physics motor/raycast, prefab spawn, HUI)
- Path-based asset identity with move/rename rewrite and trash
- Windows x64 generic-player packaging (`HephaestusBuild` / Build Project)

What looks present but is not a production feature:

- Material “shader generation” (GLSL snippets + bake to `MaterialComponent`; GPU still uses `triangle.frag`)
- Physics collision layers, cylinder shapes, debug draw, kinematic factory
- Environment yaw, render-distance cull, shadow normal bias, exposure settings
- C++ `ScriptEngine` (still constructed; gameplay is Lua)
- `SceneExporter` sources (retired from CMake, files remain)
- README/roadmap claims of ImGui editor, clustered lighting, completed save/load, C#

The engine can support a **small first-person or walking-sim project on Windows** if the team writes interaction, UI flow, and persistence in Lua. It cannot currently support character-action, large open worlds, multiplayer, or controller-first games without major custom systems.

---

## Top strengths

1. **Qt editor productization** after UX Phases 1–4: real gizmos, snapping, multi-select, undo/redo, recovery, asset operations.
2. **Prefab system** with tests (`PrefabSystemTests`) covering save/load, overrides, apply/revert, unpack, missing assets, legacy blueprints.
3. **Stage 6 packaging**: shared `HephaestusBuildCore`, validation, transactional staging, portability tests.
4. **Lua as a real gameplay language**: lifecycle, `Scene.instantiate`/`destroy`, physics motor, HUI `display.ui`.
5. **Forward PBR + lights + CSM + HDRI** sufficient for interior/outdoor static scenes.
6. **Portable asset paths** (`assets/`, `.engine/`, `scripts/`, `builtin://`) with a reference index.

---

## Top weaknesses

1. **No animation system** (not even clip playback or skinning vertex format).
2. **Split renderer**: editor uses `EngineFrameRenderer`; packaged game reimplements the frame loop in `GameMain.cpp`.
3. **Gameplay framework is absent**: no save games, scene streaming, navigation, AI, events/triggers API, inventory, localization.
4. **Input is raw GLFW/Qt key polling** — no actions, contexts, rebinding, or gamepad.
5. **Audio is Windows-only 2D MCI.**
6. **Almost no automated tests** for renderer, physics, Lua, or full editor play mode.
7. **Public docs are substantially wrong** (ImGui editor, Diligent, clustered lighting, completed persistence).

---

## Most important blockers to making games

| Blocker | Why it matters | Severity |
|---------|----------------|----------|
| No skeletal animation | Blocks third-person, character combat, most action/RPG | Critical for those genres; not for walking-sim/puzzle |
| No save/load | Almost every shipped game needs persistence | High |
| Dual renderer | Editor/play vs packaged game can (and will) diverge | High (architecture) |
| Physics uses local transforms | Parent–child physics is wrong | High for any hierarchy |
| No trigger/overlap Lua | Interaction/volumes must be hacked via raycasts | High |
| No action-mapped input / gamepad | Keyboard-only; no rebind UI | Medium–high |
| No scene streaming | Soft cap on world size | Medium |
| Material graph is bake-only | Artists cannot author unique shaders | Medium |
| Audio not portable / not 3D | Packaged Linux/macOS silent; no spatial SFX | Medium |

---

## Recommended next major priorities

See [ENGINE_PRIORITY_ROADMAP.md](ENGINE_PRIORITY_ROADMAP.md). Immediate focus (finish-before-adding plus gameplay plumbing):

1. **Unify packaged runtime onto `EngineFrameRenderer`** (stop dual-stack drift).
2. **Physics correctness:** world-transform sync, layers, trigger callbacks, actually draw debug colliders.
3. **Input abstraction** (actions + gamepad) so gameplay is not GLFW key names.
4. **Save/load + scene transition helpers** (small reusable runtime, not a full RPG system).
5. **Animation import + clip playback** if character games are in scope; otherwise defer graphs/IK.

Deliberately wait: deferred rendering, SSR/SSAO/bloom, GPU particles, networking, terrain, consoles, C# scripting, clustered lighting.

---

## Runtime vs editor vs Lua vs tests (major systems)

| System | Runtime | Editor | Lua/API | Tests | Overall |
|--------|--------:|-------:|--------:|------:|--------:|
| Rendering | 3 | 3 | 1 | 0 | 3 |
| Materials | 3 | 3 | 0 | 3 | 3 |
| Scene/ECS | 3 | 4 | 3 | 2 | 3 |
| Prefabs | 4 | 4 | 3 | 4 | 4 |
| Physics | 3 | 3 | 2 | 0 | 3 |
| Scripting | 3 | 3 | 3 | 0 | 3 |
| HephaestusUI | 3 | 3 | 3 | 0 | 3 |
| Audio | 2 | 3 | 2 | 0 | 2 |
| Animation | 0 | 0 | 0 | 0 | 0 |
| Input | 2 | 2 | 2 | 0 | 2 |
| Assets | 3 | 4 | 1 | 4 | 3 |
| Build/package | 4 | 4 | n/a | 4 | 4 |
| Editor product | n/a | 4 | n/a | 2 | 4 |

---

## Designer vs programmer readiness

| Role | What they can do today | What they cannot |
|------|------------------------|------------------|
| **Programmer (Lua)** | Player movement, raycast interaction, spawn/destroy prefabs, menus, 2D audio, camera | Secure mods, materials at runtime, animation, save framework, nav/AI |
| **Programmer (C++)** | Engine internals; leftover `ScriptEngine` is not a product path | Should not write game scripts in C++ |
| **Designer** | Place meshes/lights/prefabs, inspect components, build `.hui` menus, package Windows builds | Author animation, particles, nav, input maps, script variables in Inspector |
| **Technical artist** | PBR materials via graph bake, texture slots, HDRI, lights/shadows | Custom shaders, VFX graphs, tessellation, layered materials |

A feature that exists only as C++ internals is scored lower than the same feature with editor + Lua workflow.

---

## Third-party dependencies (concise)

| Dependency | Purpose | Runtime / editor | Coupling | Replacement difficulty | Current risks |
|------------|---------|------------------|----------|------------------------|---------------|
| Vulkan + VMA | GPU | Both | High | Very high | Shader path heuristics; no MSAA |
| GLFW | Game window/input | Runtime (+ Lua fallback) | High | High | Game input is GLFW key names |
| Qt 6 | Editor, launcher, HUI overlay host, tools | Editor | High | Very high | Qt viewport surface is Win32-only |
| Jolt | Physics | Both | Medium–high | High | Thin wrapper; unused Jolt features (ragdoll, vehicles) |
| EnTT | ECS | Both | High | High | Entity IDs are not stable GUIDs |
| Lua 5.4 | Game scripts | Both | High | Medium | Optional at configure time; `luaL_openlibs` unsandboxed |
| nlohmann/json | Scenes, materials, glTF JSON | Both | Medium | Medium | Hand-rolled serializers |
| stb_image | Texture decode | Both | Low | Low | CPU decode; no GPU compression pipeline |
| Custom OBJ/ASCII-FBX/glTF importer | Meshes | Both | Medium | Medium | Binary FBX unsupported; no skins/animation |

Licensing: this audit is **not** a legal review. Packaged builds emit `ThirdPartyNotices.txt` (`PackageLayoutTests`). `ROADMAP.md` still lists a license audit as future work. Do not treat notices generation as a completed legal clearance.

---

## Documentation vs implementation

Root `README.md` still describes an **ImGui editor**, ImGuizmo, and an export path that hides the editor inside `HephaestusEngine.exe`. The product is a **Qt editor + generic `HephaestusGame` player**. `ROADMAP.md` marks save/load complete and lists clustered lighting, C#, and GPU particles as near-term. See [DOCUMENTATION_GAPS.md](DOCUMENTATION_GAPS.md).

The most accurate current docs are recent Stage 6 guides (`Documentation/Guides/GameExportGuide.md`, `HeadlessBuilds.md`) and `docs/editor/UX_QOL_AUDIT.md`. Lua guides are mixed: APIs exist that the guide still calls “planned”.
