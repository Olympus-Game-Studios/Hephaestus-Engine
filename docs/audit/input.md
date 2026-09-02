# Input audit

**Overall: 2 / 5 (Raw poll, not an input system)**  
Runtime 2 · Editor 2 · Lua 2 · Tests 0

Developers talk to **GLFW key names** (and a Qt-filled `RuntimeInputState` in editor play). There is **no** `InputManager`, action map, rebind UI, or engine gamepad API.

GLFW itself supports joysticks/gamepads; **engine code does not call them** (matches only in `extern/glfw`).

---

## Keyboard / mouse

- **Exists:** Yes
- **Maturity:** 2
- **Lua:** `Input.isKeyPressed`, `isMouseButtonPressed`, `getMousePosition`, `getMouseDelta`, `setCursorMode`
- **Packaged game:** GLFW window on `LuaScriptEngine`
- **Editor play:** Host copies keys/mouse/delta into `RuntimeInputState` because the Qt overlay can steal mouse move (changelog FPS look fixes)
- **Limitation:** Held poll only — no pressed/released edges unless scripts edge-detect

---

## Gamepad / hotplug / vibration / dead zones / multi-controller

- **Exists:** No engine API
- **Maturity:** 0

---

## Action / axis mapping / contexts / rebinding / serialization

- **Exists:** No
- **Maturity:** 0
- Cursor **Game vs UI** is the only context (`UI.setCursorMode` / `Input.setCursorMode`)

---

## UI vs gameplay input

- PlayModeManager routes to runtime when playing
- HephaestusUI consumes mouse in UI cursor mode
- Escape exits editor play (changelog) — editor concern, not a game pause stack

**Maturity:** 2 (works for FPS after significant Qt glue)

---

## Lua / settings

- No project input-map asset
- Settings screens cannot offer rebind without a new system

---

## Editor camera vs game

- `EditorCameraController` + `CameraInputState` — editor only
- Game cameras: `CameraComponent` + Lua

---

## Verdict

**Usable for WASD + mouse look prototypes. Not a reusable input architecture.**

---

## Recommended next step

Action/axis assets + GLFW gamepad state + dead zone. Keep Lua `Input.isActionPressed("Jump")`. Rebind UI after HUI sliders/fields. Do not bind gameplay to Qt keys in packaged games.
