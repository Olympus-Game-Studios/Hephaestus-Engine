# Hephaestus Engine - Scripting Layer

## Overview

The scripting layer provides a framework for game developers to write game logic and control entity behavior in the Hephaestus Engine using **Lua**. Lua offers fast iteration without requiring recompilation, making it ideal for gameplay programming.

**Future Roadmap**: C# scripting support is planned for future releases.

## 📚 Documentation

For comprehensive guides on creating and using scripts, see:
- **[LUA_SCRIPTING_GUIDE.md](../../Documentation/Scripting/LUA_SCRIPTING_GUIDE.md)** - Complete Lua guide with API reference
- **[SCRIPTING_QUICK_START.md](../../Documentation/Scripting/SCRIPTING_QUICK_START.md)** - Getting started in 5 minutes
- **[LUA_INSTALLATION_GUIDE.md](../../Documentation/Scripting/LUA_INSTALLATION_GUIDE.md)** - Set up Lua development environment

## Quick Start

### Create a Lua Script

Create `scripts/my_script.lua`:
```lua
function onAwake()
    -- OOP-style: use 'this' to access entity components
    local transform = this:GetTransform()
    local pos = transform:GetPosition()
    print("Starting at: " .. pos.x .. ", " .. pos.y .. ", " .. pos.z)
end

function onUpdate(deltaTime)
    local transform = this:GetTransform()
    transform:Rotate(0, 45 * deltaTime, 0)  -- Rotate 45 deg/sec
end

function onDestroy()
    print("Script cleaned up")
end
```

### Attach to Entity

1. Select entity in scene
2. Details Panel → Scripts
3. Click "Attach Lua Script"
4. Enter path: `scripts/my_script.lua`
5. Press Play

## Lua API Overview

Lua supports two equivalent API styles:

**OOP-style (Recommended - using `this`):**
```lua
local transform = this:GetTransform()
local pos = transform:GetPosition()
transform:SetPosition({x = pos.x, y = pos.y + 1, z = pos.z})
transform:Rotate(0, 45, 0)  -- Rotate around Y axis
```

**Functional-style (Legacy - using globals):**
```lua
local pos = Transform.getPosition()
Transform.setPosition(pos.x, pos.y + 1, pos.z)
Transform.rotate(0, 45, 0)
```

## Core Components

### Lua Scripting Files

| File | Purpose |
|------|---------|
| `LuaScriptEngine.h/cpp` | Lua runtime and C++ bindings |
| `LuaScriptComponent.h` | ECS component for Lua scripts |

### Classes

- **`LuaScriptEngine`** - Manages Lua state, lifecycle, and bindings
- **`LuaScriptComponent`** - ECS component for storing Lua script references

## Key Features

✅ Multiple Lua scripts per entity  
✅ Full lifecycle hooks (onAwake, onUpdate, onDestroy, etc.)  
✅ OOP-style API with `this` binding  
✅ Transform, Input, Camera, and Entity access  
✅ Audio playback control from Lua (`.wav` and `.mp3`)  
✅ Runtime prefab spawn/destroy (`Scene.instantiate` / `Scene.destroy`)  
✅ Entity handle methods + `entity:Call` + `Scene.findAllWithPrefix`  
✅ Physics raycast hit entity + basic FPS motor (`move` / `jump` / `isGrounded`)  
✅ No recompilation required - instant iteration  
✅ Automatic hot-reload on script changes  
✅ Console logging for debugging  

## Future Roadmap

- [ ] **C# Scripting** - Safe, fast, compiled scripting language
- [ ] Hot-reload during gameplay
- [ ] Lua debugger integration
- [ ] Script profiler
- [ ] More bindings (UI, nav, AI, etc.)

## Script Lifecycle

Lua scripts go through this lifecycle:

1. **Loading** - Script file loaded and parsed
2. **Awake** - `onAwake()` called once when attached
3. **Enable** - `onEnable()` called when entity becomes active
4. **Updates** - `onUpdate()` called each frame
5. **LateUpdate** - `onLateUpdate()` called after all updates
6. **Disable** - `onDisable()` called when entity becomes inactive
7. **Destroy** - `onDestroy()` called before cleanup

## Example Lua Scripts

See `scripts/example.lua` for working examples:

```lua
function onAwake()
    print("[MyScript] Initialized")
end

function onUpdate(deltaTime)
    -- Update game logic
end

function onDestroy()
    print("[MyScript] Cleaned up")
end
```

For a complete example showing entity interaction, see `scripts/UpDownRotate.lua`.

## Lua API Reference

For detailed API documentation, see `LUA_SCRIPTING_GUIDE.md`. Quick reference:

### Entity Access (OOP-style)

```lua
-- Get the entity's transform
local transform = this:GetTransform()

-- Get/Set entity name
local name = this:GetName()
this:SetName("NewName")

-- Check if entity is valid
if this:IsValid() then
    -- ...
end
```

### Transform Methods

```lua
-- Get/Set position
local pos = transform:GetPosition()  -- Returns {x, y, z}
transform:SetPosition({x = 1, y = 2, z = 3})
transform:SetPosition(1, 2, 3)  -- Also works

-- Get/Set rotation
local rot = transform:GetRotation()  -- Returns {x, y, z} in degrees
transform:SetRotation({x = 0, y = 45, z = 0})

-- Get/Set scale
local scale = transform:GetScale()  -- Returns {x, y, z}
transform:SetScale(1, 1, 1)

-- Relative transforms
transform:Translate(1, 0, 0)  -- Move +1 on X
transform:Rotate(0, 45, 0)    -- Rotate 45 degrees around Y
```

### Input

```lua
local forward = Input.getAxis("MoveForward")
local right = Input.getAxis("MoveRight")

if Input.isActionPressed("Jump") then
    -- Rising edge
end

if Input.isActionDown("Sprint") then
    -- Held
end

if Input.isActionReleased("Interact") then
    -- Falling edge
end

if Input.isMouseButtonPressed(0) then
    -- Left mouse button pressed
end

local mousePos = Input.getMousePosition()  -- Returns {x, y}
```

Action and axis bindings are authored in the project's `.hpinput` v1 profile.
`Input.isKeyPressed` remains available with its legacy held behavior.

### Scene / Physics (gameplay)

```lua
local enemy = Scene.instantiate("assets/prefabs/Enemy.hpprefab", {
  position = { x = 0, y = 1, z = 0 },
  vars = { health = 40, moveSpeed = 3.5 }
})
local spawns = Scene.findAllWithPrefix("EnemySpawn")
Scene.destroy(enemy)

local hit = Physics.raycast(ox, oy, oz, dx, dy, dz, 80)
-- Physics.raycast(..., maxDistance, "Character") filters by the project matrix

function onTriggerEnter(other, contact) end   -- contact is nil without a valid manifold
function onTriggerStay(other, contact) end
function onTriggerExit(other) end              -- no contact
function onCollisionEnter(other, contact) end  -- contact may be nil
function onCollisionStay(other, contact) end
function onCollisionExit(other) end            -- no contact
-- contact = { point, normal, relativeVelocity, penetration }
-- normal separates this body from other; point is the manifold midpoint
if hit and hit.entity then
  hit.entity:Call("onDamage", 25)
end

Physics.move(wishX, wishZ, 6.0)
if Physics.isGrounded() then
  Physics.jump(5.5)
end
```

### Audio

```lua
-- Play a one-shot sound
local handle = Audio.play("assets/audio/click.wav")

-- Loop background music at 40% volume
local music = Audio.play("assets/audio/theme.mp3", true, 0.4)

if Audio.isPlaying(music) then
    Audio.setVolume(music, 0.3)
end

Audio.pause(music)
Audio.resume(music)
Audio.stop(music)
```

Available helpers (Audio):

- `Audio.isAvailable()` → `boolean`
- `Audio.play(path, loop=false, volume=1.0)` → `handle` (`0` on failure)
- `Audio.stop(handle)` → `boolean`
- `Audio.pause(handle)` → `boolean`
- `Audio.resume(handle)` → `boolean`
- `Audio.setVolume(handle, volume)` → `boolean` (`volume` in `0.0..1.0`)
- `Audio.isPlaying(handle)` → `boolean`
- `Audio.stopAll()`

## Performance

- **Lua execution**: ~100x slower than native code, but plenty fast for gameplay
- **Script overhead**: Minimal - only active scripts update
- **Memory**: Lua scripts are lightweight (~10-50KB per script)
- **Best practices**:
  - Use Lua for game logic, input handling, AI
  - Keep tight loops and heavy math in C++ (if needed)
  - Profile with `engine_log()` to identify bottlenecks

## Debugging

Use `engine_log()` (Lua) or `print()` for console output:

```lua
function onUpdate(deltaTime)
    engine_log("[MyScript] Frame time: " .. deltaTime)
    
    local transform = this:GetTransform()
    local pos = transform:GetPosition()
    engine_log("[MyScript] Position: " .. pos.x .. ", " .. pos.y .. ", " .. pos.z)
end
```

Check the Output panel in the editor for `[Lua]` prefixed messages.

## Installation

Lua support requires the Lua development libraries. See `LUA_INSTALLATION_GUIDE.md` for platform-specific instructions:

**Windows (vcpkg):**
```powershell
.\\vcpkg install lua:x64-windows
cd build && cmake .. && cmake --build . --config Release
```

**Linux:**
```bash
sudo apt-get install liblua5.3-dev
cd build && cmake .. && cmake --build . --config Release
```

## Next Steps

1. ✅ Read `SCRIPTING_QUICK_START.md` for immediate setup
2. ✅ Create your first Lua script
3. ✅ Read `LUA_SCRIPTING_GUIDE.md` for complete API reference
4. ✅ Check `scripts/example.lua` for patterns

## Support

- Full documentation in `Documentation/Scripting/`
- Examples in `scripts/` directory
- API reference in `LUA_SCRIPTING_GUIDE.md`

---

**Ready to create your first Lua script?** Start with `SCRIPTING_QUICK_START.md`!
