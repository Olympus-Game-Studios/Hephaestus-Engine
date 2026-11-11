-- A soldier. It patrols, notices anyone on the other team (the player is on team 1) by sight or sound, fights with
-- burst fire from cover it finds on the navmesh, searches where it last saw them, gets out of the way of live
-- grenades and warns nearby teammates. How well it does all of this comes from FPS.AI.Difficulty, so a settings
-- menu can change it.
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
local Smoke = require("FPS.AI.Smoke")
local Tracers = require("FPS.Tracers")

Expose.number("maxHealth", 100)
Expose.integer("team", 2) -- soldiers never shoot their own team; the player is on team 1
Expose.string("displayName", "") -- the name the kill feed and scoreboard show
Expose.bool("huntWhenIdle", false) -- instead of patrolling, head for where the other team is
Expose.string("hideOnDeath", "") -- names of children (";" between them) removed when it dies, such as a team marker
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
-- Strafing legs: while moving, the hips and legs play these by move direction under the rifle upper body. Leave
-- the strafe clips empty to keep whole-body clips only.
Expose.string("walkStrafeLeftClip", "WalkStrafeLeft")
Expose.string("walkStrafeRightClip", "WalkStrafeRight")
Expose.string("runStrafeLeftClip", "JogStrafeLeft")
Expose.string("runStrafeRightClip", "JogStrafeRight")
Expose.number("walkClipSpeed", 1.6) -- metres per second the walk clips cover, so the feet do not slide
Expose.number("runClipSpeed", 4.0)
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
local smokeFire = nil -- once smoke hides the player: whether this enemy keeps firing into it (rolled once)
local visible = false
local visibleTime = 0
local lostTime = 0
local perceptionTimer = 0
local lastHeardId = 0
local heardNoise = nil
local danger = nil
local fire = { magazine = 30, reloading = 0, reaction = 0, burstLeft = 0, shotTimer = 0, pause = 0 }
local currentClip = nil
local legsLayer = true -- false once the strafe clips turn out to be missing
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
local selfRow = nil
local target = nil -- the combatant row this soldier is fighting
local killer = nil

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

local function alive(row)
    return row ~= nil and row.entity ~= nil and row.entity:IsValid() and not Health.isDead(row.entity)
end

local function hostile(row)
    return row ~= nil and row.key ~= selfKey and not Registry.sameTeam(row, selfRow) and alive(row)
end

-- Who this soldier is fighting, while they are alive.
local function opponent()
    if hostile(target) then
        return target
    end
    target = nil
    return nil
end

local function feetOf(row)
    local transform = row.entity:GetTransform()
    return transform and transform:GetWorldPosition()
end

local function isTarget(entity)
    local row = Registry.combatantOf(entity)
    return row ~= nil and target ~= nil and row.key == target.key
end

local function nearestHostile()
    local p = Transform.getPosition()
    local best, bestDistance = nil, math.huge
    for _, row in pairs(Registry.combatants) do
        if hostile(row) then
            local feet = feetOf(row)
            local d = feet and (feet.x - p.x) ^ 2 + (feet.z - p.z) ^ 2
            if d and d < bestDistance then
                best, bestDistance = row, d
            end
        end
    end
    return best
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
    return not Perception.clearLine(from, to, ignore, isTarget)
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

-- aimPoint (optional) shoots there instead of at the player, with extraSpread degrees more error: blind fire.
local function fireShot(row, aimPoint, extraSpread)
    local feet = feetOf(row)
    if not feet and not aimPoint then
        return
    end
    local e = eye()
    local fx, fz = facing()
    local muzzle = { x = e.x + fx * 0.5 - fz * 0.15, y = e.y - 0.15, z = e.z + fz * 0.5 + fx * 0.15 }
    local target = aimPoint or { x = feet.x, y = feet.y + (row.eyeHeight or 1.6) * 0.75, z = feet.z }
    local dx, dy, dz = target.x - muzzle.x, target.y - muzzle.y, target.z - muzzle.z
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-3 then
        return
    end
    dx, dy, dz = Gunplay.spreadDirection(dx / length, dy / length, dz / length,
                                         math.rad(aimError(row) + (extraSpread or 0)), random(), random())
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
    local hitRow = hit and hit.entity and Registry.combatantOf(hit.entity)
    local friendly = hitRow ~= nil and (hitRow.key == selfKey or Registry.sameTeam(hitRow, selfRow))
    local playerRow = Registry.player
    local hitPlayer = hitRow ~= nil and not friendly and hitRow == playerRow
    local hitSurface = hit and hit.entity and not hitRow and not Health.get(hit.entity)
        and not Registry.hitZones[Registry.key(hit.entity)]
    if alive(playerRow) then
        playerRow.entity:Call("onIncomingShot", muzzle.x, muzzle.y, muzzle.z, dx, dy, dz, reach,
                              hitPlayer and true or false, hitSurface and true or false)
    end
    local amount = damage * skill.damageScale
    if hitPlayer then
        Registry.notePlayerHit(barrel.x, barrel.y, barrel.z, Registry.clock or 0)
        Health.applyDamage(playerRow.entity, amount, this)
        playerRow.entity:Call("onHealthChanged", Health.getHealth(playerRow.entity),
                              Health.getMaxHealth(playerRow.entity))
    elseif hitRow and not friendly then
        local zoneRow = Registry.hitZones[Registry.key(hit.entity)]
        if not Health.isDead(hitRow.entity) then
            Health.applyDamage(hitRow.entity, amount, this)
            hitRow.entity:Call("onHit", amount, zoneRow and zoneRow.zone or "body", reach, this)
        end
    elseif hitSurface then
        Impacts.surfaceEffect(hit, false)
    end
    if debugAI then
        engine_log(string.format("[FPS.Enemy] %s shot %s", this:GetName(),
                                 hit and hit.entity and isTarget(hit.entity) and "hit player" or "missed"))
    end
    if fireSound ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(fireSound, { volume = fireVolume, bus = "sfx", priority = 2, pitchJitter = 0.04,
                                position = muzzle, minDistance = 3, maxDistance = 150 })
    end
    Registry.reportNoise(muzzle.x, muzzle.y, muzzle.z, 40, "enemy", team)
end

-- Bursts at the player while canShoot, after the reaction delay; reloads when the magazine is empty. aimPoint and
-- extraSpread pass through to fireShot for blind fire.
local function updateFiring(dt, canShoot, row, aimPoint, extraSpread)
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
        fireShot(row, aimPoint, extraSpread)
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
    for key, ally in pairs(Registry.combatants) do
        if key ~= selfKey and ally.bot and Registry.sameTeam(ally, selfRow) and alive(ally) then
            local q = feetOf(ally)
            if q and (q.x - p.x) ^ 2 + (q.z - p.z) ^ 2 <= skill.allyAlertRadius ^ 2 then
                ally.entity:Call("onAllyAlert", lastKnown.x, lastKnown.y, lastKnown.z)
            end
        end
    end
end

-- Whether row is in sight, how far it is and where its feet are.
local function sight(row)
    local feet = feetOf(row)
    if not feet then
        return false
    end
    local h = row.eyeHeight or 1.6
    local head = { x = feet.x, y = feet.y + h, z = feet.z }
    local chest = { x = feet.x, y = feet.y + h * 0.75, z = feet.z }
    local fx, fz = facing()
    local seen, distance = Perception.canSee(eye(), fx, fz, head, chest, skill, ignore, function(entity)
        local owner = Registry.combatantOf(entity)
        return owner ~= nil and owner.key == row.key
    end)
    return seen, distance, feet
end

-- Keeps fighting the current opponent while it is in sight, otherwise turns to the nearest other one in sight.
local function perceive(interval)
    local row = opponent()
    local wasAlert = awareness >= 1
    visible = false
    local seen, distance, feet = false, nil, nil
    if row then
        seen, distance, feet = sight(row)
    end
    if not seen then
        for _, other in pairs(Registry.combatants) do
            if other ~= row and hostile(other) then
                local s, d, f = sight(other)
                if s and (not seen or d < distance) then
                    row, seen, distance, feet = other, s, d, f
                end
            end
        end
    end
    if seen then
        target = row
    end
    do
        if feet then
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
    if noise and awareness >= 1 and noise.team ~= nil and noise.team ~= team and not visible then
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
        vars = { vx = vx, vy = vy, vz = vz, damage = grenadeDamage * skill.grenadeDamageScale, thrownBy = selfKey },
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
    if huntWhenIdle then
        local spots = {}
        for _, row in pairs(Registry.combatants) do
            local feet = hostile(row) and feetOf(row)
            if feet then
                spots[#spots + 1] = feet
            end
        end
        if #spots > 0 then
            local feet = spots[1 + math.floor(random() * #spots)]
            return Nav.randomPoint(feet, 8) or Nav.closestPoint(feet)
        end
    end
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
        local row = opponent()
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
        -- Smoke between this enemy and where it last saw the player: some keep firing into it at that spot.
        local suppress = nil
        if visible then
            smokeFire = nil
        elseif lastKnown then
            local spot = { x = lastKnown.x, y = lastKnown.y + (row.eyeHeight or 1.6) * 0.75, z = lastKnown.z }
            if Smoke.blocks(eye(), spot, Registry.smokeClouds) then
                if smokeFire == nil then
                    smokeFire = random() < (skill.smokeFireChance or 0)
                    if debugAI then
                        engine_log(string.format("[FPS.Enemy] %s lost sight in smoke, %s", this:GetName(),
                                                 smokeFire and "firing into it" or "holding fire"))
                    end
                end
                suppress = smokeFire and spot or nil
            end
        end
        updateFiring(dt, visible or suppress ~= nil, row, suppress, suppress and skill.smokeFireSpreadDegrees or 0)
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
        local row = opponent()
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
        local row = opponent()
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

-- The hips and legs follow the way the enemy moves relative to where it faces, mixing the forward, backward
-- (the forward clip reversed) and sideways clips, at a rate that matches the speed.
local function updateLegs(vx, vz, speed)
    local animator = this:GetAnimator()
    if not animator or not legsLayer then
        return
    end
    if speed < 0.4 or walkStrafeLeftClip == "" then
        animator:ClearLayer("Legs", 0.2)
        return
    end
    local running = speed >= 3.2
    local fx, fz = math.cos(yaw), -math.sin(yaw)
    local forward = (vx * fx + vz * fz) / speed
    local right = (vx * -fz + vz * fx) / speed
    local ahead = running and runClip or walkClip
    local ok, err = animator:SetLayer("Legs", {
        clips = {
            { clip = ahead, weight = math.max(0, forward) },
            { clip = ahead, weight = math.max(0, -forward), reverse = true },
            { clip = running and runStrafeRightClip or walkStrafeRightClip, weight = math.max(0, right) },
            { clip = running and runStrafeLeftClip or walkStrafeLeftClip, weight = math.max(0, -right) },
        },
        mask = "Hips", exclude = "Spine", excludeBlend = 0.5, fade = 0.2,
        rate = Gunplay.clamp(speed / math.max(running and runClipSpeed or walkClipSpeed, 0.1), 0.5, 1.8),
    })
    if not ok then
        legsLayer = false
        engine_log("[FPS.Enemy] strafing legs are off: " .. tostring(err))
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
    selfRow = { entity = this, key = selfKey, team = team, bot = true, eyeHeight = eyeHeight, stance = "standing",
                speed = 0, name = displayName ~= "" and displayName or this:GetName() }
    Registry.combatants[selfKey] = selfRow
    target, killer = nil, nil
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
    elseif killer and killer:IsValid() then
        local transform = killer:GetTransform()
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
            if string.find(";" .. hideOnDeath .. ";", ";" .. child:GetName() .. ";", 1, true) then
                Scene.destroy(child)
            else
                child:Call("dropWeapon", dx * 2, 1.5, dz * 2)
            end
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
        local row = huntOnSpawn and (opponent() or nearestHostile())
        local feet = row and feetOf(row)
        if feet then
            target = row
            -- Sent in to find the other team: start out searching where they are.
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
    selfRow.speed = speed
    playClip(speed < 0.4 and idleClip or (speed < 3.2 and walkClip or runClip))
    updateLegs(velocity.x or 0, velocity.z or 0, speed)
end

function onHit(amount, zone, distance, attacker)
    if debugAI then
        engine_log(string.format("[FPS.Enemy] %s hit in the %s for %.0f", this:GetName(), tostring(zone), amount))
    end
    if dead then
        if ragdollPending and not deathZone then
            deathZone = zone
        end
        return
    end
    local by = attacker and Registry.combatantOf(attacker)
    if hostile(by) then
        target = by
    end
    local row = opponent()
    if row then
        local feet = feetOf(row)
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

function onDeath(instigator)
    dead = true
    killer = instigator
    Registry.combatants[selfKey] = nil
    local director = Registry.matchDirector
    if director and director:IsValid() then
        director:Call("onCombatantDeath", this, instigator)
    end
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
    Registry.combatants[selfKey] = nil
    Health.detach(this)
end

-- What the enemy is doing, for debugging and tests.
function getAIState()
    return brain and brain.current or "none", awareness, visible, fire.magazine
end

return Enemy
