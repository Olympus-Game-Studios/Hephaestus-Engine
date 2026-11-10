# Hephaestus FPS Plugin 1.0

Enable `com.hephaestus.fps` in the Plugin Manager and restart. Run **Tools / FPS / Install FPS Input Defaults**. Place `plugin://com.hephaestus.fps/Prefabs/FPSPlayer.hpprefab` in a scene and press Play.

The plugin is off by default (`defaultEnabled` is false). Packaged games stage plugin `Content/` when the plugin is enabled at package time. They do not load `HephaestusFPS.dll`.

## Look invert

`invertX` and `invertY` default to false. In this renderer's yaw convention positive mouse X decreases controller yaw and turns the rendered view right; negative mouse Y increases camera pitch and looks up. Positive right-stick X turns right and positive right-stick Y looks down. Set the corresponding `invert*` option to true for inverted controls.

Facing yaw is `Character.setFacingYaw`. Camera pitch is the active camera entity's local X. Aim and movement use `FPS.Logic.yawBasis` / `aimRay`, which match `ActiveCameraSync` (parent yaw θ → render yaw −θ). Movement and look are applied in the same `onUpdate` before physics.

## Input

Install is additive. Existing names such as Jump are not rewritten. Disable does not remove Crouch or Fire.

`LookStickX` / `LookStickY` are the gamepad look axes. Mouse look uses `Input.getMouseDelta`, not `LookX`.

`ThrowFrag` (G, D-pad up) and `ThrowSmoke` (T, D-pad down) throw grenades.

## Crouch camera

Keep `crouchEyeHeight` below `crouchedHeight` on the character controller so the camera stays inside the crouched capsule.

## Collision

Use the seed layers. Character must collide with World and Character. Hitscan uses layer `Character`, `ignoreSensors = true`, and the player as the ignore entity. Interaction uses the same aim ray with sensors enabled, but steps past triggers that are not registered interactables, so a large trigger volume never hides a button or pickup inside it.

## Viewmodel

Put each weapon (an entity with `Weapon.lua`) under the player's camera. Its arms and gun meshes are children named `<Weapon>_Part_<anything>` with `WeaponPart.lua` and **First-Person View Model** ticked on their Mesh Renderer. The weapon's own position is its hip pose in the camera's frame: X forward, Y up, Z right. Everything else is procedural: ADS moves the weapon so the point `(sightUp, sightRight)` above its origin sits on the screen centre `adsForward` metres ahead, and kick, look inertia, idle, movement, walk bob, sprint lowering, reload dip and weapon raise are layered on top. Reload and raise tilt the weapon about the shoulders, 0.4 m behind its origin, so full-length arms stay out of the camera. Set `adsForward` so the eye sits a few centimetres behind the rear sight, not inside it.

Aligning iron sights: measure the rear sight (the aperture centre, or the top of the rear blade for a notch) and the front post tip in the weapon's own space. `sightPitch` and `sightYaw` (degrees, pitch up and yaw left positive) tilt the weapon in ADS so the line through those two points points straight ahead: pitch is `atan(rise / length)` of the line negated, yaw is `atan(rightward drift / length)`. `sightUp` and `sightRight` are where that line crosses the weapon's origin plane (X = 0). Then the front post tip lands on the screen centre, where the shot goes, with the rear sight centred around it. Leave both at 0 for sights that are already parallel to the barrel.

Set the camera's **View Model FOV** (`viewModelFov` in the scene) to draw the arms and weapon with their own field of view. The world then zooms for ADS while the gun stays the same size on screen, and a value below the world field of view makes it look less stretched. 0 uses the camera's field of view. Keep the camera's **Near** small (0.02) so the weapon is not cut when it comes up to the eye.

## Gunplay

Every weapon is tuned in data, so a rifle and a pistol differ only in their numbers. Put the numbers in a weapon data file: a Lua module under the project's `assets/scripts` that returns a table, for example `assets/scripts/weapons/Rifle.lua` returning `{ displayName = "Rifle", roundsPerSecond = 12.5, ... }`. Set the weapon's `definition` variable to its module name (`weapons.Rifle`). A value in the data file replaces the same `Weapon.lua` variable; anything it leaves out keeps the variable's value. Weapons without a `definition` use their variables as before. Distances are in metres, angles in degrees, times in seconds and spring frequencies in radians per second.

- **ADS**: hold `Aim`. Blends over `adsSeconds`. The weapon moves on a smoothstep; the field of view (from the player's `fieldOfView` to `adsFov`) and look speed (`adsSensitivityScale`) follow a gentler sine curve, so the zoom and the turn speed change together. Move speed scales by `adsMoveMultiplier`.
- **Recoil** moves the real aim. `recoilPattern` is `pitch,yaw;pitch,yaw;...` in degrees per shot of a string (the last entry repeats), plus `recoilJitter`. The jitter comes from `recoilSeed` and the shot number, so the same string always kicks the same way and can be learned; spread uses the same seed. ADS scales recoil by `adsRecoilScale`. After `recoilRecoveryDelay` the aim returns at `recoilRecoveryDegreesPerSecond`; pulling down against the recoil counts towards it, so recovery never drags the aim below where you held it.
- **Weapon kick** is separate from recoil and only moves the gun. `kickBack` and `kickUp` (metres) and `kickRise`, `kickSide` and `kickRoll` (degrees) are the largest movement one shot gives; side and roll pick a seeded direction per shot. They run on damped springs with `kickFrequency` and `kickDampingRatio`, and ADS scales them by `adsKickScale`.
- **Camera kick**: each shot also nudges the camera by `cameraKickPitch` and up to `cameraKickYaw` degrees on its own spring (`cameraKickFrequency`, `cameraKickDampingRatio`). It changes what you see, never where shots go. Landings dip the camera slightly too.
- **Look inertia**: the weapon trails how fast you turn by `swayLag` seconds, at most `swayMaxDegrees`, settling on a spring (`swayFrequency`, `swayDampingRatio`). A heavier weapon uses a lower frequency. `swayAmount` scales it and ADS scales it by `adsSwayScale`.
- **Idle**: the weapon breathes (`idleBreathRate` cycles per second) and drifts on smooth seeded noise by about `idleAmount` degrees, more after sprinting. ADS scales it by `idleAdsScale`.
- **Movement**: the weapon trails your velocity by `moveLag` metres per metre per second, at most `moveMaxOffset`, on a spring of `moveFrequency`, so starting, stopping and strafe reversals overshoot. It rolls up to `strafeRoll` degrees into strafes, dips `landKick` metres per metre per second of landing speed, and dips `footfallDip` on each footstep. The walk bob is a figure eight scaled by `bobAmount` whose low points land on the footsteps.
- **Spread**: hip fire uses a cone of `hipSpread` degrees, widened by movement (`moveSpread` at the player's `fullSpreadMoveSpeed`), by being airborne (`airSpread`) and by sustained fire (`bloomPerShot`, `maxBloom`, `bloomDecay`), capped at `maxSpread`. ADS uses `adsSpread`.
- **Damage**: `damage` up to `falloffStart` metres, falling linearly to `falloffMinMultiplier` at `falloffEnd`. Head hits multiply by `headMultiplier` (1.5), limbs by `limbMultiplier` (0.9).
- **Hit zones**: give a target separate colliders with `HitZone.lua`, `owner` set to the target's entity name and `zone` set to `head`, `body` or `limb`. The owner keeps the health (`FPS.Health`) and receives `onHit(amount, zone, distance)`.
- **Hit feedback**: the HUD shows a white hit marker, yellow for a headshot, and a red kill marker.
- **Ammunition**: a weapon holds a magazine and one chambered round, and starts with `magazineSize` rounds (one of them chambered). The HUD counts both.
- **Reloads** are timed events. `tacticalReload = { seconds, detach, insert }` is used with a round chambered: the magazine comes out at `detach` and the new one goes in at `insert`, where the rounds move, so a tactical reload ends with `magazineSize` plus the chambered round (30+1). `emptyReload = { seconds, detach, insert, chamber }` also chambers a round at `chamber`. `reloadSound`, `reloadPart2Sound` and `boltSound` play at detach, insert and chamber. Weapon sounds play on the `weapons` bus. Rounds left in the old magazine go back to the reserve. Sprinting or switching weapons stops a reload where it is: before the insert you keep the old magazine; after the insert of an empty reload the chamber is still empty and the next reload or trigger pull only racks the action. Weapons without these tables use `reloadSeconds`, `reloadEmptySeconds`, `reloadSoundTime`, `reloadPart2Time` and `boltTime`.
- **Shot sound**: `fireLayers` lists the sounds one shot plays together, for example a sharp transient, the mechanical action and a tail: `{ { sound = "...", volume = 0.7, pitchJitter = 0.03, volumeJitter = 0.06 }, ... }`. Each layer varies a little every shot. Without it the weapon plays `fireSound` at `fireVolume`, varied by `firePitchJitter` and `fireVolumeJitter`.
- **Muzzle flash**: give a weapon children named `<Weapon>_Flash_<anything>` with `MuzzleFlash.lua`: a flat quad (the built-in plane works) at the muzzle with an additive material and **First-Person View Model** ticked. Each shot shows one of them, chosen from the weapon's seed, for `flashSeconds` with a random roll and size.
- **Bullet holes**: list decal prefabs on the player's `impactDecalPrefabs` (`;` separated). A shot that hits a surface without health or a hit zone places one of them on the surface, its +Y axis along the surface normal, lifted slightly so it never flickers into the surface. The oldest are removed once there are more than `maxImpactDecals`. Use an alpha-blended material so the edges fade.
- **Impact and muzzle effects**: the player's `impactDustPrefab` plays where a shot hits an ordinary surface and `impactSparksPrefab` where it hits a target with health; each is spawned with its +Y along the surface normal. A weapon's `muzzleSmokePrefab` plays just past the muzzle on every shot, pointing along the aim. Use particle prefabs with **Destroy When Done** so they remove themselves.
- **Timing**: firing waits `sprintToFireSeconds` after a sprint ends and `raiseSeconds` after a weapon is raised. A trigger press within `fireBufferSeconds` of the end of that wait fires as soon as it ends. Switching lowers the current weapon over its `lowerSeconds` first. Weapons are ordered by `slot`.
- **Movement**: the player moves at full speed straight ahead, at `strafeSpeedScale` (0.85) sideways and `backpedalSpeedScale` (0.7) backwards, with diagonals in between, and at `crouchSpeedScale` (0.6) crouched; these multiply with the weapon's ADS movement scale. Sprinting (`sprintMultiplier`) only works moving forward. Landing from a fall of at least `landingSlowMinSpeed` m/s slows the player to `landingSlowScale` for `landingSlowSeconds`, easing back. The player prefab's Character Controller has 0.12 s of Coyote Time and Jump Buffer.
- **Slide**: pressing Crouch while sprinting on the ground slides along the way the player is moving, starting at `slideBoost` times sprint speed and easing to crouch speed over `slideSeconds`. Move input only steers it slowly (`slideSteer`). The player is crouched, the camera tilts by `slideRollDegrees` (scaled by `viewBobScale`) and the field of view widens by `slideFovBoost`. The weapon can fire once its sprint-to-fire time has passed. Jumping ends the slide in a jump that keeps its speed; otherwise it ends after `slideSeconds` or on leaving the ground, and the next slide waits `slideCooldown`. Holding Crouch stays crouched afterwards. Turn it off with `slideEnabled`.
- **Mantle and vault**: jumping while moving forward at a ledge within `mantleReach` metres (or holding Jump in the air when one comes in reach) climbs it instead of jumping. A ledge from 0.4 m up to `mantleMaxHeight` with room to stand on top is climbed onto (a mantle); one up to `vaultMaxHeight` high and under about 0.9 m deep, with lower ground behind it, is vaulted over. The move takes `mantleSeconds` plus `mantleSecondsPerMetre` per metre of height, during which the weapon dips and cannot fire. Turn it off with `mantleEnabled`.
- **Comfort**: the player's `viewBobScale` (0 to 1) scales the weapon's bob, idle and movement motion, and `cameraShakeScale` (0 to 1) scales the camera kick. Neither changes recoil, spread or anything else that affects where shots go. `presentationEnabled` off sets both to 0.

## Grenades

Set the player's `fragGrenadePrefab` and `smokeGrenadePrefab` to grenade prefabs; while both are empty, grenades are off and the HUD hides their count. The player starts with `fragGrenades` and `smokeGrenades` of each. Pressing a throw key arms the grenade and lowers the weapon over the first `throwReleaseFraction` of `throwSeconds`; firing and aiming are blocked and a reload is cut. Hold the key to aim: a dotted arc (`throwPreviewDots` copies of `throwPreviewDotPrefab`, from `throwPreviewStartDistance` metres out) and markers (`throwPreviewMarkerPrefab`, its +Y along the surface) show the throw's path: a half-size marker at each of up to `throwPreviewBounces` bounces and a full-size one where it comes to rest, or where it will be after `throwPreviewSeconds` (3, the frag's fuse) if it is still moving. Bounces use `throwPreviewRestitution` and `throwPreviewFriction`; the defaults (0.2 and 0.55) match the grenade prefabs on ordinary ground. Grenades get a random spin, so later bounces vary by a metre or so from throw to throw; lower the grenade's `spin` to make them more predictable. Let go to throw; a quick tap throws as soon as the wind-up ends, and the weapon comes back up over the rest of `throwSeconds`. A grenade is only used once it leaves the hand, so losing control (death, a menu) cancels the throw. Set `throwPreviewDamping` to the grenade's Rigid Body Linear Damping (and the restitution and friction to match its Rigid Body) so the preview matches its flight, or turn the preview off with `throwPreviewEnabled`. The grenade leaves from beside the eye, along the aim lifted by `throwLift`, at `throwSpeed` metres a second plus the player's own velocity. `grenadeArmSound` plays when a throw starts and `grenadeThrowSound` when the grenade leaves, both at `grenadeSoundVolume`.

A grenade prefab is a mesh with a dynamic Rigid Body, a collision shape and `Grenade.lua`:

- `kind` is `frag` or `smoke`, and `fuseSeconds` starts counting when it is thrown.
- A frag spawns `effectPrefab` and plays `sound` (3D, heard out to `soundMaxDistance`), then damages everything with health within `radius`: full `damage` inside `innerRadius`, falling to none at `radius`. Cover between the blast and a target blocks it, and that includes the player. The player's camera shakes by up to `shakeDegrees` within `shakeRadius`.
- A smoke grenade spawns `effectPrefab` (the cloud) and plays `sound`, and removes itself after `smokeSeconds`.

Use a box collision shape: a sphere or capsule never stops rolling, because the physics has no rolling resistance.

## Scripts

Lua modules live under `Content/Scripts/FPS/` and load with `require("FPS.Logic")` after `package.path` includes the plugin `Scripts` directory. Project `assets/scripts` is searched first.

Weapon, health, and HUD scripts are optional. Omitting them leaves first-person move and look intact.
