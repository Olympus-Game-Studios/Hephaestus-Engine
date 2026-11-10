# Hephaestus FPS Plugin 1.0

Enable `com.hephaestus.fps` in the Plugin Manager and restart. Run **Tools / FPS / Install FPS Input Defaults**. Place `plugin://com.hephaestus.fps/Prefabs/FPSPlayer.hpprefab` in a scene and press Play.

The plugin is off by default (`defaultEnabled` is false). Packaged games stage plugin `Content/` when the plugin is enabled at package time. They do not load `HephaestusFPS.dll`.

## Look invert

`invertX` and `invertY` default to false. In this renderer's yaw convention positive mouse X decreases controller yaw and turns the rendered view right; negative mouse Y increases camera pitch and looks up. Positive right-stick X turns right and positive right-stick Y looks down. Set the corresponding `invert*` option to true for inverted controls.

Facing yaw is `Character.setFacingYaw`. Camera pitch is the active camera entity's local X. Aim and movement use `FPS.Logic.yawBasis` / `aimRay`, which match `ActiveCameraSync` (parent yaw θ → render yaw −θ). Movement and look are applied in the same `onUpdate` before physics.

## Input

Install is additive. Existing names such as Jump are not rewritten. Disable does not remove Crouch or Fire.

`LookStickX` / `LookStickY` are the gamepad look axes. Mouse look uses `Input.getMouseDelta`, not `LookX`.

`ThrowFrag` (G, D-pad up) and `ThrowSmoke` (T, D-pad down) throw grenades. `Prone` (X, right stick press) lies down. `LeanLeft` (C) and `LeanRight` (V) lean while held. `Pause` and the `Menu...` actions drive the menus (see Menus and settings).

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
- **Bullet travel**: a weapon with `muzzleVelocity` above 0 fires bullets that take time to arrive and drop under gravity (`bulletGravityScale`) instead of hitting instantly. Each frame a bullet moves on and checks the stretch it covered, so close shots still land on the frame they are fired. Bullets stop at `range` or after `bulletMaxSeconds`, keep flying after a weapon swap, and damage falloff uses the distance actually flown. 0 keeps instant hitscan.
- **Shell casings**: a weapon's `casingPrefab` is ejected on every shot from beside the action, up and to the right at `casingEjectSpeed` m/s plus the player's own velocity. Give the prefab a mesh, a small box collision shape, a light dynamic Rigid Body and `Casing.lua`, which sets its launch speed and tumble and removes it after `lifeSeconds`. Each weapon keeps at most `maxCasings`, removing the oldest first.
- **Impact and muzzle effects**: the player's `impactDustPrefab` plays where a shot hits an ordinary surface and `impactSparksPrefab` where it hits a target with health; each is spawned with its +Y along the surface normal. A weapon's `muzzleSmokePrefab` plays just past the muzzle on every shot, pointing along the aim. Use particle prefabs with **Destroy When Done** so they remove themselves.
- **Timing**: firing waits `sprintToFireSeconds` after a sprint ends and `raiseSeconds` after a weapon is raised. A trigger press within `fireBufferSeconds` of the end of that wait fires as soon as it ends. Switching lowers the current weapon over its `lowerSeconds` first. Weapons are ordered by `slot`.
- **Movement**: the player moves at full speed straight ahead, at `strafeSpeedScale` (0.85) sideways and `backpedalSpeedScale` (0.7) backwards, with diagonals in between, and at `crouchSpeedScale` (0.6) crouched; these multiply with the weapon's ADS movement scale. Sprinting (`sprintMultiplier`) only works moving forward. Landing from a fall of at least `landingSlowMinSpeed` m/s slows the player to `landingSlowScale` for `landingSlowSeconds`, easing back. The player prefab's Character Controller has 0.12 s of Coyote Time and Jump Buffer.
- **Lean**: holding `LeanLeft` or `LeanRight` moves the view and the aim `leanDistance` (0.35 m) sideways over `leanSeconds` and rolls the camera `leanRollDegrees`, so the player can see and shoot round a corner. A wall on that side stops the head 0.2 m short of it. Leaning is off while sprinting, sliding or climbing. Turn it off with `leanEnabled`.
- **Prone**: `Prone` toggles lying down (the Character Controller's Prone Height); Crouch, Jump or Sprint get up instead, and Jump stands without jumping. Under something too low to stand, the player stays down until there's room. Prone moves at `proneSpeedScale` (0.25), the eye goes to `proneEyeHeight` at `proneEyeBlendMetersPerSecond`, and sprinting, sliding and mantling are off. Weapons spread `proneSpreadScale` (0.6) and recoil `proneRecoilScale` (0.7) as much as standing. Turn it off with `proneEnabled`.
- **Tactical sprint**: double-tapping Sprint while running (pressing it again within 0.3 s of letting go) runs at `tacticalSprintMultiplier` (1.9, against `sprintMultiplier` 1.6) for up to `tacticalSprintSeconds` of charge, then drops back to a normal sprint. The charge refills from empty in `tacticalSprintRechargeSeconds` and needs a quarter of it to start. The weapon is carried barrel-up and its `tacticalSprintToFireSeconds` (0.38) replaces `sprintToFireSeconds` afterwards; the field of view widens by `tacticalSprintFovBoost`. A slide from a tactical sprint starts from the faster speed. Turn it off with `tacticalSprintEnabled`.
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

### Grenade warning

Every live frag within `grenadeWarningRange` metres of the player (or inside its blast radius, if that is larger) gets a warning on the HUD: `grenadeWarningIcon`, `grenadeWarningIconSize` pixels across, on a ring `grenadeWarningRadius` pixels round the crosshair in the direction of the grenade, with an arrow (0.75 of the icon's size) just outside it pointing the same way. Straight up means in front of you and straight down means behind. It is white when the grenade is near but out of reach, orange when the blast would hurt you and red, pulsing, when it would kill you at your current health. Your own grenades count too. Up to four show at once, nearest first. The icons should be white so the colours tint them. The arrow is `grenadeWarningArrowSteps` images pointing round the clock, named by `grenadeWarningArrowPattern` with the step number (`assets/textures/ui/grenade_arrow_%02d.png` for steps 00 to 23, where 00 points up and each step turns clockwise). Leave the icon or pattern empty, or turn off `grenadeWarningEnabled`, to hide the warning.

## Enemies

An enemy is an entity with a Character Controller, a Character (its model and animations) and `Enemy.lua`, with hit zone children: boxes with a **kinematic** Rigid Body and `HitZone.lua` whose `owner` is left empty, so the zone belongs to its parent. Kinematic zones follow the enemy as it moves and are not part of the navmesh. Enemies walk on the [navmesh](../../../docs/user-guide/lua-api.md#navigation), so the level's walls and cover need static box colliders.

What an enemy does:

- **Patrol**: walks between `patrolPoints` (`"x,z;x,z"`) or to random points within `patrolRadius` of where it started, waiting `patrolWaitSeconds` at each.
- **Notice**: sees the player inside its view cone and sight range when nothing is in the way, getting aware faster the closer, more upright and faster moving the player is. Within 3 m it notices you from any side. It hears gunshots and explosions (the player's weapons report a noise with `noiseRadius`) and goes to look.
- **Fight**: after its reaction time it fires bursts at you. Its aim starts loose and settles while it keeps you in sight, and is worse against a moving target. It reloads after `magazineSize` shots.
- **Cover**: looks for a spot on the navmesh within `coverSearchRadius` that hides it from you, preferring one with a peek spot a step to the side. It hides, steps out to shoot, steps back, and reloads while hidden. If you can see it at its cover, it finds another.
- **Search**: when it has lost sight of you for a while it either pushes to where it last saw you or goes there carefully, then looks around and goes back to patrolling.
- **Pressure**: every `tacticsIntervalSeconds` while you stay hidden it may throw a frag at where it last saw you (`fragGrenadePrefab`, thrown at `throwAngleDegrees`, doing `grenadeDamage` times the difficulty's scale) or go round your cover to a spot that can see you. Only one enemy flanks at a time.
- **Grenades**: it runs from a frag that has landed near it. **Squad**: it tells allies near it where you are.
- **Death**: it goes limp as a ragdoll, pushed away from the player by `ragdollImpulse`, or away from the explosion by `ragdollBlastImpulse` when a frag kills it, and drops its weapon. Bodies stay until more than `maxCorpses` (8; 0 for no limit) lie in the level, counting every enemy; then the oldest body and its dropped weapon are removed. Set `corpseSeconds` to also remove a body after that long. A character that can't ragdoll falls over instead.

The model turns to face where it is walking or aiming. Set `modelYawOffsetDegrees` to how far the model must turn to face +X (90 for a Mixamo character, which faces +Z). `idleClip`, `walkClip` and `runClip` are cross-faded by speed. Shots come from `eyeHeight`, do `damage` up to `range` metres, and play `fireSound` in 3D. Turn on `debugAI` to log every state change, shot, hit, grenade and death.

To give an enemy a visible weapon, add a child with the weapon's Mesh Renderer (barrel along +X) and `HeldWeapon.lua`. Each frame it puts the grip at the character's `gripSocket` (default `RightHand`) and points the barrel at `supportSocket` (default `LeftHand`), moved `gripForward` metres along the barrel and `gripUp` above it. The character needs both sockets on its hand bones. Set `dropPrefab` to a physics copy of the weapon with `Casing.lua` and the enemy drops it when it dies.

### Difficulty

Everything about how well enemies fight comes from `FPS.AI.Difficulty`. It has four presets, `recruit`, `regular` (the default), `hardened` and `veteran`, each with these values:

| Value | Means |
| --- | --- |
| `reactionSeconds` | Delay between noticing you and the first shot. |
| `aimErrorDegrees`, `aimErrorMinDegrees`, `aimSettleSeconds` | Aim error when you are first seen, after it settles, and how long settling takes. |
| `movingTargetErrorDegrees` | Extra error for each metre a second you are moving. |
| `burstMin`, `burstMax`, `roundsPerSecond`, `burstPauseSeconds` | Burst length, fire rate and the pause between bursts. |
| `damageScale` | Multiplies the enemy's `damage`. |
| `sightRange`, `fovDegrees` | How far and how wide it sees. |
| `awarenessPerSecond`, `awarenessDecayPerSecond` | How fast it notices you, and forgets a glimpse. |
| `hearingScale` | Multiplies how far noises carry to it. |
| `coverChance`, `peekSeconds`, `hideSeconds` | How often it takes cover and how long it peeks and hides. |
| `lostTargetSeconds`, `aggression` | How long before it searches, and the chance it pushes instead of being careful. |
| `grenadeAwareness`, `squadAlerts`, `allyAlertRadius` | Whether it runs from frags, whether it warns allies, and how far. |
| `flankChance`, `flankDistance` | Chance to go round your cover each time it considers it, and how far from you it goes. |
| `grenadeChance`, `grenadeCount`, `grenadeCooldownSeconds` | Chance to throw a frag each time it considers it, how many it carries, and the time between throws. |
| `grenadeMinDistance`, `grenadeMaxDistance`, `grenadeErrorMetres`, `grenadeDamageScale` | The range it throws at, how far from you a throw may land, and its damage multiplier. |

A settings menu can change all enemies at once: `Difficulty.setLevel("hardened")` picks a preset, `Difficulty.setOverride("damageScale", 0.5)` changes one value on top of any preset (`nil` removes it), and `Difficulty.clearOverrides()` removes them all. Enemies read their settings when they spawn. One enemy can use its own level with its `difficulty` variable, and its own values with `skillOverrides` (`"aggression=0.9;grenadeAwareness=false"`), which win over everything else.

### Adding behaviour

The enemy is a state machine (`FPS.AI.Brain`). Each state in `Enemy.lua`'s `states` table has `enter`, `update` and `exit` functions, and `update` returns the name of the next state, so a new behaviour is a new state. The parts it uses are separate modules you can use for other kinds of enemy: `FPS.AI.Tactics` (flank spots and grenade throws), `FPS.AI.Perception` (sight and hearing), `FPS.AI.Mover` (following navmesh paths), `FPS.AI.Cover` (finding cover) and the noise and danger lists in `FPS.Registry` (`reportNoise`, `setDanger`, `clearDanger`).

## Waves and restarting

`WaveDirector.lua` turns a level into a wave survival game. Put it on any entity and set `enemyPrefab`, `spawnPoints` (`"x,z;x,z"`) and `waves`, a list of enemy counts with an optional difficulty level each (`"3:recruit;5:regular;7:hardened"`; no level means the global one). After `startDelaySeconds` the first wave begins: its enemies appear one every `spawnIntervalSeconds` at a spawn point at least `minSpawnDistance` metres from the player (the farthest one if none is that far) and come looking for the player (`huntOnSpawn` on the enemy). When they are all dead a "Wave cleared" message shows for `breakSeconds` before the next wave, and clearing the last wave wins. The top of the screen shows the wave and how many enemies are left.

When the player dies the screen darkens with "You died", and when the last wave is cleared it shows "All waves cleared". After `restartDelaySeconds` the player's `restartPrompt` appears, and pressing Jump restarts the level (`Scene.reload`). Turn this off with `restartEnabled`. Other scripts can show the same large message with `Hud.showBanner(title, subtitle, seconds)`.

## Menus and settings

Escape, F1 or gamepad Start opens the in-game menu (in Editor Play Escape stops Play, so use F1); F1 or Start also closes it from any page: Resume, Settings, Restart, Quit to menu and Quit game. The game keeps running behind it, as it must in multiplayer; only your own player stops responding until you close it, and a click still held from the menu does not fire. Set the player's `pauseFreezesTime` to stop the game while the menu is open instead (single-player only). `pauseMenuRestart` false hides Restart, Quit to menu shows only when `mainMenuScene` is set to a scene path, and `pauseMenuEnabled` turns the menu off. Menus move with the arrow keys or WASD, choose with Enter or Space, go back with Escape or Backspace; the orange highlight slides to the chosen item and its label eases a little to the right. They also work with the mouse (an item reacts only when the pointer is over it) and the gamepad D-pad. These keys are the `Pause` and `Menu...` actions that Install FPS Input Defaults adds.

Settings holds look sensitivity, field of view (the player's `fieldOfView` until changed), view bob, camera shake, master, effects, weapons and music volume, difficulty, and Reduce Motion, which makes the menus' highlight jump instead of slide. Changes apply at once and are saved when you leave the page, in the player's data folder (`Storage`), so they are the same in every level and next time.

To make a main menu, put a script on the menu scene's camera that builds one with `FPS.Menu`:

```lua
local Menu = require("FPS.Menu")
local Settings = require("FPS.Settings")
local menu

function onAwake()
    Settings.load()
    UI.setCursorMode(CursorMode.UI)
    Camera.setActive(true)
    menu = Menu.new("MainMenu", { hints = "ENTER  SELECT        ESC  BACK" })
    Menu.setPage(menu, { items = {
        { label = "PLAY", action = function() Scene.load("assets/scenes/Level.hpscene") end },
        { label = "SETTINGS", action = function() Menu.push(menu, Settings.page()) end },
        { label = "QUIT", action = function() Settings.save(); Application.quit() end },
    } })
end

function onUpdate(dt)
    Menu.update(menu)
end
```

A page item is a button (`label`, `action`), a slider (`kind = "slider"`, `min`, `max`, `step`, `get`, `set`, optional `format`) or a choice (`kind = "choice"`, `options = { { value, text }, ... }`, `get`, `set`). `Menu.push` opens a page on top, and Escape returns to the one before. `Menu.theme` holds the colours, font and label size, and `labelPaddingLeft` / `labelPaddingRight`, the space around a button's label inside its highlight (the highlight is sized to the label's measured width). Pass `tiles = true` to `Menu.new` to put each button on its own dark tile sized to its label instead of a full-width bar.

## Scripts

Lua modules live under `Content/Scripts/FPS/` and load with `require("FPS.Logic")` after `package.path` includes the plugin `Scripts` directory. Project `assets/scripts` is searched first.

Weapon, health, and HUD scripts are optional. Omitting them leaves first-person move and look intact.
