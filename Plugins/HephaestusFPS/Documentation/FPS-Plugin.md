# Hephaestus FPS Plugin 1.0

Enable `com.hephaestus.fps` in the Plugin Manager and restart. Run **Tools / FPS / Install FPS Input Defaults**. Place `plugin://com.hephaestus.fps/Prefabs/FPSPlayer.hpprefab` in a scene and press Play.

The plugin is off by default (`defaultEnabled` is false). Packaged games stage plugin `Content/` when the plugin is enabled at package time. They do not load `HephaestusFPS.dll`.

## Look invert

`invertX` and `invertY` default to false. In this renderer's yaw convention positive mouse X decreases controller yaw and turns the rendered view right; negative mouse Y increases camera pitch and looks up. Positive right-stick X turns right and positive right-stick Y looks down. Set the corresponding `invert*` option to true for inverted controls.

Facing yaw is `Character.setFacingYaw`. Camera pitch is the active camera entity's local X. Aim and movement use `FPS.Logic.yawBasis` / `aimRay`, which match `ActiveCameraSync` (parent yaw θ → render yaw −θ). Movement and look are applied in the same `onUpdate` before physics.

## Input

Install is additive. Existing names such as Jump are not rewritten. Disable does not remove Crouch or Fire.

`LookStickX` / `LookStickY` are the gamepad look axes. Mouse look uses `Input.getMouseDelta`, not `LookX`.

## Crouch camera

Keep `crouchEyeHeight` below `crouchedHeight` on the character controller so the camera stays inside the crouched capsule.

## Collision

Use the seed layers. Character must collide with World and Character. Hitscan uses layer `Character`, `ignoreSensors = true`, and the player as the ignore entity. Interaction uses the same aim ray with sensors enabled, but steps past triggers that are not registered interactables, so a large trigger volume never hides a button or pickup inside it.

## Viewmodel

Put each weapon (an entity with `Weapon.lua`) under the player's camera. Its arms and gun meshes are children named `<Weapon>_Part_<anything>` with `WeaponPart.lua` and **First-Person View Model** ticked on their Mesh Renderer. The weapon's own position is its hip pose in the camera's frame: X forward, Y up, Z right. Everything else is procedural: ADS moves the weapon so the point `(sightUp, sightRight)` above its origin sits on the screen centre `adsForward` metres ahead, and kick, sway, walk bob, sprint lowering, reload dip and weapon raise are layered on top. Reload and raise tilt the weapon about the shoulders, 0.4 m behind its origin, so full-length arms stay out of the camera. Set `adsForward` so the eye sits a few centimetres behind the rear sight, not inside it.

Set the camera's **View Model FOV** (`viewModelFov` in the scene) to draw the arms and weapon with their own field of view. The world then zooms for ADS while the gun stays the same size on screen, and a value below the world field of view makes it look less stretched. 0 uses the camera's field of view. Keep the camera's **Near** small (0.02) so the weapon is not cut when it comes up to the eye.

## Gunplay

Every weapon is tuned on its `Weapon.lua` variables, so a rifle and a pistol differ only in data.

- **ADS**: hold `Aim`. Blends over `adsSeconds`, narrows the camera field of view from the player's `fieldOfView` to `adsFov`, scales move speed by `adsMoveMultiplier` and look speed by `adsSensitivityScale`.
- **Recoil** moves the real aim. `recoilPattern` is `pitch,yaw;pitch,yaw;...` in degrees per shot of a string (the last entry repeats), plus `recoilJitter`. ADS scales it by `adsRecoilScale`. After `recoilRecoveryDelay` the aim returns at `recoilRecoveryDegreesPerSecond`; pulling down against the recoil counts towards it, so recovery never drags the aim below where you held it. The gun's visual kick (`kickBack`, `kickRise`, `kickRoll`) is separate and runs on springs.
- **Spread**: hip fire uses a cone of `hipSpread` degrees, widened by movement (`moveSpread` at the player's `fullSpreadMoveSpeed`), by being airborne (`airSpread`) and by sustained fire (`bloomPerShot`, `maxBloom`, `bloomDecay`), capped at `maxSpread`. ADS uses `adsSpread`.
- **Damage**: `damage` up to `falloffStart` metres, falling linearly to `falloffMinMultiplier` at `falloffEnd`. Head hits multiply by `headMultiplier` (1.5), limbs by `limbMultiplier` (0.9).
- **Hit zones**: give a target separate colliders with `HitZone.lua`, `owner` set to the target's entity name and `zone` set to `head`, `body` or `limb`. The owner keeps the health (`FPS.Health`) and receives `onHit(amount, zone, distance)`.
- **Hit feedback**: the HUD shows a white hit marker, yellow for a headshot, and a red kill marker.
- **Reloads**: `reloadSeconds` with rounds left, `reloadEmptySeconds` from empty. Emptying the magazine starts the reload. `reloadSound`, `reloadPart2Sound` and `boltSound` play at `reloadSoundTime`, `reloadPart2Time` and `boltTime` seconds into the reload; the bolt sound only on an empty reload.
- **Timing**: firing waits `sprintToFireSeconds` after a sprint ends and `raiseSeconds` after a weapon is raised. Switching lowers the current weapon over its `lowerSeconds` first. Weapons are ordered by `slot`.

## Scripts

Lua modules live under `Content/Scripts/FPS/` and load with `require("FPS.Logic")` after `package.path` includes the plugin `Scripts` directory. Project `assets/scripts` is searched first.

Weapon, health, and HUD scripts are optional. Omitting them leaves first-person move and look intact.
