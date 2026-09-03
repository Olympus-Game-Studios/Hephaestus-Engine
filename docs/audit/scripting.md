# Scripting audit

**Overall: 3 / 5 (Lua can run small-game logic)**  
Runtime 3 · Editor 4 · API 3 · Tests 2

Lua is the **product language**. C++ scripting is deprecated in docs but **`ScriptEngine` is still constructed** on Qt play. Focused scanner, variable reconciliation, serialization, and prefab workflow tests cover Inspector parameters.

---

## Runtime integration

- **Exists:** `LuaScriptEngine` (`LUA_AVAILABLE`), per-entity environments, `LuaScriptComponent`
- **Maturity:** 3
- **Hosts:** `QtEditorEngine` play; `GameMain` (LateUpdate after Update)
- **Lifecycle:** `onAwake`, `onEnable`, `onUpdate`, `onLateUpdate`, `onDisable`, `onDestroy`
- **Triggers:** **Not implemented** (guide still says “when implemented”)
- **Hot reload:** `EnsureInstance` compares `last_write_time` / embedded hash — **implemented**. Some docs still say Planned.
- **Errors:** `LogLuaError`; instance can fail valid flag; not a structured debugger
- **Sandbox:** `luaL_openlibs` — full standard libraries. **No** sandbox.
- **Packaged paths:** Search paths rebuilt from project root; Stage 6 portability fixture includes Lua

**Singleton:** `LuaScriptEngine::s_Instance` — architecture risk.

---

## Editor / Inspector

- `LuaScriptInspector`: enabled, script path, and ordered typed `Expose.*` parameters
- `variables` map exists and is applied into the environment (`ApplyVariablesToEnvironment`); spawn `vars` merge on instantiate
- Static metadata scanning ignores comments/strings and never executes gameplay code in Edit Mode
- Asset Browser creates scripts with an `Expose.number` example and `onAwake`/`onUpdate`/`onDestroy` templates
- **Maturity:** 3 attach · 4 variables UX

---

## API surface (verified in `LuaScriptEngine.cpp`)

| Namespace | Maturity | Notes |
|-----------|--------:|-------|
| `Entity` / `this` | 3 | GetTransform/Name/Camera/Call/IsValid on **receiver** |
| `Transform` | 3 | Global (running entity) + object methods |
| `Input` | 2 | Key/mouse poll, cursor mode — GLFW names |
| `Time` | 3 | deltaTime |
| `Camera` | 3 | Active camera + component FOV/planes |
| `Scene` | 3 | instantiate/destroy/find cameras/findAllWithPrefix |
| `Physics` | 2 | Motor + raycast + velocities; no forces/triggers |
| `Audio` | 2 | play/stop/pause/resume/volume |
| `UI` | 3 | Large widget API + `loadHUI` |
| `display.ui` / `destroy.ui` | 3 | HUI document lifecycle |
| `Application.quit` | 3 | |
| `engine_log` | 2 | |
| Materials / rendering | 0 | |
| Save / nav / animation | 0 | |

Two styles: OOP `this:GetTransform()` (recommended) and global `Transform.getPosition()` (legacy, current entity).

---

## Isolation / performance

- Per-instance Lua environment (good).
- No script profiler (README lists it as future).
- Hot reload re-executes chunks; cost is fine for small games.

---

## C++ legacy path

| Piece | Status |
|-------|--------|
| `ScriptEngine` / `ScriptBase` / `ScriptComponent` / `ExampleScript` | Still compiled; Qt play constructs `ScriptEngine` |
| `ScriptInspector.cpp` | Legacy C++ script UI remnants |
| Docs | “C++ scripting removed” |

**Maturity:** 1 Legacy. Gameplay samples are Lua (`scripts/fp-controller.lua`, `assets/ui/*.lua`).

---

## Can Lua support full game logic without native code?

**For a small Windows 3D game: mostly yes**, if the team accepts:

- Writing save/load themselves
- Raycasts instead of trigger messages
- Capsule FPS instead of animated characters
- GLFW key names
- 2D audio
- No runtime material edits

**Not yes** for mods (no sandbox), character action (no animation API), multiplayer, or anything needing engine events.

Sample `scripts/fp-controller.lua` is the strongest gameplay reference. Changelog Boomer-Shooter content is **not in this repository snapshot** (only mentioned in changelog / UX notes).

---

## Documentation

`Documentation/Scripting/LUA_SCRIPTING_GUIDE.md` and `engine/Scripting/README.md` mix accurate APIs with stale roadmaps (Input/Scene as future; hot-reload planned). See [DOCUMENTATION_GAPS.md](DOCUMENTATION_GAPS.md).

---

## Recommended next step

1. Stop constructing `ScriptEngine` in play.
2. Inspector for `variables`.
3. Headless Lua tests (instantiate, raycast mock or Jolt).
4. Trigger callbacks after physics overlap exists.
5. Do not add C#.
