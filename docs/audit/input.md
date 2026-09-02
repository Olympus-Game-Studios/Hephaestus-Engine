# Input audit

**Overall: 3 / 5 (Action/axis foundation)**
Runtime 3 · Editor 3 · Lua 3 · Tests 3

Gameplay reads a shared `InputSystem` populated by either the Qt Play host or GLFW
runtime. Project-owned `.hpinput` v1 assets define action and one-dimensional axis
bindings. The Project Settings Input page edits the selected profile.

---

## Keyboard / mouse

- **Exists:** Yes
- **Maturity:** 2
- **Lua:** action down/pressed/released, axes, raw compatibility polls, position,
  delta, wheel, and cursor mode
- **Packaged game:** GLFW host populates the shared input frame before scripts
- **Editor play:** Qt host populates the same input frame and preserves captured deltas
- **Compatibility:** legacy `Input.isKeyPressed` intentionally retains held semantics

---

## Gamepad / hotplug / vibration / dead zones / multi-controller

- **Exists:** Player-zero GLFW gamepad in packaged runtime
- **Maturity:** 2
- **Supported:** Standard GLFW buttons/axes, disconnect reset, per-binding deadzones
- **Limitation:** Qt Play gamepad, rebinding capture, vibration, and multi-controller
  assignment remain follow-ups

---

## Action / axis mapping / contexts / rebinding / serialization

- **Exists:** `.hpinput` v1 JSON, actions, 1D axes, multiple scaled bindings,
  validation, round-trip tests, Project Settings authoring
- **Maturity:** 3
- **Contexts:** intentionally absent from v1; add only with a later schema when runtime
  context behavior is implemented

---

## UI vs gameplay input

- PlayModeManager routes to runtime when playing
- HephaestusUI consumes mouse in UI cursor mode
- Escape exits editor play (changelog) — editor concern, not a game pause stack

**Maturity:** 2 (works for FPS after significant Qt glue)

---

## Lua / settings

- New projects receive `assets/input/Default.hpinput`
- `project.input.defaultInputProfile` selects the portable profile path
- Unknown Lua action/axis names return safe defaults and warn once

---

## Editor camera vs game

- `EditorCameraController` + `CameraInputState` — editor only
- Game cameras: `CameraComponent` + Lua

---

## Verdict

**Reusable keyboard/mouse and packaged-gamepad action mapping is available. Contexts,
runtime rebinding, Qt gamepad parity, and richer axis types remain future work.**

---

## Recommended next step

Add `.hpinput` v2 contexts when their runtime semantics are implemented, then add
capture-based rebinding and Qt-native gamepad polling without changing the shared
`InputSystem`.
