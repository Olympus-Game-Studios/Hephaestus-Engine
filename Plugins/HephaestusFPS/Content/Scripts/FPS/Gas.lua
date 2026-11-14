-- Tear gas clouds (Registry.gasClouds, set by FPS/Grenade.lua kind "gas"): how deep a point is in one, and the
-- damage a cloud does to everyone inside it. A cloud is { x, y, z, radius, height, age, life, damagePerSecond,
-- thrower (combatant key or nil) }; it spreads to full size over its first spreadSeconds.
local Registry = require("FPS.Registry")
local Health = require("FPS.Health")

local Gas = {}

local SPREAD_SECONDS = 1.5
local CORE = 0.6 -- full strength inside this fraction of the radius, fading to nothing at the edge

-- 0 outside the cloud, 1 in its core.
function Gas.exposure(cloud, x, y, z)
    local radius = cloud.radius * math.min(1, 0.35 + 0.65 * (cloud.age or 0) / SPREAD_SECONDS)
    if y < cloud.y - 0.5 or y > cloud.y + (cloud.height or 3) then
        return 0
    end
    local d = math.sqrt((x - cloud.x) ^ 2 + (z - cloud.z) ^ 2)
    if d >= radius then
        return 0
    end
    return math.min(1, (radius - d) / (radius * (1 - CORE)))
end

-- The strongest exposure at a point and the cloud it comes from.
function Gas.at(x, y, z)
    local best, from = 0, nil
    for _, cloud in pairs(Registry.gasClouds or {}) do
        local e = Gas.exposure(cloud, x, y, z)
        if e > best then
            best, from = e, cloud
        end
    end
    return best, from
end

-- Whether the cloud hurts this combatant: its thrower always (as their own grenade would), anyone when friendly fire
-- is on, otherwise only the other team.
function Gas.hurts(cloud, victim)
    local thrower = cloud.thrower and Registry.combatants[cloud.thrower]
    if not thrower or not victim or thrower.key == victim.key or Registry.friendlyFire then
        return true
    end
    return not Registry.sameTeam(thrower, victim)
end

-- Damages everyone with health inside the cloud for `seconds` of exposure, credited to the thrower.
function Gas.applyDamage(cloud, seconds)
    local thrower = cloud.thrower and Registry.combatants[cloud.thrower]
    local instigator = thrower and thrower.entity or nil
    local rows = {}
    for _, row in pairs(Registry.health) do
        rows[#rows + 1] = row
    end
    for _, row in ipairs(rows) do
        local entity = row.entity
        local transform = not row.dead and entity and entity:IsValid() and entity:GetTransform()
        local p = transform and transform:GetWorldPosition()
        local victim = Registry.combatants[row.key]
        if p and Gas.hurts(cloud, victim) then
            local e = Gas.exposure(cloud, p.x, p.y + 1.2, p.z)
            if e > 0 then
                local amount = cloud.damagePerSecond * e * seconds
                Health.applyDamage(entity, amount, instigator)
                entity:Call("onHit", amount, "gas", 0, instigator)
                entity:Call("onHealthChanged", Health.getHealth(entity), Health.getMaxHealth(entity))
            end
        end
    end
end

return Gas
