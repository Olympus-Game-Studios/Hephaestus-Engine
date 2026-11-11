local Registry = {
    sessionOpen = false,
    player = nil,
    weapons = {},
    weaponOrder = {},
    health = {},
    interactables = {},
    hitZones = {},
    pickups = {},
    waveDirector = nil,
    noises = {},
    noiseCount = 0,
    dangers = {},
    enemies = {},
    combatants = {},
    friendlyFire = true,
    matchDirector = nil,
    flanker = nil,
    lastBlast = nil,
    liveGrenades = {},
    smokeClouds = {},
    muzzles = {},
    playerHits = {},
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
    Registry.combatants = {}
    Registry.friendlyFire = true
    Registry.matchDirector = nil
    Registry.flanker = nil
    Registry.lastBlast = nil
    Registry.liveGrenades = {}
    Registry.smokeClouds = {}
    Registry.muzzles = {}
    Registry.playerHits = {}
    Registry.corpses = {}
    Registry.gameOver = nil
    Registry.interactables = {}
    Registry.hitZones = {}
    Registry.pickups = {}
    Registry.waveDirector = nil
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
        row.team = row.team or 1
        Registry.combatants[row.key] = row
    end
end

function Registry.clearPlayer()
    if Registry.player and Registry.player.key then
        Registry.combatants[Registry.player.key] = nil
    end
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

-- team is the team that made it (a player noise is the player's team); nil for explosions.
function Registry.reportNoise(x, y, z, radius, source, team)
    Registry.noiseCount = Registry.noiseCount + 1
    source = source or "player"
    if team == nil and source == "player" and Registry.player then
        team = Registry.player.team
    end
    table.insert(Registry.noises, { id = Registry.noiseCount, x = x, y = y, z = z, radius = radius or 20,
                                    source = source, team = team })
    while #Registry.noises > 32 do
        table.remove(Registry.noises, 1)
    end
end

-- Where something that just hurt the player was, for the damage direction indicator (newest last, at most 8).
function Registry.notePlayerHit(x, y, z, time)
    table.insert(Registry.playerHits, { x = x, y = y, z = z, time = time })
    while #Registry.playerHits > 8 do
        table.remove(Registry.playerHits, 1)
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

-- Everyone who fights, by key: the player's row and each soldier's, with entity, key, team, name, eyeHeight,
-- stance and speed. The combatant an entity belongs to, looking through hit zones to their owner, or nil.
function Registry.combatantOf(entity)
    local key = entityKey(entity)
    local zone = key and Registry.hitZones[key]
    if zone and zone.owner then
        key = entityKey(zone.owner)
    end
    return key and Registry.combatants[key] or nil
end

function Registry.sameTeam(a, b)
    return a ~= nil and b ~= nil and a.team ~= nil and a.team == b.team
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
