# CI / Smoke-tests: Add automated build and startup checks

Description
-----------
Add a lightweight automated smoke-test flow that verifies a Release build and that the runtime starts and can load a test scene. This should be runnable locally and suitable for GitHub Actions.

Acceptance criteria
-------------------
- Add a PowerShell script (or small executable) that builds the project and attempts to run the runtime with `--smoke-test <scene>` returning exit code 0 on success.
- Add a GitHub Actions workflow (optional) or instructions that use the script to run on PRs and main branch.

Notes / Implementation pointers
------------------------------
- For Windows, use `cmake --build build --config Release` and then run `build/Release/HephaestusEngine.exe --smoke-test <path-to-scene>`.
- The smoke-test should be fast: no rendering verification required beyond successful startup and scene deserialization.

Suggested labels: `roadmap/1.0`, `area:ci`, `priority:medium`
