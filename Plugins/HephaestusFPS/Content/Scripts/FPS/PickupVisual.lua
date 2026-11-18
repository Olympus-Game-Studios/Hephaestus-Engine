-- A visible part of a pickup (FPS.Pickup on its parent): it spins, or turns to face the player, and hides while the
-- pickup is taken.
local Registry = require("FPS.Registry")

Expose.string("motion", "spin") -- spin, or face: a plane stood up by its Z rotation turns its face to the player
Expose.number("spinDegreesPerSecond", 90)

local parentKey
local rest
local angle = 0
local shown = true

function onAwake()
    local parent = this:GetParent()
    parentKey = parent and Registry.key(parent)
    rest = Transform.getRotation()
    angle = 0
    shown = true
end

function onUpdate(dt)
    local pickup = parentKey and Registry.pickups[parentKey]
    local show = not (pickup and pickup.taken)
    if show ~= shown then
        shown = show
        Mesh.setVisible(show)
    end
    if not show then
        return
    end
    if motion == "face" then
        local row = Registry.player
        local player = row and row.entity and row.entity:GetTransform()
        local at = player and player:GetWorldPosition()
        local here = this:GetTransform():GetWorldPosition()
        if at and here then
            Transform.setRotation(rest.x, math.atan(at.z - here.z, here.x - at.x), rest.z)
        end
    else
        angle = angle + math.rad(spinDegreesPerSecond) * dt
        Transform.setRotation(rest.x, rest.y + angle, rest.z)
    end
end
