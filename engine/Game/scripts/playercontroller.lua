-- playercontroller.lua
-- First-person controller for Hephaestus Engine Lua runtime.
-- Requires collider + rigidbody on the entity. When the Camera binding is available,
-- this script also drives the runtime camera for first-person view.

local config = {
    moveSpeed = 6.0,
    sprintMultiplier = 1.8,
    lookSensitivity = 0.0035,
    maxPitch = math.rad(89.0),
    requireRightMouse = true,
    verticalSpeed = 0.0,
    cameraOffsetY = 1.6,
}

local state = {
    yaw = 0.0,
    pitch = 0.0,
    lastMouseX = nil,
    lastMouseY = nil,
    initialized = false,
    hadRightMouse = false,
    cameraActive = false,
}

local function normalize2(x, z)
    local lengthSq = x * x + z * z
    if lengthSq < 1e-5 then
        return 0.0, 0.0
    end
    local invLen = 1.0 / math.sqrt(lengthSq)
    return x * invLen, z * invLen
end

local function getMoveInput()
    local x = 0.0
    local z = 0.0

    if Input.isKeyPressed("A") then x = x - 1.0 end
    if Input.isKeyPressed("D") then x = x + 1.0 end
    if Input.isKeyPressed("W") then z = z - 1.0 end
    if Input.isKeyPressed("S") then z = z + 1.0 end

    x, z = normalize2(x, z)

    local isSprinting = Input.isKeyPressed("LEFT_SHIFT") or Input.isKeyPressed("RIGHT_SHIFT")
    local speed = config.moveSpeed * (isSprinting and config.sprintMultiplier or 1.0)

    return x, z, speed
end

local function applyMouseLook()
    if config.requireRightMouse and not Input.isMouseButtonPressed("RIGHT") then
        state.lastMouseX = nil
        state.lastMouseY = nil
        return
    end

    local mouse = Input.getMousePosition()
    if not mouse then
        return
    end

    if state.lastMouseX == nil then
        state.lastMouseX = mouse.x
        state.lastMouseY = mouse.y
        return
    end

    local deltaX = mouse.x - state.lastMouseX
    local deltaY = mouse.y - state.lastMouseY

    state.lastMouseX = mouse.x
    state.lastMouseY = mouse.y

    state.yaw = state.yaw - deltaX * config.lookSensitivity
    state.pitch = state.pitch - deltaY * config.lookSensitivity

    if state.pitch > config.maxPitch then state.pitch = config.maxPitch end
    if state.pitch < -config.maxPitch then state.pitch = -config.maxPitch end

    Transform.setRotation(state.pitch, state.yaw, 0.0)
end

local function movePlayer(deltaTime)
    local x, z, speed = getMoveInput()
    if x == 0.0 and z == 0.0 then
        return
    end

    local forwardX = math.cos(state.yaw)
    local forwardZ = math.sin(state.yaw)
    local rightX = -math.sin(state.yaw)
    local rightZ = math.cos(state.yaw)

    local moveX = (rightX * x + forwardX * z) * speed * deltaTime
    local moveZ = (rightZ * x + forwardZ * z) * speed * deltaTime

    Transform.translate(moveX, 0.0, moveZ)
end

local function updateCamera()
    if not Camera or not Camera.setPosition or not state.cameraActive then
        return
    end

    local pos = Transform.getPosition()
    if not pos then
        return
    end

    Camera.setPosition(pos.x, pos.y + config.cameraOffsetY, pos.z)
    if Camera.setRotation then
        Camera.setRotation(state.pitch, state.yaw, 0.0)
    end
end

function onAwake()
    local rot = Transform.getRotation()
    if rot then
        state.pitch = rot.x or 0.0
        state.yaw = rot.y or 0.0
    end
    Transform.setRotation(state.pitch, state.yaw, 0.0)

    local name = Entity.getName() or "Player"
    Entity.setName(name .. " [Lua]")
    engine_log("[playercontroller.lua] Ready - WASD to move, hold RMB to look")

    if Camera and Camera.setActive then
        engine_log("[playercontroller.lua] Camera binding found, taking control...")
        Camera.setActive(true)
        state.cameraActive = true
        updateCamera()
        engine_log("[playercontroller.lua] Camera control active: " .. tostring(Camera.isActive()))
    else
        engine_log("[playercontroller.lua] WARNING: Camera binding not available!")
    end
end

function onEnable()
    state.lastMouseX = nil
    state.lastMouseY = nil
    engine_log("[playercontroller.lua] onEnable called")
    if Camera and Camera.setActive then
        engine_log("[playercontroller.lua] Re-enabling camera control")
        Camera.setActive(true)
        state.cameraActive = true
        if Camera.setRotation then
            Camera.setRotation(state.pitch, state.yaw, 0.0)
        end
        updateCamera()
        engine_log("[playercontroller.lua] Camera active: " .. tostring(Camera.isActive()))
    end
end

function onUpdate(deltaTime)
    deltaTime = deltaTime or Time.deltaTime()
    if deltaTime <= 0.0 then
        return
    end

    applyMouseLook()
    movePlayer(deltaTime)
    updateCamera()
end

function onDisable()
    state.lastMouseX = nil
    state.lastMouseY = nil
    if state.cameraActive and Camera and Camera.setActive then
        Camera.setActive(false)
    end
    state.cameraActive = false
end

function onDestroy()
    state.lastMouseX = nil
    state.lastMouseY = nil
    if state.cameraActive and Camera and Camera.setActive then
        Camera.setActive(false)
    end
    state.cameraActive = false
end
