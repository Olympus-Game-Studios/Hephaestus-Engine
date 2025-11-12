-- Simple rotation test script
-- Makes the entity rotate in circles to verify Lua scripting works

function onAwake()
    engine_log("[rotate.lua] Script awakened on entity: " .. tostring(Entity.getName() or "unnamed"))
end

function onUpdate(deltaTime)
    -- Constant rotation around Y axis (vertical spin)
    Transform.rotate(0.0, 2.0 * deltaTime, 0.0)
    
    -- Optional: also rotate around X and Z for interesting motion
    Transform.rotate(0.5 * deltaTime, 0.0, 0.3 * deltaTime)
end

function onDestroy()
    engine_log("[rotate.lua] Script destroyed")
end
