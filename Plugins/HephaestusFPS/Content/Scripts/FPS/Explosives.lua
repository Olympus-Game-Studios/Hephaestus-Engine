local Registry = require("FPS.Registry")
local Health = require("FPS.Health")

local Explosives = {}

-- Full effect inside inner, falling linearly to none at radius.
function Explosives.falloff(distance, inner, radius)
    if distance >= radius then
        return 0
    end
    if distance <= inner then
        return 1
    end
    return 1 - (distance - inner) / math.max(radius - inner, 1e-4)
end

-- Origins are usually at the feet, so targets are tested above them: the player at chest height, others
-- half a metre up so the ray does not graze the floor.
local function targetPoint(row)
    local transform = row.entity and row.entity.GetTransform and row.entity:GetTransform()
    local p = transform and transform:GetWorldPosition()
    if not p then
        return nil
    end
    local lift = (Registry.player and row.key == Registry.player.key) and 1.0 or 0.5
    return p.x, p.y + lift, p.z
end

local function belongsTo(entity, row)
    local key = Registry.key(entity)
    if key == row.key then
        return true
    end
    local zone = Registry.hitZones[key]
    return zone ~= nil and Registry.key(zone.owner) == row.key
end

-- Cover between the blast and a target blocks the damage. The ray skips the calling script's own entity.
local function exposed(x, y, z, tx, ty, tz, distance, row)
    if distance < 1e-3 then
        return true
    end
    local dx, dy, dz = (tx - x) / distance, (ty - y) / distance, (tz - z) / distance
    local hit = Physics.raycast(x, y, z, dx, dy, dz, distance, nil, true)
    return not hit or not hit.entity or belongsTo(hit.entity, row) or (hit.distance or 0) >= distance - 0.25
end

-- Damages every living entity with health in range. byPlayer shows the player's hit and kill markers.
-- Returns how many targets were damaged.
function Explosives.applyBlast(x, y, z, options)
    local radius = options.radius or 6
    local inner = options.innerRadius or 1.5
    local damaged = 0
    local rows = {}
    for _, row in pairs(Registry.health) do
        rows[#rows + 1] = row
    end
    for _, row in ipairs(rows) do
        local tx, ty, tz = targetPoint(row)
        if tx and not row.dead then
            local distance = math.sqrt((tx - x) ^ 2 + (ty - y) ^ 2 + (tz - z) ^ 2)
            local scale = Explosives.falloff(distance, inner, radius)
            if scale > 0 and exposed(x, y, z, tx, ty, tz, distance, row) then
                local amount = (options.damage or 100) * scale
                local target = row.entity
                local died = Health.applyDamage(target, amount, options.instigator)
                target:Call("onHit", amount, "blast", distance)
                target:Call("onHealthChanged", Health.getHealth(target), Health.getMaxHealth(target))
                damaged = damaged + 1
                local player = Registry.player
                if options.byPlayer and player and row.key ~= player.key then
                    player.hitEvent = (died or player.hitEvent == "kill") and "kill" or "hit"
                end
            end
        end
    end
    return damaged
end

return Explosives
