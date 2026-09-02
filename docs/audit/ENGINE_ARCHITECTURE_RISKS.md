# Architecture risks

Evidence-based technical risks only. Date: 2026-09-02. Revision: `72a4dd7`.

Urgency: **Now** / **Near** / **Later**. Likelihood and impact are qualitative.

---

## Risk: Dual render stacks (editor vs packaged game)

**Evidence**

- Editor: `QtEditorEngine` → `EngineFrameRenderer::RenderFrame`.
- Game: `engine/Game/GameMain.cpp` creates `VulkanPipeline`, `g_DirectionalShadow`, lighting UBOs, mesh draws. Grep shows **no** `EngineFrameRenderer` include in `engine/Game/`.
- Nested `EngineFrameRenderer::Skybox` vs `VulkanSkybox` used by the game.

**Impact:** Feature drift (shadows, IBL, shading modes, material slots). Packaged games silently miss editor visuals. Every graphics change costs 2×.

**Likelihood:** Already happening.

**Recommended response:** Make `HephaestusGame` a host around `EngineFrameRenderer`. Do not add post-process until this is done.

**Urgency:** Now

---

## Risk: Physics bodies use local transforms

**Evidence**

- Renderer: `EngineFrameRenderer` uses `scene->GetWorldTransform(entity)`.
- Physics: `JoltPhysicsSystem.cpp` writes `JPH::RVec3(transform.position.x, …)` from `TransformComponent` local translation.

**Impact:** Child colliders sit at local offset, not world pose. Parent moving a physics child desyncs mesh vs body.

**Likelihood:** Certain for any hierarchy with rigid bodies.

**Recommended response:** Sync world TRS into Jolt; write back carefully (world → local through parent). Document that dynamic children are unsupported until then.

**Urgency:** Now

---

## Risk: Serialization identity is `entt::entity` integers

**Evidence**

- `TransformComponent.parent` / `children` stored as entity ids.
- No UUID component. Prefabs have `prefabGuid` / `instanceId` but ordinary entities do not.

**Impact:** Fragile save games, merges, and any future networking. Scene files break if entity packing changes.

**Likelihood:** High if a game save dumps the scene JSON as-is.

**Recommended response:** For game saves, key by name, prefab instance id, or new stable ids. Do not treat `.hpscene` as a player save format.

**Urgency:** Now (for save design); Later (full GUID migration — not required for path-based assets)

---

## Risk: Lua engine is a singleton mega-file

**Evidence**

- `LuaScriptEngine.cpp` ~6k lines; `s_Instance` global.
- `luaL_openlibs` — full `io`/`os`/`package`/`debug`.
- `LuaScriptEngine::GetInstance()` used from many bindings.

**Impact:** Hard to test; hidden cross-talk; packaged games can run arbitrary OS Lua. Two hosts must remember to bind physics/audio/UI.

**Likelihood:** High for maintenance; medium for security (local games, not mods).

**Recommended response:** Keep Lua as language; split bindings by module; drop singleton; optional sandbox later. Do not add C# until this file is manageable.

**Urgency:** Near

---

## Risk: Editor/runtime coupling in play mode

**Evidence**

- `PlayModeManager` snapshots scenes for Qt play.
- Shared `InputSystem` now removes the former Qt-only input-state shim, but
  `RuntimeUIOverlayWidget` and Qt cursor-capture workarounds remain host-specific.
- `EditorOnlyComponent`, hide/isolate/lock sidecars.

**Impact:** Play-in-editor bugs that do not reproduce in `HephaestusGame` and vice versa.

**Likelihood:** Already documented in changelog (HUD stacking, mouse delta).

**Recommended response:** Keep the shared input core; extend the same host-adapter
boundary to UI overlay and Lua binding setup used by both Qt and GameMain.

**Urgency:** Near

---

## Risk: Unwired but serialized inspector fields

**Evidence**

- `EnvironmentComponent.rotationYaw`, `enableRenderDistance` — inspector + `SceneSerializer`; not consumed in `EngineFrameRenderer` draw cull/sky rotation.
- `LightComponent.shadowNormalBias` — inspector + IO; shadow pass uses bias/strength, not normal bias.
- `SettingsSchema` `defaultExposure` / HDR flags — not shader uniforms.
- `RigidBodyComponent.debugDraw` / `MeshColliderComponent.debugDraw` — inspector only.
- `PhysicsShapeType::Cylinder` serialized; no `case` in `JoltPhysicsSystem` shape builder.
- `MaterialComponent.refraction` reserved.

**Impact:** Designers believe the engine does something it does not. Silent quality bugs.

**Likelihood:** Certain.

**Recommended response:** Wire or hide. Treat as product bugs, not backlog ideas.

**Urgency:** Now

---

## Risk: Retired code still in the tree

**Evidence**

- Changelog 2026-09-02: `SceneExporter` retired. `CMakeLists.txt` does **not** list it. `engine/Scene/SceneExporter.cpp` still exists (~680 lines).
- `MeshCollisionSystem` compiled with physics sources; never constructed (only self-references).
- `RelationshipComponent` unused; hierarchy lives on `TransformComponent`.
- `ScriptEngine` still `make_unique` in `QtEditorEngine` play path.
- Comments still say “legacy ImGui editor” (`EditorContext.h`, `PlayModeManager.h`, `AssetManager.h`).

**Impact:** Contributors revive the wrong export path; dead collision code diverges from Jolt; C++ scripts look supported.

**Likelihood:** Medium.

**Recommended response:** Quarantine/delete unused targets; stop constructing `ScriptEngine`; update comments.

**Urgency:** Near (do not revive SceneExporter)

---

## Risk: Custom mesh importers, not a maintained library

**Evidence**

- `ModelImporter.cpp`: OBJ, **ASCII FBX only** (binary FBX errors out), custom glTF/GLB. No Assimp.
- `Vertex` has no joints/weights; glTF skins ignored.

**Impact:** DCC round-trip failures; “FBX support” overstated; animation cannot be added without importer rewrite.

**Likelihood:** High for artist-sourced FBX.

**Recommended response:** Document ASCII-only FBX; prefer glTF; plan a real loader if animation starts.

**Urgency:** Near for docs; Later for Assimp/cgltf unless animation is Priority A

---

## Risk: Path-based assets without content-addressed runtime cache

**Evidence**

- `AssetPathPolicy` + `AssetReferenceIndex` are production.
- `DerivedDataCache` exists; changelog and pipeline comments: not the runtime load path.
- FileWatcher implemented; `EditorAssetPipeline` does not start it.

**Impact:** Large textures/meshes re-decode; artists must manual reimport. Fine for small projects.

**Likelihood:** High as content grows.

**Recommended response:** Finish DDC load when import cost hurts; keep path identity.

**Urgency:** Later unless content size already hurts

---

## Risk: Shader and asset search path heuristics

**Evidence**

- Repeated candidates: `shaders/`, `../shaders/`, `build/Release/shaders/` in pipeline/skybox/gizmo loaders.
- Stage 6 packaging copies compiled SPIR-V; portability tests cover relocated packages.

**Impact:** Dev launches from the wrong cwd pick Debug vs Release SPIR-V. Less of an issue for packaged games after Stage 6.

**Likelihood:** Medium in source checkouts.

**Recommended response:** Single runtime locator (already exists for player); reuse for editor shader root.

**Urgency:** Near

---

## Risk: Optional Lua at configure time

**Evidence**

- `CMakeLists.txt` can set `LUA_AVAILABLE=0` and stub the engine.
- Game scripting option `HEPHAESTUS_GAME_SCRIPTING`.

**Impact:** Two product configurations. Easy to ship an editor that cannot run the sample scripts.

**Likelihood:** Low if Lua is always found on the team’s machines; high for clean CI clones.

**Recommended response:** Make Lua required for editor+game; keep stub only for theoretical slim builds.

**Urgency:** Near (CI)

---

## Risk: No first-party CI

**Evidence**

- `.github/` has issue drafts only; no workflows.
- Headless tests exist and are CMake/CTest registered.

**Impact:** Prefab/build regressions land on `main` unnoticed.

**Likelihood:** High.

**Recommended response:** Run existing CTest targets on Windows in CI. Do not wait for GPU tests.

**Urgency:** Near

---

## Risk: Qt viewport is Win32-only

**Evidence**

- `QtViewportWindow::GetNativeSurface`: non-Win32 sets `windowHandle = nullptr` (`TODO: Support other platforms`).
- `VulkanDevice::AttachToSurface`: `#else` “Platform not supported yet”.

**Impact:** Linux/macOS editor cannot present Vulkan in the Qt viewport even if the rest compiles.

**Likelihood:** Certain on those platforms.

**Recommended response:** Document Windows-only editor. GLFW game runtime is the realistic Linux experiment later.

**Urgency:** Later for ports; Now for honest platform claims

---

## Risk: Snapshot undo vs command model

**Evidence**

- `EditorUndoManager` / `EditorUndoCommand` snapshot scene JSON (coalesced transforms added).
- `SceneSerializer::DeserializeFromString` was raced on a shared temp file (fixed 2026-09-01 with unique temps).

**Impact:** Memory cost on large scenes; undo may drop editor-only sidecars if not in the snapshot.

**Likelihood:** Medium as scenes grow.

**Recommended response:** Keep snapshots for now; measure. Do not rewrite undo unless large-scene work starts.

**Urgency:** Later

---

## Risk: `MeshRendererComponent::GetWorldBounds` uses local matrix

**Evidence**

- `Components.h` `GetWorldBounds` calls `transform.GetLocalMatrix()`, not `Scene::GetWorldTransform`.

**Impact:** Hierarchy bounds/picking/culling using this helper will be wrong. Authoring AABB raycast may use a different path (`SceneAuthoring`).

**Likelihood:** Medium depending on call sites.

**Recommended response:** Audit callers; use world matrix.

**Urgency:** Near

---

## Risk: Hard-coded lighting ambient and caps

**Evidence**

- Lighting UBO: 16 point + 16 spot; ambient hardcoded (~`0.1, 0.1, 0.15`).
- Last directional light wins in gather loops (typical overwrite pattern).

**Impact:** Busy interiors lose lights; mood depends on HDRI + one sun.

**Likelihood:** High for real levels.

**Recommended response:** Expose ambient on `EnvironmentComponent`; document caps; later clustered lighting (not now).

**Urgency:** Near (ambient expose); Later (more lights)

---

## Risk: Insufficient tests on critical runtime

**Evidence**

- Strong: prefab, asset ops, validation, package layout.
- None: Vulkan frame, Jolt step, Lua bindings, HUI hit-testing, audio.

**Impact:** Physics/Lua regressions only found in play mode.

**Likelihood:** High.

**Recommended response:** Headless Lua + physics tests without GPU; GPU tests later.

**Urgency:** Near

---

## Risks considered and not listed

- “Need to replace EnTT / Vulkan / Qt” — no evidence the architecture is the wrong bet.
- “Need global GUIDs for all assets” — path policy is intentional (`docs/editor/UX_QOL_AUDIT.md`).
- Legal/license compliance — notices are generated; **not** audited here. See `ROADMAP.md` license-audit line and packaged `ThirdPartyNotices.txt`.
