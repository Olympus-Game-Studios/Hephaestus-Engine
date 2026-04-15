# Hephaestus Engine — Prefab System

## Overview

The Prefab system provides reusable entity hierarchies that can be instantiated into scenes. Prefabs are first-class assets (`.hpprefab`) that preserve hierarchy, transforms, components, and script data. Scene instances of a prefab can have per-node overrides for transforms, component properties, and script variables.

---

## 1. Technical Architecture

### Core Types

| Type | Header | Purpose |
|------|--------|---------|
| `PrefabAsset` | `PrefabAsset.h` | In-memory representation of a prefab: nodes, hierarchy, components |
| `PrefabNode` | `PrefabAsset.h` | Single node in the prefab tree |
| `PrefabComponentEntry` | `PrefabAsset.h` | Serialized component data (type + JSON) |
| `PrefabSerializer` | `PrefabSerializer.h` | Read/write `.hpprefab` files |
| `PrefabInstantiator` | `PrefabInstantiator.h` | Spawn prefab entities into a Scene |
| `PrefabDocument` | `EditorDocument.h` | Editor document model for editing prefabs |

### ECS Components

| Component | Purpose |
|-----------|---------|
| `PrefabInstanceComponent` | Attached to the root entity of a prefab instance. Stores prefabPath, GUID, instanceId, and per-node overrides. |
| `PrefabNodeComponent` | Attached to every entity spawned from a prefab. Stores instanceId, nodeId, and isRoot flag. |

### Data Flow

```
.hpprefab file
    → PrefabSerializer::Deserialize() → PrefabAsset
    → PrefabInstantiator::Instantiate() → entities in Scene
    → overrides applied per node
```

### Serialization Format

New prefabs are saved with schema `"HephaestusPrefab"`. The deserializer also accepts legacy `"HephaestusBlueprint"` schema, legacy field names (`assetId`, `version`), and legacy component object formats.

New scenes serialize prefab instances under `"prefabInstances"` with `"prefabPath"` / `"prefabGuid"` fields. The deserializer also reads legacy `"blueprintInstances"` / `"blueprintPath"` / `"blueprintGuid"`.

### File Extensions

| Extension | Status |
|-----------|--------|
| `.hpprefab` | **Preferred** new extension |
| `.hpblueprint` | Legacy, still loadable |
| `.blueprint` | Legacy, still loadable |

---

## 2. Scene Integration

### How instances are stored in scenes

When a scene is serialized, entities with `PrefabNodeComponent` are **excluded** from the main entity list. Only the root-level `PrefabInstanceComponent` data is stored under `prefabInstances[]`, including:

- `prefabPath` — asset path to the `.hpprefab`
- `prefabGuid` — asset GUID
- `instanceId` — unique instance identifier
- `rootNodeId` — which prefab node is the root
- `parent` — optional parent entity ID in the scene
- `overrides` — per-node transform, component, and script var overrides

### Loading a scene with prefab instances

1. Base entities are deserialized first
2. Each `prefabInstances[]` entry loads the `.hpprefab` via `PrefabSerializer::Deserialize`
3. `PrefabInstantiator::Instantiate` creates entities with `PrefabNodeComponent`
4. Stored overrides (transforms, components, script vars) are applied on top

---

## 3. Override System

### Per-node overrides (`PrefabNodeOverride`)

Each node in an instance can have:
- **Transform override** — replaces the prefab's default transform
- **Component overrides** — per-property overrides for specific component types
- **Script variable overrides** — per-key overrides for LuaScript variables

### Supported component override types

MeshRenderer, MeshCollider, PhysicsShape, RigidBody, Material, Light, Camera

### Instance operations

| Operation | Description |
|-----------|-------------|
| **Instantiate** | Spawn a new prefab instance in the scene |
| **Edit instance** | Modify transforms/components; changes stored as overrides |
| **Revert overrides** | Remove overrides and rebuild from prefab data |
| **Apply to prefab** | Write instance changes back to the `.hpprefab` file |
| **Unpack prefab** | Remove prefab tracking; entities become normal scene entities |
| **Refresh from prefab** | Destroy and re-instantiate from latest prefab on disk |

---

## 4. Prefab Editor (Qt)

### Architecture

- `PrefabEditorWindow` — `QMainWindow`-based dedicated window
- `PrefabEditorManager` — Central tracker; prevents duplicate windows, manages lifecycle
- Each window owns an **isolated** `Scene` (preview scene) and a `PrefabDocument`

### Workflow

1. Double-click a `.hpprefab` in the Asset Browser → opens `PrefabEditorWindow`
2. If already open → focuses existing window
3. Edit hierarchy/transforms in the window
4. Save → writes to disk and notifies all open scenes
5. Scene instances are automatically refreshed

### Features

- Hierarchy tree with add/delete child operations
- Transform inspector (position, rotation, scale)
- Save / Reload / Undo toolbar
- Path bar showing the asset path
- Dirty state tracking (window title shows `*`)
- Close prompt for unsaved changes

### Sync protocol

When a prefab is saved:
1. `PrefabDocument::OnSave()` writes the file
2. `postSaveCallback_` fires
3. `PrefabEditorManager::onPrefabSaved()` receives notification
4. `PrefabSavedNotification` callback on `EditorMainWindow` runs
5. All scene instances referencing that prefab are destroyed and re-instantiated
6. Overrides are preserved through the rebuild

---

## 5. Editor UX

### Context menu actions (Outliner)

| Action | When available |
|--------|---------------|
| Create Prefab From Selection | Entity is NOT a prefab node |
| Open Prefab | Entity IS a prefab instance root |
| Refresh From Prefab | Entity IS a prefab instance root |
| Unpack Prefab | Entity IS a prefab instance root |

### Asset Browser

- Prefab assets appear with a shield icon and `[Prefab]` label
- Double-click opens the Prefab Editor window
- Right-click shows standard asset operations

### Outliner visual indicators

- Prefab instance roots show a shield icon with tooltip showing the source path
- Prefab child nodes are tinted blue

---

## 6. Migration Guide (Blueprint → Prefab)

### What changed

| Old | New |
|-----|-----|
| `BlueprintAsset` | `PrefabAsset` (alias available) |
| `BlueprintNode` | `PrefabNode` (alias available) |
| `BlueprintSerializer` | `PrefabSerializer` (alias available) |
| `BlueprintInstantiator` | `PrefabInstantiator` (alias available) |
| `BlueprintDocument` | `PrefabDocument` (alias available) |
| `BlueprintInstanceComponent` | `PrefabInstanceComponent` (alias available) |
| `BlueprintNodeComponent` | `PrefabNodeComponent` (alias available) |
| `BlueprintNodeOverride` | `PrefabNodeOverride` (alias available) |
| `BlueprintComponentOverride` | `PrefabComponentOverride` (alias available) |
| `AssetType::Blueprint` | `AssetType::Prefab` (alias available) |
| Schema `"HephaestusBlueprint"` | Schema `"HephaestusPrefab"` |
| JSON key `"blueprintInstances"` | JSON key `"prefabInstances"` |
| JSON key `"blueprintPath"` | JSON key `"prefabPath"` |
| `.hpblueprint` extension | `.hpprefab` extension (preferred) |

### Backward compatibility

- All legacy `using` aliases are provided for seamless compilation
- Old `.hpblueprint` and `.blueprint` files are loaded transparently
- Old scene files with `"blueprintInstances"` / `"blueprintPath"` are loaded correctly
- Legacy header files (`BlueprintAsset.h`, `BlueprintSerializer.h`, `BlueprintInstantiator.h`) redirect to new headers
- Legacy `.cpp` files are empty stubs; implementation lives in `Prefab*.cpp`
- New saves always use the `Prefab` format

### Migration steps

1. **No code changes required** for existing C++ code — aliases provide full compatibility
2. To adopt new naming: replace `Blueprint` with `Prefab` in your code at your convenience
3. Re-save scenes to migrate from `blueprintInstances` to `prefabInstances` on disk
4. Re-save prefab assets to migrate from `HephaestusBlueprint` to `HephaestusPrefab` schema

---

## 7. Limitations

- **Nested prefabs** are not currently supported. A prefab cannot reference another prefab as a child node.
- **Prefab Editor viewport** is not yet a 3D rendered view — editing is hierarchy + inspector only. A future version may add a preview render using the engine's Vulkan renderer.
- **Component override detection** is manual. The system does not automatically diff instance state against prefab defaults. Overrides must be explicitly stored.
- **GUID generation** uses timestamp-based IDs, not cryptographic UUIDs.
