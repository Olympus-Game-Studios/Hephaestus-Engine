# Engine State — Inventory, Architecture, and Completeness Matrix

This document covers repository inventory, engine architecture, runtime/editor separation, and the master completeness matrix. System-by-system detail with evidence lives in `SYSTEM_AUDIT.md`.

---

## 1. Repository Inventory

- **~288 commits** on `main`; **1,552 tracked files**; **~266,000 lines of C++** across 574 `.cpp/.h` files. Predominantly a single-developer project.
- CMake (>= 3.24) multi-target build; MSVC 2022 on Windows is the shipping target; macOS and Linux presets exist (`CMakePresets.json`) but are not validated by CI. Version stamped `2026.9.10` from a single source of truth in `CMakeLists.txt`.

### Top-level modules

| Directory | Contents |
|---|---|
| `engine/` | All engine code: Core, Gfx, Scene, Assets, Animation, Physics, Scripting, Input, Audio, Build, Plugins, PluginAPI, UI (QtEditor, HephaestusUI, Launcher, Shared, MaterialEditor), Game, ToolsCommon, Tests |
| `editor/` | Editor resources only (icons, fonts, styles, billboards) — all editor code lives under `engine/UI/QtEditor` |
| `shaders/` | GLSL sources; precompiled `.spv` committed; CMake recompiles via `glslc` |
| `assets/` | Engine sample content: meshes (builtin GLBs), textures, HDRI, scenes, prefabs, scripts, UI, input maps, YBot character FBX sources |
| `scripts/` | Reference gameplay scripts (`fp-controller.lua`, `mainMenu.lua`, `pauseUI.lua`, etc.) |
| `Plugins/` | Plugin System 1.0 example (`PrimitiveTools`) |
| `TestProject/` | Empty shell project (no scenes/assets of substance) |
| `engine/Tests/` | 28 test translation units + a packaging portability fixture project |
| `docs/`, `Documentation/` | 30+ markdown files (see `TEST_AND_BUILD_REPORT.md` for accuracy) |
| `extern/` | Third-party: EnTT, GLFW, ImGui (vestigial), ImGuizmo (vestigial), VMA, stb_image, nlohmann/json, ufbx (FBX), JoltPhysics, Lua 5.4 (bundled DLL + import lib) |
| `.github/` | Issue drafts + Copilot instructions only — **no CI workflows** |

### Build targets

| Target | Role |
|---|---|
| `HephaestusEngine` | Qt editor application |
| `HephaestusGame` | Generic packaged game player (GLFW + Vulkan + Lua, no Qt) |
| `HephaestusLauncher` | Qt project launcher |
| `HephaestusBuild` | Headless build/packaging CLI |
| `HephaestusPrimitiveTools` | Example plugin DLL |
| `HephaestusInputCore` / `HephaestusPluginCore` / `HephaestusBuildCore` / `HephaestusToolsCommon` | Non-Qt shared libraries |
| 27 `*Tests` executables | CTest suite (see `TEST_AND_BUILD_REPORT.md`) |

---

## 2. Engine Architecture

### Lifecycle

- Editor: `main.cpp` → `EditorQtApp` (QApplication) → `EditorMainWindow` → `QtEditorEngine` (owns Vulkan device, `EngineFrameRenderer`, play mode). Editor and play mode share one process; play mode snapshots the scene to a string, runs the simulation inside the editor, and restores the snapshot on stop (`PlayModeManager`).
- Runtime: `HephaestusGame` → `RuntimeBootstrap` (log, crash handler, DLL discovery, dependency validation) → `GameMain::main` → GLFW window → `VulkanDevice`/`VulkanSwapchain`/`EngineFrameRenderer` → scene load → input/physics/audio/Lua/HUI init → single while-loop.

### Entity/Component architecture

- EnTT registry per scene. `Scene::GetRegistry()` is public and reached into by rendering, physics, scripting, and editor code. Components are plain structs in `Components.h` (+ `LuaScriptComponent`, `CharacterComponent`, `ScriptComponent`).
- Entity identity is a raw EnTT handle (32-bit). Handles are persisted in `.hpscene` and recreated on load with `registry.create(hint)`. This works only for entities created through the `entities` array and breaks for prefab-instance roots (fresh IDs) — see `TECHNICAL_DEBT.md`.

### Scene graph

- Parent/child stored in `TransformComponent.parent`/`children`. World transform computed by recursive multiply. `SetParent` supports world-transform preservation with cycle detection. `RelationshipComponent` is a dead duplicate (unused).
- Physics does **not** use `Scene::GetWorldTransform` — it reads local transform fields directly, corrupting parented bodies (`TECHNICAL_DEBT.md`).

### System initialization / services

- No DI/service-locator framework. Systems are hand-wired singletons and globals: `LuaScriptEngine::s_Instance`, process-global `AssetMountRegistry`, static `PrefabOverrideSystem::SetProjectRoot`, global editor cameras, and a global entity clipboard. Many systems reach across via `Scene*` passed to functions (`GameMain` wires everything through lambdas into Lua bindings).

### Event/message systems

- No central event bus. Inter-script communication is string-named `entity:Call("name", ...)`. C++ `ScriptBase` declares `OnTriggerEnter/Exit/Stay` but nothing dispatches them. Physics has no contact listener. Editor uses Qt signals/slots.

### Threading

- Jolt runs its own job-system thread pool (up to 31 threads here). No engine job system. All rendering, script, and asset work is on the main thread. UI runs on the Qt main thread in the editor; the game runtime is single-threaded + Jolt threads.

### Runtime/editor separation

- The frame loop is shared via `EngineFrameRenderer::RenderFrame` (editor and game), with editor-only passes (ID picker, outlines, gizmos, grid, billboards, debug overlay) compiled out of the game build via `HEPHAESTUS_GAME_RUNTIME=1`.
- **Residual divergence:** `GameMain.cpp` reimplements a large amount of the host loop by hand (mesh upload, scene setup, Lua wiring, physics bridging, per-frame input), duplicating code in `QtEditorEngine.cpp`. The Character/Prefab editor windows each re-implement their own viewport host instead of reusing `QtEditorEngine`.
- **Critical gap:** the packaged game never consumes HUI draw commands, so runtime UI only exists in the editor.

### Game/engine separation

- Games are **data + Lua scripts only** — there is no way to ship game-specific C++ compiled into the player. The generic player is shared by all games. This is a strength for portability but a hard ceiling for performance-critical game code.
- `ScriptComponent` (native C++ scripting) exists but is inert: `ScriptEngine::AttachScript` has no callers and the Qt play path never binds C++ scripts. Lua is the only scripting path that works.

---

## 3. Engine Completeness Matrix

Legend: **Editor** = authorable in the Qt editor; **Runtime** = works in editor Play; **Packaged** = works in a shipped `HephaestusGame` package; **Tests** = automated coverage.

| Area | Status | Editor | Runtime | Packaged | Tests | Priority |
|---|---|---|---|---|---|---|
| Rendering (forward PBR + CSM + HDRI) | Functional | Yes | Yes | Yes | Minimal | High |
| Materials (PBR slots + graph bake) | Functional | Yes | Yes | Yes | Partial | High |
| Skeletal animation / `.hpcharacter` | Functional | Yes | Yes | Yes | Strong | Medium |
| Multi-mesh characters / GPU skinning | Functional | Yes | Yes | Yes | Partial | Medium |
| Physics (rigid bodies + raycast) | Functional | Yes | Yes | Yes | None | High |
| Physics layers / triggers / joints / kinematic | Missing | — | — | — | — | High |
| Scene/ECS/serialization | Functional | Yes | Yes | Yes | Good | Medium |
| Prefabs + overrides | Production Ready | Yes | Yes | Yes | Strong | Low |
| Input (`.hpinput` actions/axes) | Functional | Partial* | Yes | Yes | Partial | Medium |
| Gamepad | Partial | No | No | Yes (1 controller) | None | Medium |
| Lua scripting | Functional | Yes | Yes | Yes | Good | Medium |
| Lua `Expose.*` inspector variables | Production Ready | Yes | Yes | Yes | Good | Low |
| Runtime UI (HUI) | Partial (editor-only render) | Yes | Yes | **No** | None | **Critical** |
| Audio | Partial (WinMM, Windows 2D) | Yes | Yes | Yes (Windows) | None | High |
| Asset import (GLB/FBX/OBJ/textures) | Functional | Yes | Yes | Yes | Good | Low |
| Asset database / derived data | Partial | Yes | n/a | n/a | Partial | Medium |
| Asset ops (rename/trash/rewrite) | Production Ready | Yes | n/a | n/a | Good | Low |
| Packaging / portability | Functional | Yes | — | Yes | Good | Low |
| Project creation / launcher | Functional | Yes | — | — | Good | Low |
| Plugin System 1.0 | Functional | Yes | — | — | Partial | Medium |
| Character Editor | Functional | Yes | — | — | Partial | Medium |
| Prefab Editor | Production Ready | Yes | — | — | Partial | Low |
| Play mode | Functional | Yes | — | — | None | Medium |
| Save/load (game) | Missing | — | — | — | — | **Critical** |
| Scene/level transitions | Missing | — | — | — | — | High |
| Navigation / AI | Missing | — | — | — | — | Medium |
| Particles / decals / fog / post | Missing | — | — | — | — | Medium |
| Entity IDs (stable GUIDs) | Missing | — | — | — | — | High |
| Debug/profiling tooling | Stub | Partial | Partial | Partial | — | Medium |
| Documentation accuracy | Partial | — | — | — | — | High |

\* Editor Play forwards keyboard/mouse only; gamepad is explicitly disabled in the editor (`QtEditorEngine.cpp:1558-1560`).

---

## 4. Engine/Runtime/Editor Feature Split (what must stay engine, what is game-specific)

From the audit evidence, the correct ownership split going forward:

- **Core Engine:** rendering, GPU skinning, physics world + shape sync, runtime UI compositor, portable audio backend, input action system, save/load primitives, stable entity identity, scene streaming, animation core (clips/playback), runtime navmesh.
- **Engine Module:** animation state-machine/graphs, IK/retargeting, trigger/event helpers, navigation/steering helpers, particles/VFX, terrain, decals, post-processing, editor QoL infrastructure.
- **Official Plugin:** custom importers, custom asset types/editors, editor panels, terrain/tile tools (already architecturally supported by Plugin System 1.0).
- **Template/Example:** FPS controller, character controller patterns, main/pause menu Lua, a sample level. The changelog-advertised "Boomer-Shooter starter" must either be restored here or unadvertised.
- **Game Project:** health/damage/inventory/items/weapons/projectiles/teams/game modes/save schemas/dialogue — game-specific; build them on engine primitives rather than expanding `Components.h`.