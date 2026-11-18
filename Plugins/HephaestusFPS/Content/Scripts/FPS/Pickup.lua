-- A health, ammo or grenade pickup (see FPS.Pickups for the kinds). The player takes it by walking over it, or by
-- using it when it has a prompt. It stays when it would do nothing (full health, full ammo), and once taken it hides
-- and comes back after respawnSeconds (0: never). It bobs while it waits; children with FPS.PickupVisual spin or
-- face the player and hide with it.
local Interactable = require("FPS.Interactable")
local Health = require("FPS.Health")
local Registry = require("FPS.Registry")
local Pickups = require("FPS.Pickups")
local Hud = require("FPS.Hud")

Expose.string("kind", "health")
Expose.number("amount", 50) -- health points, magazines of ammo, or grenades
Expose.number("radius", 1.3)
Expose.number("respawnSeconds", 25)
Expose.string("prompt", "") -- set to make the player use it instead of walking over it
Expose.string("sound", "")
Expose.number("soundVolume", 0.8)
Expose.number("bobHeight", 0.08)
Expose.number("bobSeconds", 2.0)

local state
local base
local selfKey
local clock = 0

local function playerFeet()
    local row = Registry.player
    local transform = row and row.entity and row.entity:GetTransform()
    return transform and transform:GetWorldPosition()
end

local function gather(player)
    local weapons, active = {}, nil
    for i, key in ipairs(Registry.weaponOrder) do
        local row = Registry.weapons[key]
        weapons[i] = row or {}
        if key == Registry.activeWeaponId then
            active = i
        end
    end
    local row = Registry.player or {}
    return {
        health = Health.getHealth(player),
        maxHealth = Health.getMaxHealth(player),
        weapons = weapons,
        activeWeapon = active,
        grenades = row.grenades,
        maxGrenades = row.maxGrenades,
    }
end

local function setShown(shown)
    state.taken = not shown
    if Mesh and Mesh.setVisible then
        Mesh.setVisible(shown)
    end
    if prompt ~= "" then
        if shown then
            Interactable.register(this, { prompt = prompt, maxDistance = 2.5, enabled = true })
        else
            Interactable.unregister(this)
        end
    end
end

local function take(player)
    if state.taken or Registry.gameOver or not player then
        return false
    end
    local now = gather(player)
    local plan = Pickups.plan(kind, amount, now)
    if not plan then
        return false
    end
    if plan.health then
        Health.heal(player, plan.health)
        player:Call("onHealthChanged", Health.getHealth(player), Health.getMaxHealth(player))
    end
    for i, gain in pairs(plan.ammo or {}) do
        now.weapons[i].reserve = (now.weapons[i].reserve or 0) + gain
    end
    for _, which in ipairs({ "frag", "smoke" }) do
        if plan[which] and now.grenades then
            now.grenades[which] = (now.grenades[which] or 0) + plan[which]
        end
    end
    local color = Pickups.colors[kind] or { 1, 1, 1 }
    Hud.showPickup(Pickups.describe(plan, now.activeWeapon, Registry.player and Registry.player.tacticalLabel),
                   color[1], color[2], color[3])
    if sound ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(sound, { volume = soundVolume, bus = "sfx", priority = 3, pitchJitter = 0.04 })
    end
    setShown(false)
    state.respawnAt = respawnSeconds > 0 and clock + respawnSeconds or nil
    return true
end

function onAwake()
    clock = 0
    base = Transform.getPosition()
    state = { taken = false, respawnAt = nil }
    selfKey = Registry.key(this)
    Registry.ensureSession() -- opening it later would clear pickups
    Registry.pickups[selfKey] = state
    setShown(true)
end

function onUpdate(dt)
    clock = clock + dt
    if state.taken then
        if state.respawnAt and clock >= state.respawnAt then
            setShown(true)
        end
        return
    end
    local phase = clock * 2 * math.pi / math.max(bobSeconds, 0.1)
    Transform.setPosition(base.x, base.y + math.sin(phase) * bobHeight, base.z)
    if prompt ~= "" then
        return
    end
    local feet = playerFeet()
    if feet then
        local dx, dz = feet.x - base.x, feet.z - base.z
        if dx * dx + dz * dz <= radius * radius and math.abs(feet.y - base.y) <= 2.0 then
            take(Registry.player.entity)
        end
    end
end

function onInteract(player)
    take(player)
end

function onDestroy()
    Interactable.unregister(this)
    if selfKey then
        Registry.pickups[selfKey] = nil
    end
end
