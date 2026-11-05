# Physics runtime contract (Jolt)

Hephaestus uses vendored Jolt Physics (`extern/JoltPhysics`) through `JoltPhysicsSystem` (`engine/Physics/`). This page is the contract editor Play and packaged games follow. For authoring physics in the editor, see the user guide's [Physics](../../docs/user-guide/physics.md) page; for the Lua side, [LUA_SCRIPTING_GUIDE.md](../Scripting/LUA_SCRIPTING_GUIDE.md#physics).

## Stepping and ownership

Play Mode and the packaged game share `JoltPhysicsSystem`. One fixed step is `1/60` second, and a host frame takes at most four substeps. Scripts run before that frame's physics steps. There is no `FixedUpdate`. Character controllers and dynamic bodies are rendered interpolated between the last two fixed steps; simulation state and script-visible transforms are not interpolated.

Motion ownership:

- **Static** — the authored world pose drives the body. `isStatic` is true.
- **Dynamic** — the solver owns the world pose. The local transform is that pose under the parent. `isStatic` is false.
- **Kinematic** — the authored world pose is the end target. `MoveKinematic` spreads the delta across that frame's substeps. `isStatic` is false.

`RigidBodyComponent::SetMotion` keeps `isStatic == (motion == Static)`. Old files that only store `isStatic` still load: true becomes Static, false becomes Dynamic. An explicit `motion` value wins over a contradictory `isStatic`, and the next save writes a matching mirror.

Collision layers live in `.hephaestus/physics_layers.json` (at most 16 stable ids). Names are display-only. Default (id 0) cannot be deleted; its pairs can be edited. User layers can be renamed or tombstoned, and a tombstoned id is not reused. A missing file keeps collide-all on Default, and a body with no `layer` uses id 0. The Inspector layer combo lists names from that file. Project Settings → Physics edits the matrix through `PhysicsLayerConfig`. A matrix edit applies on the next Play. It does not rebuild a running simulation.

A body pose is a world translation and rotation. A static child uses the parent's world pose. A dynamic child's solver pose stays authoritative and the local transform is rewritten to preserve it. A non-uniform ancestor scale produces no body and a Physics error.

An authored static trigger is created as a kinematic sensor. Triggers do not detect static non-sensors with the current Jolt flags. Trigger versus trigger reports when the matrix allows it. Character grounding ignores sensors.

`onCollisionEnter` / `onCollisionStay` receive `contact` only when the manifold has a point and penetration is greater than or equal to zero. A speculative or otherwise invalid contact leaves the argument nil. `contact.point` is the midpoint of the two shape points. `contact.normal` points so the receiver separates from the other body. `contact.relativeVelocity` is the other body's velocity minus the receiver. `onCollisionExit` never receives contact data. A solid pair may emit `onCollisionExit` when one body sleeps, so `onCollisionStay` is not a ground signal. Trigger callbacks are entity events: enter and stay include a contact only under that same rule, and exit never does.

The temp allocator is a fixed block sized from the measured Jolt 5.4.1 settling peak, with a malloc fallback. The historic 8-box stack and the heavier 64-box scene survived on 5.4.1; the old 4.1.0-rc1 settling crash was not reproduced.

## Not implemented

- Joints, ragdolls and vehicles
- Overlap and shape cast queries (raycasts only)
- Debug drawing: the Rigid Body **Debug Draw Collider** and **Debug Draw Bounds** flags are saved but nothing draws them
- Coyote time and jump buffering in the character controller
