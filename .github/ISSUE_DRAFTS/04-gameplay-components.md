# Gameplay components: implement RigidBody and basic lights

Description
-----------
Implement essential gameplay components required for demo scenes: a simple RigidBody component (integrated with existing physics hooks) and Light components (Directional, Point). Provide example usage in the sample scene.

Acceptance criteria
-------------------
- Add `RigidBodyComponent` with mass, velocity, and basic collision response using existing `engine/Physics` hooks.
- Add simple `LightComponent` types (Directional, Point) and wire them into the renderer's uniform data for lighting.
- Update a sample scene to demonstrate physics and lighting.

Notes / Implementation pointers
------------------------------
- Keep physics deterministic for simple interactions; full physics engine integration is out of scope for 1.0.
- Use simple attenuation for point lights and a single directional light for demo scenes.

Suggested labels: `roadmap/1.0`, `area:physics`, `area:lighting`, `priority:medium`
