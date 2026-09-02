# Physics audit

**Overall: 3 / 5 (Usable Jolt basics; gameplay API incomplete)**  
Runtime 3 · Editor 3 · Lua 2 · Tests 0

Jolt is integrated for **rigid bodies + mesh static collision + FPS capsule motor + raycast**. It is not a full physics authoring suite. Several inspector/enum features are unwired.

---

## Integration

| File | Role |
|------|------|
| `engine/Physics/JoltPhysicsEngine.*` | World, gravity, job pool, **always-collide** layer filters |
| `engine/Physics/JoltPhysicsSystem.*` | ECS create/sync/destroy, raycast, mesh colliders |
| `engine/Physics/CharacterMotor.*` | Ground probe + XZ move + jump (explicitly **not** `CharacterVirtual`) |
| `engine/Physics/MeshCollisionSystem.*` | CPU triangle tests — **never constructed** |

Hosts: `QtEditorEngine` play mode; `GameMain` packaged.

---

## Rigid / static / dynamic

- **Exists:** Yes
- **Maturity:** 3
- **Evidence:** `RigidBodyComponent` mass/static/trigger/gravity/`freezeRotation`/damping/friction/restitution. Dynamic bodies `EMotionQuality::LinearCast` (CCD-like).
- **Kinematic factory:** `CreateKinematic` exists on the component; creation path is static vs dynamic — kinematic is **not a first-class Jolt motion type** in the system.
- **Sleeping:** Jolt defaults; no engine API.
- **Limitation:** Sync uses **local** `transform.position/rotation`, not `GetWorldTransform`. Parenting is wrong.

---

## Collision shapes

| Shape | Runtime | Editor | Notes |
|-------|---------|--------|-------|
| Box | Yes | Yes | |
| Sphere | Yes | Yes | |
| Capsule | Yes | Yes | FPS player |
| Cylinder | Enum + JSON | Not in inspector cases | **No `case` in shape builder** — falls through |
| Mesh | Static `MeshShape` | MeshCollider inspector | Dynamic mesh not a general feature |

**Maturity:** 3 primitives · 1 cylinder

---

## Triggers / sensors

- **Exists:** `isTrigger` → Jolt `mIsSensor`
- **Maturity:** 2
- **Lua:** No `onTriggerEnter`. Docs mention triggers as future.
- **Contacts:** `MeshColliderComponent.contacts` served the unused CPU system.

---

## Queries

- **Raycast:** Yes — ignore one entity; hit entity, point, normal, distance. Shared editor/game path.
- **Overlap / shapecast / sweep:** No public API
- **Maturity:** 3 raycast · 0 overlap

---

## Layers / filtering

- **Exists:** Stub
- **Maturity:** 1
- **Evidence:** `InitializeLayers` / `InitializeCollisionFilters` declared. Implementation: `ShouldCollide` **always true**; single broadphase layer 0.

---

## Materials (friction / restitution)

- **Exists:** Per-body floats on `RigidBodyComponent`
- **Maturity:** 3
- **No** shared physics material assets.

---

## Constraints / ragdolls / vehicles

- **Exists:** No engine wrappers (Jolt itself has them in `extern/`)
- **Maturity:** 0

---

## Character movement

- **Exists:** `Physics::CharacterMotor` + Lua `isGrounded` / `move` / `jump`
- **Maturity:** 3 for FPS capsules
- **Evidence:** Changelog: freezeRotation DOF, grounded probe, LinearCast to reduce tunneling
- **Not:** slope limits, crouch, stepping CharacterVirtual, third-person animation root motion

---

## Debug drawing

- **Exists:** Inspector checkboxes + serialized `debugDraw`
- **Runtime draw:** **No** references outside IO/inspectors
- **Maturity:** 1 Unwired

---

## Editor / serialization / Lua

- Inspectors: `PhysicsInspectors.*` (presets, shapes, mesh collider)
- Scene + prefab IO: yes
- Lua `Physics`: `isAvailable`, `hasBody`, get/set linear velocity, set angular velocity, set rotation, raycast, motor
- **Missing Lua:** AddForce/impulse, layers, overlap, gravity set, constraints

---

## Tests

No first-party physics tests. Indirect: prefab/inspector add component.

---

## Missing for common games

| Game need | Status |
|-----------|--------|
| Platforms / parented movers | Broken (local sync) |
| Ignore player vs camera / layers | Missing |
| Trigger volumes | Sensor only, no events |
| Ragdoll death | Missing |
| Vehicles | Missing |
| Physics materials library | Missing |
| Editor collider visualization | Unwired |

---

## Recommended next step

World-transform sync, implement or hide layers, Lua trigger callbacks, draw debug colliders. Then overlap queries. Joints/ragdolls wait.
