# Hephaestus Engine: Lua Scripting Guide

## Overview

Hephaestus uses **Lua** as its primary scripting language. Lua provides:

- **Fast iteration** - Changes take effect instantly, no recompilation
- **Easy syntax** - Beginner-friendly, similar to Python
- **Great performance** - Plenty fast for gameplay logic
- **Rich API** - Access to transform, input, camera, and more

This is the technical reference for Lua in Hephaestus: exact callback order, binding semantics and edge cases. For a friendlier introduction, start with the user guide's [Scripting with Lua](../../docs/user-guide/scripting-lua.md) and [Lua API reference](../../docs/user-guide/lua-api.md).

Lua 5.4 is built in. Other languages are added by plugins through a C interface; see [script-languages.md](../../docs/plugins/script-languages.md). The old C++ `ScriptComponent` path still exists for engine code but is not a gameplay scripting option.

## Quick Start

### 1. Create a Lua Script

In the Content Browser, right click `assets/scripts` and choose **New Lua Script**, or create `assets/scripts/MyGame.lua` yourself:

```lua
-- Called when the script is attached to an entity
function onAwake()
    engine_log("MyGame script started!")
end

-- Called every frame
function onUpdate(deltaTime)
    engine_log("Frame time: " .. tostring(deltaTime))
end

-- Custom functions
function handleCollision(other)
    engine_log("Collided with something!")
end
```

### 2. Attach via Editor

1. Select an entity
2. In the Inspector, **Add Component > Lua Script**
3. Set **Script** to your `.lua` file (or drag the script from the Content Browser onto the field)

### 3. Run

Enter play mode - your Lua script runs automatically!

## Inspector Parameters

Declare designer-editable parameters at file scope with `Expose.*`:

```lua
Expose.number("moveSpeed", 5.0)
Expose.integer("ammo", 30)
Expose.bool("canSprint", true)
Expose.string("doorId", "main_door")
Expose.vec2("screenOffset", 0.0, 0.0)
Expose.vec3("spawnOffset", 0.0, 1.0, 0.0)
Expose.color("tint", 1.0, 0.8, 0.6, 1.0)

function onUpdate(dt)
    Transform.translate(0.0, 0.0, -moveSpeed * dt)
end
```

The Lua Script Inspector scans these declarations without executing gameplay code and
shows native bool, numeric, text, vector, and color editors in source order. Parameter
names are normal Lua globals after declaration; no `Expose.get()` call is required.

Defaults must be literals. Expressions such as
`Expose.number("speed", BASE_SPEED * difficulty)` are rejected. Names must be unique:
duplicate declarations are conflicts and prevent the script's parameter metadata from
being used until corrected.

Inspector edits participate in undo/redo, scene serialization, and prefab Apply/Revert.
Values resolve in this order:

- Normal scene entity: authored Inspector value, then Lua declaration default
- Prefab instance: scene instance override, prefab-authored value, then Lua declaration default

Replacing the script path starts with the new script's defaults and does not carry values
from the previous script merely because names match. When the same script changes, explicit
designer values remain, new declarations receive defaults, removed declarations are cleaned
up, and incompatible type changes reset to the new default.

Runtime changes to these globals remain runtime-only. Stopping Play Mode restores the
authored scene values, and packaged games receive the same serialized values as Play Mode.

## Lifecycle Callbacks

Lua scripts support these lifecycle functions:

```lua
-- Called once when script initializes
function onAwake()
end

-- Called every frame
function onUpdate(deltaTime)
end

-- Called after all updates (for camera/post-processing)
function onLateUpdate(deltaTime)
end

-- Called when script is destroyed
function onDestroy()
end

-- Called when entity is enabled
function onEnable()
end

-- Called when entity is disabled
function onDisable()
end

-- Non-looping clip finished after pose eval (controller then gameplay)
function onAnimationFinished(clipId)
end

-- Trigger overlaps. The other entity is the payload. Exit never receives a contact.
-- Enter and stay receive a contact only when a valid manifold exists; otherwise contact is nil.
function onTriggerEnter(other, contact)
end

function onTriggerStay(other, contact)
end

function onTriggerExit(other)
end

-- Solid contacts. contact is nil when the event is real but Jolt did not produce
-- a point with non-negative penetration (including speculative contacts).
-- onCollisionExit never receives contact data.
function onCollisionEnter(other, contact)
end

function onCollisionStay(other, contact)
end

function onCollisionExit(other)
end
```

`onAwake` runs once when the script is first loaded for play; `onEnable` is invoked immediately afterwards and whenever a disabled script is re-enabled. `onDisable` and `onDestroy` fire when play mode stops, the script is detached, or the entity is removed. `onAnimationFinished(clipId)` runs after character pose eval for non-looping clips (controller slot first, then gameplay).

## C++ Bindings

Hephaestus supports two API styles for accessing entity data:

1. **OOP-style** (recommended): Use `this` to access the current entity and its components
2. **Functional-style**: Use global tables like `Transform`, `Entity`, `Input`

### Logging

```lua
engine_log("This message appears in console")
```

### OOP-Style API (using `this`)

The `this` global represents the entity the script is attached to:

```lua
function onAwake()
    -- Get the transform component
    local transform = this:GetTransform()
    
    -- Get current position
    local pos = transform:GetPosition()
    print(pos.x, pos.y, pos.z)
    
    -- Set new position (using a table)
    transform:SetPosition({x = 1.0, y = 2.0, z = 3.0})
    
    -- Or set position with individual values
    transform:SetPosition(1.0, 2.0, 3.0)
    
    -- Get entity name
    local name = this:GetName()
    this:SetName(name .. " (Lua)")
end

function onUpdate(deltaTime)
    local transform = this:GetTransform()
    
    -- Rotate over time
    local rotation = transform:GetRotation()
    rotation.y = rotation.y + 45.0 * deltaTime
    transform:SetRotation(rotation)
    
    -- Or use relative methods
    transform:Rotate(0, 45.0 * deltaTime, 0)
    transform:Translate(0, 0, 5.0 * deltaTime)
end
```

**`this` methods:**

- `this:GetTransform()` → returns a transform object
- `this:GetName()` → `string|nil`
- `this:SetName(name)`
- `this:IsValid()` → `boolean`
- `this:GetCamera()` → camera object or nil
- `this:GetAnimator()` → animator object or nil (requires `CharacterComponent`)
- `this:GetCharacter()` → character object or nil (requires `CharacterComponent`)
- `this:Call(functionName, ...)` → `boolean` — calls a named function in **this entity's gameplay** script environment (e.g. `hit.entity:Call("onDamage", 25)`). Extra args are forwarded (numbers recommended). Does **not** call the character controller.
- `this:CallController(functionName, ...)` → `boolean` — calls a named function on this entity's **character controller** slot only.

Entity userdata returned from `Scene.instantiate`, `Scene.findAllWithPrefix`, `Physics.raycast`, or camera queries uses the same methods; they operate on the **receiver**, not the currently running script.

**Transform object methods:**

- `transform:GetPosition()` → `{ x, y, z }`
- `transform:SetPosition(pos)` or `transform:SetPosition(x, y, z)`
- `transform:Translate(dx, dy, dz)` (relative move)
- `transform:GetRotation()` → `{ x, y, z }` (degrees)
- `transform:SetRotation(rot)` or `transform:SetRotation(rx, ry, rz)`
- `transform:Rotate(rx, ry, rz)` (relative rotation)
- `transform:GetScale()` → `{ x, y, z }`
- `transform:SetScale(scale)` or `transform:SetScale(sx, sy, sz)`

**Animator object methods** (clip ids, not display names):

- `animator:Play(clipId)` / `animator:CrossFade(clipId, duration)`
- `animator:Pause()` / `animator:Resume()` / `animator:Stop()`
- `animator:SetSpeed(speed)` — runtime multiplier `> 0`; use `Pause()` to freeze
- `animator:IsPlaying()` / `animator:IsPlaying(clipId)`
- `animator:GetCurrentAnimation()` / `animator:GetTime()` / `animator:GetDuration()` / `animator:GetDuration(clipId)`

During CrossFade, queries use the destination clip. Repeated Play of the current unpaused clip is a no-op; Play of a finished clip restarts. Locomotion may CrossFade every frame; Shoot/Hurt/Death must Play once on state entry.

**Character object methods:**

- `character:GetSocket(name)` — animated world space
- `character:GetSocketLocal(name)` — animated model space (not the authored bone-local offset)

Both return `{ position, rotation }` or `nil`. Socket queries in `onLateUpdate` see this frame's pose. Full slot/tick/Cultist details: [CharacterControllers.md](../Guides/CharacterControllers.md).

### Functional-Style API (Global Tables)

Alternatively, use global tables that operate on the current entity:

```lua
-- Get entity position
local pos = Transform.getPosition()
print(pos.x, pos.y, pos.z)

-- Set entity position
Transform.setPosition(1.0, 2.0, 3.0)

-- Rotate entity
Transform.rotate(0, 1.5, 0)

-- Get/set scale
Transform.setScale(1.0, 1.0, 1.0)
```

Available helpers:

- `Transform.getPosition()` → `{ x, y, z }`
- `Transform.setPosition(x, y, z)`
- `Transform.translate(dx, dy, dz)` (relative move)
- `Transform.getRotation()` / `Transform.setRotation(rx, ry, rz)` (degrees)
- `Transform.rotate(rx, ry, rz)` (relative rotation)
- `Transform.getScale()` / `Transform.setScale(sx, sy, sz)`

### Entity

```lua
if Entity.isValid() then
    local name = Entity.getName() or "Entity"
    Entity.setName(name .. " (Lua)")
end
```

Available helpers:

- `Entity.isValid()` → `boolean`
- `Entity.getName()` → `string|nil`
- `Entity.setName(name)`

### Input

```lua
local moveForward = Input.getAxis("MoveForward")
local moveRight = Input.getAxis("MoveRight")

if Input.isActionPressed("Jump") then
    engine_log("Jump started")
end

if Input.isActionDown("Sprint") then
    engine_log("Sprint is held")
end

if Input.isActionReleased("Interact") then
    engine_log("Interact was released")
end

if Input.isMouseButtonPressed(0) then
    engine_log("Left mouse button down")
end

-- Get mouse position
local mouse = Input.getMousePosition()
if mouse then
    engine_log(string.format("Mouse position: %.0f %.0f", mouse.x, mouse.y))
end
```

Available helpers:

- `Input.isActionDown(name)` → `boolean` (held)
- `Input.isActionPressed(name)` → `boolean` (rising edge for one frame)
- `Input.isActionReleased(name)` → `boolean` (falling edge for one frame)
- `Input.getAxis(name)` → `number` (combined scaled bindings)
- `Input.isGamepadConnected()` → `boolean` (player-zero gamepad)
- `Input.isKeyPressed(key)` → `boolean` (legacy held behavior; string name or key code)
- `Input.isMouseButtonPressed(button)` → `boolean`
- `Input.getMousePosition()` → `{ x, y }` (window pixels)
- `Input.getMouseDelta()` → `{ x, y }` (pixels since last frame)
- `Input.getScrollDelta()` → `{ x, y }` (wheel movement since last frame)
- `Input.setCursorMode(mode)` — low-level cursor control (`"normal"` or `"disabled"`)

Action and axis names come from the project's `.hpinput` v1 profile, selected under
**Project Settings → Input**. Bindings support keyboard keys, mouse buttons/delta/wheel,
and gamepad buttons/axes with scales and deadzones. `.hpinput` v1 does not define input
contexts. Packaged games poll the player-zero GLFW gamepad; Qt Play Mode currently uses
the same mappings for keyboard and mouse, with Qt gamepad polling tracked separately.

### Cursor Mode (UI ↔ Game)

For switching between gameplay mouse capture and UI interaction, use the high-level `UI.setCursorMode` API:

```lua
-- Show cursor and unlock it for UI interaction
UI.setCursorMode(CursorMode.UI)

-- Hide and capture cursor for gameplay (camera look, etc.)
UI.setCursorMode(CursorMode.Game)

-- Query current mode
local mode = UI.getCursorMode()  -- returns "ui" or "game"
```

This is the recommended way to manage cursor state. It works identically in the Qt editor play mode and in exported game builds.

### Camera

```lua
Camera.setActive(true)
Camera.setPosition(0.0, 1.6, 0.0)
Camera.setRotation(math.rad(0.0), math.rad(180.0), 0.0)
```

- `Camera.setActive(enabled)` → enable manual control from scripts (auto-disabled on stop)
- `Camera.isActive()` → `boolean`
- `Camera.setPosition(x, y, z)` / `Camera.getPosition()`
- `Camera.translate(dx, dy, dz)` → offsets camera in world space
- `Camera.setRotation(rx, ry, rz)` / `Camera.getRotation()` (radians, pitch/yaw/roll)

### Audio

```lua
-- One-shot effect
local click = Audio.play("assets/audio/click.wav")

-- Looping music (path, loop, volume)
local music = Audio.play("assets/audio/music/theme.mp3", true, 0.5)

if Audio.isPlaying(music) then
    Audio.setVolume(music, 0.35)
end

Audio.pause(music)
Audio.resume(music)
Audio.stop(music)
```

Available helpers:

- `Audio.isAvailable()` → `boolean`
- `Audio.play(path, loop=false, volume=1.0)` → `handle` (`0` on failure)
- `Audio.stop(handle)` → `boolean`
- `Audio.pause(handle)` → `boolean`
- `Audio.resume(handle)` → `boolean`
- `Audio.setVolume(handle, volume)` → `boolean` (`0.0..1.0`)
- `Audio.isPlaying(handle)` → `boolean`
- `Audio.stopAll()`

### Time

```lua
local dt = Time.deltaTime()
```

Use `Time.deltaTime()` whenever you need the most recent frame delta outside of `onUpdate` parameters (for example inside utility modules).

### Scene spawn, destroy, and queries

```lua
-- Spawn a prefab (optional pose + script vars applied before onAwake)
local enemy = Scene.instantiate("assets/prefabs/Enemy.hpprefab", {
  position = { x = 4, y = 1, z = -8 },
  rotation = { x = 0, y = 45, z = 0 }, -- degrees
  vars = { health = 40, moveSpeed = 3.5 }
})

-- Attach the spawned prefab to an entity: position/rotation are then local to it, and it moves with it
-- (including the smoothed, physics-interpolated position a moving character is drawn at)
local legs = Scene.instantiate("assets/prefabs/PlayerLegs.hpprefab", { parent = this, position = { x = 0, y = 0, z = 0 } })

-- Destroy a prefab instance or entity (queued until end of script Update)
Scene.destroy(enemy)

-- Find spawn markers / living enemies by name prefix
local points = Scene.findAllWithPrefix("EnemySpawn")
local enemies = Scene.findAllWithPrefix("Enemy")
```

Also available:

- `Scene.FindFirstCamera()` / `Scene.FindCameras()` / `Scene.GetActiveCamera()` / `Scene.SetActiveCamera(entity)`
- `Scene.reload()` requests a fresh reconstruction of the current authored scene. See [Runtime Scene Lifecycle](../Guides/RuntimeSceneLifecycle.md).

### Physics

Play steps physics at a fixed `1/60` second after `onUpdate`, with at most four substeps. There is no `FixedUpdate`. Character controllers and dynamic bodies are drawn interpolated between the last two fixed steps; script-visible transforms are the simulated ones. Callbacks below run on the main thread after the fixed step that produced them.

```lua
-- Hitscan (ignores the calling entity's body when possible)
local hit = Physics.raycast(ox, oy, oz, dx, dy, dz, maxDistance)
-- Optional layer name filters the cast with the project collision matrix:
-- Physics.raycast(ox, oy, oz, dx, dy, dz, maxDistance, "Character")
if hit then
  -- hit.point, hit.normal, hit.distance, hit.entity (userdata or nil)
  if hit.entity then
    hit.entity:Call("onDamage", 25)
  end
end

function onCollisionEnter(other, contact)
  if contact then
    -- contact.point {x,y,z} is the midpoint of the two shape points
    -- contact.normal {x,y,z} points so this body separates from other
    -- contact.relativeVelocity {x,y,z} is other velocity minus this body
    -- contact.penetration is meters; negative values are not delivered
  end
end

function onCollisionStay(other, contact)
end

function onCollisionExit(other)
  -- no contact argument
end

function onTriggerEnter(other, contact)
  -- other is the overlapping entity. contact may be nil.
end

function onTriggerStay(other, contact)
end

function onTriggerExit(other)
end

-- CharacterControllerComponent locomotion. Wish is world-space and horizontal.
-- Jump is latched for the next fixed step and is legal only on OnGround.
-- Coyote time and jump buffering are not part of this controller.
Character.move(wishX, wishZ)
if Character.isGrounded() then
  Character.jump()
end

-- Physics.move / jump / isGrounded route to that controller when it is live.
-- On a rigid body with no character controller they keep the legacy motor.
-- Physics.jump(speed) uses that speed for the next jump request only.
Physics.move(wishX, wishZ, 6.0)
Physics.setRotation(0, yawRadians, 0)
```

`onCollisionEnter` and `onCollisionStay` omit `contact` when the pair exists but there is no valid contact payload (no manifold point, or penetration is negative / speculative). Do not invent a contact in that case. `onCollisionExit` never receives contact data. A solid pair can emit `onCollisionExit` when one body sleeps, so `onCollisionStay` is not a ground signal. `Physics.isGrounded` ignores sensors.

Trigger callbacks identify the other entity. Enter and stay also receive `contact` when the same valid-contact rule passes, and otherwise pass nil. `onTriggerExit` never receives a contact. An authored static trigger is simulated as a kinematic sensor. With the current Jolt flags, a trigger does not detect a static non-sensor. Trigger versus trigger reports when the collision matrix allows that pair.

Available helpers:

- `Physics.isAvailable()` / `Physics.hasBody()`
- `Physics.getLinearVelocity()` / `Physics.setLinearVelocity(x,y,z)` / `Physics.setAngularVelocity(...)` / `Physics.setRotation(rx,ry,rz)` (radians)
- `Physics.raycast(ox,oy,oz,dx,dy,dz,maxDistance[, layerName])` → hit table or `nil`
- `Character.move(x, z)` / `Character.move(x, y, z)` / `Character.jump()` / `Character.setCrouched(bool)` / `Character.isGrounded()`
- `Character.getVelocity()` / `Character.getGroundNormal()` / `Character.getGroundVelocity()` / `Character.getGroundState()`
- `Character.teleport(x, y, z)` / `Character.setFacingYaw(radians)`
- `Physics.isGrounded()` / `Physics.move(wishX, wishZ, speed)` / `Physics.jump(speed)` — controller when one is live, otherwise the legacy rigid-body motor. A jump speed does not carry into a later jump.

## Performance Considerations

- Lua is a good fit for game logic, UI, AI, event handling and input.
- Avoid large per frame loops over many entities; cache entity and transform handles in `onAwake` instead of looking them up every frame.
- There is no profiler for Lua yet. Use the editor status bar's frame time and `engine_log` timing to find slow scripts.

## Complete Script Example

Here's a full example using the OOP-style API to make an entity bob up and down while rotating:

```lua
-- UpDownRotate.lua
-- Makes the object bob up and down subtly while rotating

-- Configuration
local BOB_SPEED = 2.0          -- Speed of bobbing motion (Hz)
local BOB_HEIGHT = 0.5         -- Height of bob movement (units)
local ROTATION_SPEED = 45.0    -- Rotation speed (degrees per second)

-- Internal state
local transform = nil
local initialPosition = nil
local elapsedTime = 0

function onAwake()
    print("[UpDownRotate] Script awakened")
    -- Get the transform component
    transform = this:GetTransform()
    -- Store the initial position
    local pos = transform:GetPosition()
    initialPosition = {x = pos.x, y = pos.y, z = pos.z}
    elapsedTime = 0
end

function onUpdate(deltaTime)
    elapsedTime = elapsedTime + deltaTime
    
    -- Calculate bobbing motion using sine wave
    local bobOffset = math.sin(elapsedTime * BOB_SPEED * math.pi * 2) * BOB_HEIGHT
    
    -- Calculate rotation
    local rotationAmount = elapsedTime * ROTATION_SPEED
    
    -- Apply new position (bob up and down)
    local newPosition = {
        x = initialPosition.x,
        y = initialPosition.y + bobOffset,
        z = initialPosition.z
    }
    transform:SetPosition(newPosition)
    
    -- Apply rotation around Y axis
    local currentRotation = transform:GetRotation()
    currentRotation.y = rotationAmount % 360
    transform:SetRotation(currentRotation)
end

function onDestroy()
    print("[UpDownRotate] Script destroyed")
end
```

## Hot Reload

Saving a script while Play is running reloads it: the old instance receives `onDisable` and `onDestroy`, then the new file loads and receives `onAwake` and `onEnable`. Script local state is lost; values set through `Expose` are kept.

## Debugging

The editor's Code panel has a Lua debugger: gutter breakpoints, the Debugger panel (call stack, locals, watches), Continue `F5`, Step over `F10`, Step into `F11`, Step out `Shift+F11` and Stop `Shift+F5`. The Console's `lua>` prompt evaluates Lua in the running game, or in the selected frame while stopped at a breakpoint. Script errors are written to the Console as `path:line: message`.

## Troubleshooting

**"Lua script not found"**
- Check the path on the Lua Script component; paths are project relative, such as `assets/scripts/Player.lua`
- `require("Name")` searches `assets/scripts/` and the `Scripts` folder of enabled plugins

**"Function not defined"**
- Callbacks (`onUpdate` and so on) and functions called through `entity:Call` must be global in the script, not `local`
- Lua is case sensitive

## Not Yet Supported

- No sandbox: scripts have the full Lua standard library, including `io` and `os`
- No coroutine scheduling helpers (plain Lua coroutines work)
- No Lua profiler
- No precompiled bytecode in packages
- No material, navigation or save game bindings
