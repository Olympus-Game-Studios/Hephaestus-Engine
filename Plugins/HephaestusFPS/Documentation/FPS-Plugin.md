# Hephaestus FPS Plugin 1.0

Enable `com.hephaestus.fps` in the Plugin Manager and restart. Run **Tools / FPS / Install FPS Input Defaults**. Place `plugin://com.hephaestus.fps/Prefabs/FPSPlayer.hpprefab` in a scene and press Play.

The plugin is off by default (`defaultEnabled` is false). Packaged games stage plugin `Content/` when the plugin is enabled at package time. They do not load `HephaestusFPS.dll`.

## Look invert

`invertY` defaults to true. `Input.getMouseDelta().y` is positive when the mouse moves down. With invert on, moving the mouse up looks up.

Facing yaw is `Character.setFacingYaw`. Camera pitch is the active camera entity's local X. Aim and movement use `FPS.Logic.yawBasis` / `aimRay`, which match `ActiveCameraSync` (parent yaw θ → render yaw −θ).

## Input

Install is additive. Existing names such as Jump are not rewritten. Disable does not remove Crouch or Fire.

`LookStickX` / `LookStickY` are the gamepad look axes. Mouse look uses `Input.getMouseDelta`, not `LookX`.

## Crouch camera

Keep `crouchEyeHeight` below `crouchedHeight` on the character controller so the camera stays inside the crouched capsule.

## Collision

Use the seed layers. Character must collide with World and Character. Hitscan uses layer `Character`, `ignoreSensors = true`, and the player as the ignore entity. Interaction uses the same aim ray with sensors enabled.

## Scripts

Lua modules live under `Content/Scripts/FPS/` and load with `require("FPS.Logic")` after `package.path` includes the plugin `Scripts` directory. Project `assets/scripts` is searched first.

Weapon, health, and HUD scripts are optional. Omitting them leaves first-person move and look intact.
