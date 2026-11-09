local Registry = {
    sessionOpen = false,
    player = nil,
    weapons = {},
    weaponOrder = {},
    health = {},
    interactables = {},
    hitZones = {},
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
