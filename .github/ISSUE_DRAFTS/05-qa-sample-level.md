# QA & sample level polish: finish demo and playthrough

Description
-----------
Create a polished sample level demonstrating the engine's features (PBR materials, basic physics, scripted interaction). Add a short QA playthrough checklist and ensure the scene is runnable via the exported runtime.

Acceptance criteria
-------------------
- Create a `demo_scene` with example assets and at least one scripted interaction (once scripting is implemented).
- Document a 5–10 step playthrough checklist that QA can run to verify the demo.
- Ensure the scene exports and runs in the packaged runtime without editor present.

Notes / Implementation pointers
------------------------------
- Keep the demo small and focused; the goal is to exercise the core 1.0 features.
- Add a `DEMO_README.md` describing how to run and what to verify.

Suggested labels: `roadmap/1.0`, `area:qa`, `priority:low`
