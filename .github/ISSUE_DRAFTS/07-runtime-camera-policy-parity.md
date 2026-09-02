# Camera policy: Align Play Mode and packaged runtime

Description
-----------
Define and implement one explicit camera-authority policy for Qt Play Mode and packaged games. Stage 7A1 intentionally preserves the current difference: Play Mode initially retains the editor fly camera until a script takes authority, while the packaged runtime starts from an active or primary scene camera.

Acceptance criteria
-------------------
- Document deterministic selection when multiple, one, or no active scene cameras exist.
- Apply the same scene-camera and Lua `Camera.setActive` authority transitions in Play Mode and packaged games.
- Preserve editor-camera restoration when Play Mode stops.
- Cover active/primary compatibility, script activation/deactivation, and missing-camera fallback.
- Add focused non-rendering tests for camera selection and authority transitions.

Notes / Implementation pointers
------------------------------
- Treat this as post-Stage-7A1 host/lifecycle work, not renderer work.
- Keep scene/entity-to-`RenderCamera` conversion outside `EngineFrameRenderer`.
- Do not couple the packaged runtime to Qt editor state.

Suggested labels: `area:runtime`, `area:editor`, `priority:medium`
