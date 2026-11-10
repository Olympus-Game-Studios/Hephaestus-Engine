-- An enemy soldier. It patrols, notices the player by sight or sound, fights with burst fire from cover it finds
-- on the navmesh, searches where it last saw the player, gets out of the way of live grenades and warns nearby
-- allies. How well it does all of this comes from FPS.AI.Difficulty, so a settings menu can change it.
-- Needs a Character Controller, a Character (for animation) and hit zone children (FPS.HitZone with no owner).
local Registry = require("FPS.Registry")
local Health = require("FPS.Health")
local Gunplay = require("FPS.Gunplay")
local Impacts = require("FPS.Impacts")
local Difficulty = require("FPS.AI.Difficulty")
local Perception = require("FPS.AI.Perception")
local Mover = require("FPS.AI.Mover")
local Cover = require("FPS.AI.Cover")
local Brain = require("FPS.AI.Brain")
local Tactics = require("FPS.AI.Tactics")
local Tracers = require("FPS.Tracers")

Expose.number("maxHealth", 100)
Expose.string("difficulty", "")
Expose.string("skillOverrides", "")
Expose.number("patrolRadius", 8)
Expose.string("patrolPoints", "")
Expose.number("patrolWaitSeconds", 2)
Expose.number("walkSpeedScale", 0.45)
Expose.number("runSpeedScale", 1.0)
Expose.number("turnDegreesPerSecond", 360)
Expose.number("modelYawOffsetDegrees", 0)
Expose.number("eyeHeight", 1.6)
Expose.number("damage", 14)
Expose.number("range", 80)
Expose.number("magazineSize", 30)
Expose.number("reloadSeconds", 2.4)
Expose.string("fireSound", "")
Expose.string("muzzleFlashPrefab", "")
Expose.string("tracerPrefab", "")
Expose.number("tracerSpeed", 350) -- how fast tracers fly, in m/s
Expose.number("fireVolume", 0.8)
Expose.number("engageDistance", 25)
Expose.number("coverSearchRadius", 12)
Expose.number("corpseSeconds", 0)
Expose.integer("maxCorpses", 8)
Expose.number("perceptionInterval", 0.1)
Expose.string("idleClip", "RifleIdle")
Expose.string("walkClip", "RifleWalk")
Expose.string("runClip", "RifleRun")
Expose.string("fragGrenadePrefab", "")
Expose.number("grenadeDamage", 150)
Expose.number("throwAngleDegrees", 40)
Expose.number("grenadeDamping", 0.1)
Expose.number("tacticsIntervalSeconds", 1.5)
Expose.bool("huntOnSpawn", false)
Expose.number("ragdollImpulse", 45)
Expose.number("ragdollBlastImpulse", 220)
Expose.bool("debugAI", false)

local Enemy = {}

local skill = {}
local brain = nil
local mover = nil
local home = nil
local patrolList = {}
local patrolIndex = 0
local yaw = 0
local ignore = {}
local awareness = 0
local lastKnown = nil
local visible = false
local visibleTime = 0
local lostTime = 0
local perceptionTimer = 0
local lastHeardId = 0
local heardNoise = nil
local danger = nil
local fire = { magazine = 30, reloading = 0, reaction = 0, burstLeft = 0, shotTimer = 0, pause = 0 }
local currentClip = nil
local speedScale = 0
local wishX, wishZ = 0, 0
local faceTarget = nil
local dead = false
local corpseTimer = 0
local rng = 1
local selfKey = nil
local grenadesLeft = 0
local grenadeCooldown = 0
local tacticsTimer = 0
local ragdollPending = false
local deathZone = nil
local children = {}

local function random()
    rng = (rng * 1103515245 + 12345) % 2147483648
    return rng / 2147483648
end

-- "name=value;name=value" with numbers and true/false.
local function parseOverrides(text)
    local out = {}
    for name, value in string.gmatch(text or "", "([%w_]+)%s*=%s*([^;]+)") do
        value = value:match("^%s*(.-)%s*$")
        if value == "true" or value == "false" then
            out[name] = value == "true"
        else
            out[name] = tonumber(value)
        end
    end
    return out
end

local function position()
    return Transform.getPosition()
end

local function player()
    local row = Registry.player
    if not row or not row.entity or not row.entity:IsValid() or Health.isDead(row.entity) then
        return nil
    end
    return row
end

local function playerFeet(row)
    local transform = row.entity:GetTransform()
    return transform and transform:GetWorldPosition()
end

local function isPlayer(entity)
    local row = Registry.player
    return row ~= nil and Registry.key(entity) == row.key
end

local function eye()
    local p = position()
    return { x = p.x, y = p.y + eyeHeight, z = p.z }
end

local function facing()
    return math.cos(yaw), -math.sin(yaw)
end

-- The enemy's own entities, so its rays never stop on its own hit zones.
local function refreshIgnore()
    ignore = { this }
    local key = selfKey
    for zoneKey, zone in pairs(Registry.hitZones) do
        if zone.owner and Registry.key(zone.owner) == key and zone.entity then
            ignore[#ignore + 1] = zone.entity
        end
    end
end

local function blocked(from, to)
    return not Perception.clearLine(from, to, ignore, isPlayer)
end

local function nearestNav(p)
    return Nav.closestPoint(p)
end

local function moveTo(goal, run)
    speedScale = run and runSpeedScale or walkSpeedScale
    return Mover.follow(mover, position(), goal, 0.75)
end

local function stopMoving()
    Mover.stop(mover)
    speedScale = 0
end

-- Fire control ------------------------------------------------------------------------------------------------
local function aimError(row)
    local settle = math.min(1, visibleTime / math.max(skill.aimSettleSeconds, 0.01))
    local errorDegrees = Gunplay.lerp(skill.aimErrorDegrees, skill.aimErrorMinDegrees, settle)
    return errorDegrees + skill.movingTargetErrorDegrees * (row.speed or 0)
end

local function fireShot(row)
    local feet = playerFeet(row)
    if not feet then
        return
    end
    local e = eye()
    local fx, fz = facing()
    local muzzle = { x = e.x + fx * 0.5 - fz * 0.15, y = e.y - 0.15, z = e.z + fz * 0.5 + fx * 0.15 }
    local target = { x = feet.x, y = feet.y + (row.eyeHeight or 1.6) * 0.75, z = feet.z }
    local dx, dy, dz = target.x - muzzle.x, target.y - muzzle.y, target.z - muzzle.z
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-3 then
        return
    end
    dx, dy, dz = Gunplay.spreadDirection(dx / length, dy / length, dz / length, math.rad(aimError(row)), random(),
                                         random())
    local hit = Physics.raycast(muzzle.x, muzzle.y, muzzle.z, dx, dy, dz, range, "Character", true, ignore)
    -- What the player sees: a flash at the rifle's real muzzle and a tracer from there to where the shot ends.
    local barrel = Registry.muzzles[selfKey] or muzzle
    Impacts.spawnEffect(muzzleFlashPrefab, barrel.x, barrel.y, barrel.z, dx, dy, dz)
    local reach = hit and (hit.distance or range) or range
    local ex, ey, ez = muzzle.x + dx * reach, muzzle.y + dy * reach, muzzle.z + dz * reach
    local tx, ty, tz = ex - barrel.x, ey - barrel.y, ez - barrel.z
    local tracerLength = math.sqrt(tx * tx + ty * ty + tz * tz)
    if tracerLength > 0.5 then
        Tracers.spawn(tracerPrefab, barrel.x, barrel.y, barrel.z, tx / tracerLength, ty / tracerLength,
                      tz / tracerLength, { speed = tracerSpeed, maxDistance = tracerLength })
    end
    if hit and hit.entity and isPlayer(hit.entity) then
        local amount = damage * skill.damageScale
        Registry.notePlayerHit(barrel.x, barrel.y, barrel.z, Registry.clock or 0)
        Health.applyDamage(row.entity, amount, this)
        row.entity:Call("onHealthChanged", Health.getHealth(row.entity), Health.getMaxHealth(row.entity))
    elseif hit and hit.entity and not Health.get(hit.entity) and not Registry.hitZones[Registry.key(hit.entity)] then
        Impacts.surfaceEffect(hit, false)
    end
    if debugAI then
        engine_log(string.format("[FPS.Enemy] %s shot %s", this:GetName(),
                                 hit and hit.entity and isPlayer(hit.entity) and "hit player" or "missed"))
    end
    if fireSound ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(fireSound, { volume = fireVolume, bus = "sfx", priority = 2, pitchJitter = 0.04,
                                position = muzzle, minDistance = 5, maxDistance = 150 })
    end
    Registry.reportNoise(muzzle.x, muzzle.y, muzzle.z, 40, "enemy")
end

-- Bursts at the player while canShoot, after the reaction delay; reloads when the magazine is empty.
local function updateFiring(dt, canShoot, row)
    if fire.reloading > 0 then
        fire.reloading = fire.reloading - dt
        if fire.reloading <= 0 then
            fire.magazine = magazineSize
        end
        return
    end
    if not canShoot or not row then
        fire.burstLeft = 0
        return
    end
    if fire.magazine <= 0 then
        fire.reloading = reloadSeconds
        return
    end
    if fire.reaction > 0 then
        fire.reaction = fire.reaction - dt
        return
    end
    if fire.pause > 0 then
        fire.pause = fire.pause - dt
        return
    end
    if fire.burstLeft <= 0 then
        fire.burstLeft = skill.burstMin + math.floor(random() * (skill.burstMax - skill.burstMin + 1))
        fire.shotTimer = 0
    end
    fire.shotTimer = fire.shotTimer - dt
    while fire.shotTimer <= 0 and fire.burstLeft > 0 and fire.magazine > 0 do
        fireShot(row)
        fire.burstLeft = fire.burstLeft - 1
        fire.magazine = fire.magazine - 1
        fire.shotTimer = fire.shotTimer + 1 / math.max(skill.roundsPerSecond, 0.1)
    end
    if fire.burstLeft <= 0 then
        fire.pause = skill.burstPauseSeconds * (0.7 + 0.6 * random())
    end
end

-- Perception ---------------------------------------------------------------------------------------------------
local function alertAllies()
    if not skill.squadAlerts or not lastKnown then
        return
    end
    local p = position()
    for key, ally in pairs(Registry.enemies) do
        if key ~= selfKey and ally:IsValid() then
            local transform = ally:GetTransform()
            local q = transform and transform:GetWorldPosition()
            if q and (q.x - p.x) ^ 2 + (q.z - p.z) ^ 2 <= skill.allyAlertRadius ^ 2 then
                ally:Call("onAllyAlert", lastKnown.x, lastKnown.y, lastKnown.z)
            end
        end
    end
end

local function perceive(interval)
    local row = player()
    local wasAlert = awareness >= 1
    visible = false
    if row then
        local feet = playerFeet(row)
        if feet then
            local h = row.eyeHeight or 1.6
            local head = { x = feet.x, y = feet.y + h, z = feet.z }
            local chest = { x = feet.x, y = feet.y + h * 0.75, z = feet.z }
            local fx, fz = facing()
            local seen, distance = Perception.canSee(eye(), fx, fz, head, chest, skill, ignore, isPlayer)
            visible = seen
            if seen then
                awareness = math.min(1, awareness + Perception.awarenessRate(distance, skill, row.stance,
                                                                             (row.speed or 0) > 0.5) * interval)
                if awareness >= 1 then
                    lastKnown = { x = feet.x, y = feet.y, z = feet.z }
                elseif awareness > 0.35 then
                    lastKnown = lastKnown or { x = feet.x, y = feet.y, z = feet.z }
                end
            end
        end
    end
    if visible and awareness >= 1 then
        visibleTime = visibleTime + interval
        lostTime = 0
    else
        visibleTime = 0
        if awareness >= 1 then
            lostTime = lostTime + interval
        elseif not visible then
            awareness = math.max(0, awareness - skill.awarenessDecayPerSecond * interval)
        end
    end
    if awareness >= 1 and not wasAlert then
        fire.reaction = skill.reactionSeconds
        alertAllies()
    end
    local p = position()
    local noise
    noise, lastHeardId = Perception.hear(Registry.noises or {}, lastHeardId, p.x, p.y, p.z, skill.hearingScale)
    -- An alert enemy treats a player noise as a fresh sighting; a calm one goes to look at any noise.
    if noise and awareness >= 1 and noise.source == "player" and not visible then
        lastKnown = Nav.closestPoint(noise) or lastKnown
    elseif noise and awareness < 1 then
        heardNoise = { x = noise.x, y = noise.y, z = noise.z }
    end
    danger = nil
    if skill.grenadeAwareness then
        for _, d in pairs(Registry.dangers or {}) do
            if (d.x - p.x) ^ 2 + (d.z - p.z) ^ 2 <= (d.radius + 1.5) ^ 2 then
                danger = d
                break
            end
        end
    end
end

-- Tactics ----------------------------------------------------------------------------------------------------
local function log(text)
    if debugAI then
        engine_log("[FPS.Enemy] " .. this:GetName() .. " " .. text)
    end
end

-- Throws a frag at the player's last known position, give or take the skill's error. False when it can't.
local function throwGrenade()
    if fragGrenadePrefab == "" or not lastKnown then
        return false
    end
    local p = position()
    local distance = math.sqrt((lastKnown.x - p.x) ^ 2 + (lastKnown.z - p.z) ^ 2)
    if distance < skill.grenadeMinDistance or distance > skill.grenadeMaxDistance then
        return false
    end
    local a, r = random() * 2 * math.pi, math.sqrt(random()) * skill.grenadeErrorMetres
    local target = { x = lastKnown.x + math.cos(a) * r, y = lastKnown.y, z = lastKnown.z + math.sin(a) * r }
    local fx, fz = facing()
    local from = { x = p.x + fx * 0.5, y = p.y + eyeHeight, z = p.z + fz * 0.5 }
    local vx, vy, vz = Tactics.throwVelocity(from, target, throwAngleDegrees, 9.81, grenadeDamping)
    if not vx then
        return false
    end
    Scene.instantiate(fragGrenadePrefab, {
        position = from,
        vars = { vx = vx, vy = vy, vz = vz, damage = grenadeDamage * skill.grenadeDamageScale },
    })
    grenadesLeft = grenadesLeft - 1
    grenadeCooldown = skill.grenadeCooldownSeconds
    log(string.format("threw a grenade at %.1f, %.1f", target.x, target.z))
    return true
end

-- A spot beside the player's cover with a clear view of them, or nil.
local function findFlank(row)
    local p = position()
    local side = random() < 0.5 and 1 or -1
    local angles = { 70 * side, -70 * side, 100 * side, -100 * side }
    local chest = { x = lastKnown.x, y = lastKnown.y + (row.eyeHeight or 1.6) * 0.75, z = lastKnown.z }
    for _, c in ipairs(Tactics.flankCandidates(p, lastKnown, skill.flankDistance, angles)) do
        local q = Nav.closestPoint(c)
        if q and not blocked({ x = q.x, y = q.y + eyeHeight, z = q.z }, chest) then
            return q
        end
    end
    return nil
end

-- While the player hides: now and then throw a grenade at them or go round their cover. Returns a state change.
local function tactics(dt, row)
    tacticsTimer = tacticsTimer - dt
    if tacticsTimer > 0 or visible or not lastKnown or lostTime < 1 then
        return nil
    end
    tacticsTimer = tacticsIntervalSeconds
    if grenadesLeft > 0 and grenadeCooldown <= 0 and random() < skill.grenadeChance and throwGrenade() then
        return nil
    end
    if Registry.flanker == nil and random() < skill.flankChance then
        local spot = findFlank(row)
        if spot then
            return "flank", spot
        end
    end
    return nil
end

-- States -------------------------------------------------------------------------------------------------------
local function nextPatrolPoint()
    if #patrolList > 0 then
        patrolIndex = patrolIndex % #patrolList + 1
        return patrolList[patrolIndex]
    end
    return Nav.randomPoint(home, patrolRadius)
end

-- What any calm state does first: fight, flee or investigate.
local function reactions()
    if danger then
        return "evade"
    end
    if awareness >= 1 then
        return "combat"
    end
    if heardNoise then
        local noise = heardNoise
        heardNoise = nil
        return "investigate", noise, false
    end
    return nil
end

local states = {}

states.patrol = {
    enter = function(_, brain)
        local point = nextPatrolPoint()
        if point then
            moveTo(point, false)
        end
        brain.waitTimer = nil
    end,
    update = function(_, brain, dt)
        local next, a, b = reactions()
        if next then
            return next, a, b
        end
        if awareness > 0.35 and lastKnown then
            stopMoving()
            faceTarget = lastKnown
            return nil
        end
        faceTarget = nil
        if mover.arrived then
            stopMoving()
            brain.waitTimer = (brain.waitTimer or patrolWaitSeconds) - dt
            if brain.waitTimer <= 0 then
                brain.waitTimer = nil
                local point = nextPatrolPoint()
                if point then
                    moveTo(point, false)
                end
            end
        end
        return nil
    end,
}

states.investigate = {
    enter = function(_, brain, target, run)
        brain.target = target or lastKnown
        brain.lookTime = 0
        if brain.target then
            moveTo(brain.target, run)
        end
    end,
    update = function(_, brain, dt)
        if danger then
            return "evade"
        end
        if awareness >= 1 and visible then
            return "combat"
        end
        if heardNoise then
            brain.target = heardNoise
            heardNoise = nil
            moveTo(brain.target, true)
        end
        if mover.arrived then
            stopMoving()
            brain.lookTime = brain.lookTime + dt
            yaw = yaw + dt * 1.5
            faceTarget = nil
            if brain.lookTime > 3 then
                awareness = math.min(awareness, 0.3)
                return "patrol"
            end
        end
        if brain.time > 20 then
            return "patrol"
        end
        return nil
    end,
}

states.combat = {
    enter = function(_, brain, skipCover)
        brain.coverTried = skipCover == true
        stopMoving()
    end,
    update = function(_, brain, dt)
        if danger then
            return "evade"
        end
        local row = player()
        if not row then
            awareness = 0
            return "patrol"
        end
        if not brain.coverTried and lastKnown then
            brain.coverTried = true
            if random() < skill.coverChance then
                local threatEye = { x = lastKnown.x, y = lastKnown.y + (row.eyeHeight or 1.6), z = lastKnown.z }
                local spot = Cover.find(position(), threatEye, { radius = coverSearchRadius, samples = 14,
                                                                 eyeHeight = eyeHeight },
                                        function(c, r) return Nav.randomPoint(c, r) end, nearestNav, blocked)
                if spot then
                    return "cover", spot
                end
            end
        end
        faceTarget = lastKnown
        if visible then
            local p = position()
            local distance = math.sqrt((lastKnown.x - p.x) ^ 2 + (lastKnown.z - p.z) ^ 2)
            if distance > engageDistance then
                moveTo(lastKnown, false)
            else
                stopMoving()
            end
        else
            local next, spot = tactics(dt, row)
            if next then
                return next, spot
            end
        end
        -- A second to turn and look before deciding the player is gone (a flanker arrives facing its path).
        if not visible and lostTime > skill.lostTargetSeconds and brain.time > 1 then
            if random() < skill.aggression then
                return "investigate", lastKnown, true
            end
            awareness = 0.5
            return "investigate", lastKnown, false
        end
        updateFiring(dt, visible, row)
        return nil
    end,
}

states.cover = {
    enter = function(_, brain, spot)
        brain.spot = spot
        brain.phase = "moving"
        brain.phaseTime = 0
        moveTo(spot.point, true)
    end,
    update = function(_, brain, dt)
        if danger then
            return "evade"
        end
        local row = player()
        if not row then
            return "patrol"
        end
        brain.phaseTime = brain.phaseTime + dt
        faceTarget = lastKnown
        if brain.phase == "moving" then
            updateFiring(dt, visible, row)
            if mover.arrived then
                stopMoving()
                brain.phase, brain.phaseTime = "hide", 0
            elseif brain.phaseTime > 8 then
                return "combat"
            end
        elseif brain.phase == "hide" then
            updateFiring(dt, false, row)
            -- Flanked: the player can see the enemy at its cover spot.
            if visible and awareness >= 1 then
                brain.coverTried = false
                return "combat"
            end
            local next, spot = tactics(dt, row)
            if next then
                return next, spot
            end
            if brain.phaseTime > skill.hideSeconds and fire.reloading <= 0 then
                if brain.spot.peek then
                    moveTo(brain.spot.peek, false)
                    brain.phase, brain.phaseTime = "peek", 0
                elseif lostTime > skill.lostTargetSeconds then
                    return "investigate", lastKnown, random() < skill.aggression
                end
            end
        elseif brain.phase == "peek" then
            updateFiring(dt, visible and mover.arrived, row)
            if fire.magazine <= 0 or brain.phaseTime > skill.peekSeconds + 1 then
                moveTo(brain.spot.point, false)
                brain.phase, brain.phaseTime = "return", 0
            end
        elseif brain.phase == "return" then
            updateFiring(dt, false, row)
            if mover.arrived or brain.phaseTime > 3 then
                stopMoving()
                brain.phase, brain.phaseTime = "hide", 0
                if lostTime > skill.lostTargetSeconds then
                    return "investigate", lastKnown, random() < skill.aggression
                end
            end
        end
        return nil
    end,
}

states.flank = {
    enter = function(_, brain, spot)
        Registry.flanker = selfKey
        brain.spot = spot
        moveTo(spot, true)
    end,
    update = function(_, brain, dt)
        if danger then
            return "evade"
        end
        local row = player()
        if not row then
            return "patrol"
        end
        faceTarget = nil
        if visible and awareness >= 1 then
            stopMoving()
            return "combat", true
        end
        if mover.arrived or brain.time > 15 then
            return "combat", true
        end
        updateFiring(dt, false, row)
        return nil
    end,
    exit = function()
        if Registry.flanker == selfKey then
            Registry.flanker = nil
        end
    end,
}

states.evade = {
    enter = function(_, brain)
        brain.from = danger
        local p = position()
        local best, bestDistance = nil, -1
        for _ = 1, 10 do
            local candidate = Nav.randomPoint(p, 9)
            if candidate and brain.from then
                local d = (candidate.x - brain.from.x) ^ 2 + (candidate.z - brain.from.z) ^ 2
                if d > bestDistance then
                    best, bestDistance = candidate, d
                end
            end
        end
        if best then
            moveTo(best, true)
        end
    end,
    update = function(_, brain)
        if mover.arrived or not danger or brain.time > 4 then
            if danger and mover.arrived then
                return "evade"
            end
            return awareness >= 1 and "combat" or "investigate"
        end
        return nil
    end,
}

states.dead = {
    enter = function()
        stopMoving()
        Character.move(0, 0)
    end,
}

Enemy.states = states

-- Engine callbacks ---------------------------------------------------------------------------------------------
local function playClip(clip)
    if clip == currentClip or clip == "" then
        return
    end
    local animator = this:GetAnimator()
    if animator then
        animator:CrossFade(clip, 0.2)
        currentClip = clip
    end
end

local function parsePatrolPoints()
    local list = {}
    for x, z in string.gmatch(patrolPoints or "", "(-?[%d%.]+)%s*,%s*(-?[%d%.]+)") do
        list[#list + 1] = { x = tonumber(x), y = home.y, z = tonumber(z) }
    end
    return list
end

function onAwake()
    Registry.ensureSession()
    selfKey = Registry.key(this)
    Registry.enemies[selfKey] = this
    Health.attach(this, { maxHealth = maxHealth })
    skill = Difficulty.resolve(difficulty, parseOverrides(skillOverrides))
    rng = (selfKey or 1) * 7919 % 2147483647 + 1
    home = position()
    patrolList = parsePatrolPoints()
    patrolIndex = 0
    yaw = Transform.getRotation().y - math.rad(modelYawOffsetDegrees)
    mover = Mover.new()
    fire = { magazine = magazineSize, reloading = 0, reaction = 0, burstLeft = 0, shotTimer = 0, pause = 0 }
    grenadesLeft = skill.grenadeCount
    grenadeCooldown = skill.grenadeCooldownSeconds * 0.5
    tacticsTimer = 0
    awareness, visible, visibleTime, lostTime = 0, false, 0, 0
    lastKnown, heardNoise, danger, dead, corpseTimer = nil, nil, nil, false, 0
    lastHeardId = Registry.noiseCount or 0
    ragdollPending, deathZone, children = false, nil, {}
    brain = nil
end

-- Falls as a ragdoll pushed away from what killed it, dropping held weapons. Starts the update after death,
-- because the weapon reports the hit zone after the death.
local function fall()
    local p = position()
    local from = nil
    if deathZone == "blast" and Registry.lastBlast then
        from = Registry.lastBlast
    elseif Registry.player and Registry.player.entity and Registry.player.entity:IsValid() then
        local transform = Registry.player.entity:GetTransform()
        from = transform and transform:GetWorldPosition()
    end
    local dx, dz = 0, 0
    if from then
        dx, dz = p.x - from.x, p.z - from.z
        local length = math.sqrt(dx * dx + dz * dz)
        if length > 1e-3 then
            dx, dz = dx / length, dz / length
        else
            dx, dz = 0, 0
        end
    end
    local blast = deathZone == "blast"
    local strength = blast and ragdollBlastImpulse or ragdollImpulse
    local height = deathZone == "head" and eyeHeight or (deathZone == "limb" and 0.5 or 1.2)
    local ix, iy, iz = dx * strength, blast and strength * 0.6 or 0, dz * strength
    local ok
    if blast then
        ok = Character.ragdoll(ix, iy, iz)
    else
        ok = Character.ragdoll(ix, iy, iz, p.x, p.y + height, p.z)
    end
    if not ok then
        Transform.setRotation(0, yaw + math.rad(modelYawOffsetDegrees), math.rad(-90))
    end
    for _, old in ipairs(Registry.addCorpse(this, selfKey, maxCorpses)) do
        if old:IsValid() then
            Scene.destroy(old)
        end
    end
    for _, child in ipairs(children) do
        if child:IsValid() then
            child:Call("dropWeapon", dx * 2, 1.5, dz * 2)
        end
    end
end

function onUpdate(dt)
    if dead then
        if ragdollPending then
            ragdollPending = false
            fall()
        end
        corpseTimer = corpseTimer + dt
        if corpseSeconds > 0 and corpseTimer >= corpseSeconds then
            Scene.destroy(this)
            corpseSeconds = math.huge
        end
        return
    end
    if not brain then
        if not Nav.isReady() then
            return
        end
        refreshIgnore()
        children = {}
        for _, entity in ipairs(Scene.findAllWithPrefix("")) do
            local parent = entity:GetParent()
            if parent and Registry.key(parent) == selfKey then
                children[#children + 1] = entity
            end
        end
        local row = huntOnSpawn and player()
        local feet = row and playerFeet(row)
        if feet then
            -- Sent in to find the player: start out searching where they are.
            lastKnown = { x = feet.x, y = feet.y, z = feet.z }
            awareness = math.max(awareness, 0.6)
            brain = Brain.new(this, states, "investigate", lastKnown, true)
        else
            brain = Brain.new(this, states, "patrol")
        end
    end
    perceptionTimer = perceptionTimer - dt
    grenadeCooldown = grenadeCooldown - dt
    if perceptionTimer <= 0 then
        perceive(perceptionInterval - perceptionTimer)
        perceptionTimer = perceptionInterval
    end
    local before = brain.current
    Brain.update(brain, dt)
    if debugAI and brain.current ~= before then
        local p = position()
        engine_log(string.format("[FPS.Enemy] %s %s -> %s at %.1f, %.1f awareness %.2f", this:GetName(), before,
                                 brain.current, p.x, p.z, awareness))
    end

    local p = position()
    wishX, wishZ = 0, 0
    if speedScale > 0 then
        local arrived
        wishX, wishZ, arrived = Mover.update(mover, p, dt, 0.5)
        if arrived then
            speedScale = 0
        end
    end
    Character.move(wishX, wishZ)
    Character.setSpeedMultiplier(math.max(speedScale, 0.01))

    local targetYaw = yaw
    if faceTarget then
        targetYaw = math.atan(-(faceTarget.z - p.z), faceTarget.x - p.x)
    elseif wishX ~= 0 or wishZ ~= 0 then
        targetYaw = math.atan(-wishZ, wishX)
    end
    local delta = (targetYaw - yaw + math.pi) % (2 * math.pi) - math.pi
    local step = math.rad(turnDegreesPerSecond) * dt
    yaw = yaw + math.max(-step, math.min(step, delta))
    Character.setFacingYaw(yaw + math.rad(modelYawOffsetDegrees))

    local velocity = Character.getVelocity() or {}
    local speed = math.sqrt((velocity.x or 0) ^ 2 + (velocity.z or 0) ^ 2)
    playClip(speed < 0.4 and idleClip or (speed < 3.2 and walkClip or runClip))
end

function onHit(amount, zone, distance)
    if debugAI then
        engine_log(string.format("[FPS.Enemy] %s hit in the %s for %.0f", this:GetName(), tostring(zone), amount))
    end
    if dead then
        if ragdollPending and not deathZone then
            deathZone = zone
        end
        return
    end
    local row = player()
    if row then
        local feet = playerFeet(row)
        if feet then
            lastKnown = { x = feet.x, y = feet.y, z = feet.z }
        end
    end
    if awareness < 1 then
        fire.reaction = skill.reactionSeconds
    end
    awareness = 1
    fire.pause = math.max(fire.pause, 0.25)
    if brain and (brain.current == "patrol" or brain.current == "investigate") then
        Brain.change(brain, "combat")
    end
end

function onAllyAlert(x, y, z)
    if dead or awareness >= 1 then
        return
    end
    lastKnown = { x = x, y = y, z = z }
    awareness = math.max(awareness, 0.6)
    if brain and brain.current == "patrol" then
        Brain.change(brain, "investigate", lastKnown, true)
    end
end

function onDeath()
    dead = true
    if debugAI then
        engine_log("[FPS.Enemy] " .. this:GetName() .. " died")
    end
    if brain then
        Brain.change(brain, "dead")
    end
    Character.move(0, 0)
    Registry.enemies[selfKey] = nil
    local animator = this:GetAnimator()
    if animator then
        animator:Pause()
    end
    ragdollPending = true
    deathZone = nil
end

function onDestroy()
    Registry.removeCorpse(selfKey)
    if Registry.flanker == selfKey then
        Registry.flanker = nil
    end
    if debugAI then
        engine_log("[FPS.Enemy] " .. tostring(selfKey) .. " removed")
    end
    Registry.enemies[selfKey] = nil
    Health.detach(this)
end

-- What the enemy is doing, for debugging and tests.
function getAIState()
    return brain and brain.current or "none", awareness, visible, fire.magazine
end

return Enemy
