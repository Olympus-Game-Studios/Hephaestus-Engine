# Prefab audit

**Overall: 4 / 5 (Strongest content system)**  
Runtime 4 · Editor 4 · Lua 3 · Tests 4

Prefabs are a **production-usable** workflow for static-mesh actors, lights, Lua, audio, and physics components. This is one of the few Level 4 systems.

---

## Asset format

- **Exists:** `.hpprefab` JSON, `assetVersion = 1`, per-asset `guid`
- **Evidence:** `engine/Scene/PrefabAsset.h`, `PrefabSerializer.*`
- **Legacy:** `.hpblueprint` still loads (`Blueprint*` type aliases). Sample: `assets/prefabs/Lamp.hpprefab`; `assets/blueprints/Player.hpblueprint`.
- **Maturity:** 4

---

## Instantiation / root placement

- **Exists:** `PrefabInstantiator`
- **Lua:** `Scene.instantiate(path, opts)` — project-relative path, optional root `position` / `rotation` (degrees), `vars` merged onto Lua maps **before** `onAwake`
- **Editor:** Drag from Asset Browser, Add Actors → Prefabs, context **Instantiate in Scene**
- **Maturity:** 4
- **Tests:** `PrefabSystemTests` `InstantiatePoseAndVars`

---

## Overrides (child / property)

- **Exists:** `PrefabOverrideSystem` + `PrefabComponentIO` property paths
- **Inspector:** Override indicators, Revert / Apply (single instance; Apply All confirms)
- **Scene serialize:** `rootTransform` stored separately from child overrides; prefab children not duplicated in `entities[]`
- **Maturity:** 4

---

## Apply / Revert / Refresh / Unpack / Create From Selection

| Workflow | Status |
|----------|--------|
| Apply property / component / node / all to prefab | Yes |
| Revert | Yes |
| Refresh reapplies overrides | Yes |
| Unpack bakes to ordinary entities | Yes |
| Create Prefab From Selection | Yes (supported component set) |
| Missing prefab | Stub entity; scene still loads |

**Evidence:** `PrefabOverrideSystem.h`; Outliner commands; `PrefabInstanceBar.*`  
**Tests:** ApplyAndRevert, CreateFromSelectionAndUnpack, MissingPrefab, RefreshKeepsOverride, SceneRoundtripAndTransforms

**Maturity:** 4

---

## Prefab Editor

- **Exists:** `PrefabEditorWindow` / `PrefabEditorManager` / hierarchy + inspector panels
- **Maturity:** 3
- **Evidence:** Inspector is the same scene component widgets, synced back into `PrefabAsset` (changelog 2026-08-28). Rotation in degrees. Searchable Add Component.
- **Limitation:** Preview is an isolated scene (`SetExternalScene`); not a full in-context multi-instance editor.

---

## Supported components in prefab IO

From `PrefabComponentIO.cpp`:

`MeshRenderer`, `MeshCollider`, `PhysicsShape`, `RigidBody`, `Material`, `Light`, `Camera`, `LuaScript`, `ScriptComponent`, `AudioSource`

Unsupported components on a captured entity are dropped (not a variant system).

---

## Nested prefabs

- **Multi-node hierarchy:** Yes (Lamp root + children).
- **Nested prefab *instances* as first-class links:** **No.** `CaptureHierarchy` bakes subtree components; does not keep inner `PrefabInstanceComponent` live links.

**Maturity:** 2 for nesting · 4 for single-asset hierarchies

---

## Variants

- **Exists:** No
- **Maturity:** 0

---

## References / duplication / scene integration

- Scene stores path + guid + instance id + overrides.
- Duplicate of a prefab root re-instantiates (preserves link) rather than exploding to raw entities.
- Asset move/rename: `AssetReferenceIndex` rewrites `.hpprefab` references (path-based).
- **Maturity:** 4 scene integration

---

## Lua / designer workflow

Designers can place, override, apply, unpack without code.  
Programmers spawn at runtime with pose + vars.  
**Gap:** Inspector does not edit `variables` map (spawn `vars` still work from Lua).

---

## Unsupported (clear list)

- Prefab variants
- Nested instance links / overrides cascading through nested prefabs
- Prefab-only editor simulation of physics/play
- Capturing arbitrary future components without IO updates
- Diff viewer / three-way merge UI

---

## Recommended next step

Keep prefabs stable. Nested instances are Priority C. Finish Lua variable Inspector (depends on scripting, not prefab core).
