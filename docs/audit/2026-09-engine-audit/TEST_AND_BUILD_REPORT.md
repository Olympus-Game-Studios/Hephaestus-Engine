# Test and Build Report

Revision `26e3326`, built and tested on Windows x64 (MSVC 2022, Release) on 2026-09-24. Vulkan SDK 1.4.328.1, Qt 6.10.2 (MSVC 2022), bundled `extern/lua` (lua54.dll).

---

## 1. Build Status

### Targets built (incremental Release)

| Target | Result |
|---|---|
| `HephaestusEngine` (Qt editor) | **Builds** |
| `HephaestusGame` (packaged player) | **Builds** |
| `HephaestusLauncher` | **Builds** |
| `HephaestusBuild` (CLI) | **Builds** |
| All 27 CTest test executables | **Build** (pre-built; suite run) |

The repository builds cleanly on Windows. No build failures were encountered. Lua is found via the bundled `extern/lua` import lib + DLL path (CMake's `find_package(Lua)` is NOTFOUND but the bundled fallback is used). Qt is auto-located at `C:/Qt/6.10.2/msvc2022_64`.

### Runtime launch verification (this audit)

| Check | Result |
|---|---|
| `HephaestusGame.exe` from `build/Release` loading a scene | **Works** — boots, creates Vulkan device (RTX 5090), loads scene, runs render loop (killed after 10s, expected) |
| Packaged game relocated to a temp dir and run | **Works** — exit code 0; portability log markers present (`STAGE6_LUA_AWAKE`, `STAGE6_HUI_CONTROLLER_LOADED`, `STAGE6_PORTABILITY_COMPLETE`); HDRI resolved from relocated package; engine repo path never appears |
| `HephaestusEngine.exe` (editor) | **Works** — boots, creates Qt window + Vulkan viewport, enters Qt event loop (killed after 15s, expected) |

### Compiler warnings of note

- A runtime Qt warning on editor startup: `QObject::connect(QtEditorEngine, EditorMainWindow): unique connections require a pointer to member function of a QObject subclass` — appears twice, non-fatal, but indicates a Qt API misuse worth fixing.
- Link note: `Jolt.lib ... MSIL .netmodule or module compiled with /GL found; restarting link with /LTCG` — benign.
- The game runtime logs a warning when a referenced material file is missing (expected path-resolution fallback).

### Known environment dependencies

- Requires Vulkan SDK at configure time (`glslc` for shader compilation; falls back to committed `.spv`).
- Qt 6 auto-discovery via `C:/Qt`; MinGW Qt builds are explicitly rejected for MSVC.
- Lua is optional at configure; bundled `extern/lua/lua54.dll` + import lib make it work out of the box here. If absent, `LUA_AVAILABLE=0` and the Lua test (`CharacterLua`) is skipped.
- macOS/Linux presets exist (`CMakePresets.json`) but are **not validated by any CI or test** in this environment; audio is a no-op on non-Windows.

---

## 2. Test Status

### CTest run (Release)

```
27 tests registered
26 passed
1 "failed": ProjectCreationLayout (exit code 0xc0000139)
```

**`ProjectCreationLayout` is a test-harness PATH issue, not a logic failure.** The test links `Qt6::Core` and constructs a `QCoreApplication`. Test binaries live in `build/Release/tests/` while the Qt DLLs are in `build/Release/`, and CTest does not add that to PATH. Running the test executable directly with `build/Release` on PATH passes all assertions (verified). Fix: add the Qt bin dir / `build/Release` to the test environment or copy Qt DLLs next to test binaries.

### Coverage table

| System | Tests Present | Tests Passing | Coverage Quality | Major Missing Tests |
|---|---|---|---|---|
| Asset paths / reference index / ops / recovery | AssetPathPolicy, AssetReferenceIndex, AssetOperationTransaction, RecoveryStore | All pass | Good (genuine integration) | — |
| Prefabs | PrefabSystemTests (11 scenarios) | Pass | Strong, behaviour-based | Nested prefabs (unsupported anyway) |
| Scene authoring / inspector edits | SceneAuthoring, InspectorEdit | Pass | Good (non-Qt layers) | Real Qt widget inspector |
| Project creation | ProjectCreation | Pass w/ PATH fix | Good | — |
| Build / validation / runtime locator | ProjectValidation, RuntimeLocator, BuildService, PackageLayout | Pass | Good | Real compiler-fallback path (only fake builder) |
| Plugin system | PluginSystemTests | Pass | Moderate | **No real plugin DLL is ever loaded**; no Qt plugin host |
| Input | InputSystemTests | Pass | Good (unit) | Gamepad connect/disconnect, editor forwarding, Lua bindings |
| Lua scanner / reconciler | LuaExposeScanner, LuaScriptVariable | Pass | Good (unit) | — |
| Lua runtime / character controllers | CharacterLua (real Lua VM) | Pass | Good; `CharacterRuntime::UploadVisual`/`BindScene` stubbed (GPU) | Hot-reload mtime path, UI/audio/input bindings, sandbox |
| Skeletal import / skinning / playback / workflow / editor-doc / FBX / glTF | SkeletalImport, SkinnedMesh, CharacterPlayback, CharacterWorkflow, CharacterEditor, CharacterAsset, FbxImport, ModelImporter | Pass | **Strong** (real FBX/GLB fixtures, golden matrices) | GPU skinning draw path; Qt Character Editor window |
| **Rendering / GPU** | SkinnedMesh (vertex layout), MaterialCompatibility (GLSL snippet) | Pass | **None meaningful** | No frame loop, pipeline, shadow, swapchain, or device tests |
| **Physics simulation** | None | — | **None** | Gravity, bodies, raycast, CharacterMotor, collisions, determinism, the documented Jolt crash |
| **Editor Qt UI** | EditorQtSmoke (opt-in, trivial `QLineEdit` test) | N/A (OFF) | **None meaningful** | EditorMainWindow/viewport/inspector |
| **Audio** | None | — | **None** | — |
| **Runtime UI rendering** | None (UI sources linked into CharacterLua only) | — | **None** | Draw-command, hit-test, layout |
| **Packaged game end-to-end** | `tools/test_package_portability.ps1` (standalone) | Works when invoked correctly* | Good script | Not wired into CTest/CI; currently broken as-written (`--engine-root` = `build/Release`) |

\* The portability script's `--engine-root` is computed as `build/Release` (no `CMakeLists.txt`), so `RuntimeLocator` finds nothing and the script throws. Passing the source root works (verified manually — package built, relocated, ran, exit 0).

### Test categories

- **Unit:** AssetPathPolicy, Input, LuaExposeScanner, LuaScriptVariable, SkinnedMesh (CPU math), parts of Material/Inspector.
- **Integration/headless:** Prefab, SceneAuthoring, Asset ops, Recovery, Build/Validation/Locator/Package, Plugin, Character/animation/import (fixture-driven).
- **Smoke:** EditorLogicSmoke (recent-list logic is a *mirrored reimplementation*, not the real editor code; deliberately avoids Vulkan).
- **Packaging:** BuildService, PackageLayout, RuntimeLocator, ProjectValidation, ProjectCreation.
- **Portability:** standalone PowerShell script, unwired.
- **Editor Qt:** effectively none.

### Important systems with no meaningful automated coverage

Rendering/GPU, physics simulation, editor Qt UI, audio, and runtime UI rendering. The portability end-to-end gate exists but is unwired and currently red. **There is no CI** (no `.github/workflows/`); ROADMAP's "nightly smoke test in CI" claim is unmet.

---

## 3. Documentation Accuracy

Classification: **Accurate** / **Partially outdated** / **Outdated** / **Missing**. "Historical" files are retained as evidence but describe an earlier engine state.

| Document | Classification | Notes |
|---|---|---|
| `README.md` (root) | **Outdated** | Describes an ImGui/ImGuizmo editor, "exported editor hides UI" packaging (now a generic player), stale Future checklist (lights/shadows/Lua done), stale project layout, C# listed |
| `ROADMAP.md` | **Partially outdated** | Vision doc; status flags wrong (clustered lighting, C#/GPU particles near-term; "save/load runtime persistence complete"; nightly CI absent) |
| `CHANGELOG.md` | **Accurate** | Most reliable doc; matches implementation; **but** the 2026-08-31 "Boomer-Shooter starter content" entry does not exist in the tree |
| `CONTRIBUTING.md` | Partially outdated | Missing Qt6 prerequisite; smoke-test instructions reference only the editor exe |
| `Documentation/Guides/AssetImporting.md` | Accurate | Matches ufbx FBX 1.0 / GLB / character import |
| `Documentation/Guides/CharacterControllers.md` | Accurate | Matches two-slot model, Animator 1.0, tick order |
| `Documentation/Guides/CharacterEditor.md` / `CharacterAssets.md` | Accurate | Match implementation |
| `Documentation/Guides/GameExportGuide.md` / `HeadlessBuilds.md` | Accurate | Match BuildService/CLI |
| `Documentation/Guides/AssetReimporting.md` | **Partially outdated** | Claims auto-reimport active (it is **disabled**); lists .ogg audio reimport |
| `Documentation/Guides/QtEditorBuildNotes.md` | **Outdated** | "UI-only / placeholder viewport"; ImGui-alternative framing |
| `Documentation/Guides/SCRIPT_DEVELOPMENT_GUIDE.md` | **Outdated** | C++-centric; "attach as many scripts as you want" (false); reload-via-rebuild |
| `Documentation/Scripting/README.md` | **Outdated** | "Use C++ and Lua on same entity: Yes!"; C++ as supported path |
| `Documentation/Scripting/SCRIPTING_QUICK_START.md` | Partially outdated | "Attach multiple Lua scripts" inaccurate |
| `Documentation/Scripting/LUA_SCRIPTING_GUIDE.md` | Partially outdated | "C++ scripting removed" (inert but present); hot-reload listed as Planned (implemented); input/Expose sections current |
| `Documentation/Scripting/SESSION_SUMMARY_SCRIPTING.md` | **Outdated** | "No hot-reload yet" |
| `Documentation/Scripting/LUA_INTEGRATION_ROADMAP.md` | **Outdated** | Hot-reload/Input/Scene listed as future |
| `Documentation/Scripting/WHAT_YOU_CAN_DO_NOW.md` | Accurate | Mentions hot-reload iteration |
| `Documentation/Features/EXPORT_PACKAGING.md` | Outdated (self-labeled historical) | Old SceneExporter flow, correctly banner-marked superseded |
| `Documentation/Integration/IMGUIZMO_INTEGRATION.md` | **Outdated** | ImGuizmo replaced by Vulkan gizmos; not flagged historical |
| `Documentation/PrefabSystem.md` | Accurate | Matches overrides/scriptVars/controller vars |
| `Documentation/Lighting/*`, `ProjectManager/*`, `Roadmaps/ROADMAP_1-0.md`, `Archive/*` | Outdated / historical | ImGui-era designs |
| `docs/plugins/*` (3 files) | Accurate | Match PluginSystem/PluginAPI 1.0; a few overstated points (failure isolation is graceful-only; settings lookup id-only) |
| `docs/editor/UX_QOL_AUDIT.md` | Accurate | Self-consistent through Phase 4 |
| `docs/audit/*` (2026-09-02) | Historical | See next section |

### Discrepancies between the previous audit and current code (current code wins)

| Previous audit claim | Current reality |
|---|---|
| "Editor and packaged game do not share a frame loop" (`rendering.md`) | Both now call `EngineFrameRenderer::RenderFrame` (shared). |
| "No animation system" (`animation.md`) | Skeletal animation, `.hpcharacter`, clips, GPU skinning, Character Editor, Lua controllers all implemented. |
| "Binary FBX unsupported / no skins" (`assets_import.md`) | ufbx FBX 1.0 supports static + skinned + animation. |
| "Lua/hot-reload: None; ModelImporter: None" (`testing_quality.md`) | CharacterLua, ModelImporter, FbxImport, and hot-reload now exist/tested. |
| "Inspector cannot edit variables map" (`prefabs.md`) | Lua `Expose.*` inspector with reconciliation is implemented. |
| README "Save/Load ✅" | Only editor scene serialization + editor recovery; no game save system. |

### Other verified doc-vs-code claims

- "Auto Reimport — background file watcher monitors AssetDB" (`AssetReimporting.md:34-39`) — **false**: FileWatcher is never started; manual reimport only.
- "No hot-reload yet" (`SESSION_SUMMARY_SCRIPTING.md:351`, `LUA_INTEGRATION_ROADMAP.md:162-185`, `LUA_SCRIPTING_GUIDE.md:554`) — **false**: hot reload via per-frame mtime polling is implemented.
- "Multiple Lua scripts per entity" (`SCRIPTING_QUICK_START.md:207`, `SCRIPT_DEVELOPMENT_GUIDE.md:366-367`) — **false**: exactly one gameplay slot + one character-controller slot.
- "C++ scripting has been removed" (`LUA_SCRIPTING_GUIDE.md:14`) — **false**: `ScriptEngine`/`ScriptComponent` exist; inert in play mode.
- "Nightly smoke test workflow in CI" (`ROADMAP.md`) — **false**: no CI exists.
- Boomer-Shooter starter content (`CHANGELOG.md:50`) — **absent** from the tree.

### Missing documentation

No dedicated guide for the Lua `Expose.*` Inspector, the shared-frame-loop runtime unification, or the packaged-game HUI gap (the last is a feature gap that should be documented as known). No on-boarding sample-level guide exists in the tree.