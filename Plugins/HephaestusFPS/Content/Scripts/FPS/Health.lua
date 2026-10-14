local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")

local Health = {}

function Health.attach(entity, options)
    Registry.ensureSession()
    options = options or {}
    local key = Registry.key(entity)
    if not key then
        return
    end
    local maxHealth = options.maxHealth or 100
    local row = Logic.createHealth(maxHealth)
    row.invulnerable = options.invulnerable == true
    row.entity = entity
    row.key = key
    Registry.health[key] = row
end

function Health.detach(entity)
    local key = Registry.key(entity)
    if key then
        Registry.health[key] = nil
    end
end

function Health.get(entity)
    local key = Registry.key(entity)
    return key and Registry.health[key] or nil
end

function Health.getHealth(entity)
    local row = Health.get(entity)
    return row and row.health or 0
end

function Health.getMaxHealth(entity)
    local row = Health.get(entity)
    return row and row.maxHealth or 0
end

function Health.isDead(entity)
    local row = Health.get(entity)
    return row ~= nil and row.dead == true
end

function Health.setInvulnerable(entity, value)
    local row = Health.get(entity)
    if row then
        row.invulnerable = value == true
    end
end

function Health.heal(entity, amount)
    local row = Health.get(entity)
    if row then
        Logic.heal(row, amount)
    end
end

function Health.applyDamage(entity, amount, instigator)
    local row = Health.get(entity)
    if not row then
        return false
    end
    local died = Logic.applyDamage(row, amount)
    if died and entity and entity.Call then
        entity:Call("onDeath", instigator)
    end
    return died
end

return Health
