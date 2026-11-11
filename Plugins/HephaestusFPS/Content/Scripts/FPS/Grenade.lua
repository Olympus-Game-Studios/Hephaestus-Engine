-- A thrown grenade: launched with vx, vy, vz once its body exists, then a frag explodes or a smoke grenade
-- spawns its cloud when the fuse runs out.
local Registry = require("FPS.Registry")
local Explosives = require("FPS.Explosives")

Expose.string("kind", "frag")
Expose.number("fuseSeconds", 3.0)
Expose.number("vx", 0)
Expose.number("vy", 0)
Expose.number("vz", 0)
Expose.number("spin", 10)
Expose.bool("thrownByPlayer", false)
Expose.string("thrownBy", "") -- the key of the soldier that threw it, who is credited with its kills
Expose.number("damage", 150)
Expose.number("radius", 7)
Expose.number("innerRadius", 2)
Expose.number("shakeRadius", 18)
Expose.number("shakeDegrees", 2.5)
Expose.string("effectPrefab", "")
Expose.string("sound", "")
Expose.number("soundVolume", 1.0)
Expose.number("soundMaxDistance", 120)
Expose.number("smokeSeconds", 20)

local launched = false
local selfKey = nil
local fuse = 0
local linger = nil

local function spawnEffect(p)
    if effectPrefab ~= "" then
        Scene.instantiate(effectPrefab, { position = { x = p.x, y = p.y, z = p.z } })
    end
    if sound ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(sound, { volume = soundVolume, bus = "sfx", priority = 3, pitchJitter = 0.04,
                            position = { x = p.x, y = p.y, z = p.z }, minDistance = 4,
                            maxDistance = soundMaxDistance })
    end
end

local function shakePlayer(p)
    local player = Registry.player
    local transform = player and player.entity and player.entity:GetTransform()
    local at = transform and transform:GetWorldPosition()
    if not at then
        return
    end
    local distance = math.sqrt((at.x - p.x) ^ 2 + (at.y + 1.0 - p.y) ^ 2 + (at.z - p.z) ^ 2)
    local scale = Explosives.falloff(distance, 0, shakeRadius)
    if scale > 0 then
        player.cameraImpulses = player.cameraImpulses or {}
        table.insert(player.cameraImpulses, { pitch = math.rad(shakeDegrees) * scale,
                                              yaw = math.rad(shakeDegrees) * 0.5 * scale * (math.random() * 2 - 1),
                                              omega = 30, zeta = 0.35 })
    end
end

local function detonate()
    local p = Transform.getPosition()
    spawnEffect(p)
    if kind == "smoke" then
        linger = smokeSeconds
        -- Centred a little above the ground, where the cloud is thickest.
        Registry.smokeClouds[selfKey] = { x = p.x, y = p.y + 1.2, z = p.z, age = 0, life = smokeSeconds }
        return
    end
    Registry.clearDanger(this)
    Registry.liveGrenades[selfKey] = nil
    Registry.reportNoise(p.x, p.y, p.z, radius * 8, "explosion")
    Registry.lastBlast = { x = p.x, y = p.y, z = p.z }
    -- Lifted off the ground so the floor under the grenade does not block the blast.
    Explosives.applyBlast(p.x, p.y + 0.2, p.z, {
        damage = damage,
        radius = radius,
        innerRadius = innerRadius,
        instigator = thrownByPlayer and Registry.player and Registry.player.entity
            or (Registry.combatants[thrownBy] and Registry.combatants[thrownBy].entity) or nil,
        byPlayer = thrownByPlayer,
    })
    shakePlayer(p)
    Scene.destroy(this)
end

function onAwake()
    selfKey = Registry.key(this)
    launched = false
    fuse = fuseSeconds
    linger = nil
end

function onUpdate(dt)
    if not launched and Physics.hasBody() then
        Physics.setLinearVelocity(vx, vy, vz)
        Physics.setAngularVelocity((math.random() * 2 - 1) * spin, (math.random() * 2 - 1) * spin,
                                   (math.random() * 2 - 1) * spin)
        launched = true
    end
    -- The AI reacts to a frag once it is down (slow), not in flight, so it does not flee from the thrower's hand.
    if kind == "frag" and fuse < math.huge then
        local live = Transform.getPosition()
        Registry.liveGrenades[selfKey] = { x = live.x, y = live.y, z = live.z, damage = damage,
                                                      radius = radius, innerRadius = innerRadius }
        local v = Physics.getLinearVelocity()
        if v and v.x * v.x + v.y * v.y + v.z * v.z < 16 then
            local p = Transform.getPosition()
            Registry.setDanger(this, p.x, p.y, p.z, radius)
        else
            Registry.clearDanger(this)
        end
    end
    if linger then
        linger = linger - dt
        local cloud = Registry.smokeClouds[selfKey]
        if cloud then
            cloud.age = smokeSeconds - linger
        end
        if linger <= 0 then
            Registry.smokeClouds[selfKey] = nil
            Scene.destroy(this)
            linger = nil
        end
        return
    end
    fuse = fuse - dt
    if fuse <= 0 then
        fuse = math.huge
        detonate()
    end
end

function onDestroy()
    if selfKey then
        Registry.dangers[selfKey] = nil
        Registry.liveGrenades[selfKey] = nil
        Registry.smokeClouds[selfKey] = nil
    end
end
