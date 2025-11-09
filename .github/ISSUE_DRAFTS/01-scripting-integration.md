# Scripting integration: add runtime and example scripts

Description
-----------
Integrate a scripting runtime into the engine and ship 1–2 example scripts for gameplay and level flow. For 1.0 we keep the scope small: provide hooks for entity behavior, level flow, and simple input handling via scripts.

Acceptance criteria
-------------------
- Add a lightweight scripting runtime (recommended: Lua via sol2 or LuaJIT; alternative: ChaiScript or Duktape).
- Expose a minimal host API: create/destroy entities, get/set Transform, register update callbacks, print/logging.
- Provide 2 example scripts in `assets/scripts/`: one controlling player input, one triggering a simple event (e.g., door open, spawn actor).
- Document scripting build steps and runtime usage in `Documentation/`.

Notes / Implementation pointers
------------------------------
- Prefer header-only or small dependencies to keep build simple.
- Use script hot-reload in-editor if feasible (optional for 1.0).
- Place script files under `assets/scripts/` and load them via the asset manager.

Suggested labels: `roadmap/1.0`, `area:scripting`, `priority:high`
