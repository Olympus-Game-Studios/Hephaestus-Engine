# Scene / ECS audit

**Overall: 3 / 5 (Usable editor scene graph)**  
Runtime 3 · Editor 4 · Lua 3 · Tests 2

Hephaestus uses **EnTT** with a thin `Entity` handle. Hierarchy is stored on `TransformComponent` (`parent` + `children`), not on the unused `RelationshipComponent`.

---

## ECS library / architecture

- **Exists:** Yes — EnTT
- **Maturity:** 3
- **Evidence:** `engine/Scene/Scene.h`, `Entity.h`, `Components.h`
- **Limitations:** No component enable-bit distinct from “remove component”. No groups/views API for games (Lua uses engine-provided queries).
- **Legacy:** `engine/Scene/Transform.h` `SceneObject` — not referenced elsewhere.

---

## Entity lifecycle

- **Exists:** `CreateEntity`, `DestroyEntity`, play-mode snapshot restore
- **Maturity:** 3
- **Evidence:** `Scene.cpp`
- **Limitation:** `DestroyEntity` **reparents** children to root rather than recursively destroying (recursive destroy commented). Easy to leak child entities when deleting a parent from scripts/editor depending on command used.
- **Lua:** `Scene.destroy` deferred to end of script tick (safe vs iterator invalidation)

---

## Component ownership

- Components are EnTT-owned POD/structs.
- Prefab instances add `PrefabInstanceComponent` / `PrefabNodeComponent`.
- Editor-only: `SelectionComponent`, `FolderComponent`, `EditorOnlyComponent`, `EditorIconComponent`.

**Maturity:** 3

---

## Hierarchy / transforms

- **Exists:** Parent/child, cycle-safe `SetParent`, optional world-preserve reparent, `GetWorldTransform`
- **Maturity:** 3
- **Evidence:** `Scene.h` APIs; `SceneAuthoringTests` parent world-preserve
- **Euler:** Radians on component; Inspector/prefab editor expose degrees in places (changelog)
- **Bug risk:** `MeshRendererComponent::GetWorldBounds` uses **local** matrix (`Components.h`), not world — wrong for children
- **Physics:** Uses local translation — see [physics.md](physics.md)

World vs local is **implemented for rendering and authoring**, not consistently for physics or all helpers.

---

## Serialization / scene load-save

- **Exists:** `.hpscene` JSON via `SceneSerializer`
- **Maturity:** 4 for editor scenes
- **Evidence:** File + `SerializeToString` / `DeserializeFromString` (unique temp files after 2026-09-01 race fix)
- **Prefab children:** Omitted from `entities[]`; stored as instances + overrides
- **Missing assets:** Prefab stubs do not fail the whole load
- **Tests:** Prefab round-trip; not a dedicated SceneSerializer suite

---

## Runtime vs editor scene

- **Play:** `PlayModeManager` snapshots the editor scene, runs simulation, restores on stop.
- **Packaged:** `GameMain` loads `game.ini` default scene.
- **Maturity:** 3
- **Limitation:** Editor-only components must not leak into packages (packager exclusions exist in Stage 6). Play-in-editor input/UI differs from game host.

---

## Large scenes / streaming / additive / partition

- **Exists:** No
- **Maturity:** 0
- **Impact:** Soft cap is “whatever fits in one JSON + one GPU mesh library”. No world partition.

---

## Entity references

- **Exists:** Raw `entt::entity` in parent pointers and some Lua ids (`uint32_t`)
- **Maturity:** 1 as a public reference type
- **Prefab:** `instanceId` / `prefabGuid` are stable-ish for instances
- **Lua:** `Scene.findAllWithPrefix` name search — practical for gameplay without GUIDs

---

## Component dependencies

- RigidBody expects Transform (+ shape or mesh collider).
- MeshRenderer optional Material paths.
- No engine-enforced dependency graph; inspectors add components independently.
- **Maturity:** 2

---

## Cloning / snapshots / undo

- **Duplicate:** Hierarchy-aware `DuplicateEntities`; prefab roots re-instantiate
- **Clipboard:** Shallow single-entity (`CopyEntityToClipboard`) — weaker than duplicate
- **Undo:** Full-scene JSON snapshots + coalesced transform undo (`EditorUndoCommand`)
- **Maturity:** 3 undo · 2 clipboard
- **Scalability:** Snapshot undo will hurt large scenes

---

## Editor metadata

- Hide / isolate / lock sidecars (Phase 3)
- Folders in outliner
- Camera bookmarks, scene statistics (changelog)

**Maturity:** 3

---

## Lua scene API (subset)

`Scene.instantiate`, `destroy`, `findAllWithPrefix`, camera find/set. Entity userdata: transform, name, camera, `Call`.

No generic `GetComponent` reflection.

---

## Testing

`SceneAuthoringTests`: snap, bounds raycast, parent world-preserve, duplicate hierarchy. Prefab tests cover serialize of instances.

Missing: destroy-children policy, large-file load, concurrent deserialize (regression exists as a fixed bug).

---

## Recommended next step

1. Fix physics + bounds to use world transforms.
2. Define destroy-parent policy and match Lua/editor.
3. Do not build world partition until a game actually needs two loaded scenes.
