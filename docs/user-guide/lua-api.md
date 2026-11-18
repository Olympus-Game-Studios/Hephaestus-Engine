---
title: Lua API reference
description: Every table and function Hephaestus gives your Lua scripts.
order: 12
---

# Lua API reference

This is the full list of what the engine gives your scripts. For how to use it, start with [Scripting with Lua](scripting-lua.md).

Two conventions to keep in mind:

- **Entity methods use a colon** (`this:GetName()`), engine tables use a dot (`Scene.destroy(e)`).
- **Global tables like `Transform`, `Mesh`, `Physics` and `Character` act on the entity the script is attached to.** To act on a different entity, use that entity's methods instead.

Vectors come back as tables with `x`, `y` and `z`. Rotations on transforms are in **degrees**; `Camera` and `Physics` rotations are in **radians**.

## Entities

`this` is the script's own entity. Entities you get from `Scene.instantiate`, `Scene.findAllWithPrefix`, raycasts and physics callbacks have the same methods.

| Method | Does |
| --- | --- |
| `e:GetName()` / `e:SetName(name)` | Read or change the entity's name. |
| `e:GetParent()` | The entity's parent, or `nil` at the top of the hierarchy. |
| `e:IsValid()` | False once the entity has been destroyed. Check this before using an entity you've held onto. |
| `e:GetId()` | The entity's integer id, or `nil` once it is destroyed. Every reference to the same entity returns the same id, so use it as a table key instead of the name. |
| `e:GetTransform()` | Its transform (see below). |
| `e:GetCamera()` | Its camera, or `nil`. |
| `e:GetAnimator()` / `e:GetCharacter()` | Animation control for characters, or `nil`. |
| `e:Call(name, ...)` | Run a function by name in that entity's gameplay scripts. Returns whether it found one. |
| `e:CallController(name, ...)` | Same, but in the entity's character controller script. |

The `Entity` table has `Entity.isValid()`, `Entity.getName()` and `Entity.setName(name)` for the script's own entity.

## Transforms

| Method | Does |
| --- | --- |
| `t:GetPosition()` / `t:SetPosition(x, y, z)` | Position. `SetPosition` also takes a `{x, y, z}` table. |
| `t:GetWorldPosition()` | Position in the world, including every parent's transform. |
| `t:GetRotation()` / `t:SetRotation(x, y, z)` | Rotation in degrees. |
| `t:GetScale()` / `t:SetScale(x, y, z)` | Scale. |
| `t:Translate(dx, dy, dz)` | Move by an amount. |
| `t:Rotate(dx, dy, dz)` | Rotate by an amount, in degrees. |

The `Transform` table has the same as functions for the script's own entity: `getPosition`, `setPosition`, `getRotation`, `setRotation`, `getScale`, `setScale`, `translate` and `rotate`.

## Scene

| Function | Does |
| --- | --- |
| `Scene.instantiate(path, options)` | Spawn a prefab. `options` can have `position`, `rotation` (degrees), `parent` (an entity to attach it to; `position` and `rotation` are then relative to that entity, and it moves with it) and `vars` (overrides for the prefab's `Expose` values). Returns the new entity. |
| `Scene.destroy(entity)` | Remove an entity at the end of this frame's update. |
| `Scene.findAllWithPrefix(prefix)` | Every entity whose name starts with `prefix`. |
| `Scene.FindFirstCamera()` / `Scene.FindCameras()` | Find camera entities. |
| `Scene.GetActiveCamera()` / `Scene.SetActiveCamera(entity)` | Which camera the player sees through. |
| `Scene.reload()` | Restart the current scene from its saved file (at a safe point after this frame). |
| `Scene.load(path)` | Switch to another saved scene, such as `"assets/scenes/Level1.hpscene"`, at the same safe point. In the editor, stopping Play goes back to the scene you were editing. |

## Mesh

| Function | Does |
| --- | --- |
| `Mesh.setVisible(visible)` / `Mesh.isVisible()` | Show or hide the script's own mesh. |

## Particles

For the script's own Particle Emitter.

| Function | Does |
| --- | --- |
| `Particles.play()` / `Particles.stop()` | Start emitting from the beginning (with its Burst), or stop emitting and let live particles finish. |
| `Particles.burst(count)` | Emit `count` particles at once (the emitter's Burst if left out), playing or not. |
| `Particles.setRate(perSecond)` | Change how many particles a second it emits while playing. |
| `Particles.isPlaying()` / `Particles.count()` | Whether it is emitting, and how many particles are alive. |

## Time and application

| Function | Does |
| --- | --- |
| `Time.deltaTime()` | Seconds since the last frame, times the time scale. Same as the `dt` passed to `onUpdate`. |
| `Time.setScale(scale)` / `Time.getScale()` | Slow down, speed up (up to 10) or freeze (0) gameplay. Scripts, physics, animation and particles all follow it; sound and the UI don't. A pause menu sets 0. It goes back to 1 when Play starts or the scene changes. |
| `Time.unscaledDeltaTime()` | Real seconds since the last frame, for anything that should keep moving while the game is paused, such as menus. |
| `Application.quit()` | Close the game. In the editor, it stops Play. |
| `Application.isDevelopment()` | `true` in Editor Play and in Development packages, `false` in Release packages. Use it to keep developer tools such as cheat menus out of shipped games. |
| `print(...)` / `engine_log(text)` | Write to the Console and log. |

## Storage

Keep data such as settings between sessions. Each name is one file of the player's own data: on Windows in `%LOCALAPPDATA%\<project name>\Saves`, on macOS in `~/Library/Application Support/<project name>/Saves`, and on Linux in `~/.local/share/<project name>/Saves`. In Editor Play it is kept in the project's `.hephaestus/editor/saves` folder instead, so testing never touches a player's files.

| Function | Does |
| --- | --- |
| `Storage.save(name, table)` | Save a table of numbers, strings, booleans and tables (string keys, or a list). Names may use letters, digits, `_` and `-`. Returns `true`, or `false` and the reason. |
| `Storage.load(name)` | The saved table, or `nil` if there is none. |

## Config files

Settings players may want to read or edit by hand go in INI files instead. `Config.save("Settings", ...)` writes `Settings.ini` in the player's `Saved\Config` folder (on Windows `%LOCALAPPDATA%\<project name>\Saved\Config`); in Editor Play it writes `EditorSettings.ini` in the project's `Saved/Config` folder, which packaging leaves out (add `Saved/` to your `.gitignore`).

| Function | Does |
| --- | --- |
| `Config.save(name, sections[, header])` | Write the file. `sections` is a list of `{ name, comment, keys = { { key, value, comment }, ... } }`; values are written as text (numbers to 6 significant digits) and each comment as `;` lines above what it describes. Returns `true`, or `false` and the reason. |
| `Config.load(name)` | `{ [section] = { [key] = "text" } }`, or `nil` when there is no file. Lines that are not `[Section]`, `key = value`, blank or a `;` / `#` comment are skipped with a warning in the log; converting and checking the values is up to the script. |
| `Config.path(name)` | The file's full path, or `nil` where config files are not available. |

## Input

| Function | Does |
| --- | --- |
| `Input.isActionPressed(name)` | True on the frame the action was pressed. |
| `Input.isActionDown(name)` | True every frame it's held. |
| `Input.isActionReleased(name)` | True on the frame it was let go. |
| `Input.getAxis(name)` | The axis value, usually -1 to 1. |
| `Input.getMouseDelta()` | Mouse movement since the last frame, `{x, y}` in pixels. |
| `Input.getMousePosition()` | Mouse position in the window, `{x, y}`. |
| `Input.getScrollDelta()` | Wheel movement since the last frame. |
| `Input.isMouseButtonPressed(button)` | True while the button is held. `button` is `"left"`, `"right"` or `"middle"` (or `0`, `1`, `2`). |
| `Input.isGamepadConnected()` | Whether player one's gamepad is plugged in. |
| `Input.isKeyPressed(key)` | Raw key check. Prefer actions so players can use a pad. |
| `Input.setCursorMode(mode)` | Low level cursor control. Use `UI.setCursorMode` instead. |
| `Input.getBindings(name[, defaults])` | An action's or axis's bindings, as `{ type, control, scale, deadzone }` tables (`type` as in the input map: `keyboard`, `mouse_button`, `gamepad_button`, ...). With `defaults` true, the ones the project's input map gave it. `nil` for an unknown name. |
| `Input.setBindings(name, list)` | Replace an action's or axis's bindings for this session, for rebinding menus. Returns `true`, or `false` and the reason (an unknown control leaves the bindings as they were). Save them yourself (for example with `Config`) and set them again at startup. |
| `Input.getPressedControl()` | `{ type, control }` for the first key, mouse button or gamepad button pressed this frame, or `nil`: what a rebinding menu waits for. |

See [Input](input.md) for setting up actions and axes.

## Camera

These control a free script camera, separate from Camera components. Call `Camera.setActive(true)` first.

| Function | Does |
| --- | --- |
| `Camera.setActive(on)` / `Camera.isActive()` | Take or release control of the view. |
| `Camera.setPosition(x, y, z)` / `Camera.getPosition()` | Camera position. |
| `Camera.setRotation(pitch, yaw, roll)` / `Camera.getRotation()` | Rotation in radians. |
| `Camera.translate(dx, dy, dz)` | Move in world space. |
| `Camera.worldToScreen(x, y, z)` (or a `{x, y, z}` table) | Where a world point appears in the active Camera component's view: `x`, `y` in game UI units (the same units widgets are placed in, the screen being 1080 tall), whether it is on screen and in front of the camera, and its distance along the view. `nil` when there is no active camera. For name tags and markers over objects. |

If the script's entity has a Camera component, these move that entity instead.

## Graphics

| Function | Does |
| --- | --- |
| `Graphics.setMaxTextureSize(pixels)` / `Graphics.getMaxTextureSize()` | Loads material textures at most this many pixels on their longer side, halving larger ones (0, the default, keeps every texture at its full size). Textures already loaded are reloaded at the new size, which takes a moment, so call it from a settings change rather than every frame. For a texture quality setting. |
| `Graphics.setScopeLens(options)` / `Graphics.setScopeLens(nil)` | Draws a full-screen rifle scope over the view: the scene through a round lens with barrel distortion, colour fringing and darker edges, a dark tube around it, black outside and a mil-dot reticle with a red centre dot. `options`: `radius` (lens radius as a fraction of the screen height, 0.45), `distortion` (0.12), `aberration` (0.006), `edgeDarkening` (0.45), `ringWidth` (tube width as a fraction of the radius, 0.14), `opacity` (0 to 1, to fade it in and out), `reticle` (true). `nil` turns it off; it also goes when the scene changes or play stops. Zoom with the camera's field of view. |

## Physics

| Function | Does |
| --- | --- |
| `Physics.raycast(ox, oy, oz, dx, dy, dz, maxDistance[, layer[, ignoreSensors[, ignore]]])` | Cast a ray. Returns `nil`, or a hit with `point`, `normal`, `distance`, `entity` and `sensor` (true when the hit is a trigger). Ignores the calling entity. `layer` is a layer name from your collision matrix. `ignoreSensors` skips every trigger. `ignore` is an entity or a list of entities to skip instead of the calling entity, so a query can step past a trigger it has already met. |
| `Physics.isAvailable()` / `Physics.hasBody()` | Whether physics is running and this entity has a body. |
| `Physics.getLinearVelocity()` / `Physics.setLinearVelocity(x, y, z)` | Body velocity. |
| `Physics.setAngularVelocity(x, y, z)` | Spin the body. |
| `Physics.setRotation(x, y, z)` | Set the body's rotation, in radians. |
| `Physics.move(x, z, speed)` / `Physics.jump(speed)` / `Physics.isGrounded()` | Simple movement. Uses the Character Controller if there is one. |

Physics callbacks are covered in [Scripting with Lua](scripting-lua.md#reacting-to-physics).

## Character

For entities with a [Character Controller](physics.md#character-controller).

| Function | Does |
| --- | --- |
| `Character.move(x, z)` or `Character.move(x, y, z)` | Set the direction to walk this step, in world space. |
| `Character.jump()` | Jump on the next physics step, if standing on something. |
| `Character.setCrouched(on)` / `Character.isCrouched()` | Crouch. |
| `Character.isStandBlocked()` | True if there's no room to stand up. |
| `Character.setProne(on)` / `Character.isProne()` | Lie down, using the controller's **Prone Height**; prone wins over crouch. Getting up waits until there's room. |
| `Character.isGrounded()` / `Character.getGroundState()` | Whether and how it's standing on something. |
| `Character.getVelocity()` | Current velocity. |
| `Character.getGroundNormal()` / `Character.getGroundVelocity()` | The slope underfoot and how fast the ground is moving (moving platforms). |
| `Character.setSpeedMultiplier(m)` / `Character.getSpeedMultiplier()` | Scale walk speed, for sprinting or slowing effects. |
| `Character.teleport(x, y, z)` | Move instantly. |
| `Character.setFacingYaw(radians)` | Turn to face a direction. |
| `Character.ragdoll([ix, iy, iz[, px, py, pz]])` | Go limp: the character becomes a ragdoll and falls under physics, optionally pushed by an impulse (newton seconds) at a world point. Needs a humanoid skeleton with Mixamo bone names. The Character Controller and the colliders of the entity's children are removed, the animation stops, and hits on the ragdoll report the entity. Characters walk through ragdolls; raycasts still hit them. Returns whether it worked. |
| `Character.isRagdoll()` | True once the character is a ragdoll. |

## Navigation

A navmesh lets AI find paths round obstacles. It is built the first time a script asks for it in each Play session, from every static, solid Rigid Body with a **Box** collision shape (other shapes are skipped). It fits a character 1.8 m tall with a 0.35 m radius that can step 0.4 m and walk up 45 degree slopes. Points are tables with `x`, `y` and `z`.

| Function | Does |
| --- | --- |
| `Nav.isReady()` | True once the navmesh is built; builds it if it hasn't been tried yet. The log says how many polygons it has, or why it could not be built. |
| `Nav.rebuild()` | Build it again, after moving or adding static boxes. Returns whether it worked. |
| `Nav.findPath(from, to)` | A list of points from `from` to `to` along the navmesh, corners only, or `nil`. When `to` can't be reached the path ends at the nearest reachable point. A point with `link = true` is where the path vaults low cover: the next point is on the far side, and a walker has to jump or climb between them. |
| `Nav.closestPoint(point[, extents])` | The nearest point on the navmesh, searching `extents` (default 2, 4, 2 metres) around `point`, or `nil`. |
| `Nav.randomPoint(center, radius)` | A random reachable point within `radius` metres of `center`, or `nil`. |

## Characters and animation

From `this:GetAnimator()` on an entity with a Character component. Clips are referred to by their id (their name in the Character Editor).

| Method | Does |
| --- | --- |
| `a:Play(clip)` | Play a clip from the start. |
| `a:CrossFade(clip, seconds)` | Blend into a clip. |
| `a:Pause()` / `a:Resume()` / `a:Stop()` | Playback control. |
| `a:SetSpeed(multiplier)` | Playback speed. Must be above 0; use `Pause` to freeze. |
| `a:IsPlaying([clip])` | Whether anything (or a given clip) is playing. |
| `a:GetCurrentAnimation()` / `a:GetTime()` / `a:GetDuration([clip])` | Where playback is. |
| `a:SetTime(seconds)` | Jumps the current animation to that time, for example to start characters at different points of the same loop. |
| `a:SetLayer(name, options)` | Lays a pose over the main animation on some bones, such as strafing legs under an aiming upper body. `options.clips` lists clip ids or `{ clip = id, weight = w, reverse = true }`; they play at one shared phase (so their steps stay in time) and mix by weight. `mask` names the bone whose branch it drives (the whole body when left out) and `exclude` a branch it leaves alone; `excludeBlend` (0 to 1) lets the excluded bone still take that share, halving for each bone below it, so the change at the waist is not sudden. `weight` (1) is how strongly it applies, `fade` how many seconds it takes to get there and `rate` its playback speed. `phase` (0 to 1) sets where in its clips the layer is; with `rate = 0` a script can play a one-off move (a vault, a slide) on just those bones by setting `phase` each frame. Bone names match without a prefix such as `mixamorig:`. Call it again each frame to change the weights; it returns `false` and a message when a clip or bone is missing. |
| `a:ClearLayer(name, fadeSeconds)` | Fades a layer out and removes it. |

From `this:GetCharacter()`: `c:GetSocket(name)` (world space) and `c:GetSocketLocal(name)` (character space), each returning `position` and `rotation`, or `nil`.

## Audio

| Function | Does |
| --- | --- |
| `Audio.play(path, loop, volume)` | Play a `.wav`, `.mp3` or `.flac` in 2D. `loop` defaults to false, `volume` (0 to 1) to 1. Returns a handle, or `0` if it failed. |
| `Audio.play(path, options)` | Play with a table of `volume`, `pitch`, `loop`, `bus` (`sfx`, `weapons`, `music`, `ui`), `priority`, `pitchJitter`, `volumeJitter` and, for 3D, `position = {x, y, z}`, `minDistance` and `maxDistance`. With `loop`, `loopStart` and `loopEnd` (seconds) make it play from the start, then repeat just that stretch once it reaches `loopEnd` (0 is the end). |
| `Audio.stop(h)` / `Audio.pause(h)` / `Audio.resume(h)` | Control a sound. `Audio.stop(h, fadeSeconds)` fades it out first. |
| `Audio.setVolume(h, volume)` / `Audio.setPitch(h, pitch)` | Change its volume or pitch (1 is normal speed). |
| `Audio.setPosition(h, x, y, z)` | Move a sound, making it 3D. |
| `Audio.isPlaying(h)` | Whether it's still playing. |
| `Audio.stopAll()` | Silence everything. |
| `Audio.setBusVolume(bus, volume)` / `Audio.getBusVolume(bus)` | A bus's volume; `master` is everything. |
| `Audio.getVoiceCount()` | How many sounds are playing. |
| `Audio.isAvailable()` | Whether sound works on this machine. |

## UI

Showing and removing `.hui` layouts:

| Function | Does |
| --- | --- |
| `display.ui(path)` | Show a layout. Returns a handle, or `nil` and an error. |
| `destroy.ui(handle)` | Remove it. Also takes `"current"` or `"all"`. |
| `UI.getWidget(handle, id)` | Find a widget by its ID. |
| `UI.setCursorMode(CursorMode.UI)` / `UI.setCursorMode(CursorMode.Game)` | Free the mouse for menus, or capture it for gameplay. `UI.getCursorMode()` reads it. |

Changing widgets (each takes the widget first):

- **Content:** `setText`, `setImage`, `setImageTint`, `setImageScaleMode`
- **Visibility and order:** `setVisible`, `setOpacity`, `setZOrder`, `setEnabled`, `setFocused`
- **Placement:** `setPosition`, `setSize`, `setMinSize`, `setMaxSize`, `setAnchor`, `setAlignment`, `setPadding`, `setMargin`, `setSpacing`, `setChildAlignment`, `setGridColumns`, `setGridSpacing`
- **Look:** `setBackgroundColor`, `setBorderColor`, `setBorderWidth`, `setCornerRadius`, `setTextColor`, `setFontSize`, `setFontBold`, `setFontItalic`, `setTextAlignment`, `setTextShadow`, `setWordWrap`, `setStyle`, `setButtonStyle`, `registerThemeStyle`, `applyThemeStyle`
- **Layout size:** `UI.getLayoutSize()` returns the width and height of the screen in game UI units: 1080 tall at 100% UI size, less when the player makes the UI bigger (720 at 150%), so screens can fit what is left.
- **Measure:** `UI.measureText(text, size, bold, family)` returns the width in pixels the text takes on one line, then the left and right edges of its drawn pixels (`width, inkLeft, inkRight`), for sizing panels around labels with even padding.
- **Size:** game UI is laid out for a screen 1080 units tall and scaled to the real one, so a HUD keeps its proportions at any resolution and in any size of Editor Play viewport. Positions and sizes you give are in those units, and mouse positions reach widgets in them too. `UI.setScale(scale)` makes all of it bigger or smaller on top of that (0.5 to 2, for a player's UI size setting), and `UI.getScale()` returns it.
- **Events:** `setOnClick(widget, function)`, `setOnHover(widget, function(isHovered))`

Building UI in code: `UI.createScreen`, `UI.getRoot`, `UI.createPanel`, `UI.createText`, `UI.createButton`, `UI.createImage`, `UI.createVerticalBox`, `UI.createHorizontalBox`, `UI.createGrid`, `UI.setScreenVisible`, `UI.setScreenActive`, `UI.clearScreens` and `UI.getScreenCount`. `UI.setScreenVisible(screen, visible)` and `UI.setScreenActive(screen, active)` take the handle `UI.createScreen` returns, or a screen's name. `UI.loadHUI(path)` loads a layout without the bookkeeping `display.ui` does.

Constants: `UIAnchor` (`TopLeft`, `TopCenter`, `TopRight`, `CenterLeft`, `Center`, `CenterRight`, `BottomLeft`, `BottomCenter`, `BottomRight`, `Stretch`), `UIAlign` (`Left`, `Center`, `Right`, `Top`, `Bottom`, `Stretch`), `UIScaleMode` (`Stretch`, `Fit`, `Fill`, `Center`) and `CursorMode` (`Game`, `UI`).

## Inspector variables

`Expose.number`, `Expose.integer`, `Expose.bool`, `Expose.string`, `Expose.vec2`, `Expose.vec3` and `Expose.color`. See [Scripting with Lua](scripting-lua.md#inspector-variables-with-expose).
