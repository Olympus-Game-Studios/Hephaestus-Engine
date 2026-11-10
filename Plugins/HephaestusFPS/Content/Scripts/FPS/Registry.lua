local Registry = {
    sessionOpen = false,
    player = nil,
    weapons = {},
    weaponOrder = {},
    health = {},
    interactables = {},
    hitZones = {},
    noises = {},
    noiseCount = 0,
    dangers = {},
    enemies = {},
    flanker = nil,
    lastBlast = nil,
    liveGrenades = {},
    corpses = {},
    gameOver = nil,
    currentTarget = nil,
    activeWeaponId = nil,
}

local function entityKey(entity)
    if entity == nil then
        return nil
    end
    local kind = type(entity)
    if kind == "string" or kind == "number" then
        return tostring(entity)
    end
    if kind == "table" and entity.key then
        return tostring(entity.key)
    end
    if kind == "userdata" and entity.GetId then
        return entity:GetId()
    end
    return tostring(entity)
end

function Registry.ensureSession()
    if Registry.sessionOpen then
        return
    end
    Registry.player = nil
    Registry.weapons = {}
    Registry.weaponOrder = {}
    Registry.health = {}
    Registry.noises = {}
    Registry.noiseCount = 0
    Registry.dangers = {}
    Registry.enemies = {}
    Registry.flanker = nil
    Registry.lastBlast = nil
    Registry.liveGrenades = {}
    Registry.corpses = {}
    Registry.gameOver = nil
    Registry.interactables = {}
    Registry.hitZones = {}
    Registry.currentTarget = nil
    Registry.activeWeaponId = nil
    Registry.sessionOpen = true
end

function Registry.key(entity)
    return entityKey(entity)
end

function Registry.setPlayer(entity, row)
    Registry.ensureSession()
    Registry.player = row
    if row then
        row.entity = entity
        row.key = entityKey(entity)
    end
end

function Registry.clearPlayer()
    Registry.player = nil
    Registry.currentTarget = nil
    Registry.sessionOpen = false
end

-- Sounds the AI can hear: each noise gets a rising id so a listener only reacts to new ones. radius is how far
-- away it is heard, before each listener's own hearing scale. source is "player", "enemy" or "explosion".
-- Bodies left by the dead, oldest first. Adding one past `limit` (0 = no limit) returns the entities of the oldest
-- bodies to remove.
function Registry.addCorpse(entity, key, limit)
    table.insert(Registry.corpses, { entity = entity, key = key })
    local removed = {}
    while limit > 0 and #Registry.corpses > limit do
        removed[#removed + 1] = table.remove(Registry.corpses, 1).entity
    end
    return removed
end

function Registry.removeCorpse(key)
    for i, corpse in ipairs(Registry.corpses) do
        if corpse.key == key then
            table.remove(Registry.corpses, i)
            return
        end
    end
end

function Registry.reportNoise(x, y, z, radius, source)
    Registry.noiseCount = Registry.noiseCount + 1
    table.insert(Registry.noises, { id = Registry.noiseCount, x = x, y = y, z = z, radius = radius or 20,
                                    source = source or "player" })
    while #Registry.noises > 32 do
        table.remove(Registry.noises, 1)
    end
end

-- Live threats the AI should move away from, such as a frag about to go off, keyed by entity.
function Registry.setDanger(entity, x, y, z, radius)
    local key = entityKey(entity)
    if key then
        Registry.dangers[key] = { x = x, y = y, z = z, radius = radius }
    end
end

function Registry.clearDanger(entity)
    local key = entityKey(entity)
    if key then
        Registry.dangers[key] = nil
    end
end

function Registry.activeWeapon()
    local key = Registry.activeWeaponId
    return key and Registry.weapons[key] or nil
end

function Registry.weaponSnapshot(entity)
    local key = entityKey(entity)
    local row = key and Registry.weapons[key] or Registry.activeWeapon()
    if not row then
        return nil
    end
    return {
        displayName = row.displayName,
        magazine = (row.magazine or 0) + (row.chamber or 0),
        reserve = row.reserve,
        magazineSize = row.magazineSize,
        reloading = (row.reloadRemaining or 0) > 0,
        entity = row.entity,
        key = row.key,
    }
end

return Registry
