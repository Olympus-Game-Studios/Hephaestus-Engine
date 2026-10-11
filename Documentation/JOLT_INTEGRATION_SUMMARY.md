# Jolt Physics Engine Integration - Summary

## Stage 9A runtime contract

Play Mode and the packaged game share `JoltPhysicsSystem`. One fixed step is `1/60` second, and a host frame takes at most four substeps. Scripts run before that frame's physics steps. There is no `FixedUpdate` and no render interpolation.

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

## Overview
Successfully integrated **Jolt Physics Engine** into the Hephaestus Engine as the primary physics simulation system. This integration replaces the previous mesh-based collision system with a full-featured physics engine capable of handling dynamic rigid bodies, static colliders, and complex physics interactions.

## What Was Integrated

### 1. CMake Configuration (`CMakeLists.txt`)
- Added Jolt Physics as a subdirectory from `extern/JoltPhysics/Build`
- Configured Jolt build options:
  - `BUILD_SHARED_LIBS`: OFF (static library)
  - `JOLT_SANITIZE_UNDEFINED_BEHAVIOR`: OFF
  - `JOLT_CROSS_PLATFORM_DETERMINISTIC`: ON
- Added Jolt library to link targets
- Included Jolt header directories in project include paths
- Added `JoltPhysicsEngine.cpp` and `JoltPhysicsSystem.cpp` to build sources

### 2. Physics Components (`engine/Scene/Components.h`)
Added two new ECS components for physics:

#### `PhysicsShapeComponent`
Defines collision shapes for entities:
- **Supported Shapes**: Box, Sphere, Capsule, Cylinder, Mesh
- **Properties**: 
  - Shape type and dimensions
  - Offset from entity center
- **Factory Methods**: CreateBox(), CreateSphere(), CreateCapsule()

#### `RigidBodyComponent`
Defines physics properties for dynamic/static bodies:
- **Mass & Motion**: mass, isStatic, isTrigger, useGravity
- **Linear Properties**: linearVelocity, linearAcceleration, linearDamping
- **Angular Properties**: angularVelocity, angularAcceleration, angularDamping
- **Material**: friction, restitution
- **Factory Methods**: CreateDynamic(), CreateStatic(), CreateKinematic()
- **Debugging**: debugDraw flag, joltBodyId storage

### 3. Core Physics Engine (`engine/Physics/JoltPhysicsEngine.h/cpp`)
Wrapper around Jolt Physics core functionality:
- **Initialization**: Sets up physics world, job system, and allocators
- **Update Loop**: Simulates physics for each frame with delta time clamping
- **Gravity Control**: SetGravity() and GetGravity() methods
- **Configuration**:
  - Max bodies: 65,536
  - Max body pairs: 65,536
  - Max contact constraints: 20,480
  - Automatic hardware concurrency detection

### 4. Physics System (`engine/Physics/JoltPhysicsSystem.h/cpp`)
ECS system that manages physics entities and bodies:
- **Body Management**: CreateRigidBody(), DestroyRigidBody(), GetBodyID()
- **Transform Synchronization**: 
  - SyncTransformToPhysics() - sends entity transforms to physics engine
  - SyncTransformFromPhysics() - receives physics positions back to entities
- **Automatic Body Creation**: Creates bodies for entities with RigidBodyComponent
- **Shape Support**: Supports Box, Sphere, and Capsule shapes

### 5. Main Integration (`engine/Core/main.cpp`)
- Added includes for `JoltPhysicsEngine.h` and `JoltPhysicsSystem.h`
- Initialize physics engine at startup: `joltPhysicsEngine.Initialize()`
- Create physics system: `JoltPhysicsSystem joltPhysicsSystem(joltPhysicsEngine)`
- Update physics each frame during main loop with current delta time
- Physics updates are gated by simulation state flags

## Usage Example

To add physics to an entity:

```cpp
// Create an entity
Entity entity = scene.CreateEntity("PhysicsObject");

// Add transform (required)
auto& transform = entity.AddComponent<TransformComponent>();
transform.position = Vec3(0, 5, 0);

// Add physics shape
auto& shape = entity.AddComponent<PhysicsShapeComponent>();
shape = PhysicsShapeComponent::CreateBox(Vec3(1, 1, 1));

// Add rigid body (will be simulated)
auto& rigidBody = entity.AddComponent<RigidBodyComponent>();
rigidBody = RigidBodyComponent::CreateDynamic(1.0f, 0.3f);

// Optional: Add mesh renderer
auto& renderer = entity.AddComponent<MeshRendererComponent>();
```

The physics system will:
1. Detect the entity has RigidBodyComponent
2. Create a physics body in Jolt
3. Simulate it each frame
4. Sync the transform back to the entity

## Physics Features Now Available

- ✅ Dynamic rigid bodies with gravity
- ✅ Static bodies (kinematic)
- ✅ Trigger volumes (sensor bodies)
- ✅ Multiple collision shapes (Box, Sphere, Capsule)
- ✅ Configurable friction and restitution
- ✅ Linear and angular damping
- ✅ Multi-threaded physics simulation
- ✅ Transform synchronization
- ✅ Automatic mass calculation

## Build Status

✅ **Successfully Compiled** - All Jolt integration code compiles without errors

### Build Configuration
- Platform: Windows MSVC
- C++ Standard: C++20
- Configuration: Release
- Output: `build/Release/HephaestusEngine.exe`

## Next Steps (Optional Enhancements)

1. **Advanced Features**:
   - Implement contact callbacks for collision events
   - Add joint/constraint support
   - Implement raycasting for picking
   - Add convex hull collision shapes

2. **Editor Integration**:
   - UI panel for editing RigidBodyComponent properties
   - Visual debugging of collision shapes
   - Physics simulation toggle in editor

3. **Performance**:
   - Profile physics performance
   - Optimize body count and collision layers
   - Implement physics multithreading tuning

4. **Testing**:
   - Create physics test scenes
   - Verify gravity and collision behavior
   - Performance benchmark against mesh collision system

## Technical Notes

- The Jolt Physics library is automatically built as part of the CMake configuration
- TempAllocator is created on-demand in the Update loop (not stored)
- Broad phase layer filtering is simplified to allow all collisions
- Physics updates are synchronized with the main game loop
- Transform synchronization happens once per frame after physics step

## Files Modified/Created

**Created:**
- `engine/Physics/JoltPhysicsEngine.h`
- `engine/Physics/JoltPhysicsEngine.cpp`
- `engine/Physics/JoltPhysicsSystem.h`
- `engine/Physics/JoltPhysicsSystem.cpp`

**Modified:**
- `CMakeLists.txt` - Added Jolt linking and configuration
- `engine/Scene/Components.h` - Added PhysicsShapeComponent and RigidBodyComponent
- `engine/Core/main.cpp` - Added physics engine initialization and update loop integration

---

**Status**: ✅ Jolt Physics integration complete and compiling successfully!
