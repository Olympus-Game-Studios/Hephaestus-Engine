# System Audit — Detailed Status Classifications with Evidence

Every significant subsystem is classified below. Status scale: **Production Ready** / **Functional** / **Partial** / **Prototype** / **Stub** / **Missing** / **Unknown**. Evidence cites current source at revision `26e3326`.

---

## 1. Rendering

**Overall: Functional** — a single forward-pass Vulkan renderer shared by editor and game via `EngineFrameRenderer`.

**Architecture.** One main PBR pass into the swapchain (+ directional CSM shadow pass, editor-only ID pick pass). Editor and packaged game both call `EngineFrameRenderer::RenderFrame` (`QtEditorEngine.cpp:2082`, `GameMain.cpp:1613`); editor-only passes are compiled out via `HEPHAESTUS_GAME_RUNTIME`. The previous audit's claim that "editor and game do not share a frame loop" is now stale.

| Feature | Status | Evidence |
|---|---|---|
| PBR metallic/roughness (GGX/Smith/Fresnel) | Production Ready | `triangle.frag:88-125,361-391` |
| Albedo texture | Production Ready | `triangle.frag:297-302` |
| Normal map | Partial (non-TBN approximation, 70% blend) | `triangle.frag:318-330` |
| Roughness / AO textures | Functional | `triangle.frag:333-349` |
| Metallic texture | Functional in shader; **dropped by `.hematerial` loader** | `EngineFrameRenderer.cpp:2851-2861` omits `useMetallicTexture` |
| Emissive | Functional | `triangle.frag:496` |
| Transparency | Partial — **cutout only, no alpha blend** (main pipeline `blendEnable=false`, `VulkanPipeline.cpp:200`) | — |
| Two-sided / backface toggle | Functional | `MeshRendererComponent::backfaceCulling`, pipeline variants |
| HDR + Reinhard tonemap | Functional | `triangle.frag:498-502` |
| HDRI skybox + env reflection | Functional (equirect, roughness-scaled; no convolution) | `VulkanTexture.cpp:147-233`, `skybox.frag` |
| Directional light | Production Ready | `triangle.frag:369-392` |
| Point lights (≤16) / Spot lights (≤16) | Functional | UBO `EngineFrameRenderer.cpp:147-153` |
| Cascaded shadow maps (directional, 4 cascades, 4096², skinned included) | Functional | `recordShadowPass` `EngineFrameRenderer.cpp:2586-2755` |
| Point/spot shadows | Missing | — |
| Reflections | Partial (env sample only, "Nearby objects are not reflected yet") | `triangle.frag:481-493` |
| Ambient / AO | Partial (flat ambient + material AO; **no SSAO**) | `triangle.frag:477-479`, hardcoded ambient `:2449` |
| Fog / particles / decals | Missing | no code |
| Post-processing (bloom/AA) | Missing; MSAA absent (`VK_SAMPLE_COUNT_1_BIT`) | — |
| Render targets | Partial (shadow atlas + ID picker only) | — |
| Entity-ID picking | Production Ready (editor) | `entityid.*`, `PickEntityAt` |
| Outlines | Functional (editor, inverted hull, skinned variant) | `outline.*` |
| Debug rendering (grid, lines/points, gizmos, light icons) | Functional (editor) | — |
| Render-distance culling | **Stub/Unwired** — serialized + inspector only, never consulted by renderer | `SceneSerializer.cpp:971`, zero hits in `EngineFrameRenderer` |
| Instancing / frustum culling / occlusion / LOD | Missing | per-submesh draw, no culling |
| Shader compilation | Functional (build-time `glslc`); **no `VkPipelineCache`** | `CMakeLists.txt:948-1006` |
| Skinned rendering (128-bone palette UBO, main/shadow/outline/ID passes) | Functional | `triangle_skinned.vert`, `EngineFrameRenderer.cpp:699-928` |

**Material graph is bake-only.** `GenerateMaterialShader` runs only for editor diagnostics and `MaterialCompatibilityTests`; runtime always uses the fixed `triangle.frag`/`triangle_skinned.vert` with a `MaterialUBO` baked from the graph evaluation. The generated GLSL is incompatible with the runtime pipeline layout (sampler bindings vs bone UBO at `set=1,binding=0`). Artists cannot author custom GPU shaders today.

**Key limits:** 1024 material slots/frame, 128 texture slots, 16 point + 16 spot lights, 256 skinned bone-descriptor sets (concurrent-character ceiling), 2 frames in flight, 1 command buffer per swapchain image. No instancing, no culling, no pipeline cache, and the shadow pass redraws every mesh 4× (once per cascade).

---

## 2. Scene / ECS / World

**Overall: Functional** — EnTT-based ECS, serializable scenes, strong prefab system; identity and runtime world transitions are weak.

| Sub-area | Status | Evidence |
|---|---|---|
| ECS (EnTT) | Production Ready for authoring | `Scene.h:74` |
| Transform hierarchy (parent/child, world-preserving reparent) | Functional | `Scene.cpp:87-194` |
| Scene serialization (`.hpscene`) | Functional | `SceneSerializer.cpp` |
| Format versioning | **Missing** — `"version":1` written but never validated on load | `SceneSerializer.cpp:640,1050-1062` |
| Entity identity across save/load | **Partial/Fragile** — raw EnTT handles; prefab roots get fresh IDs | `SceneSerializer.cpp:694,1125` |
| Prefab system (overrides, apply/revert/unpack, refresh, create-from-selection, missing stubs) | **Production Ready** | `PrefabOverrideSystem.cpp`, `PrefabComponentIO.cpp`; 11 tests |
| Nested prefabs / prefab-over-prefab | **Missing** — instances flattened on capture; parent links lost on reload | `PrefabComponentIO.cpp:1267-1273`, `SceneSerializer.cpp:1980-1991` |
| Prefab GUID resolution | **Decoratonal** — `prefabGuid` stored but lookup is path-based | `PrefabOverrideSystem.cpp:343-383` |
| Runtime scene load | Functional (startup single scene) | `GameMain.cpp:706` |
| Level transitions / scene swap | **Missing** — no runtime API, no Lua `Scene.load` | — |
| Streaming / world partitioning | Missing | — |
| Spawn points / triggers | **Missing** — commented-out placeholder; physics sensor flag only, no events | `Components.h:226,309,730` |
| Scene duplication | Partial (Save As only) | `EditorMainWindow.cpp:4726-4749` |
| Entity destroy semantics | Partial — children reparent to root on delete | `Scene.cpp:72-81` |
| Editor/runtime format consistency | **Mostly consistent**; `SerializeToString()` (play/undo path) drops native `ScriptComponent` while `Serialize()` (file path) writes it | `SceneSerializer.cpp:2186` vs `882` |

**Note:** editor state (Selection, Folder) is serialized into `.hpscene` files and therefore shipped in packages.

---

## 3. Skeletal Animation & Characters

**Overall: Functional** — the largest new body of work since the previous audit (Stages A–G), well-tested at the logic layer. GPU and Qt-UI paths are not automated-tested.

| Sub-area | Status | Evidence |
|---|---|---|
| `.hpcharacter` asset (schema v1, owned `.source/`, versioning) | Production Ready | `CharacterAsset.cpp:285-299` |
| Skeleton import (GLB + FBX/ufbx), bone names, signature | Production Ready | `SkeletalModelImporter.cpp:1042-1199`, `FbxSkeletalImporter.cpp:115-229` |
| GPU skinning (128 bones, 4 influences, multi-mesh parts) | Functional | `Vertex.h:47-95`, `triangle_skinned.vert` |
| Animation import (glTF clips, FBX bake, takes, loops, trim) | Functional | `SkeletalModelImporter.cpp:568-696`, `FbxSkeletalImporter.cpp:474-511` |
| Clip playback (play/stop/pause/speed/loop, same-frame commit) | Functional | `CharacterPlayback.cpp`; tests |
| Crossfade | Partial — gated, TRS-lerp, single blend; no layering/additive | `CharacterPlayback.cpp:79-218` |
| Animation events | Partial — `onAnimationFinished` only | `LuaScriptEngine.cpp:599-644` |
| Root motion | **Missing** | no transform application |
| Animation state machines / graphs | **Missing** | controllers hand-roll flag→clip in Lua |
| IK / look-at / retargeting | **Missing** | — |
| Sockets (bone-attached) | Functional (query + editor gizmo) | `CharacterPlayback.cpp:313-335`, Lua `GetSocket` |
| Weapon/socket attachment (entity parenting to socket) | Partial — query-only, no auto-parent | — |
| Character controllers (two-slot Lua: gameplay + controller) | Production Ready | `LuaScriptEngine.h:72-75`, `CharacterLuaTests` |
| Character Editor (import, animation library, timeline, skeleton overlay, sockets, materials, reimport) | Functional | `CharacterEditorWindow.cpp`, `CharacterDocument.cpp` |
| Runtime binding (editor + packaged) | Functional | `CharacterRuntimeGpu.cpp:180-208`, `GameMain.cpp:1114-1116` |

**Notable:** no `.hpcharacter` authored asset ships in the repo (only YBot FBX sources); the import workflow is validated almost entirely by tests.

---

## 4. Physics (Jolt)

**Overall: Functional for simple rigid-body + raycast use; not production-grade for gameplay.**

| Sub-area | Status | Evidence |
|---|---|---|
| World lifecycle (init/step/shutdown) | Functional; **variable timestep, threaded, non-deterministic** | `JoltPhysicsEngine.cpp:77-217` |
| Static + dynamic bodies | Functional | `JoltPhysicsSystem.cpp:585` |
| Kinematic bodies | **Missing** — `CreateKinematic()` actually produces a dynamic body | `Components.h:365-372` |
| Shapes | Partial — box/sphere/capsule + static mesh; **cylinder is a silent no-op** (serializes, never builds) | `JoltPhysicsSystem.cpp:476-521` |
| Collision layers / filtering | **Missing** — all bodies layer 0, pass-through filters; `InitializeLayers` never implemented | `JoltPhysicsEngine.cpp:109-126` |
| Raycast | Functional (centralized, Lua-exposed) | `JoltPhysicsSystem.cpp:687-757`, `Physics.raycast` |
| Sweeps / overlaps | **Missing** | — |
| Trigger volumes | **Inert** — sensors created (`isTrigger`) but **no contact listener** registered, no Lua events | `JoltPhysicsSystem.cpp:589,918` |
| Contact events | **Missing** | no `SetContactListener`; `MeshCollisionSystem` is dead code that never runs |
| Constraints / joints | **Missing** | — |
| Mass / friction / restitution / damping | Functional (scalar, per-body) | `JoltPhysicsSystem.cpp:600-615` |
| Forces / impulses / explosions | **Missing** — only velocity setting; `Jump` sets velocity | `CharacterMotor.cpp:77` |
| CharacterMotor (FPS) | Prototype — velocity-driven dynamic-body motor, not collide-and-slide; no step/slope handling | `CharacterMotor.h:8-11` |
| Parent-child world sync | **Broken** — reads/writes local transform; parented bodies misplace and corrupt hierarchy | `JoltPhysicsSystem.cpp:791-844` |
| Physics debug rendering | **Missing** — `debugDraw` flags are no-ops | — |
| Scene/prefab serialization | Functional | `SceneSerializer.cpp:761-805` |
| Tests | **None** — no physics simulation tests exist | `engine/Tests/` |
| Known crash | Documented: "Physics simulation crashes after ~50-60 frames of body settling" (Jolt 4.1.0-rc1), exceptions swallowed | `JoltPhysicsEngine.cpp:202-216` |

**Sufficiency:** hitscan (raycast) is the strongest feature; projectiles work as dynamic bodies with linear-cast quality; FPS is workable; NPCs are weak; **moving platforms/doors/elevators are not achievable** (no kinematic bodies + broken parenting).

---

## 5. Input

**Overall: Functional** — solid `.hpinput` action/axis system; gamepad and remapping gaps.

| Sub-area | Status | Evidence |
|---|---|---|
| Keyboard / mouse (buttons, delta, scroll) | Production Ready | `InputSystem.cpp:49-125` |
| `.hpinput` v1 map (actions, axes, bindings, deadzone, scale) | Production Ready | `InputMap.cpp:249-342`; tests |
| Pressed/down/released semantics | Production Ready | `InputSystem.cpp:128-148` |
| Gamepad — packaged game | Functional (single controller, `GLFW_JOYSTICK_1`) | `GameMain.cpp:1556-1574` |
| Gamepad — editor Play | **Missing** — explicitly disabled | `QtEditorEngine.cpp:1558-1560` |
| Multiple controllers | **Missing** | single joystick polled |
| Input contexts (per-mode maps) | **Missing** (v1 explicitly dropped) | `InputSystemTests.cpp:97` |
| Runtime remapping | **Missing** — editor-only map editing; no in-game capture UI | `InputMapEditorWidget.cpp` |
| Editor Play keyboard/mouse adaptation | Functional | `QtEditorEngine.cpp:1470-1566` |
| Lua input API | Functional (actions/axes/gamepad + legacy key polling) | `LuaScriptEngine.cpp:2335-2433` |
| Project Settings integration | Functional | `SettingsWindow.cpp:335-374` |

---

## 6. Lua Scripting

**Overall: Functional** — mature, used by real gameplay scripts; single unsandboxed global state and per-frame overhead.

| Sub-area | Status | Evidence |
|---|---|---|
| Script attach (1 gameplay + 1 controller slot per entity) | Production Ready | `LuaScriptComponent.h`, `LuaScriptEngine.h:72-75` |
| Lifecycle (`onAwake/Update/LateUpdate/Destroy/AnimationFinished`) | Functional | `LuaScriptEngine.cpp:372-648` |
| `onEnable`/`onDisable` | Partial — `enabled=false` is treated as destroy (state lost) | `LuaScriptEngine.cpp:440-443` |
| Lua API surface (Scene/Entity/Transform/Physics/Input/Audio/UI/Camera/Animator/Character) | Production Ready (broad) | `RegisterBindings` `:866-882` |
| Generic component access / trigger events / coroutines | **Missing** | `onTriggerEnter` documented but no code |
| `Expose.*` static scanner + Inspector editors + authored-value reconciliation | Production Ready | `LuaExposeScanner.cpp`, `LuaScriptVariableReconciler.cpp` |
| Error handling | Functional — per-script isolation via pcall; **errors logged every frame** (no backoff), broken scripts re-parsed each frame | `LuaScriptEngine.cpp:1465-1491,1817-1822` |
| Hot reload | Functional — mtime polling per frame (docs say it is "planned") | `LuaScriptEngine.cpp:1452-1465` |
| Sandboxing / security | **Stub/None** — `luaL_openlibs` opens `io`/`os`/`package`/`debug`; scripts can read/write files and run OS commands | `LuaScriptEngine.cpp:248` |
| Serialization (variables/authored in scenes + prefabs) | Production Ready | `SceneSerializer.cpp:901-922`, `PrefabComponentIO.cpp:534-596` |
| State management | Functional — cleaned per Play session; `_G` and UI handle maps can accumulate across loads | `LuaScriptEngine.cpp:661-711` |
| C++ `ScriptComponent` | **Inert** — displayed in inspector, never bound in play mode; `ScriptEngine::AttachScript` has no callers | `ScriptEngine.cpp:6-9` |

---

## 7. Editor (Qt)

**Overall: Production Ready for core authoring** — the strongest surface in the engine. Notable UX gaps below.

| Sub-area | Status | Evidence |
|---|---|---|
| Viewport (play/edit, camera, gizmos, snapping, placement preview, shading modes, ortho views) | Production Ready | `EditorMainWindow.cpp`, `QtEditorEngine.cpp`, `EditorCameraController.cpp` |
| Outliner (multi-select, filters, drag-reparent, shortcuts) | Production Ready | `EditorMainWindow.cpp:1204-3607` |
| Inspector (registry inspectors, multi-edit, clipboard, asset refs) | Production Ready | `InspectorSelectionModel.cpp`, `PropertyEditors.cpp`, `ComponentInspector.h` |
| Asset browser (import, thumbnails, search, drag-to-viewport, rename/trash with reference rewrite, reimport, Find References) | Production Ready | `AssetBrowserWidget.cpp` |
| Play mode | Functional — Lua/physics/audio/UI wired; **C++ ScriptComponent dropped**; edits made in play are discarded on stop | `PlayModeManager.cpp`, `QtEditorEngine.cpp:3183-3245` |
| Undo/redo | Functional for scene/prefab/character docs; **no undo for asset-browser ops** | `EditorUndoCommand.cpp` |
| Project Settings | Functional — **several settings do nothing** (shadow quality, HDR, exposure, gizmo size are stored but unwired) | `SettingsSchema.cpp`, `SettingsWindow.cpp` |
| Build/package window | Production Ready | `EditorMainWindow.cpp:5937-6275`, `BuildSettingsDialog.cpp` |
| Plugin Manager | Functional (minimal: enable/disable, restart-required; no refresh/install) | `PluginManagerDialog.cpp` |
| Launcher | Functional (project create/open/scan; **Duplicate disabled**; Templates "coming soon") | `LauncherWindow.cpp` |
| Layout/dock persistence, engine settings | Production Ready | QSettings |
| Crash recovery (RecoveryStore autosave) | Functional (editor-only; **not a real scene autosave** — crash window exists) | `RecoveryStore.cpp`, `EditorMainWindow.cpp:5125-5315` |
| Log console | **Missing** — no GUI console dock; errors go to `hephaestus.log` | — |
| Minidumps | Missing (editor writes text log only; game writes minidumps via `RuntimeBootstrap`) | — |
| Auto-reimport (FileWatcher) | **Disabled** despite docs; manual reimport only | `EditorAssetPipeline.h:14` |
| Keyboard-shortcut reference | Missing | — |

**Structural debt:** `EditorMainWindow.cpp` is ~7,600 lines (god-object); inspector teardown and viewport-UI-update logic are duplicated; the old CMake/QProcess build path is retained in a `#if 0` block; Character/Prefab editors reimplement the viewport host instead of reusing `QtEditorEngine`; asset-browser spawn paths are duplicated and noisy (`std::cout`).

---

## 8. Runtime UI (HephaestusUI)

**Overall: Partial — a capable editor-side UI framework with no packaged-game renderer.**

| Sub-area | Status | Evidence |
|---|---|---|
| `.hui` asset format + parser/builder | Functional | `HUIAsset.cpp` |
| Widgets (panel/text/button/image/canvas) + layouts (VBox/HBox/Grid) + anchors | Functional | `UI*.cpp` |
| Editor Play rendering (QPainter overlay) | Functional | `RuntimeUIOverlayWidget.cpp` |
| **Packaged game rendering** | **RESOLVED (Stage 8A)** — Vulkan overlay backend (`VulkanHUIRenderer`) records the HUI pass after the 3D scene; `GameMain` submits commands via `EngineFrameRenderer::SubmitHUICommands`; verified by the relocated packaged-game portability gate and headless `HUIRendererTests` | `engine/UI/HephaestusUI/Rendering/` |
| Text rendering | Functional (packaged) — stb_truetype glyph atlas (`UIFontAtlas`), caching, metrics, kerning, wrapping, shadows | `engine/UI/HephaestusUI/Rendering/UIFontAtlas.cpp` |
| Image rendering | Functional (packaged) — reuses `VulkanTexture` (PNG/JPG/HDR/PPM), scale modes, tint, missing-asset placeholder | `VulkanHUIRenderer.cpp` |
| Lists / scroll / sliders / text input / tooltips | **Missing** | — |
| Focus manager / controller navigation | Partial — mouse-only router; scripts hand-roll key nav (see `mainMenu.lua`) | `UIInputRouter.cpp:10-63` |
| Lua UI API (~75 functions) | Production Ready | `LuaScriptEngine.cpp:4680-4901` |
| Visual editor | Functional | `HUIVisualEditorDialog.cpp` |

**Conclusion (historical):** menus/HUD/dialogue work in editor Play but **no shipped game can display any UI**. This was the highest-severity production gap.

**Status update (Stage 8A):** the packaged runtime UI gap has been resolved. `HephaestusGame` renders HUI through the Vulkan backend (panels, buttons, text, images, alpha, clipping, anchors, resize). The editor Play QPainter overlay remains for authoring; both paths consume the same renderer-independent draw command list. See `Documentation/Features/HEPHAESTUS_UI_RUNTIME_GUIDE.md`.

---

## 9. Audio

**Overall: Prototype** — minimal Windows-only WinMM/MCI 2D playback.

| Sub-area | Status | Evidence |
|---|---|---|
| Backend | Prototype — WinMM MCI, **Windows-only**; no-op elsewhere | `AudioEngine.cpp:40-48,220-226` |
| Formats | Partial — wav/mp3 only; `.ogg` rejected at validation | `AudioEngine.cpp:24-27`, `ProjectValidator.cpp:234-238` |
| Playback / stop / pause / volume / loop | Functional (Windows) | `AudioEngine.cpp:76-181` |
| Pitch / 3D / listener / attenuation / spatialization / occlusion / reverb / streaming / buses | **Missing** | — |
| Multiple concurrent sources | Functional | handle map |
| Editor preview | **Missing** — no audition button; audio only in Play | `AudioSourceInspector.cpp:50-104` |
| Runtime (packaged) | Functional (Windows) | `GameMain.cpp:759-761,1596-1597` |
| Lua API | Functional | `LuaScriptEngine.cpp:4542-4678` |
| Tests | **Missing** | — |

---

## 10. Asset Pipeline

**Overall: Functional** — solid path-based pipeline; source files are consumed directly at runtime (no compiled runtime mesh format).

| Sub-area | Status | Evidence |
|---|---|---|
| Portable paths (`assets/`, `.engine/`, `scripts/`, `builtin://`, `plugin://`) | Production Ready | `AssetPathPolicy.cpp` |
| Asset identity / IDs | Partial — `AssetDatabase` ids are time+counter (not GUIDs); runtime never queries the DB | `AssetDatabase.cpp:296-303` |
| Importers (texture, static mesh, audio, character) | Functional | `AssetImporters.cpp`, `ModelImporter.cpp` (glTF/OBJ), ufbx FBX |
| **Runtime formats** | **Partial** — no `.hpmodel`/`.hpmesh`; **packaged game loads raw `.glb`/`.fbx`/`.obj`/`.png` at runtime** | `GameMain.cpp:1199-1333` |
| Derived data cache | **Write-only** — `DerivedDataCache` stores payloads nothing ever reads back | `DerivedDataCache.cpp` |
| Auto-reimport | **Disabled** — FileWatcher polling exists but never started (docs claim it is active) | `EditorAssetPipeline.cpp:20`, `FileWatcher.cpp` |
| Reference index + rewrite on rename/move | Production Ready | `AssetReferenceIndex.cpp` |
| Safe ops (transactions, trash/restore, rollback) | Production Ready | `AssetOperationService.cpp` |
| Missing-asset handling | Functional (validation errors + missing-prefab stubs) | `ProjectValidator.cpp` |
| Companion assets (character `.source/`) | Functional | `CharacterAsset.cpp:162-260` |

---

## 11. Packaging / Deployment

**Overall: Functional** — the most mature shipping slice; Windows x64 verified end-to-end in this audit.

| Sub-area | Status | Evidence |
|---|---|---|
| `HephaestusBuild` CLI | Functional (validate/package/clean/zip) | `BuildCli.cpp` |
| BuildService / PackageStager | Functional — transactional, preserves previous output, locked-output detection | `BuildService.cpp`, `PackageStager.cpp` |
| Project validation (actionable diagnostics) | Functional | `ProjectValidator.cpp` |
| Runtime locator (dev vs distribution) | Functional | `RuntimeLocator.cpp` |
| Portable package (no engine-repo leakage) | **Verified in this audit** — packaged game ran from a relocated dir, exit 0, markers `STAGE6_PORTABILITY_COMPLETE` etc. | `tools/test_package_portability.ps1` |
| Asset selection | Partial — **copies entire `assets/` and `scripts/`**; no reference filtering; editor source art ships | `PackageStager.cpp:325` |
| Third-party notices | Partial — emitted with a review warning (Lua/Vulkan/VMA/nlohmann/stb/MSVC missing) | `PackageStager.cpp:154-176` |
| Absolute-path references | Partial — `ASSET_PATH_ABSOLUTE_IN_PROJECT` is only a Warning; ships verbatim and breaks on another machine | `ProjectValidator.cpp` |
| Portability test script | **Buggy as-written** — computes `--engine-root` as `build/Release` (no `CMakeLists.txt`); fails; works when pointed at the source root | `tools/test_package_portability.ps1:11` |

---

## 12. Plugin System 1.0

**Overall: Functional** — real, versioned, with a working example; crash isolation and API stability are limited.

| Sub-area | Status | Evidence |
|---|---|---|
| `.hpplugin` manifest (schema v1, id rules, engine-version constraints) | Production Ready | `PluginManifest.cpp:28-84` |
| Discovery (engine + project roots) | Functional | `PluginSystem.cpp:35-73` |
| Loading / lifecycle / teardown | Functional | `PluginSystem.cpp:128-207` |
| Unloading | Functional (shutdown/reload only; enable-toggle requires restart) | `PluginSystem.cpp:194-207` |
| Versioned PluginAPI (structSize + apiVersion handshake) | Production Ready | `PluginAPI.h:16,176-196` |
| Dependency handling | **Missing** (deferred to 1.1) | `plugin-system-1.0.md:35` |
| Content mounting (`plugin://`) | Functional | `AssetMountRegistry.h`, `PluginSystem.cpp:84-89` |
| Failure isolation | Partial — C++ exceptions isolated; **crashes/hangs take down the editor** (in-process, main thread) | — |
| Persistence (enabled state, plugin settings) | Functional — settings lookup is **id-only, not owner-scoped** (cross-plugin collision risk) | `EditorPluginHost.cpp:67` |
| Plugin surface (commands/menus/toolbars/docks/context actions/asset types/importers/scene ops/undo/settings) | Functional (wide); **`inspector` context location unwired**, context actions receive no selection data, panel icon ignored, settings not in Settings UI | `EditorPluginHost.cpp:48-96` |
| PrimitiveTools example | Functional (builds, deploys, exercises full surface) | `Plugins/PrimitiveTools` |
| Tests | Partial — manifest/discovery/mount/persistence covered; **no real plugin DLL is ever loaded** by tests | `PluginSystemTests.cpp` |
| Third-party capability | Commands/menus/toolbars/docks/context actions/asset types/importers/scene primitives/content/settings; **no renderer/ECS/physics/audio/scripting/build access** | — |
| API stability | Partial — engine-versioned C++ virtual-interface SDK; same-toolchain only, not a permanent ABI | `plugin-api.md:30` |

---

## 13. Gameplay Framework / Save-Load / AI-Navigation

| Area | Status | Evidence |
|---|---|---|
| Gameplay components (health/damage/inventory/weapons/teams/checkpoints/game modes) | **Missing** — only animation-focused `CharacterComponent`; all gameplay is game-specific Lua | `Components.h` |
| Boomer-Shooter starter content | **Absent** — changelog (2026-08-31) advertises Player/Enemy/Director prefabs, `Hud.hui`, `default.hpscene`; **none exist in the tree** (only `CultistAI.lua`/`CultistCharacter.lua` remain) | `CHANGELOG.md:50` vs `assets/` |
| Spawning (prefab instantiate/destroy via Lua) | Functional | `LuaScriptEngine.cpp:4381-4477` |
| Game save/load (runtime persistence) | **Missing** — no save slots, no Lua `Save.*`, no player/game state persistence; only editor scene serialization + crash recovery | — |
| Save versioning / migration / corruption handling | **Missing** (for game saves) | — |
| Navmesh / pathfinding / A* / steering / perception | **Missing** (Level 0) | — |
| AI at engine level | **Missing** | — |
| AI at game level | Minimal sample (`CultistAI.lua`: straight-line move + timer attack; no LOS/pathfinding) | `assets/scripts/CultistAI.lua` |

---

## 14. Debugging / Development Tooling

| Capability | Status | Evidence |
|---|---|---|
| Logging (`hephaestus.log`, EngineLog) | Functional | `EngineLog.cpp` |
| Crash handler (game minidump) | Functional (game); editor is log-only | `RuntimeBootstrap.cpp:321-437` |
| Free camera / editor camera | Functional | `EditorCameraController.cpp` |
| Physics / collision / nav visualization | **Missing** | — |
| Animation debug (skeleton overlay in Character Editor) | Functional | `CharacterEditorWindow.cpp:645-673` |
| Entity inspection at runtime | Partial (Inspector is authoring-only; no runtime debug inspector) | — |
| Performance stats (FPS/draws/RAM overlay) | Partial — minimal overlay | — |
| GPU profiling | **Missing** | — |
| Console / in-editor log | **Missing** | — |
| Spawn/load/cheat commands | **Missing** | — |

---

## 15. Testing

See `TEST_AND_BUILD_REPORT.md` for the full table. Summary: strong coverage for assets, prefabs, packaging, input, Lua scanner/reconciler, and skeletal animation/character logic; **no meaningful coverage for rendering/GPU, physics simulation, Qt editor UI, audio, or runtime UI rendering**. No CI exists.

---

## 16. Documentation

See `TEST_AND_BUILD_REPORT.md`. The root `README.md` and `ROADMAP.md` describe the pre-Qt/ImGui-era engine and are **outdated**; recent guides (AssetImporting, CharacterControllers, GameExportGuide, plugin docs) are **accurate**. Known false claims: auto-reimport active (disabled), hot-reload not implemented (implemented), "multiple Lua scripts per entity" (1+1 slots), "C++ scripting removed" (inert but present), "editor/game don't share a frame loop" (now shared).