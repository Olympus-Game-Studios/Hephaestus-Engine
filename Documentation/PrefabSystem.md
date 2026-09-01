# Hephaestus Engine — Prefab System

Prefabs are reusable entity hierarchies stored as first-class assets (`.hpprefab`). A scene holds **instances** of those assets: the live entities are rebuilt from the prefab plus instance placement and per-property overrides.

Blueprint is the legacy name for the same system. Old files and C++ aliases still load; new assets and UI use Prefab terminology only.

Nested prefabs are not supported in this version.

---

## 1. What a prefab is

A prefab asset describes:

- Node names and hierarchy
- Local transforms
- Serializable components (mesh, materials, physics, lights, cameras, Lua/scripts)
- Optional exposed keys

File extension: **`.hpprefab`**. Schema: `"HephaestusPrefab"`.

Legacy files still load:

| Extension | Schema |
|-----------|--------|
| `.hpprefab` | `HephaestusPrefab` (preferred) |
| `.hpblueprint` | `HephaestusBlueprint` |
| `.blueprint` | `HephaestusBlueprint` |

Sample assets:

- `assets/prefabs/Lamp.hpprefab`
- `assets/blueprints/Player.hpblueprint` (legacy)

---

## 2. Core types

| Type | Header | Role |
|------|--------|------|
| `PrefabAsset` / `PrefabNode` / `PrefabComponentEntry` | `PrefabAsset.h` | In-memory asset |
| `PrefabSerializer` | `PrefabSerializer.h` | Disk I/O for `.hpprefab` |
| `PrefabInstantiator` | `PrefabInstantiator.h` | Asset → scene hierarchy |
| `PrefabComponentIO` | `PrefabComponentIO.h` | Generic JSON capture / apply / property-path diff |
| `PrefabOverrideSystem` | `PrefabOverrideSystem.h` | Override tracking, apply, revert, refresh, unpack, create-from-selection |
| `PrefabDocument` | `EditorDocument.h` | Prefab Editor document + undo snapshots |
| `PrefabEditorWindow` | Qt editor | Dedicated authoring window |

### ECS

| Component | Attached to | Stores |
|-----------|-------------|--------|
| `PrefabInstanceComponent` | Instance **root** | Prefab path/GUID, instance id, **scene root placement** (`rootTransform`), missing-asset flags, per-node overrides |
| `PrefabNodeComponent` | Every spawned prefab entity | `instanceId`, `nodeId`, `isRoot` |

Root placement is **not** an override. Moving the instance in the scene updates `rootTransform`. Child local transforms that differ from the prefab are stored as overrides.

---

## 3. Data flow

```
.hpprefab
    → PrefabSerializer::Deserialize → PrefabAsset
    → PrefabInstantiator::Instantiate → scene entities
         + PrefabInstanceComponent.rootTransform
         + PrefabNodeOverride property paths
```

Scene save:

1. `PrefabOverrideSystem::SyncAllInstances` records live diffs
2. Prefab children are **omitted** from `entities[]`
3. Each instance is written under `prefabInstances[]`

Scene load:

1. Ordinary entities deserialize first
2. Each `prefabInstances[]` entry loads the asset
3. Instantiator rebuilds the hierarchy and applies placement + overrides
4. If the asset is missing, a stub entity is created (`missingPrefab: true`) and the scene still loads

Conceptual instance payload:

```text
prefabInstances:
  - prefabPath: "assets/prefabs/Lamp.hpprefab"
    rootTransform: { position, rotation, scale }
    overrides:
      Shade:
        components:
          Transform: { position: [0, 3.5, 0] }
          Light:     { intensity: 2500 }    # if the light lives on that node
        scriptVars:
          glow: 4
```

---

## 4. Automatic instance overrides

Editing a prefab instance in the Scene Editor records overrides without a separate “create override” action.

Flow:

```text
Inspector / gizmo edit
    → EditorContext::MarkDirty
    → PrefabOverrideSystem::SyncEntityOverrides
    → compare live property vs prefab default
    → write or remove a property-path override
```

Supported capture types (via `PrefabComponentIO`, not a per-property switch in the editor):

MeshRenderer, MeshCollider, PhysicsShape, RigidBody, Material, Light, Camera, LuaScript, ScriptComponent, plus Transform on **child** nodes.

Lua exposed variables are stored in `scriptVars` (runtime `variables` are stripped from JSON diffs).

If a value matches the prefab again, the override is removed.

---

## 5. Inspector visualization and actions

For entities that belong to an instance:

- Gold left border on overridden child transform spinboxes
- Gold left border on component inspectors that contain overrides
- Prefab Instance bar: path, missing-asset warning, override list with Revert / Apply per property
- Component / transform context menus: revert or apply that component
- Prefab bar context menu: Revert Node / Apply Node to Prefab

Root placement is shown as a normal transform, not as an override.

### Revert

| Action | Effect |
|--------|--------|
| Revert Property | Restore one property; drop its override |
| Revert Component | Restore all overrides in that component |
| Revert Node | Restore one prefab node |
| Revert All | Rebuild the instance from the prefab; **keeps** `rootTransform` |

All of these push editor undo snapshots.

### Apply to Prefab

Writes the live value into the `.hpprefab`, saves it, drops the corresponding override on the source instance, then refreshes other instances of that prefab (unrelated overrides on those instances are kept).

Apply All asks for confirmation because it affects every instance.

---

## 6. Editor workflows

### Place Prefab in Scene

- Drag a `.hpprefab` from the Asset Browser onto the viewport, or from **Add Actors → Prefabs**
- Or right-click the prefab in the Asset Browser and choose **Instantiate in Scene**

Placement is in front of the editor camera. The instance records `rootTransform` and is undoable.

### Create Prefab From Selection

Outliner action on a **non-prefab** entity.

1. Captures the full hierarchy and all `PrefabComponentIO` types (not just transforms)
2. Writes a new `.hpprefab` (root local transform is identity; scene placement is instance `rootTransform`)
3. Replaces the selection with an instance of that prefab
4. Preserves world-space placement
5. Undoable

### Open Prefab / Select Prefab Asset / Refresh / Unpack

Shown only when they apply:

- **Open Prefab** — Prefab Editor
- **Select Prefab Asset** — Asset Browser
- **Refresh From Prefab** — reload asset, rebuild instance, reapply overrides, drop stale override keys
- **Unpack Prefab** — instance root only: bake defaults + overrides into ordinary entities, remove `PrefabInstanceComponent` / `PrefabNodeComponent`, then serialize as normal scene entities. Undoable.

### Missing assets

A missing `.hpprefab` does not fail scene load. The instance is marked missing, logged, and shown in the Inspector. Restore the file and use Refresh to recover.

---

## 7. Prefab Editor

Dedicated Qt window (`PrefabEditorWindow`) with hierarchy, 3D viewport, gizmos, inspector, and undo.

- Preview scene is isolated (`attachInstanceComponent = false`) so preview entities are not treated as scene instances
- Inspector hosts the same `ComponentInspector` widgets (and `LightInspector`) as the Scene Editor, bound to the preview entity
- Edits update the preview immediately, sync into `PrefabAsset`, and rebuild meshes
- Save writes the asset and notifies the Scene Editor to refresh instances

You can add/remove components, assign meshes/materials, and edit physics, lights, cameras, and Lua data.

---

## 8. Outliner identification

- Shield icon on instance roots
- Tinted children
- Missing roots are tinted as broken

---

## 9. Undo

| Operation | Undo path |
|-----------|-----------|
| Scene property / transform overrides | `EditorUndoManager` scene snapshot |
| Revert / Apply / Refresh / Unpack / Create From Selection | scene snapshot; Apply also restores the `.hpprefab` backup |
| Prefab Editor edits | `PrefabDocument` asset snapshots via a custom push handler |

---

## 10. Compatibility

Legacy C++ aliases (`BlueprintAsset`, `BlueprintSerializer`, `BlueprintInstantiator`, `BlueprintInstanceComponent`, …) remain. New code should use Prefab names except inside compatibility paths.

Re-saving a scene migrates `blueprintInstances` → `prefabInstances`. Re-saving an asset migrates schema to `HephaestusPrefab`.

---

## 11. Tests

`PrefabSystemTests` (CTest target `PrefabSystemWorkflow`) covers:

- Prefab save/load
- Scene save/load with instance placement and overrides (transform, component, Lua)
- Refresh preserving overrides while picking up new defaults
- Apply from instance A updating instance B
- Revert
- Create Prefab From Selection capturing supported components
- Unpack stripping metadata without losing values
- Missing prefab stubs
- Legacy `Player.hpblueprint` load

---

## 12. Limitations

- Nested prefabs are not supported. Do not parent one prefab instance inside another expecting prefab-in-prefab semantics.
- GUID generation is timestamp-based, not a cryptographic UUID.
- Added/removed components on an instance (relative to the source prefab) are not a first-class override type yet; property diffs apply to components that exist on both the instance and the asset.
- Prefab Editor redo is not implemented (undo is).
