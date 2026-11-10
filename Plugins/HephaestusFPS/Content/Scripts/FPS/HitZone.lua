-- A collider that forwards hits to the entity named by owner with a damage zone: head, body or limb.
local Registry = require("FPS.Registry")

Expose.string("owner", "")
Expose.string("zone", "body")

function onAwake()
    Registry.ensureSession()
    local key = Registry.key(this)
    -- No owner name means the zone's parent, which keeps zones working on prefab copies with repeated names.
    local target = nil
    if owner == "" then
        target = this:GetParent()
    else
        for _, candidate in ipairs(Scene.findAllWithPrefix(owner)) do
            if candidate:GetName() == owner then
                target = candidate
                break
            end
        end
    end
    if not key or not target then
        engine_log("[FPS.HitZone] owner '" .. tostring(owner) .. "' not found for " .. tostring(this:GetName()))
        return
    end
    Registry.hitZones[key] = { zone = zone, owner = target, entity = this }
end

function onDestroy()
    local key = Registry.key(this)
    if key then
        Registry.hitZones[key] = nil
    end
end

function setZoneVisible(visible)
    Mesh.setVisible(visible == true)
end
