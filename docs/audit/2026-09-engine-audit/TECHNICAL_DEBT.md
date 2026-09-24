# Technical Debt

Items are ranked by urgency. Each entry lists impact, risk, affected systems, recommended action, and suggested timing.

---

## 1. Raw EnTT handles as durable entity identity

- **Impact:** References stored as raw 32-bit EnTT handles (`SceneSerializer.cpp:694`). Prefab-instance roots get fresh IDs on load; prefab-over-prefab parent links are lost; any save/level-transition system built today would corrupt on reload.
- **Risk:** High — silent data corruption in any persistence feature built on top.
- **Affected:** Scene serialization, prefabs, save/load (future), Lua `Scene.instantiate`/`destroy`, physics `entityId`.
- **Recommended action:** Introduce a stable per-entity GUID (or a stable instance-ID for prefab roots) maintained in a `StableIdComponent`, mapped to/from EnTT handles at serialization boundaries; resolve `prefabGuid` by GUID instead of path.
- **Timing:** Before or alongside save/load (Phase 1/2).

## 2. Packaged runtime never renders HUI

- **Impact:** The entire runtime UI framework is editor-only; shipped games have no menus/HUD/dialogue. (Feature gap C1, but also architectural debt: the command-list renderer `UIRenderer` has no consumer outside Qt.)
- **Risk:** High — blocks production; grows stale as editor-only code diverges.
- **Affected:** HephaestusUI, GameMain, EngineFrameRenderer.
- **Recommended action:** Add a Vulkan UI pass that consumes `UIRenderer` draw commands; implement text atlas and image/texture binding; remove the QPainter-only dependency for packaged builds.
- **Timing:** Phase 1.

## 3. `GameMain.cpp` monolith duplicates editor host logic

- **Impact:** `GameMain.cpp` (1,513 lines) hand-reimplements mesh upload, Lua wiring, physics bridging, input polling, and audio/sys update. Divergent mesh-upload strategies (editor `CPU_TO_GPU` vs game staging + `GPU_ONLY`); ~6 duplicated shader-loader lambdas; CSM matrices computed twice per frame.
- **Risk:** Medium-High — editor and game behaviour can (and will) diverge.
- **Affected:** Game runtime, editor play mode, renderer.
- **Recommended action:** Extract a shared runtime host (frame orchestration, mesh/texture upload, per-frame systems) used by both `QtEditorEngine` and `GameMain`; collapse duplicate upload/shader/CSM code.
- **Timing:** Phase 2 (incremental refactor alongside feature work).

## 4. Per-editor viewport hosts duplicated

- **Impact:** Prefab Editor and Character Editor each reimplement viewport setup, engine init, render loop, and scene-mesh rebuild instead of reusing `QtEditorEngine`.
- **Risk:** Medium — inconsistent behaviour, high maintenance.
- **Affected:** `PrefabEditorWindow.cpp`, `CharacterEditorWindow.cpp`.
- **Recommended action:** Extract a shared `EditorEngineWindowBase` (or reuse `QtEditorEngine` for all document editors).
- **Timing:** Phase 2.

## 5. `EditorMainWindow.cpp` god-object (~7,600 lines)

- **Impact:** Menus, outliner, inspector wiring, spawn machinery, build orchestration, recovery, and ~90 methods in one class; duplicated inspector-teardown and viewport-UI-update blocks; a dead `#if 0` CMake build path.
- **Risk:** Medium — merge conflicts, inconsistent paths, slow compile.
- **Affected:** Editor maintainability and plugin/feature work.
- **Recommended action:** Extract dock-window classes (Outliner, Details, Viewport, Build Output) and a single spawn service.
- **Timing:** Phase 2/4 (do not gate production features on it).

## 6. Physics parent-child transform sync broken

- **Impact:** Physics reads/writes local transform fields, never world transforms. Parented bodies misplace at creation and corrupt hierarchy on sync; static bodies under parents also desync.
- **Risk:** High for any hierarchy; blocks platforms/attached weapons/doors.
- **Affected:** JoltPhysicsSystem, Scene.
- **Recommended action:** Use `Scene::GetWorldTransform`/world-to-local conversion for body sync; add parent-awareness to body creation and per-step sync.
- **Timing:** Phase 1 (with kinematic bodies).

## 7. `MeshCollisionSystem` dead code + misleading inspector

- **Impact:** `MeshCollisionSystem` is never instantiated but the Inspector "Contacts: 0" readout and `debugDraw` flags imply features that never run; `MeshColliderComponent.contacts` is never populated.
- **Risk:** Medium — silent misinformation to users.
- **Affected:** Physics inspectors, MeshCollisionSystem.
- **Recommended action:** Either wire `MeshCollisionSystem` into the physics path or remove it; make `debugDraw`/contacts real (or hide them).
- **Timing:** Phase 2.

## 8. Documented Jolt crash during body settling

- **Impact:** A comment records "Physics simulation crashes after ~50-60 frames of body settling" (Jolt 4.1.0-rc1); the exception handler swallows the failure silently.
- **Risk:** High — production crashes on common scenes.
- **Affected:** JoltPhysicsEngine.
- **Recommended action:** Reproduce, fix (likely fixed-step physics + deterministic settings + Jolt upgrade), and add a physics simulation test to CI.
- **Timing:** Phase 1 (before relying on physics for gameplay).

## 9. Physics step is variable-timestep + non-deterministic

- **Impact:** `deltaTime` clamped to 0.033 with 1 collision step; 31-thread job pool makes island ordering non-deterministic.
- **Risk:** Medium-High for gameplay determinism/replay.
- **Affected:** JoltPhysicsEngine, GameMain, play mode.
- **Recommended action:** Fixed-timestep accumulation; evaluate Jolt determinism settings.
- **Timing:** Phase 2.

## 10. Cylinder physics shape is a silent no-op

- **Impact:** `PhysicsShapeType::Cylinder` round-trips through serialization and prefabs but `buildShape` has no case — silently degrades to renderer-bounds box; the inspector never offers it.
- **Risk:** Medium — users believe they have a cylinder collider.
- **Affected:** JoltPhysicsSystem, PhysicsInspectors, serialization.
- **Recommended action:** Implement the cylinder shape (Jolt `CylinderShape`) and surface it in the inspector, or remove the type and migrate.
- **Timing:** Phase 2.

## 11. `.hpscene` version never validated

- **Impact:** `"version":1` is written but never checked on load; future format changes are silently misinterpreted.
- **Risk:** High over time (silent data corruption).
- **Affected:** SceneSerializer, prefab/scene loaders.
- **Recommended action:** Validate schema+version on load with a migration path.
- **Timing:** Phase 2.

## 12. Serialization divergence between file path and string path

- **Impact:** `Serialize()` writes native `ScriptComponent`; `SerializeToString()` (play-mode snapshots, undo) drops it. Editor state baked into `.hpscene` (Selection/Folder) ships in packages.
- **Risk:** Medium — play-stop/undo loses C++ script attachments; editor state leaks to runtime.
- **Affected:** SceneSerializer, PlayModeManager, EditorUndoCommand.
- **Recommended action:** Unify the two paths; gate editor-only components out of runtime serialization.
- **Timing:** Phase 2.

## 13. Lua per-frame overhead and error spam

- **Impact:** Per-frame filesystem `exists`+`last_write_time` stat per script; full entity snapshots to vectors per frame; per-frame heap allocations; broken scripts re-parsed and re-logged every frame (`functionErrorLogged` is dead code); `this` userdata + transform tables allocated per call.
- **Risk:** Medium for script-heavy scenes; log spam.
- **Affected:** LuaScriptEngine.
- **Recommended action:** Cache mtimes (debounced polling), pool/`luaL_ref`-cache userdata, add error backoff.
- **Timing:** Phase 3 (acceptable for small scenes).

## 14. Un-sandboxed Lua

- **Impact:** `luaL_openlibs` exposes `io`/`os`/`package`/`debug`; scripts can read/write any file and execute OS commands.
- **Risk:** High for third-party/moddable content; Low for first-party.
- **Affected:** LuaScriptEngine.
- **Recommended action:** Remove/restrict dangerous libs or provide a documented opt-in; do not break existing scripts.
- **Timing:** Phase 2 (document decision before shipping mod support).

## 15. Settings that do nothing

- **Impact:** Shadow quality, HDR, exposure, gizmo size, `renderDistance`, `shadowNormalBias`, `rotationYaw`, `defaultExposure` are stored/edited but not consumed by the renderer.
- **Risk:** Medium — erodes user trust ("lying controls").
- **Affected:** SettingsSchema, SettingsWindow, EngineFrameRenderer.
- **Recommended action:** Wire each to the renderer or remove from the UI.
- **Timing:** Phase 2.

## 16. Auto-reimport disabled but documented as active

- **Impact:** `FileWatcher` polling loop exists but is never started; `AssetReimporting.md` claims active auto-reimport.
- **Risk:** Medium — designers get stale meshes/materials after external edits.
- **Affected:** EditorAssetPipeline, ReimportManager, docs.
- **Recommended action:** Decide: enable FileWatcher (opt-in) and fix docs, or keep manual and fix docs.
- **Timing:** Phase 2 (low effort).

## 17. Derived-data cache is write-only

- **Impact:** `DerivedDataCache` stores serialized mesh payloads nothing ever reads; runtime re-parses raw `.glb`/`.fbx`/`.obj` at startup; packages ship raw sources (and editor source art) via full-directory copy.
- **Risk:** Medium — startup cost, package size, no authored-format stability.
- **Affected:** DerivedDataCache, ReimportManager, ModelImporter, PackageStager.
- **Recommended action:** Define a compiled runtime mesh format and make the DDC the runtime loader; switch packaging to referenced-asset collection.
- **Timing:** Phase 3.

## 18. Release validation allows absolute-in-project paths

- **Impact:** `ASSET_PATH_ABSOLUTE_IN_PROJECT` is a Warning, so Release packages can ship non-portable references that break on other machines.
- **Risk:** Medium — portability failure that passes validation.
- **Affected:** ProjectValidator, packaging.
- **Recommended action:** Rewrite absolute-in-project paths to portable keys at package time (or promote to Error in Release).
- **Timing:** Phase 2.

## 19. Portability test script broken as-written

- **Impact:** `tools/test_package_portability.ps1` computes `--engine-root` as `build/Release` (which has no `CMakeLists.txt`), so `RuntimeLocator` finds nothing and the script fails; the underlying pipeline works when pointed at the source root (verified).
- **Risk:** Low-Medium — the end-to-end gate is unwired and currently red.
- **Affected:** tooling/docs.
- **Recommended action:** Fix `engine-root` computation; wire the script into CTest/CI.
- **Timing:** Phase 2.

## 20. Plugin settings lookup not owner-scoped

- **Impact:** `EditorPluginHost` in-memory settings match by id only; two plugins registering the same setting id collide for read/write.
- **Risk:** Low-Medium — cross-plugin corruption.
- **Affected:** EditorPluginHost.
- **Recommended action:** Key by owner + id consistently.
- **Timing:** Phase 3.

## 21. No CI

- **Impact:** All 27 CTest tests + the portability script run only when manually invoked; nothing guards regressions; macOS/Linux untested.
- **Risk:** High for long-term reliability.
- **Affected:** Repository health.
- **Recommended action:** GitHub Actions workflow running build + ctest + portability script on Windows (and presets for macOS/Linux once validated).
- **Timing:** Phase 2.

## 22. Locale-sensitive JSON parsing

- **Impact:** `Json.cpp` uses `strtod` (locale-dependent) for numbers while the writer forces classic locale.
- **Risk:** Low-Medium — float misparse in non-C locales.
- **Affected:** Json.cpp, all serializers.
- **Recommended action:** Use locale-independent parsing (`from_chars` / `std::strtod` with classic locale scope).
- **Timing:** Phase 3.

## 23. Global state and singletons

- **Impact:** `LuaScriptEngine::s_Instance`, global entity clipboard, process-local prefab instance counter, static project-root setters, global cameras — nothing is scene/context-scoped.
- **Risk:** Medium for tests and future multi-scene/multi-viewport work.
- **Affected:** Scripting, Scene, PrefabInstantiator.
- **Recommended action:** Scope state to context objects; allow multiple scene roots.
- **Timing:** Phase 3.

## 24. No `VkPipelineCache`

- **Impact:** Pipelines are rebuilt on every swapchain resize and at startup; slow resize and startup.
- **Risk:** Low-Medium (perf/pacing).
- **Affected:** EngineFrameRenderer, VulkanPipeline.
- **Recommended action:** Create + serialize a `VkPipelineCache`.
- **Timing:** Phase 2 (cheap win).

## 25. `#if 0` dead build path, dead components, dead code

- **Impact:** `EditorMainWindow.cpp:5672-5875` retains the retired QProcess/CMake build; `RelationshipComponent` unused; `ScriptComponent` inert; `VulkanImGui.cpp` is 0 bytes; `recordGizmo()` empty; `refraction` unused.
- **Risk:** Low — confusion/maintenance.
- **Affected:** Various.
- **Recommended action:** Remove or mark clearly; add compile-time guards.
- **Timing:** Opportunistic.

## 26. Documentation drift

- **Impact:** README/ROADMAP/Scripting guides describe ImGui era, removed C++ scripting, planned hot-reload, multiple scripts per entity, active auto-reimport; the changelog-advertised Boomer-Shooter starter is absent.
- **Risk:** Medium — on-boarding friction, wrong expectations.
- **Affected:** All docs.
- **Recommended action:** A documentation pass driven by `TEST_AND_BUILD_REPORT.md` classification.
- **Timing:** Phase 2 (documentation tasks, not engine features).