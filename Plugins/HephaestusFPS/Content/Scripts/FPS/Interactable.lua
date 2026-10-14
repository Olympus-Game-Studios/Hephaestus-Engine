local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")

local Interactable = {}

function Interactable.register(entity, options)
    Registry.ensureSession()
    options = options or {}
    local key = Registry.key(entity)
    if not key then
        return
    end
    Registry.interactables[key] = {
        entity = entity,
        key = key,
        prompt = options.prompt or "Interact",
        maxDistance = options.maxDistance or 2.5,
        enabled = options.enabled ~= false,
    }
end

function Interactable.unregister(entity)
    local key = Registry.key(entity)
    if not key then
        return
    end
    Registry.interactables[key] = nil
    if Registry.currentTarget == key then
        Registry.currentTarget = nil
    end
end

function Interactable.query(hit, fallbackMax)
    local packed = nil
    if hit and hit.entity then
        packed = { key = Registry.key(hit.entity), distance = hit.distance }
    elseif hit and hit.key then
        packed = hit
    end
    local key = Logic.pickInteract(packed, Registry.interactables, fallbackMax)
    Registry.currentTarget = key
    return key and Registry.interactables[key] or nil
end

function Interactable.current()
    local key = Registry.currentTarget
    return key and Registry.interactables[key] or nil
end

function Interactable.prompt()
    local row = Interactable.current()
    return row and row.prompt or nil
end

return Interactable
