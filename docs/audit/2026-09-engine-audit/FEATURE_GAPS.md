# Feature Gap Analysis

Grouped by production impact. Classification is conservative: only gaps that genuinely block or heavily restrict game development are Critical/High. Ownership column: **Core** = Core Engine, **Module** = Built-in Engine Module, **Plugin** = Official Plugin, **Template** = Example/Template, **Game** = Game-specific.

---

## Critical — Blocks development of a complete game

| # | Gap | Evidence | Ownership |
|---|---|---|---|
| C1 | **Runtime UI renderer in packaged builds.** HUI widget tree, `.hui`, Lua UI API, and editor QPainter overlay all work; `HephaestusGame` collects draw commands and never draws them. No Vulkan 2D/UI pass, no text atlas, images are placeholder rects. | `GameMain.cpp:1609-1611`; `UIRenderer.cpp:5-31`; `RuntimeUIOverlayWidget.cpp` (editor-only) | Core |
| C2 | **Runtime save/load.** No save slots, no Lua save API, no player/game/world persistence. Only editor scene serialization + editor crash recovery. Entity identity is raw handles, so any save must first solve stable identity. | `docs/audit/save_system.md`; zero `saveGame` hits in engine | Core |
| C3 | **Trigger/contact events to gameplay.** `isTrigger` creates Jolt sensors but no contact listener exists; `onTriggerEnter/Exit/Stay` documented but no dispatch; `MeshCollisionSystem` is dead code. Volumes, doors, interaction all depend on this. | `JoltPhysicsSystem.cpp:589,918`; `ScriptBase.h:64-74` | Core |
| C4 | **Collision layers/filtering.** All bodies on layer 0 with pass-through filters. No player/enemy/projectile separation; everything collides with everything. | `JoltPhysicsEngine.cpp:109-126` | Core |
| C5 | **Kinematic bodies + parent-child physics sync.** No `EMotionType::Kinematic` (factory fakes it); physics reads/writes local transforms, corrupting parented hierarchies. Blocks moving platforms, doors, elevators, attached weapons. | `JoltPhysicsSystem.cpp:583,791-844`; `Components.h:365-372` | Core |

## High — Does not block, but significantly restricts what can be built

| # | Gap | Evidence | Ownership |
|---|---|---|---|
| H1 | **Portable 3D audio.** Current backend is WinMM/MCI, Windows-only, 2D, no mixer/buses, no streaming, no pitch, no tests. Non-Windows packaged builds are silent. `.ogg` unsupported. | `AudioEngine.cpp:40-48,220-226`; `ProjectValidator.cpp:234-238` | Core |
| H2 | **Stable entity identity / real prefab GUIDs.** Raw EnTT handles persisted; prefab roots get fresh IDs on load; nested-prefab parenting lost; `prefabGuid` never used for lookup. Undermines saves, level transitions, cross-session references. | `SceneSerializer.cpp:694,1125,1980-1991`; `PrefabOverrideSystem.cpp:343-383` | Core |
| H3 | **Runtime level/scene transitions.** Single scene per process; no Lua scene load; no scene registry. | `GameMain.cpp:297-315,706` | Core |
| H4 | **Transparency (alpha blend) + sorting + MSAA.** Cutout only; `blendEnable=false`; `VK_SAMPLE_COUNT_1_BIT`. Limits materials and image quality. | `VulkanPipeline.cpp:200`; `VulkanSwapchain.cpp:206` | Core |
| H5 | **Frustum culling / instancing / LOD / pipeline cache.** Every mesh drawn every frame (shadow pass ×4); no culling at all; no `VkPipelineCache` (pipelines rebuilt on every resize). Hard ceiling on scene scale. | `EngineFrameRenderer.cpp:3449-3541,2586-2755` | Core |
| H6 | **Robust character controller.** `CharacterMotor` is a velocity-driven dynamic-body motor (no collide-and-slide, no step height/slope limit). NPC locomotion and reliable FPS movement need a real controller. | `CharacterMotor.h:8-11` | Core |
| H7 | **Gamepad in editor Play + multiple controllers + input contexts + runtime remapping.** Gamepad is disabled in the editor; single controller; no per-mode maps; no in-game rebind UI. | `QtEditorEngine.cpp:1558-1560`; `GameMain.cpp:1556-1574` | Core |
| H8 | **Sandboxing / script safety.** `luaL_openlibs` gives every gameplay script `io`/`os`/`package`/`debug`. Fine for first-party; unsafe for user mods or untrusted content. | `LuaScriptEngine.cpp:248` | Module |
| H9 | **Editor data-loss protections.** No real scene autosave (recovery snapshots only), no undo for asset ops, auto-reimport disabled. Designers lose work or stale visuals after external edits. | `EditorMainWindow.cpp:5181-5191`; `EditorAssetPipeline.h:14` | Module |
| H10 | **Material shader authoring.** Material graph bakes to fixed PBR slots; generated GLSL never runs in the pipeline and is layout-incompatible. Artists cannot author custom GPU shaders. | `EngineFrameRenderer.cpp:2851-2861`; `MaterialShaderGenerator.cpp:105` | Core |
| H11 | **`.hematerial` metallic texture dropped on load.** Authored metallic maps silently disappear in runtime materials. | `EngineFrameRenderer.cpp:2851-2861` | Core |

## Medium — Important, but can wait until the core production loop works

| # | Gap | Evidence | Ownership |
|---|---|---|---|
| M1 | Navigation: runtime navmesh/pathfinding + debug draw + editor bake. No engine AI infrastructure. Build only when a title needs NPC pathing. | zero navmesh/pathfind hits in engine | Module |
| M2 | Particles/VFX, decals, fog. None exist. | — | Module |
| M3 | Animation state machines/graphs, layering, additive, root motion, IK, retargeting. Crossfade-only today; controllers hand-roll flags. | `CharacterPlayback.cpp` | Module |
| M4 | Point/spot shadows, SSAO, screen-space reflections, bloom, post stack. | — | Module |
| M5 | Triggers/spawn-point authoring components + Lua events (depends on C3/C4). | `Components.h:730` (placeholder) | Module |
| M6 | Editor log console, minidumps for editor, keyboard-shortcut reference, real autosave. | — | Module |
| M7 | Nested prefabs (prefab-over-prefab) + prefab variants. Authoring surface exists; nesting unsupported. | `PrefabComponentIO.cpp:1267-1273` | Core |
| M8 | Scene versioning/migration on load (`.hpscene` version is never validated). | `SceneSerializer.cpp:640,1050-1062` | Core |
| M9 | World streaming / large-level partitioning; render-distance culling actually wired. | `enableRenderDistance` unwired | Core |
| M10 | Runtime remapping UI + input contexts. | — | Module |
| M11 | Runtime Lua-side material/light creation; generic component access from Lua. | no `GetComponent` | Module |
| M12 | Asset database as runtime consumer (compiled runtime mesh/audio formats, referenced-asset packaging instead of full-directory copy). | `PackageStager.cpp:325`; DDC write-only | Core |
| M13 | Editor template gallery + onboarding sample level; restore or unadvertise the "Boomer-Shooter starter". | `LauncherWindow.cpp` (Templates disabled); `CHANGELOG.md:50` | Template |

## Low — Convenience, polish, specialist features, longer-term

| # | Gap | Evidence | Ownership |
|---|---|---|---|
| L1 | Plugin API 1.1: dependencies, selection-aware context actions, `inspector` context location, typed viewport events, plugin Open UI, settings in Settings window, per-owner settings lookup fix. | `EditorPluginHost.cpp:67,89`; `plugin-system-1.0.md` | Plugin |
| L2 | Editor undo for asset operations; launcher project Duplicate; launcher templates. | `LauncherWindow.cpp:333` | Module |
| L3 | Keyboard navigation/focus manager for HUI (controller nav). | `UIInputRouter.cpp` | Core |
| L4 | Text input widget, scroll lists, sliders, tooltips for HUI. | — | Core |
| L5 | Editor world-space TRS inspector, universal `enabled` flag, per-viewport snap save. | `UX_QOL_AUDIT.md` deferred list | Module |
| L6 | Localization/i18n. | zero hits | Module |
| L7 | Editor shader-preview / material live view beyond bake diagnostics. | — | Module |
| L8 | Crash isolation for plugins (out-of-process or watchdog). | in-process plugins | Plugin |

## Deliberately NOT recommended yet (would be premature)

- **Deferred rendering / clustered lighting** — current forward UBO is fine for the light caps; move to GPU-driven rendering only when culling/instancing foundations exist.
- **Global illumination / Lumen-like / virtualized geometry / Nanite-like / temporal upscaling** — renderer lacks basic culling, instancing, pipeline cache, and MSAA; foundations first.
- **Networking / multiplayer, consoles** — no platform abstraction for input/gamepad beyond one GLFW joystick.
- **C# or other scripting languages** — Lua + two-slot model is adequate; a second runtime would split the API surface.
- **General gameplay framework (health/damage/inventory/weapons/projectiles/teams/game modes)** — belongs in game projects on top of engine primitives (C3/C4, save API, `entity:Call`). Do not expand `Components.h` with game-specific types.
- **Terrain, heightmaps** — no runtime streaming; revisit after world streaming.
- **Full animation graphs / IK / retargeting** — the current clip+crossfade model serves the Cultist slice; graphs are a large surface best added after the runtime UI and physics foundations.