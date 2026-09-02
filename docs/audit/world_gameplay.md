# World / gameplay systems audit

**Overall: 2 / 5 (Engine provides actors and hooks; games provide the game)**  
Runtime 2 · Editor 2 · Lua 2 · Tests 0

Hephaestus is **not** a gameplay framework. There is no GameMode, save, nav, AI, inventory, or messaging layer. Lua + prefabs + physics motor are the gameplay kit.

---

## Engine-provided

| System | Maturity | Evidence |
|--------|--------:|----------|
| Play / pause / stop (editor) | 3 | `PlayModeManager` |
| Camera component + play camera pick | 3 | `CameraComponent`; Lua `Camera` / `Scene` |
| Tag / name | 2 | `TagComponent`; prefix find |
| Prefab spawn/destroy | 3 | Lua `Scene.instantiate` / `destroy` |
| Rigid bodies / sensors / raycast | 3/2 | See [physics.md](physics.md) |
| FPS CharacterMotor | 3 | Lua `Physics.move` / `jump` / `isGrounded` |
| HUI menus/HUD | 3 | [ui.md](ui.md) |
| 2D audio | 2 | [audio.md](audio.md) |
| Lights / environment | 3 | Scene components |
| Editor spawnables | 3 | Meshes/lights/prefabs; empty future categories |

---

## Lua must implement

Game mode / win-lose, player state beyond capsule motor, interaction recipes (doors — raycast + animate transform), checkpoints, save/load, scene changes, inventory, AI, quests, damage, pooling, localization, input rebind, pause stack (partially sample menus).

---

## Investigated absences (not assumed required)

| Area | Maturity | Notes |
|------|--------:|-------|
| GameMode / flow | 0 | |
| Checkpoints | 0 | |
| Save games | 0 | See [save_system.md](save_system.md) |
| Level streaming / additive | 0 | Single `.hpscene` |
| Trigger scripting | 1 | Sensor flag only |
| Gameplay events / signals | 0 | Entity `Call` is ad-hoc |
| Timers | 0 | Use `onUpdate` |
| AI | 0 | |
| Navigation | 0 | [navigation.md](navigation.md) |
| Localization | 0 | |
| Inventory helpers | 0 | |
| Data assets (ScriptableObject-like) | 1 | JSON/Lua tables only |
| Networking | 0 | |

Issue draft `04-gameplay-components.md` is **obsolete** (rigid bodies/lights already exist).

---

## Camera

Editor camera ≠ game camera. Play selects `isActive` camera. Lua can drive pose. No spring arm, Cinemachine-style stack, or shake helper.

**Maturity:** 3 basic · 1 cinematic

---

## Recommended next step

Minimal **save + load scene** helpers and **trigger enter/exit** Lua. A tiny event bus if more than two scripts need decoupling. Do not build an RPG kit.
