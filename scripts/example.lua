-- Example Lua Script for Hephaestus Engine
-- This demonstrates the hybrid C++ + Lua scripting system

-- Lifecycle functions are called by the engine
function onAwake()
    engine_log("MyLuaScript.onAwake() called!")
end

function onUpdate(deltaTime)
    -- This is called every frame
    -- deltaTime is the time elapsed since last frame in seconds
    engine_log("onUpdate called with dt=" .. tostring(deltaTime))
end

function onLateUpdate(deltaTime)
    engine_log("onLateUpdate called")
end

function onDestroy()
    engine_log("MyLuaScript destroyed")
end

-- You can also define your own functions
function myCustomFunction()
    engine_log("Custom function called!")
end

-- Game logic example
local moveSpeed = 5.0
local rotationSpeed = 2.0

function updateMovement(deltaTime)
    -- Get input (when Input bindings are complete)
    -- local moving = Input.isKeyPressed("W")
    -- Move entity, update animation, etc.
    engine_log("Movement updated")
end
