# Hephaestus Engine: Lua + C++ Hybrid Scripting

## Overview

Hephaestus now supports **Lua scripting** alongside C++. This gives you the best of both worlds:

- **Lua Scripts**: Fast iteration, no recompilation, perfect for game logic
- **C++ Scripts**: Performance-critical code, tight physics loops, rendering
- **Hybrid**: Mix and match on the same entities

## Quick Start

### 1. Create a Lua Script

Create a file `scripts/MyGame.lua`:

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
2. Details Panel → Scripts
3. Select your `.lua` file in Asset Browser
4. Click "Attach"

### 3. Run

Enter play mode - your Lua script runs automatically!

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

-- Physics collisions (when implemented)
function onTriggerEnter(other)
end

function onTriggerStay(other)
end

function onTriggerExit(other)
end
```

## C++ Bindings

### Logging

```lua
engine_log("This message appears in console")
```

### Transform (Coming Soon)

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

### Entity (Coming Soon)

```lua
-- Get component
local renderer = entity:getComponent("MeshRenderer")

-- Add component
entity:addComponent("Light")

-- Remove component
entity:removeComponent("Physics")

-- Check if has component
if entity:hasComponent("Transform") then
    -- Do something
end
```

### Input (Coming Soon)

```lua
if Input.isKeyPressed("W") then
    engine_log("W key pressed!")
end

if Input.isMouseButtonDown(0) then
    engine_log("Left mouse button down")
end

-- Get mouse position
local mouseX, mouseY = Input.getMousePosition()
```

### Scene Queries (Coming Soon)

```lua
-- Find all entities with a tag
local entities = scene:findEntitiesWithTag("Enemy")

-- Get entity by name
local player = scene:findEntityByName("Player")

-- Cast rays
local hit, distance = scene:raycast(origin, direction, maxDistance)
```

## Performance Considerations

- **Lua is slower than C++** but fine for game logic
- Call C++ for performance-critical operations
- Avoid tight loops in Lua - do those in C++
- Use Lua for: AI, events, UI, game flow
- Use C++ for: physics, graphics, tight loops

## Best Practices

### ✅ Good Lua Usage
```lua
-- Game logic
function onUpdate(dt)
    -- Check conditions
    if shouldJump() then
        -- Call C++ for physics
        Player.jump()
    end
end

-- Event handling
function onItemCollected(item)
    Inventory.add(item)
    UI.updateDisplay()
end
```

### ❌ Avoid in Lua
```lua
-- DON'T: Tight render loop
for i=1,1000000 do
    -- Complex math
end

-- DON'T: Heavy physics calculations every frame
for i=1,10000 do
    -- Physics calculations
end
```

## Hybrid Example

`scripts/Player.lua` - Game logic in Lua:
```lua
local speed = 5.0

function onUpdate(dt)
    if Input.isKeyPressed("W") then
        Player.moveForward(speed * dt)
    end
end
```

`engine/Game/PlayerPhysics.h` - Physics in C++:
```cpp
class PlayerPhysics : public ScriptBase {
    void moveForward(float distance) {
        // Tight physics calculations
        // Collision detection
        // Animation updates
    }
};
```

Both attached to the same entity - Lua handles input/logic, C++ handles physics!

## Hot-Reload (Planned)

Save a Lua file while game is running → automatically reload without restarting game.

## Debugging (Planned)

```lua
-- Add breakpoints
-- Set watches
-- Inspect variables
-- Step through code
```

## Troubleshooting

**"Lua script not found"**
- Check file path is correct
- File must have `.lua` extension
- Must be in project assets or workspace scripts directory

**"Function not defined"**
- Lua functions must be defined at global scope
- Check spelling (Lua is case-sensitive)
- Avoid local functions at module level

**"Script runs slow"**
- Profile with `engine_log()` to measure frame time
- Move heavy calculations to C++
- Reduce number of script updates per frame

## Future Roadmap

- [ ] Full Entity/Component bindings
- [ ] Scene query functions
- [ ] Input system bindings
- [ ] Coroutines for time-based logic
- [ ] Hot-reload during gameplay
- [ ] Lua debugger integration
- [ ] Profiler for Lua code
- [ ] Precompiled Lua (bytecode) support
