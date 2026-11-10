-- Which live grenades to warn the player about, where on screen and how urgently. Distances are measured the way
-- Explosives.applyBlast measures them: from 0.2 m above the grenade to the player's chest.
local Explosives = require("FPS.Explosives")

local GrenadeWarning = {}

-- grenades: list of { x, y, z, damage, radius, innerRadius }. Returns the nearest `limit` within `range` metres
-- (or the blast radius, if larger) as { angle, level, distance }: angle in radians clockwise from straight ahead,
-- level "lethal" when the blast would take all of `health`, "harmful" when it would hurt, otherwise "near".
function GrenadeWarning.compute(feet, yaw, health, grenades, range, limit)
    local fx, fz = math.cos(yaw), -math.sin(yaw)
    local rx, rz = math.sin(yaw), math.cos(yaw)
    local cx, cy, cz = feet.x, feet.y + 1.0, feet.z
    local out = {}
    for _, g in ipairs(grenades) do
        local dx, dy, dz = g.x - cx, g.y + 0.2 - cy, g.z - cz
        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        if distance <= math.max(range, g.radius) then
            local damage = (g.damage or 0) * Explosives.falloff(distance, g.innerRadius or 0, g.radius)
            local level = damage >= health and "lethal" or (damage > 0 and "harmful" or "near")
            out[#out + 1] = { angle = math.atan(dx * rx + dz * rz, dx * fx + dz * fz), level = level,
                              distance = distance }
        end
    end
    table.sort(out, function(a, b) return a.distance < b.distance end)
    while #out > limit do
        table.remove(out)
    end
    return out
end

return GrenadeWarning
