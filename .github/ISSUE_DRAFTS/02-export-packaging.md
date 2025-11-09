# Editor export & packaging: create reproducible runtime packages

Description
-----------
Implement an editor export flow that produces a distributable runtime folder containing the runtime executable and all required assets (serialized scene, shaders, textures, models, materials). The export must be documented and repeatable.

Acceptance criteria
-------------------
- Add an "Export" action in the editor that copies the runtime executable and requested assets into a target folder.
- Create a small packaging script that validates required runtime files are present and writes a simple manifest listing included assets and versions.
- Document export steps and how to run the runtime from the packaged folder.

Notes / Implementation pointers
------------------------------
- Keep the exporter simple for 1.0: collect referenced asset paths from the scene serializer and copy them into `dist/<project-name>/` along with the runtime binary.
- Consider compressed archives (zip) as optional convenience.

Suggested labels: `roadmap/1.0`, `area:tools`, `priority:high`
