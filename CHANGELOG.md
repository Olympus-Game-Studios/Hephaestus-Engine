# Hephaestus Engine Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added

- 2026-10-11: Stage 9A settling repro (`PhysicsSettlingRepro`) on the pre-fix 1 GiB `TempAllocatorImpl`. Historic 8-box stack survived 20 init/300-step/shutdown cycles; peak `GetUsage` was 9028112 bytes. Heavier 64-box scene (plus gravity-free movers and overlapping sensors) survived 300 steps; peak `GetUsage` was 9039344 bytes. The Jolt 4.1.0-rc1 settling crash was not reproduced on Jolt 5.4.1. No stepping, filter, or allocator-size change.
- 2026-10-07: Editor automation adds `build.validate`, `build.package`, `build.clean`, and `build.get_status`, which drive the in-process build service. Release packages still reject runtime automation. `test.list`, `test.run`, and `test.get_status` run one named CTest from the engine build tree. These tools are editor-only.
- 2026-10-07: `runtime.capture` writes a PNG from the existing framebuffer readback during editor Play Mode and in the Development packaged runtime. It shares the viewport capture slot and uses the `runtime-` filename prefix. Editor images do not include the Qt HUI overlay; packaged images include HUI when it was submitted to the swapchain.
- 2026-10-07: Runtime physics automation adds `physics.raycast`, `physics.list_bodies`, `physics.get_body`, and `physics.get_character_state`. Snapshots include only bodies that were locked and read; missing bodies are skipped or returned as `no_body`, and bodies that cannot be queried return `unreadable`. Queries are read-only and available in editor Play Mode and the Development packaged runtime.
- 2026-10-07: Runtime UI automation adds `ui.get_tree`, `ui.find`, `ui.get`, and `ui.click`. Clicks queue press/release into `UIContext` and are dispatched by `UIInputRouter` on the next `UISystem::Update`. A timed-out click cancels only its tagged events and leaves player input queued. Available in editor Play Mode and the Development packaged runtime.
- 2026-09-24: Stage 8A HephaestusUI Runtime Renderer 1.0 adds a packaged-game Vulkan backend for HUI. New `engine/UI/HephaestusUI/Rendering/` layer (backend-agnostic `IHUIRenderBackend`, `HUIGeometryBuilder`, stb_truetype `UIFontAtlas`, `VulkanHUIRenderer`) consumes the existing renderer-independent draw command list. `HephaestusGame` now renders HUI as an overlay after the 3D scene via `EngineFrameRenderer::SubmitHUICommands`: batched primitive geometry, rounded corners (SDF), premultiplied alpha, scissor clipping, glyph-atlas text with caching/kerning/wrapping/shadows, and engine `VulkanTexture` images with scale modes and missing-asset fallback. Works across resolutions and survives swapchain resize. Bundles Roboto fonts (`assets/fonts/`) with system-font fallback; no Qt dependency. Adds `assets/ui/HUIRuntimeTest.hui`, headless `HUIRendererTests` (draw-command generation, layout/anchors, hit-testing, visibility, ordering, geometry conversion, font atlas), a HUI verification extension to the packaged portability gate (`tools/test_package_portability.ps1`), and `hui.vert`/`hui.frag` shaders.
- 2026-09-24: Vendored `extern/stb_truetype.h` and fixed the portability test harness `--engine-root` path (documented audit bug) so the relocated packaged-game gate runs.
- 2026-09-04: Prefab Editor hierarchy lists each node's components and can Add Component from the panel, context menu, toolbar popup, or Inspector. MeshRenderer/Light/Camera/AudioSource spawn as child nodes with independent transforms (Unreal Blueprint-style); other types attach to the selected node. MeshRenderer also adds Material; RigidBody also adds PhysicsShape.
- 2026-09-04: Character Editor Import Animation names clips in the import dialog (Mixamo `mixamo.com` / `Take 001` default to the source file name). Clips can be renamed after import (F2, double-click, Inspector Name), which updates the clip id used by playback and controllers.
- 2026-09-03: Character Multi-Mesh Support 1.0 lets a `.hpcharacter` own one canonical skeleton and 1..N compatible skinned mesh parts (GLB and FBX), sharing one pose and bone palette. Inspect lists every part; incompatible skeleton groups fail; YBot-style multi-mesh FBX imports without Blender cleanup; Character Editor materials group by mesh part; reimport preserves `meshPart`+slot identity.
- 2026-09-03: FBX Import 1.0 adds ufbx (`extern/ufbx` v0.23.0) for static meshes and `.hpcharacter` sources: `FbxDocument` / `FbxStaticImporter` / `FbxSkeletalImporter`, owned `mesh.fbx`, Linear `ufbx_bake_anim` clips, helper-node folding, Auto/Adjust/Modify space conversion (authored + resolved persisted), Import Character/Animation/Reimport FBX filters, skinned-FBX Import Assets warning, `FbxImportTests`, and [AssetImporting.md](Documentation/Guides/AssetImporting.md).
- 2026-09-03: Stage G Lua animation API adds a `LuaScriptSlot::CharacterController` beside gameplay Lua, `this:GetAnimator()` / `this:GetCharacter()` / `this:CallController()`, Play/CrossFade no-op guards, destination-clip CrossFade queries, `SetSpeed` lock-down (runtime multiplier `> 0`, `Pause()` to freeze), world and animated-model socket queries, `onAnimationFinished` after pose eval, Cultist/YBot controller+AI scripts, `CharacterLuaTests`, and [CharacterControllers.md](Documentation/Guides/CharacterControllers.md).
- 2026-09-03: Stage F Character Editor adds a Prefab-Editor-style window for `.hpcharacter` assets: isolated preview bound through `CharacterRuntime`, animation library/import with immutable `clip.id`, timeline scrub via `SampleAtTime`, skeleton debug overlay, sockets with bone-local gizmo TRS, materials/controller/reimport, document undo/save, live instance refresh without clobbering instance fields, and `CharacterEditorTests`.
- 2026-09-03: Stage E character product workflow adds GLB-only Import Character, shared `CharacterRuntime` bind for editor and `HephaestusGame`, runtime-generated MeshRenderer (not serialized/prefab-captured/inspected), Character Inspector (clip/speed/materials/controller Expose), hidden `.source/` listing, companion owned-source rename/move/trash, two-case character reimport, and `CharacterWorkflowTests`.
- 2026-09-03: Stage D clip playback adds glTF animation import, a CPU sampler in evaluation order, `CharacterComponent` play/loop/speed/pause/stop with same-frame Play commit, scene/prefab serialize of authored character fields, per-instance bone palettes, a gated two-pose `CrossFade`, and `CharacterPlaybackTests`.
- 2026-09-03: Stage C rest-pose GPU skinning adds `SkinnedVertex` (joints/weights at locations 4–5), a 128-bone eval-index UBO, skinned vertex shaders for shadow/PBR/outline/ID passes, a `builtin://skinned_rest_fixture` dummy, and `SkinnedMeshTests`. Static meshes are unchanged.
- 2026-09-03: Stage B skeletal glTF import adds `engine/Animation` skeleton identity vs topological evaluation order, `SkeletonSignature`, unnamed/duplicate bone rejection, a dedicated importer that does not bake static-mesh node TRS into skinned vertices, and `SkeletalImportTests` golden world-matrix fixtures.
- 2026-09-03: Stage A of the `.hpcharacter` character asset system adds `AssetType::Character`, a versioned `HephaestusCharacter` JSON schema/serializer (source/skin/signature skeleton refs, owned `.source/<Stem>/` sidecar layout, controller `scriptPath`), Asset Browser New Character, reference-index/validator/packaging inclusion of owned sources, and `CharacterAssetTests`.
- 2026-09-03: Stage 7B2 Lua Inspector Variables adds statically scanned `Expose.bool/integer/number/string/vec2/vec3/color` declarations, native ordered Inspector editors, explicit script-relative authoring metadata, deterministic reload reconciliation, existing prefab `scriptVars` Apply/Revert integration, runtime and packaged-value parity, focused tests, and updated scripting templates/documentation.
- 2026-09-02: Stage 7B1 gameplay input foundation adds shared action/axis state, validated `.hpinput` v1 profiles, Lua edge/axis APIs, Qt Play keyboard/mouse adaptation, packaged GLFW gamepad polling, Project Settings mapping authoring, seeded project defaults, packaging support, and focused tests.
- 2026-09-02: Follow-up issue drafts define packaged HephaestusUI presentation and Play Mode/runtime camera-policy parity outside Stage 7A1.
- 2026-09-02: Repository-wide capability and maturity audit under `docs/audit/` (index, capability matrix, roadmap, architecture risks, documentation gaps, genre readiness, and per-subsystem reports). Documentation only; no engine behaviour changes.
- 2026-09-02: Stage 6C/D shipping polish adds named file-count package progress, explicit confirmed package cleanup, CLI `--clean-output`, and locked-output retry or alternate-output actions.
- 2026-09-02: Stage 6 portability fixture, `RuntimeLocatorTests`, `BuildServiceTests`, and a repeatable relocated-package launch gate covering Lua, HUI, audio, physics, shaders, HDRI, manifests, and source-tree independence.
- 2026-09-02: Stage 6 build and shipping pipeline: shared non-Qt `HephaestusBuildCore`, project validation with actionable asset/path diagnostics, transactional package staging, Windows x64 runtime/dependency collection, package manifests/notices/size reports/ZIP archives, and the `HephaestusBuild` headless CLI.
- 2026-09-02: Qt Build Project workflow with Development/Release presets, persisted startup scene and runtime settings, Validate/Package/Package & Run/Run Last/Clean actions, cancellation, structured Problems, searchable raw logs, package notifications, and `Ctrl+Shift+B`.
- 2026-09-02: `ProjectValidationTests` and `PackageLayoutTests` cover broken references, malformed content, portable layout, content types, exclusions, manifests, and preservation of the previous successful package.
- 2026-09-02: Phase 4 Inspector UX: `InspectorSelectionModel` mixed-value/batch helpers, searchable Add Component popup, `AudioSource` inspector + prefab IO, shared Qt property widgets (`Numeric`/`Vec3`/`Bool`/`Enum`/`Color`), `AssetReferenceEditor` + `AssetPickerDialog`, component clipboard, and `InspectorEditTests`.
- 2026-09-02: Phase 3 scene-authoring workflow: surface-aware viewport placement previews with repeat/cancel, real per-mode transform snapping, hierarchy/prefab-aware multi-duplicate and Alt-drag, marquee and prefab-root selection, editor-only hide/isolate/lock sidecars, align/distribute/drop/group/replace commands, viewport context creation, audio placement, camera bookmarks, scene statistics, and `SceneAuthoringTests`.
- 2026-09-01: Phase 2 editor foundations: `AssetPathPolicy` (portable `assets/` / `.engine/` / `scripts/` / `builtin://` keys), project-wide Asset Browser search with Show in Folder, recent-projects remove/prune/reveal, `EditorLogicSmokeTests`, and optional `HEPHAESTUS_BUILD_QT_TESTS` harness. Updated `docs/editor/UX_QOL_AUDIT.md` with validated Phase 2 priorities and outcomes.
- 2026-09-01: Viewport shading modes Lit/Unlit/Wireframe: `ViewShadingMode` on `FrameRenderConfig`, Unlit via `triangle.frag` UBO `shadingMode`, optional wireframe pipelines when `fillModeNonSolid` is available, and a live Lit/Unlit/Wireframe toolbar menu.
- 2026-09-01: Orthographic editor views: `RenderCamera` projection mode / FOV / ortho half-extent, centralized view/proj in `updateCameraUBO`, Perspective/Front/Back/Left/Right/Top/Bottom combo with free-camera restore, ortho wheel zoom + FocusOnSelection framing, shadows disabled in ortho.
- 2026-09-01: `ProjectCreationService` unifies blank project layout for launcher and editor New Project; `ProjectCreationTests` validates folder layout; `project.info.lastScene` settings helper.
- 2026-09-01: Safe asset operations: non-Qt `OpenDocumentRegistry` + `AssetOperationService` (preflight referrers, rename/move with `AssetReferenceIndex::RewriteReferences`, trash/restore under `.hephaestus/trash/` with manifest, best-effort `.hephaestus/transactions` rollback); Qt `AssetDependencyDialog`, Asset Browser Find References / service-backed rename+trash, `EditorAssetServices` ScanProject on project open; `AssetOperationTransactionTests`.
- 2026-09-01: Manual `EditorAssetPipeline` owns AssetDatabase / DerivedDataCache / ReimportManager (no FileWatcher by default); Asset Browser Reimport / Reimport All Changed for mesh/texture/audio; copy-import registers sources; editor polls DrainCompleted with status + AssetManager refresh.
- 2026-09-01: `TextureViewerDialog` with checkerboard preview, zoom/pan, channel isolation, metadata, and Copy Path / Open Externally / Show in Explorer; texture open uses it instead of a not-implemented stub.
- 2026-09-01: Qt editor scene recovery autosave wired to `RecoveryStore` (session begin on project open, ~30s heartbeat, dirty-scene snapshots on `engine.general.autosaveInterval`, restore/discard prompt, clean end on project/window close).
- 2026-09-01: `EditorNotificationService` for Qt editor status-bar/toast notifications with short-window duplicate suppression (`ShowInfo`/`ShowWarning`/`ShowError`/`ShowProgress`/`CompleteProgress`).
- 2026-09-01: Non-Qt `RecoveryStore` under `<project>/.hephaestus/recovery/` with atomic session/index/snapshot writes, unclean-session detection (stale heartbeat / dead PID), prune/discard APIs, and `RecoveryStoreTests`.
- 2026-09-01: `AssetReferenceIndex` (non-Qt) indexes forward dependencies and reverse referrers by portable path across `.hpscene`, `.hpprefab`, `.hematerial`, `.hui`, and project settings; supports `RewriteReferences` / `RewritePathInDocument` for known JSON fields, malformed-JSON diagnostics, and headless `AssetReferenceIndexTests`.
- 2026-09-01: Editor UX/QOL polish pass: scene/prefab redo stacks, coalesced transform undo, Outliner shortcuts (Delete/F2/Ctrl+D/Esc/Ctrl+A), dock/window layout persistence, Asset Browser Import/Duplicate/Copy Path/Prefab+Audio filters/clickable breadcrumbs/folder sync, Material Graph Undo/Redo toolbar + real node Rename, HUI hierarchy context menu, in-editor New Project wizard, launcher open-last-project-on-startup, and `docs/editor/UX_QOL_AUDIT.md`.
- 2026-08-31: Lua `Scene.instantiate(path, opts)` / `Scene.destroy(entity)` for runtime prefab spawn/destroy. Instantiate resolves project-relative paths, applies optional root `position`/`rotation` (degrees) and merges `vars` onto instance `LuaScriptComponent` maps before `onAwake`. Destroy is deferred until the end of the script Update tick; Update snapshots script entities before iterating so mid-tick spawns are safe.
- 2026-08-31: `PrefabSystemTests` `InstantiatePoseAndVars` covering root pose application and spawn-var merge onto prefab Lua scripts.
- 2026-08-31: Lua entity userdata methods (`GetTransform`/`GetName`/`SetName`/`IsValid`/`GetCamera`/`Call`) operate on the receiver entity rather than the running script entity. Added `Scene.findAllWithPrefix` and `Scene::FindEntitiesWithNamePrefix`.
- 2026-08-31: `JoltPhysicsSystem::Raycast` / `GetEntityForBody` return hit entity id and world-space surface normal; Lua `Physics.raycast` exposes `entity`, `point`, `normal`, and `distance`. Editor and game hosts share the centralized raycast path.
- 2026-08-31: `Physics::CharacterMotor` with Lua `Physics.isGrounded` / `move` / `jump` for basic FPS capsule locomotion. Packaged runtime (`GameMain`) now calls `LateUpdate` after `Update`.
- 2026-08-31: Boomer-Shooter project gameplay starter content: `Player`/`Enemy` prefabs, `Player`/`Enemy`/`Director` Lua scripts, optional `Hud.hui`, and a minimal `default.hpscene` with Player instance, Director, `EnemySpawn*` markers, and a physics floor stub (no authored arena).
- 2026-08-31: Documented Lua gameplay APIs in `LUA_SCRIPTING_GUIDE.md` and `engine/Scripting/README.md` (`Scene.instantiate`/`destroy`/`findAllWithPrefix`, entity `Call`, raycast hit entity, FPS motor).
- 2026-08-28: Production prefab instance workflow: automatic property-path override tracking (`PrefabOverrideSystem` / `PrefabComponentIO`), Inspector override indicators with Revert/Apply, Apply to Prefab (with confirmation for Apply All), reliable Refresh that reapplies overrides, Create Prefab From Selection capturing all prefab-supported components, Unpack that bakes overrides into ordinary entities, missing-prefab stubs that do not fail scene load, and Prefab Editor reuse of Scene Inspector widgets for live component authoring.
- 2026-08-28: Scene serialization stores prefab instance `rootTransform` separately from child overrides. Prefab children remain omitted from `entities[]`.
- 2026-08-28: Sample prefab `assets/prefabs/Lamp.hpprefab`. Legacy `assets/blueprints/Player.hpblueprint` continues to load.
- 2026-08-28: `PrefabSystemTests` covering save/load, scene instance round-trip, refresh, apply, revert, create-from-selection, unpack, missing assets, and legacy Blueprint loading.

### Changed

- 2026-10-11: Physics temp allocation is an 18 MiB fixed block with malloc fallback (twice the recorded heavier-scene peak of 9039344 bytes, rounded up). The first fallback logs one `Physics` warning. Jolt's process-wide factory is reference-counted and released on the last engine shutdown. `PhysicsSystem::Update` errors are logged by flag name. The historic settling crash was not reproduced on Jolt 5.4.1, and the stale 4.1.0-rc1 comment is removed.
- 2026-09-04: Boomer-Shooter Enemy prefab uses `Enemy1.hpcharacter` (Idle/Walk controller) instead of a cube mesh; `Enemy.lua` still owns chase/damage while locomotion clips play from `Enemy1Character.lua`.
- 2026-09-04: Character Editor moves clips and folders out of the left Character tree into an Animations dock tabbed with Inspector, with Import Animation and Create Folder on that tab.
- 2026-09-02: `HephaestusGame` now hosts the authoritative `EngineFrameRenderer` for scene rendering, materials, lighting, cascaded shadows, HDRI, synchronization, and presentation while preserving GLFW runtime lifecycle and packaged asset resolution.
- 2026-09-02: Shared lighting now consistently honors authored attenuation, transformed local-forward spot directions, disabled lights, and the active camera FOV for cascaded shadows.
- 2026-09-02: `EngineFrameRenderer` now rebuilds swapchain-dependent pipelines, per-image resources, synchronization, descriptors, and configured editor overlays after resize while preserving material textures and VSync.
- 2026-09-02: Packaged runtime builds no longer initialize editor-only grid, picking, outline, gizmo, or billboard GPU resources in `EngineFrameRenderer`.
- 2026-09-02: Retired the unused `SceneExporter` and legacy post-export packager; `export_game.ps1` is now a compatibility wrapper over the canonical shared BuildService CLI, and old export documents are marked historical.
- 2026-09-02: Portable Editor distributions now include the launcher, generic player, build CLI, Lua, compiled shaders, builtin meshes, and HDRIs; game packages copy project `.engine` runtime content and write only runtime-supported `game.ini` keys.
- 2026-09-02: Game distribution now packages the generic player with all project assets, Lua, compiled shaders, builtin meshes, runtime metadata, and verified third-party notices without requiring project-specific compilation or editor-only `.hephaestus` state.
- 2026-09-02: Project Build settings now expose only supported Windows x64 targets, use `defaultScene` as the persisted Startup Scene, and keep personal package output paths in user-local editor settings.
- 2026-09-02: Details Inspector multi-edits the current selection (one shared widget tree, mixed axes/name, coalesced undo). Light/Environment/AudioSource use the component registry. Asset Browser drags attach portable MIME for materials/scripts/textures/UI. Prefab Apply stays single-instance; Prefab Editor rotation is degrees with searchable Add Component.
- 2026-09-02: Scene reparenting can preserve world transforms; Scene Editor duplication preserves parents, hierarchies, supported components, and linked prefab instances. Outliner rebuilds retain expansion state and support component, prefab, hidden, and locked filters.
- 2026-09-01: Scene/material save paths and material texture browse normalize through `AssetPathPolicy`; material `.hematerial` texture `resourcePath` fields are rewritten to portable keys on save.
- 2026-09-01: `ReimportManager` no longer starts FileWatcher/auto-reimport in its constructor; `WorkerLoop` calls `NotifyAssetReimported` after each completed job is queued.
- 2026-09-01: Viewport Lit/Unlit/Wireframe and Perspective/ortho standard views are live (renderer + toolbar). Launcher/editor New Project share `ProjectCreationService`. Asset Browser new Lua scripts use `onAwake`/`onUpdate`/`onDestroy`. Launcher/editor project creation also seeds `assets/scripts`, `materials`, `prefabs`, and `ui`, and treats default HDRI copy failures as non-fatal. Updated `Documentation/Features/EDITOR_SHORTCUTS.md` for Qt behaviour.
- 2026-09-01: Boomer-Shooter Player prefab uses `.engine/meshes/Capsule.glb` for visuals with the existing capsule collider; `Player.lua` mouse look prefers host mouse delta and re-asserts cursor capture each frame.
- 2026-09-01: Boomer-Shooter `Player.lua` mouse look now follows `scripts/fp-controller.lua` (position-based deltas, yaw convention, always-on capture). Player prefab remains a capsule collider with updated look sensitivity defaults.
- 2026-08-28: Prefab Editor Inspector is no longer read-only labels; it hosts the same component inspectors as the Scene Editor against the preview entity and syncs edits back into the `PrefabAsset`.

### Fixed

- 2026-10-07: `engine_log` and packaged runtime `HP_LOG` lines are recorded in the EngineLog ring, so `log.get_entries` can read them in editor Play Mode and the Development runtime. A limited page now continues from the last returned cursor instead of skipping the rest of the ring.
- 2026-09-04: MeshCollider CPU resolution applies its push to the Jolt body origin instead of writing center-of-mass through `SetPosition`, so offset character capsules no longer hop into the air and fall every tick. Transform is re-synced after the correction. FloorStub in Boomer-Shooter now uses a static complex MeshCollider so Jolt owns a mesh matching the visual cube.
- 2026-09-04: Enemy capsule uses cylinder height 1.0 + radius 0.4 (total 1.8m, offset 0.9) so the collider matches a standing YBot instead of a 2.6m Player-style capsule. Character draw lift now uses the current pose's lowest bone, not only bind-pose mesh bounds.
- 2026-09-04: Character capsules with `freezeRotation` keep pitch/roll locked but allow yaw so `Physics.setRotation` can face a target. `Physics.setRotation` also writes the entity Transform the same frame. Character meshes whose bounds sit below the origin (Mixamo hip pivot) are lifted onto the entity origin at draw time, and character colliders use the authored capsule offset instead of mesh-bounds centering.
- 2026-09-04: Runtime-spawned character prefabs bind after Lua Update instead of inside `Scene.instantiate`, so Director `onUpdate` is not aborted by FBX/GPU upload. Failed binds keep an authored MeshRenderer.
- 2026-09-04: Play Mode clears the prefab asset cache on start so disk edits to `.hpprefab` files (such as Enemy) apply without restarting the editor.
- 2026-09-04: CharacterEditorTests compile against the single `CharacterClipImportChoice` Import Animation overload (MSVC no longer sees an ambiguous `{"Walk"}` brace call).
- 2026-09-03: Character Editor skeleton overlay no longer crashes on open. Debug-line draws now bind the camera/material descriptor set with the required dynamic UBO offset.
- 2026-09-03: Extra Mixamo animation FBXs now play in the Character Editor. Runtime bind matches clips by source file plus take name (so `mixamo.com` / `mixamo.com_2` stay distinct), extra FBX/GLB takes remap onto the character skeleton by bone name, and FBX bake looks up joints by eval index after Finalize.
- 2026-09-03: Opening a Character Editor preview no longer destroys in-flight skinned GPU meshes. `InvalidateGpuMeshes` waits for the GPU, `VulkanBuffer::Destroy` clears handles, and Character Editor open binds the preview once.
- 2026-09-03: Record the missing `extern/ufbx` gitlink at v0.23.0 so Git clones the submodule instead of treating its checkout as untracked files.
- 2026-09-03: glTF import now bakes node TRS (rotation/scale/translation and parent chains) into mesh vertices, so builtin assets such as Capsule.glb stand upright. Camera.glb is reoriented upright with its lens along identity camera forward (+X) for POV actor placement. `ModelImporterTests` covers node baking plus Capsule/Camera orientation.
- 2026-09-03: Mesh Renderer (and Audio/Lua inspectors) no longer report `.engine/` asset paths as missing when the file exists at the project root; missing checks now resolve portable keys via `AssetPathPolicy` instead of `assets/`.
- 2026-09-03: Directional cascaded shadows now use a cascade-proportional depth range (so default bias no longer swallows contact shadows), world transforms for casters, and no shadow-pass culling; `triangle.vert` lighting UBO matches the fragment layout.
- 2026-09-02: Project Settings Apply now refreshes the editor's build-settings cache, and input-profile references participate in asset dependency tracking and safe path rewrites.
- 2026-09-02: Packaged runtime resize now preserves configured VSync and routes swapchain recreation through the shared renderer lifecycle.
- 2026-09-02: Qt Build Project now persists VSync defaults and reports unused assets as non-destructive dynamic-loading advisories.
- 2026-09-02: Runtime discovery now distinguishes source checkouts from distributed Editors, keeps artifacts from one deterministic runtime root, reports Development-to-Release fallback, skips compiler probing when a prebuilt player exists, and detects locked package binaries before staging.
- 2026-09-02: Packaged runtime accepts either Lua DLL name, treats `game.ini` as the authoritative package marker, avoids source-tree Lua fallbacks, and applies the configured VSync present mode.
- 2026-09-02: Packaging no longer deletes the last successful output before completion, and application-icon rebuilds restore the shared generic runtime afterward.
- 2026-09-02: Details Delete uses the full selection; Pin Inspector updates immediately and reports missing pinned entities; selection no longer disables Outliner repainting or gets cleared during rebuilds; Inspector refresh no longer creates edits or dirties the scene; component gestures now close their undo-coalescing sessions; prefab Apply controls correctly re-enable when returning to one instance.
- 2026-09-02: Transform snap controls now affect gizmo translation, rotation, and scale with Ctrl bypass; Escape restores and discards active gizmo/placement edits; focusing a selection no longer initializes editor physics as a hidden side effect.
- 2026-09-01: `TextureDiscovery.cpp` include path for `AssetPathPolicy.h` corrected (`../../Assets/`).
- 2026-09-01: `SceneSerializer::DeserializeFromString` uses a unique temp file per call instead of a shared `hephaestus_snapshot.hpscene` path, avoiding concurrent clobber races.
- 2026-09-01: Closing the main editor window now prompts to save dirty scenes; play-mode scene cameras no longer double-convert radians; Details visibility toggles `MeshRendererComponent.visible`; Settings toolbar opens real settings; gizmo drag/rename/light/environment edits push undo; undo/redo actions enable/disable correctly.
- 2026-09-01: Dynamic Jolt bodies now use swept linear-cast collision quality, and the FPS character motor clears downward velocity while grounded, preventing intermittent capsule tunneling and contact-driven floor penetration during movement.
- 2026-09-01: Boomer-Shooter Player keeps its rotationally symmetric physics capsule upright instead of rotating the body for camera yaw, and aligns the collider/visual bottoms to the entity origin to prevent ground clipping and restore grounded probes.
- 2026-09-01: Runtime HUD rendering in the Qt editor now uses an owned frameless translucent tool overlay aligned to the Vulkan viewport, avoiding native-child stacking that hid non-native UI widgets while preserving transparent backgrounds.
- 2026-09-01: Qt editor play mode now samples captured cursor displacement directly each script frame, preserving FPS mouse look when transparent runtime HUD widgets prevent the embedded native viewport from receiving mouse-move events.
- 2026-09-01: Runtime prefab spawn variables are injected before Lua chunk execution so top-level local defaults honor overrides, and newly instantiated renderers reuse matching uploaded mesh handles so Boomer-Shooter enemies are visible immediately.
- 2026-09-01: Qt play-mode HUD routing now keeps mouse events on the viewport in Game cursor mode, suspended capture survives embedded-window focus changes, script camera callbacks are rebound on every play session, and Player camera rotation no longer depends on transform-position lookup.
- 2026-09-01: Boomer-Shooter play mode now consumes accumulated Qt mouse motion for reliable FPS look, uses Jolt allowed-DOF constraints for true `freezeRotation`, spawns built-in capsule/cylinder meshes upright, restores the Player prefab as an upright capsule controller, and excludes `EnemySpawn*` markers from Director live-enemy counts.
- 2026-09-01: Editor play mode captures the cursor whenever HephaestusUI cursor mode is `Game` (not only when `cursorModeRequest` is set), so FPS mouse look works after `UI.setCursorMode(CursorMode.Game)`.
- 2026-09-01: Prefab load/save and scene deserialize resolve portable paths (`assets/prefabs/...`) against the project root instead of the process CWD, so dragged prefab instances no longer become Missing Prefab stubs.
- 2026-09-01: Runtime HUD overlay no longer forces a native HWND (which composited opaque on Windows). Transparent panel fills are skipped when painting, and `UIPanel` defaults to a transparent background.
- 2026-09-01: Escape exits editor play mode and releases viewport cursor capture so the Stop button is reachable again. Overlay mouse routing only enables in UI cursor mode.
- 2026-09-01: Prefabs can be placed into the active scene by dragging from the Asset Browser or Add Actors → Prefabs onto the viewport (also via Asset Browser context menu **Instantiate in Scene**).
- 2026-08-31: Include Jolt `BodyFilter.h` in `JoltPhysicsSystem.cpp` for `IgnoreSingleBodyFilter` used by centralized raycasts.
- 2026-08-28: Prefab instance transforms and component edits in the Scene Editor are recorded as overrides (or as instance `rootTransform` for the root) so they survive save/reload and Refresh.

### Internal

- 2026-10-07: AutomationBackend gains Qt-free runtime UI and physics observation virtuals. UI clicks carry a token so a timeout can drop only those synthetic events. Physics snapshots are defined as fully-read bodies only.

## [2026.8.27]

### Added

- 2026-08-27: Editor writes `hephaestus.log` next to `HephaestusEngine.exe` at startup. The log captures environment diagnostics (GPU, Qt plugins, Vulkan loader, command line) and mirrors all console output so a black-terminal / no-window launch can be diagnosed from disk.
- 2026-08-25: Mesh Renderer inspector shows a Show Hidden Outline checkbox when Visible is off. Checked objects still draw an editor outline and can be clicked in the viewport; unchecked objects stay fully hidden and unpickable.
- 2026-08-25: Added root `build.bat` for Windows one-command CMake configure and build (Release by default, with debug/clean/reconfigure options and automatic EnTT setup).
- 2026-04-26: Expanded the Qt Material Editor node graph with metadata-driven node definitions, scalar-to-vector connection compatibility, a GLSL snippet generator, new math/utility/texture/procedural/logic/blend/input/parameter nodes, expanded PBR output fields, and node reference documentation.
- 2026-04-25: Added basic HDRI/environment reflections for PBR materials, including renderer descriptor binding support, global environment reflection controls, per-material reflection strength, documentation, and an `assets/scenes/EnvironmentReflectionTest.hpscene` test scene.
- 2026-04-25: Material editor preview now defaults to `.engine/hdris/studio_garden_1k.hdr` as an HDRI environment and exposes preview controls to choose or restore the HDRI. New and loaded projects seed this HDRI into the project `.engine` folder from the engine runtime assets.
- 2026-04-13: Wired live draw-call count and process RAM usage into the editor status bar and viewport overlay. `EngineFrameRenderer` now tracks per-frame `FrameStats` (draw calls, meshes drawn, entities skipped) and exposes them via `GetLastFrameStats()`. Process working-set memory is read via platform APIs (Win32 `GetProcessMemoryInfo`, macOS `task_info`, Linux `/proc/self/statm`).
- 2026-04-13: Centralized app details (version, name, description, copyright, website) in root `CMakeLists.txt`. CMake `configure_file` generates `AppConfig.h` so every target (editor, game, launcher, tools) reads the same values. Removed all hardcoded `"1.0.0"` and `"Hephaestus Engine"` literals from C++ sources.
- 2026-04-13: Added `AboutDialog` (Qt) accessible via Help → About on Windows/Linux. On macOS the same action is automatically placed in the native application menu ("About Hephaestus Engine") via `QAction::AboutRole`. The dialog shows app name, version, description, tech stack, copyright, and a clickable website link — all sourced from `AppConfig.h`.
- 2026-04-13: Expanded `VersionInfo` API with `AppName()`, `AppDescription()`, `Organization()`, `Domain()`, `Website()`, and `Copyright()` accessors backed by the generated config header.
- 2026-04-13: Added Windows `VERSIONINFO` resource block to `HephaestusEngine.rc.in` so the executable's Properties dialog shows the correct product name, version, and copyright sourced from CMake.
- 2026-04-13: Set macOS bundle metadata (`MACOSX_BUNDLE_SHORT_VERSION_STRING`, `MACOSX_BUNDLE_BUNDLE_VERSION`, `MACOSX_BUNDLE_COPYRIGHT`) from the centralized CMake variables.
- 2026-04-13: Viewport multi-select: Ctrl+click toggles entities in/out of the selection, Shift+click adds to the selection. Selection outlines now render for all selected entities (not just the primary). `FrameRenderConfig::selectedEntities` vector carries the full set to the renderer each frame.
- 2026-04-03: Qt editor outliner supports extended multi-selection: Shift+click selects a contiguous range in hierarchy order; Ctrl+click toggles items. Scene `SelectionComponent` state and `EditorContext::SelectEntities` / `GetSelectedEntities` track the full set; the current (focused) row is the primary for details and viewport outline until viewport multi-select is added.
- 2026-03-30: Added `Documentation/HEPHAESTUS_ENGINE_WEBSITE.md`, a public-facing overview for the project website covering vision, core tech (C++/Vulkan/Qt/Lua/Jolt), architecture, editor, HephaestusUI (`.hui`, `display.ui` / `destroy.ui`), rendering capabilities, strengths, limitations, use cases, roadmap, and design philosophy.
- 2026-03-27: Added `display.ui(path)` and `destroy.ui(target)` Lua global APIs for managed `.hui` lifecycle. `display.ui` loads a `.hui` file, builds the UI, auto-loads the controller, tracks the instance, and returns a handle. `destroy.ui` accepts a handle, `"current"`, or `"all"` to cleanly remove UI with full callback/resource cleanup.
- 2026-03-27: Added HUI instance lifecycle management: duplicate path detection (re-display replaces the previous instance), a current-UI stack for `destroy.ui("current")`, automatic cleanup of all managed HUI instances on simulation stop, and safe no-op behavior for destroying already-destroyed handles.
- 2026-03-27: Added `display` and `destroy` global Lua tables registered in `RegisterDisplayDestroyBindings()` alongside existing binding registration.
- 2026-03-27: Updated `assets/ui/MainMenu.lua` example to demonstrate the `display.ui` / `destroy.ui` workflow with path constants and handle-based lifecycle control.
- 2026-03-27: Added `Documentation/Scripting/LUA_DISPLAY_DESTROY_API.md` with complete API reference covering display/destroy signatures, lifecycle rules, current-UI stack, error messages, recommended script patterns, and future expansion notes.
- 2026-03-27: Updated `Documentation/Features/HUI_FORMAT_REFERENCE.md` Lua integration and examples sections to use `display.ui` / `destroy.ui` as the recommended API, documenting `UI.loadHUI` as the lower-level alternative.
- 2026-03-27: Updated `Documentation/Scripting/SCRIPTING_DOCUMENTATION_INDEX.md` to reference the new display/destroy API documentation.
- 2026-03-27: Added `assets/ui/PauseMenu.hui` and `assets/ui/PauseMenu.lua` example demonstrating Escape-toggle pause overlay with play-time tracking, resume/main-menu/quit buttons, runtime widget mutation, and layered `display.ui`/`destroy.ui` lifecycle.

### Fixed

- 2026-08-27: Editor main window is shown before Vulkan initialization. A hang or GPU/driver failure during `vkCreateInstance` no longer leaves only a console window with no UI.
- 2026-08-25: Fixed RMB look jittering back toward the middle while turning. The hidden cursor is only recentered at the viewport edges, and warp-generated moves no longer update the look baseline or get applied as opposite mouse motion.
- 2026-08-25: Fixed RMB look slamming the camera straight up and spinning. Captured look now uses last-position deltas, Qt DPI-aware cursor warping, and ignores teleport-sized samples so a recenter cannot be applied as mouse motion.
- 2026-08-25: Restored RMB fly-look in the viewport. Capture no longer waits for a warp-settle that never matched Qt mouse coordinates (which discarded every look delta).
- 2026-08-25: Fixed editor viewport camera jumping when right-clicking to look. Cursor capture now waits for the recenter warp to settle and ignores stale click-position mouse moves so the click-to-center teleport is not applied as look.
- 2026-08-25: Hidden-object editor outlines now depth-mask the unexpanded mesh before the inverted-hull pass, so they render as a silhouette instead of filled inverted faces.
- 2026-08-25: Fixed play-mode / FPS mouse look being clamped to the viewport rectangle. Captured cursor now recenters with virtual unbounded coordinates so look is not limited by viewport size, and play mode no longer releases capture on the next tick while RMB or `Input.setCursorMode("disabled")` is active.
- 2026-04-13: Refined viewport entity picking for responsiveness and accuracy. Eliminated the 2-frame readback delay by immediately waiting on the GPU fence after submitting the pick copy (only stalls on click frames). Replaced single-pixel sampling with a 5x5 region sample that picks the entity closest to the click center, making edge clicks far more forgiving. Changed pick coordinates to use the mouse-press position instead of the mouse-release position, avoiding jitter drift. Rounding coordinates with `+0.5f` before truncation fixes off-by-one on DPI-scaled displays.
- 2026-04-13: Fixed viewport entity picking (click-to-select) being completely non-functional. The `entityid.vert` and `entityid.frag` shaders were missing from the CMake `SHADER_SOURCES` list, so they were never compiled to SPIR-V; `createIdPickerResources()` silently failed and `IsPickingAvailable()` returned false, disabling all viewport selection. Also fixed ID picker resources not being recreated on viewport resize, which would have caused stale dimensions and incorrect pick coordinates after resizing.
- 2026-04-03: Qt editor gizmo translate/rotate/scale now applies the same world-space delta to every entity in the current selection (using the primary entity's gizmo result), with parent-before-child write order so nested selections stay consistent.
- 2026-03-30: Fixed Qt play mode showing a flat gray viewport when no runtime UI exists. `RuntimeUIOverlayWidget` is now only shown (and mouse-enabled) when runtime UI draw commands are present, so empty projects render the scene/background instead of an empty overlay surface.
- 2026-03-30: Fixed missing per-swapchain-image fence tracking in `EngineFrameRenderer::RenderFrame`. The renderer now waits on `m_imagesInFlight[imageIndex]` before reusing image command buffers/resources, preventing GPU races that can lead to `VK_ERROR_DEVICE_LOST` on submit.
- 2026-03-30: Fixed unsafe material texture descriptor updates during rendering. `EngineFrameRenderer` now defers texture-slot descriptor writes per swapchain image and applies them only after that image is acquired, avoiding updates to descriptor sets that may still be in flight.
- 2026-03-30: Fixed Qt editor render-loop lockup after `VK_ERROR_DEVICE_LOST` by marking device-lost as fatal in `EngineFrameRenderer` and stopping swapchain recreation retries in `QtEditorEngine`. The editor now emits a clear error and halts rendering instead of repeatedly recreating the swapchain.
- 2026-03-30: Fixed `VK_ERROR_DEVICE_LOST` crash when loading scenes with complex meshes. The entity ID picker pipeline declared a vertex stride of 32 bytes (`pos+normal+uv`) instead of the correct 44 bytes (`sizeof(Vertex)`, which includes the `color[3]` field). This caused the GPU to read vertex data at wrong offsets, eventually accessing memory past the end of vertex buffers and crashing the device. Fixed stride and attribute offsets to use `sizeof(Vertex)` and `offsetof` for correctness.
- 2026-03-30: Fixed infinite "Failed to submit draw command buffer" loop after swapchain recreation. Sync objects (semaphores/fences) are now fully recreated when handling a dirty swapchain, preventing corrupted state from a prior failed frame from poisoning all subsequent frames.
- 2026-03-30: Fixed `VulkanSwapchain::initInternal` to query `VkSurfaceCapabilitiesKHR` and use `currentTransform`, clamped extents, validated `minImageCount`, and a supported `compositeAlpha` instead of hard-coded values that may be unsupported on certain surface/driver combinations.
- 2026-03-30: Added VkResult diagnostic output to `vkQueueSubmit` and `vkEndCommandBuffer` failures in `EngineFrameRenderer` for easier future debugging.
- 2026-03-27: Updated `assets/ui/MainMenu.lua` so attaching the script directly in play mode now opens the menu from `onAwake()` instead of only defining helper functions. Added a guard to avoid duplicate `display.ui()` calls when `showMainMenu()` is triggered more than once.
- 2026-03-27: Restructured `PauseMenu.hui` to use a direct `root Panel → VerticalBox` hierarchy instead of nesting through an intermediate `dialog_box` Panel. The VerticalBox now owns the dialog styling (background, corner radius, border) and Center anchor directly, matching the proven `MainMenu.hui` layout pattern and fixing an issue where nested Panel containers could prevent child widgets from rendering.
- 2026-03-27: Introduced `.hui` (Hephaestus UI) as a first-class asset format for declarative UI layout definition. `.hui` files use a JSON-based structure to describe widget hierarchy, types, properties, layout, styling, and optional Lua controller references — separating UI presentation from behavioral logic.
- 2026-03-27: Added `HUIParser` and `HUIBuilder` in `engine/UI/HephaestusUI/HUIAsset.h/.cpp` for parsing `.hui` files from disk or string, validating widget types/IDs/properties, and constructing runtime `UIScreen` widget trees with an id-to-element lookup map.
- 2026-03-27: Registered `UILayout` as a new `AssetType` in `AssetTypes.h` with `.hui` extension mapping, `ToString`/`AssetTypeFromString`/`AssetTypeFromExtension` support, and editor display integration (filter combo, type badge, icon).
- 2026-03-27: Added `UI.loadHUI(path)` Lua binding to load a `.hui` file, build a UI screen, register all widget handles, and optionally auto-load the referenced controller Lua script. Returns `(screenHandle, nil)` on success or `(nil, errorString)` on failure.
- 2026-03-27: Added `UI.getWidget(screenHandle, widgetId)` Lua binding to look up widget element handles by string ID within a loaded `.hui` screen, enabling Lua scripts to bind events and modify widgets without procedural construction.
- 2026-03-27: Added "New UI File" action to the Asset Browser right-click context menu (`showCreateMenu`), creating a valid `.hui` starter template with a root panel, vertical layout, title label, and two styled buttons.
- 2026-03-27: Added `HUIAsset.cpp` to both editor and game CMake targets.
- 2026-03-27: Added `.hui` opening support in `EditorAssetActions` (opens in external editor like scripts).
- 2026-03-27: Added `generateDefaultHUIContent()` and `serializeHUIDocument()` utility functions for template generation and deterministic round-trip serialization.
- 2026-03-27: Added example `assets/ui/MainMenu.hui` and `assets/ui/MainMenu.lua` demonstrating the full `.hui` workflow: declarative layout with styled buttons, paired with a Lua controller that binds click handlers.
- 2026-03-27: Added comprehensive `.hui` format documentation at `Documentation/Features/HUI_FORMAT_REFERENCE.md` covering file structure, widget types, layout system, styling, Lua integration, asset workflow, examples, best practices, error handling, and future expansion notes.

- 2026-03-25: Added right-click context menu to `AssetBrowserWidget` on both the contents view and folder tree. Supports creating new folders, Lua scripts, scenes (`.hpscene`), and materials (`.hematerial`). Also supports rename, delete, and "Show in Explorer" for existing assets.
- 2026-03-24: Expanded `UIStyle` with comprehensive per-widget visual properties: `fontFamily`, `fontSize`, `fontBold`, `fontItalic`, `textAlignment`, `opacity`, `scale`, `backgroundImageId`, `imageScaleMode`, `imageTint`, `textShadow`, `textShadowColor`, `textShadowOffset`.
- 2026-03-24: Added `UIHAlignment`, `UIVAlignment`, and `UIImageScaleMode` enums to `UIStyle.h` for layout alignment and image scaling control.
- 2026-03-24: Added per-element `enabled/disabled` state, `minSize`/`maxSize` constraints, horizontal/vertical alignment, and owned style support to `UIElement`.
- 2026-03-24: Implemented anchor-based layout in `UIElement::UpdateLayout()` — all `UIAnchorPreset` values (TopLeft through Stretch) now compute layout positions correctly.
- 2026-03-24: Expanded `UIButton` with `Focused` and `Disabled` visual states, per-state style getters, `SetFocused(bool)`, and `SetOnHovered` callback.
- 2026-03-24: Added `UITheme` named style registry with `SetStyle`/`GetStyle`/`HasStyle` and `CreateDefaultTheme()` factory providing preset styles (`default_button`, `primary_button`, `danger_button`, `title_text`, `heading_text`, `body_text`, `label_text`, `panel_background`, `menu_container`, `overlay_background`).
- 2026-03-24: Integrated `UITheme` into `UISystem` (initialized with default theme) for centralized style management.
- 2026-03-24: Added `UIGridLayout` container widget with configurable columns, horizontal/vertical spacing, and per-cell alignment.
- 2026-03-24: Added child alignment support to `UIVerticalBox` (`SetChildHAlignment`) and `UIHorizontalBox` (`SetChildVAlignment`) for controlling how children are positioned within the cross-axis.
- 2026-03-24: Expanded `UIDrawTextCommand` with `bold`, `italic`, `shadow`, `shadowColor`, `shadowOffset` fields for richer text rendering.
- 2026-03-24: Expanded `UIDrawRectCommand` with `backgroundImageId`, `imageScaleMode`, `imageTint` for image-backed rectangles.
- 2026-03-24: Expanded `UIDrawImageCommand` with `scaleMode` field.
- 2026-03-24: Added text shadow rendering support in `RuntimeUIOverlayWidget` QPainter backend (draws shadow pass before main text).
- 2026-03-24: Added bold/italic font support in `RuntimeUIOverlayWidget` QPainter text rendering.
- 2026-03-24: Added 30+ new Lua UI bindings: `setStyle`, `setButtonStyle`, `setFocused`, `registerThemeStyle`, `applyThemeStyle`, `setEnabled`, `setPadding`, `setMargin`, `setMinSize`, `setMaxSize`, `setAnchor`, `setAlignment`, `setChildAlignment`, `setBackgroundColor`, `setBorderColor`, `setBorderWidth`, `setCornerRadius`, `setTextColor`, `setFontSize`, `setFontBold`, `setFontItalic`, `setTextAlignment`, `setTextShadow`, `setWordWrap`, `setOnHover`, `createGrid`, `setGridColumns`, `setGridSpacing`, `setImageScaleMode`, `setImageTint`.
- 2026-03-24: Added `ParseStyleTable` helper for Lua table-to-UIStyle conversion, supporting all style properties including colors as `{r,g,b,a}` tables and spacing as uniform float or `{left,top,right,bottom}` tables.
- 2026-03-24: Added Lua enum constant tables: `UIAnchor` (TopLeft..Stretch), `UIAlign` (Left/Center/Right/Top/Bottom/Stretch), `UIScaleMode` (Stretch/Fit/Fill/Center).
- 2026-03-24: Rewrote `scripts/mainMenu.lua` to demonstrate the new styled UI system: centered panel with rounded corners, semi-transparent overlay, styled title with text shadow, per-state button styling (normal/hovered/pressed/focused/disabled), keyboard focus visuals, hover callbacks, and modern visual hierarchy.
- 2026-03-24: Added `CursorMode` enum (`Game` / `UI`) to `UISystem` for high-level cursor/input ownership switching between gameplay and UI interaction.
- 2026-03-24: Added Lua `UI.setCursorMode(mode)` and `UI.getCursorMode()` bindings plus global `CursorMode` constants table (`CursorMode.Game`, `CursorMode.UI`) for script-driven cursor mode control.
- 2026-03-24: Wired cursor mode into Qt editor play mode: editor camera controller is suppressed (mouse press/release/move/wheel) when runtime UI is in `CursorMode::UI` to prevent fly-mode interference with UI interaction.
- 2026-03-24: Wired cursor mode into GLFW game runtime via direct `glfwSetInputMode` calls from the Lua binding, with raw mouse motion toggling.
- 2026-03-24: Cursor mode automatically resets to `Game` when play mode stops in the editor.
- 2026-03-24: Updated `scripts/pauseUI.lua` example to demonstrate `UI.setCursorMode` / `CursorMode` API for pause menu toggle with proper cursor lock/unlock. Menu starts visible in UI mode on play so the UI is immediately shown with a free cursor.
- 2026-03-24: Documented cursor mode API in `LUA_UI_API_REFERENCE.md`, `LUA_SCRIPTING_GUIDE.md`, and `HEPHAESTUS_UI_RUNTIME_GUIDE.md`.
- 2026-03-24: Added `scripts/mainMenu.lua`, a basic Lua-driven runtime main menu test with keyboard navigation, cursor mode switching, start/hide behavior, and `Application.quit()` integration.
- 2026-03-24: Added `UI.setOnClick(buttonHandle, callback)` Lua binding for registering mouse click callbacks on UIButton elements. Callbacks are stored as Lua registry references and invoked when buttons are clicked.

### Fixed

- 2026-03-24: Fixed Lua scripts not receiving mouse input during Qt editor play mode. The `RuntimeUIOverlayWidget` now directly captures mouse events (press/release/move) and forwards them to the `UIContext` via `QtEditorEngine::ForwardOverlayMouseEvent()`, bypassing the viewport's focus-based input filtering.
- 2026-03-24: Fixed Lua scripts not receiving keyboard input during Qt editor play mode when the viewport window didn't have direct focus. Key events are now forwarded from `EditorMainWindow` to the engine's `RuntimeInputState` during play mode, enabling UI menu navigation (W/S/Up/Down/Enter) even when another Qt widget has focus.
- 2026-03-24: Fixed `Input.setCursorMode` and `UI.setCursorMode` calls made during `onAwake` being silently dropped in the Qt editor because `RuntimeInputState.valid` was not yet true. The `cursorModeRequest` is now always written so the host picks it up on the first tick.
- 2026-03-17: Added initial `HephaestusUI` runtime retained-mode UI module with `UISystem`, `UIContext`, `UIScreen`, base `UIElement` tree, layout containers (`UICanvas`, `UIVerticalBox`, `UIHorizontalBox`), widgets (`UIPanel`, `UIText`, `UIImage`, `UIButton`), command-list based `UIRenderer`, `UIInputRouter`, `UIFont` registry scaffolding, `UIStyle`, and a code example for building a simple menu screen.
- 2026-03-17: Connected runtime UI to Lua scripting via a new `UI` binding surface (`isAvailable`, `createSimpleMenu`, `clearScreens`, `getScreenCount`, `setScreenVisible`, `setScreenActive`, `setViewportSize`, `getDrawCommandCount`) and wired `HephaestusGame` to update UI input/layout each frame through `UISystem`/`UIContext`.
- 2026-03-17: Expanded Lua runtime UI API with handle-based widget creation and mutation (`createScreen`, `getRoot`, `createPanel`, `createText`, `createButton`, `createImage`, `createVerticalBox`, `createHorizontalBox`, `setPosition`, `setSize`, `setVisible`, `setOpacity`, `setZOrder`, `setText`, `setImage`, `setSpacing`) plus internal UI handle lifecycle tracking.
- 2026-03-17: Added full HephaestusUI documentation including runtime architecture/lifecycle/render-command integration guide and complete Lua UI API reference, with links added in Features and Scripting documentation indexes.
- 2026-03-17: Added a dedicated script-only guide for creating and displaying custom runtime UI without engine source access (`Documentation/Guides/NO_SOURCE_RUNTIME_UI_GUIDE.md`) and linked it from the Guides index and HephaestusUI runtime guide.
- 2026-03-17: Wired HephaestusUI into Qt editor play mode by binding editor Lua runtime to `UISystem`/`UIContext`/`UIRenderer`, forwarding viewport mouse input to UI routing, ticking UI per frame, and adding a viewport overlay widget to visualize runtime UI draw commands during editor play testing.

#### Multi-Selection Support in Outliner (2026-01-29)

**Feature**: Users can now select multiple actors in the World Outliner and viewport using 
standard multi-selection controls. This enables batch operations on multiple entities.

**Key Features**:

1. **Ctrl+Click (Cmd+Click on macOS)**: Toggle individual selection - add or remove an 
   entity from the current selection without affecting other selected entities.

2. **Shift+Click**: Range selection in the Outliner - select all entities between the 
   last clicked entity and the current entity in the visible tree order.

3. **Viewport Multi-Selection**: Ctrl+Click on objects in the 3D viewport to add/remove 
   them from the selection.

4. **Synchronized Selection**: Selection state is synchronized between the Outliner, 
   viewport, and Details panel. All selected entities show selection outlines.

5. **Multi-Entity Transform Editing**: When multiple entities are selected, transform 
   changes in the Details panel (position, rotation, scale) apply to all selected entities.

6. **Gizmo Multi-Transform**: Using the viewport gizmo to move/rotate/scale affects all 
   selected entities simultaneously, maintaining their relative positions.

**Changes Made**:

1. **HtmlEditorUIHost.cpp**:
   - Added `scene.range_select` handler for Shift+Click range selection
   - Handles comma-separated entity IDs for batch selection

2. **outliner.html**:
   - Added tracking of visible entity order for range selection
   - Implemented Shift+Click range selection logic
   - Enhanced Ctrl+Click to properly toggle selection
   - Updated status bar to show selection feedback

3. **main.cpp**:
   - Viewport picking now supports Ctrl+Click for multi-selection toggle
   - Gizmo transformations apply to all selected entities using delta transformation
   - Selection outline rendering updated to draw outlines for all selected entities
   - ImGui WorldOutliner updated with Ctrl+Click support

4. **Scene.cpp** (existing infrastructure):
   - `GetSelectedEntities()` returns all selected entities
   - `AddSelectedEntity()` adds to multi-selection
   - `ToggleSelectedEntity()` toggles individual selection
   - `ClearSelection()` clears all selections

**Backward Compatibility**: Single-selection behavior remains unchanged. Clicking without 
modifiers still replaces the current selection with the clicked entity.

### Fixed

#### Multi-Material Mesh Rendering in Game Runtime (2026-01-28)

**Problem**: Meshes with multiple material indices (multiple submeshes/sections) were not 
rendering correctly in the exported/standalone game runtime. Only the first material would 
show, or sections would fallback to a default material, even though they rendered correctly 
in the editor.

**Root Cause**: 
1. When loading meshes at runtime, the `MeshRendererComponent::submeshCount` was not being 
   updated from the loaded mesh data, causing a mismatch between the mesh's actual submesh 
   count and the component's tracked count.
2. The runtime was properly storing submesh data in `MeshGPU::submeshes` but the renderer 
   component wasn't synchronized.

**Changes Made**:

1. **GameMain.cpp - Mesh Loading** (`LoadMeshToGPU` integration):
   - After loading a mesh from file, now properly updates `renderer.submeshCount` from the 
     loaded `MeshGPU::submeshes` array.
   - Added debug logging showing mesh name, submesh count, and material slot count.
   - Added warning when submesh count doesn't match material slot count.
   - Same synchronization added for cached mesh reuse path.

2. **GameMain.cpp - Debug Logging** (Debug builds only):
   - Added comprehensive multi-material debug info section before the main render loop.
   - Logs each entity's mesh name, handle, submesh count, and material slot count.
   - For each submesh, logs the material slot index, index range, material path, and 
     whether the material was successfully loaded.
   - Warns when there are more submeshes than material slots.

3. **GameMain.cpp - Runtime Asset Path Resolution**:
    - `NormalizeAssetPath()` now resolves relative material/texture paths under 
       the runtime `assets/` directory if the direct runtime-root path doesn't exist.
    - Fixes cases where editor-stored paths like `materials/rock.hpmat` were 
       not found at runtime (expected under `assets/materials/...`).

4. **SceneExporter.cpp - Export Material Slot Assets**:
    - Export now includes material assets referenced by mesh slot overrides
       (`MeshRendererComponent::appliedMaterialPaths`), ensuring they are copied
       into the runtime `assets/` folder.

5. **GameMain.cpp - World-Aligned Material Graph Support (Runtime)**:
    - Runtime material loader now detects `WorldAlignedTexture` nodes in the
       material graph and enables world-aligned UVs, texture binding, and
       world texture scale without requiring the editor-side graph evaluator.

3. **Test Scene**:
   - Added `assets/scenes/MultiMaterialRuntimeTest.hpscene` to test:
     - Single material meshes (should render normally)
     - Override material tests (material should apply correctly)
     - Missing/fallback material tests (should use entity's MaterialComponent)

**New Debug Warnings** (in debug builds):
- "Mesh 'X' has N submeshes but M material paths assigned. Some sections may use fallback materials."
- Per-submesh material resolution status: "loaded", "NOT FOUND", "default", or "slot missing"

**Acceptance Criteria**:
1. ✅ Multi-material meshes render identically in editor and runtime
2. ✅ Each submesh uses its assigned material slot
3. ✅ If a material slot is missing, only that section falls back to default
4. ✅ Blueprint/scene overrides for per-slot materials work in runtime
5. ✅ Single-material meshes continue to work without regression

---
