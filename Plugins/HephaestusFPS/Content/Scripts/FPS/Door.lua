local Interactable = require("FPS.Interactable")

Expose.string("prompt", "Open")
Expose.number("openYawDegrees", 90)
Expose.number("turnSeconds", 0.4)

local closedYaw = 0
local open = false
local currentYaw = 0
local targetYaw = 0

function onAwake()
    local rot = Transform.getRotation()
    closedYaw = rot and rot.y or 0
    currentYaw = closedYaw
    targetYaw = closedYaw
    Interactable.register(this, { prompt = prompt, maxDistance = 2.5, enabled = true })
end

function onInteract(player)
    open = not open
    local delta = openYawDegrees * math.pi / 180
    targetYaw = open and (closedYaw + delta) or closedYaw
end

function onUpdate(dt)
    local speed = (openYawDegrees * math.pi / 180) / math.max(turnSeconds, 0.01)
    if currentYaw < targetYaw then
        currentYaw = math.min(targetYaw, currentYaw + speed * dt)
    elseif currentYaw > targetYaw then
        currentYaw = math.max(targetYaw, currentYaw - speed * dt)
    end
    local rot = Transform.getRotation()
    Transform.setRotation(rot and rot.x or 0, currentYaw, rot and rot.z or 0)
end

function onDestroy()
    Interactable.unregister(this)
end
