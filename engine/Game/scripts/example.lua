-- Example Lua script for Hephaestus Engine
-- Demonstrates lifecycle callbacks, transform manipulation, and input helpers.

local speed = 4.0

function onAwake()
    engine_log("[example.lua] Script attached to entity '" .. tostring(Entity.getName() or "") .. "'")
end

function onEnable()
    engine_log("[example.lua] Enabled")
end

function onUpdate(deltaTime)
    -- Simple WASD movement on the X/Z plane
    local moveX = 0.0
    local moveZ = 0.0

    local aPressed = Input.isKeyPressed("A")
    local dPressed = Input.isKeyPressed("D")
    local wPressed = Input.isKeyPressed("W")
    local sPressed = Input.isKeyPressed("S")
    
    if aPressed then moveX = moveX - 1.0 end
    if dPressed then moveX = moveX + 1.0 end
    if wPressed then moveZ = moveZ - 1.0 end
    if sPressed then moveZ = moveZ + 1.0 end

    if aPressed or dPressed or wPressed or sPressed then
        engine_log("[example.lua] Keys: A=" .. tostring(aPressed) .. " D=" .. tostring(dPressed) .. " W=" .. tostring(wPressed) .. " S=" .. tostring(sPressed) .. " moveX=" .. tostring(moveX) .. " moveZ=" .. tostring(moveZ))
    end

    if moveX ~= 0.0 or moveZ ~= 0.0 then
        -- Normalize simple 2D input so diagonal speed stays consistent
        local length = math.sqrt(moveX * moveX + moveZ * moveZ)
        moveX = moveX / length
        moveZ = moveZ / length
        Transform.translate(moveX * speed * deltaTime, 0.0, moveZ * speed * deltaTime)
    end

    -- Slowly rotate for visual feedback
    Transform.rotate(0.0, 0.5 * deltaTime, 0.0)
end

function onLateUpdate(deltaTime)
    -- Keep the entity's name synced with its position for debugging
    local pos = Transform.getPosition()
    if pos then
        local label = string.format("LuaEntity (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z)
        Entity.setName(label)
    end
end

function onDisable()
    engine_log("[example.lua] Disabled")
end

function onDestroy()
    engine_log("[example.lua] Destroyed")
end
