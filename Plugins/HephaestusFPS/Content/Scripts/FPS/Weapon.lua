Expose.string("displayName", "Pistol")
Expose.number("slot", 1)
Expose.string("fireMode", "semi")
Expose.number("roundsPerSecond", 5)
Expose.number("damage", 20)
Expose.number("range", 100)
Expose.number("falloffStart", 25)
Expose.number("falloffEnd", 60)
Expose.number("falloffMinMultiplier", 0.7)
Expose.number("headMultiplier", 1.5)
Expose.number("limbMultiplier", 0.9)
Expose.number("magazineSize", 12)
Expose.number("reserveAmmo", 48)
Expose.number("reloadSeconds", 1.4)
Expose.number("reloadEmptySeconds", 1.8)
Expose.bool("hitscan", true)
Expose.number("adsSeconds", 0.22)
Expose.number("adsFov", 65)
Expose.number("adsMoveMultiplier", 0.65)
Expose.number("adsSensitivityScale", 0.8)
Expose.number("adsForward", 0.22)
Expose.number("sightUp", 0.06)
Expose.number("sightRight", 0.0)
Expose.number("sightPitch", 0.0)
Expose.number("sightYaw", 0.0)
Expose.number("hipSpread", 2.0)
Expose.number("moveSpread", 2.5)
Expose.number("airSpread", 4.0)
Expose.number("maxSpread", 7.0)
Expose.number("adsSpread", 0.1)
Expose.number("adsBloomScale", 0.15)
Expose.number("bloomPerShot", 0.6)
Expose.number("maxBloom", 3.0)
Expose.number("bloomDecay", 6.0)
Expose.string("recoilPattern", "")
Expose.number("recoilPitch", 1.0)
Expose.number("recoilYaw", 0.3)
Expose.number("recoilJitter", 0.15)
Expose.number("adsRecoilScale", 0.8)
Expose.number("recoilRecoveryDelay", 0.12)
Expose.number("recoilRecoveryDegreesPerSecond", 12)
Expose.number("recoilSeed", 1)
Expose.number("kickBack", 0.025)
Expose.number("kickUp", 0.0)
Expose.number("kickRise", 1.8)
Expose.number("kickSide", 0.3)
Expose.number("kickRoll", 0.8)
Expose.number("kickFrequency", 16)
Expose.number("kickDampingRatio", 0.68)
Expose.number("adsKickScale", 0.45)
Expose.number("cameraKickPitch", 0.3)
Expose.number("cameraKickYaw", 0.1)
Expose.number("cameraKickFrequency", 24)
Expose.number("cameraKickDampingRatio", 0.6)
Expose.number("swayAmount", 1.0)
Expose.number("swayLag", 0.014)
Expose.number("swayMaxDegrees", 2.0)
Expose.number("swayFrequency", 13)
Expose.number("swayDampingRatio", 0.75)
Expose.number("adsSwayScale", 0.25)
Expose.number("idleAmount", 0.2)
Expose.number("idleBreathRate", 0.22)
Expose.number("idleAdsScale", 0.2)
Expose.number("bobAmount", 1.0)
Expose.number("moveLag", 0.003)
Expose.number("moveMaxOffset", 0.02)
Expose.number("moveFrequency", 9)
Expose.number("strafeRoll", 2.0)
Expose.number("landKick", 0.006)
Expose.number("footfallDip", 0.004)
Expose.number("fireBufferSeconds", 0.1)
Expose.number("flashSeconds", 0.035)
Expose.number("sprintToFireSeconds", 0.22)
Expose.number("raiseSeconds", 0.45)
Expose.number("lowerSeconds", 0.3)
Expose.string("fireSound", "")
Expose.number("fireVolume", 0.8)
Expose.number("firePitchJitter", 0.03)
Expose.number("fireVolumeJitter", 0.06)
Expose.string("reloadSound", "")
Expose.number("reloadSoundTime", 0.05)
Expose.string("reloadPart2Sound", "")
Expose.number("reloadPart2Time", 0.9)
Expose.string("boltSound", "")
Expose.number("boltTime", 1.45)
Expose.string("emptySound", "")
Expose.string("definition", "")
Expose.string("viewModelPrefab", "")
Expose.string("worldModelPrefab", "")

local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Gunplay = require("FPS.Gunplay")
local Hitscan = require("FPS.Hitscan")
local Health = require("FPS.Health")
local Impacts = require("FPS.Impacts")

local Weapon = {}
local SHOULDER_REACH = 0.4 -- metres from the grip back to the shoulders in the view frame

local TUNING = {
    "displayName", "slot", "fireMode", "roundsPerSecond", "damage", "range", "falloffStart", "falloffEnd",
    "falloffMinMultiplier", "headMultiplier", "limbMultiplier", "magazineSize", "reloadSeconds",
    "reloadEmptySeconds", "adsSeconds", "adsFov", "adsMoveMultiplier", "adsSensitivityScale", "adsForward",
    "sightUp", "sightRight", "sightPitch", "sightYaw", "hipSpread", "moveSpread", "airSpread", "maxSpread", "adsSpread", "adsBloomScale",
    "bloomPerShot", "maxBloom", "bloomDecay", "recoilPitch", "recoilYaw", "recoilJitter", "adsRecoilScale",
    "recoilRecoveryDelay", "recoilRecoveryDegreesPerSecond", "recoilSeed", "kickBack", "kickUp", "kickRise",
    "kickSide", "kickRoll", "kickFrequency", "kickDampingRatio", "adsKickScale", "cameraKickPitch", "cameraKickYaw",
    "cameraKickFrequency", "cameraKickDampingRatio", "swayAmount", "swayLag", "swayMaxDegrees", "swayFrequency",
    "swayDampingRatio", "adsSwayScale", "idleAmount", "idleBreathRate", "idleAdsScale", "bobAmount", "moveLag",
    "moveMaxOffset", "moveFrequency", "strafeRoll", "landKick", "footfallDip", "fireBufferSeconds", "flashSeconds",
    "sprintToFireSeconds", "raiseSeconds", "lowerSeconds", "fireSound", "fireVolume", "firePitchJitter",
    "fireVolumeJitter", "emptySound", "reloadSound",
    "reloadPart2Sound", "boltSound",
}

local function rad(value)
    return (value or 0) * math.pi / 180
end

-- A weapon data file is a Lua module under the project's assets/scripts that returns a table of settings.
local function loadDefinition(name)
    if not name or name == "" then
        return {}
    end
    local ok, definition = pcall(require, name)
    if ok and type(definition) == "table" then
        return definition
    end
    print("[FPS] Weapon definition '" .. name .. "' could not be loaded: " .. tostring(definition))
    return {}
end

-- options override the weapon's data file, which overrides the script's exposed variables
-- (globals of this script's environment).
local function setting(options, name, fallback)
    if options[name] ~= nil then
        return options[name]
    end
    if options.definitionTable and options.definitionTable[name] ~= nil then
        return options.definitionTable[name]
    end
    local exposed = _ENV[name]
    if exposed ~= nil then
        return exposed
    end
    return fallback
end

function Weapon.register(entity, options)
    Registry.ensureSession()
    options = options or {}
    options.definitionTable = loadDefinition(setting(options, "definition", ""))
    local key = Registry.key(entity)
    if not key then
        return
    end
    local rest = Transform.getPosition()
    local restRot = Transform.getRotation()
    local row = {
        entity = entity,
        key = key,
        rest = { x = rest and rest.x or 0.3, y = rest and rest.y or -0.2, z = rest and rest.z or 0.15 },
        restRot = { x = restRot and restRot.x or 0, y = restRot and restRot.y or 0, z = restRot and restRot.z or 0 },
        now = 0,
        nextFire = 0,
        reloadRemaining = 0,
        reloadTotal = 0,
        reloadElapsed = 0,
        fireBlockedRemaining = 0,
        equipped = false,
        adsLinear = 0,
        adsBlend = 0,
        sprintBlend = 0,
        raise = 0,
        bloom = 0,
        shotIndex = 0,
        lastFireTime = -100,
        recoilPitchAccum = 0,
        recoilYawAccum = 0,
        kickBackSpring = Gunplay.newSpring(),
        kickUpSpring = Gunplay.newSpring(),
        kickRiseSpring = Gunplay.newSpring(),
        kickSideSpring = Gunplay.newSpring(),
        kickRollSpring = Gunplay.newSpring(),
        swayYawSpring = Gunplay.newSpring(),
        swayPitchSpring = Gunplay.newSpring(),
        moveXSpring = Gunplay.newSpring(),
        moveYSpring = Gunplay.newSpring(),
        moveZSpring = Gunplay.newSpring(),
        idleTime = 0,
        breathPhase = 0,
        exertion = 0,
        bufferedPress = false,
        lastHitKey = nil,
    }
    for _, name in ipairs(TUNING) do
        row[name] = setting(options, name, nil)
    end
    row.reserve = setting(options, "reserveAmmo", 48)
    row.magazineSize = row.magazineSize or 12
    Logic.fillWeapon(row)
    row.rng = Gunplay.newRng((row.recoilSeed or 1) * 7919 + 13)
    row.hitscan = setting(options, "hitscan", true) ~= false
    row.fireLayers = setting(options, "fireLayers", nil)
    row.pattern = Gunplay.parsePattern(setting(options, "recoilPattern", ""))
    if #row.pattern == 0 then
        row.pattern = { { pitch = row.recoilPitch or 1, yaw = row.recoilYaw or 0 } }
    end
    local detach, insert = setting(options, "reloadSoundTime", 0.05), setting(options, "reloadPart2Time", 0.9)
    row.tacticalReload = setting(options, "tacticalReload", nil)
        or { seconds = row.reloadSeconds, detach = detach, insert = insert }
    row.emptyReload = setting(options, "emptyReload", nil)
        or { seconds = row.reloadEmptySeconds, detach = detach, insert = insert,
             chamber = setting(options, "boltTime", 1.45) }
    Registry.weapons[key] = row
    for _, existing in ipairs(Registry.weaponOrder) do
        if existing == key then
            return
        end
    end
    table.insert(Registry.weaponOrder, key)
    -- Script awake order is not an authored slot order; slots keep selection the same in every host.
    table.sort(Registry.weaponOrder, function(a, b)
        local left, right = Registry.weapons[a], Registry.weapons[b]
        if (left.slot or 0) ~= (right.slot or 0) then
            return (left.slot or 0) < (right.slot or 0)
        end
        return a < b
    end)
end

function Weapon.unregister(entity)
    local key = Registry.key(entity)
    if not key then
        return
    end
    Registry.weapons[key] = nil
    for i, existing in ipairs(Registry.weaponOrder) do
        if existing == key then
            table.remove(Registry.weaponOrder, i)
            break
        end
    end
    if Registry.activeWeaponId == key then
        Registry.activeWeaponId = nil
    end
end

function Weapon.get(entity)
    return Registry.weapons[Registry.key(entity)]
end

function Weapon.snapshot(entity)
    return Registry.weaponSnapshot(entity)
end

function Weapon.active()
    return Registry.activeWeapon()
end

function Weapon.giveAmmo(entity, amount)
    local row = Weapon.get(entity)
    if row then
        row.reserve = (row.reserve or 0) + (amount or 0)
    end
end

-- The player's own weapon is heard in 2D on the weapons bus; a shot outranks reloads and footsteps.
local function playOptional(path, volume, options)
    if path and path ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        options = options or {}
        Audio.play(path, { volume = volume or 1.0, bus = "weapons", priority = options.priority or 5,
                           pitch = options.pitch or 1.0, pitchJitter = options.pitchJitter or 0.02,
                           volumeJitter = options.volumeJitter or 0.04 })
    end
end

-- fireLayers lists the sounds one shot plays together (for example a transient, the action and a tail).
local function playShot(row)
    local layers = row.fireLayers
    if not layers or #layers == 0 then
        layers = { { sound = row.fireSound, volume = row.fireVolume } }
    end
    for _, layer in ipairs(layers) do
        playOptional(layer.sound, layer.volume or row.fireVolume, {
            priority = 10,
            pitch = layer.pitch,
            pitchJitter = layer.pitchJitter or row.firePitchJitter,
            volumeJitter = layer.volumeJitter or row.fireVolumeJitter,
        })
    end
end

local function setPartsVisible(visible)
    for _, part in ipairs(Scene.findAllWithPrefix(this:GetName() .. "_Part_")) do
        part:Call("setWeaponPartVisible", visible)
    end
end

local function hideFlashes(row)
    for _, flash in ipairs(row.flashes or {}) do
        flash:Call("hideFlash")
    end
end

-- One of the weapon's flash quads, picked from its seeded stream, with a random roll and size.
local function showFlash(row)
    local flashes = row.flashes or {}
    if #flashes == 0 then
        return
    end
    hideFlashes(row)
    local index = math.min(#flashes, math.floor(Gunplay.rngNext(row.rng) * #flashes) + 1)
    flashes[index]:Call("showFlash", row.flashSeconds or 0.035, Gunplay.rngNext(row.rng) * 2 * math.pi,
                        0.85 + 0.3 * Gunplay.rngNext(row.rng))
end

local function startReload(row)
    return Logic.tryStartReload(row)
end

local RELOAD_EVENT_SOUND = { detach = "reloadSound", insert = "reloadPart2Sound", chamber = "boltSound" }

local function advanceReload(row, dt)
    local _, events = Logic.advanceReload(row, dt)
    for _, name in ipairs(events) do
        playOptional(row[RELOAD_EVENT_SOUND[name]], 0.9)
    end
end

local function applyHit(row, hit, owner)
    if not hit or not hit.entity then
        return
    end
    local zoneRow = Registry.hitZones[Registry.key(hit.entity)]
    -- Targets swing and fall, so only static surfaces get bullet holes.
    if not zoneRow and not Health.get(hit.entity) then
        Impacts.spawn(hit, Gunplay.rngNext(row.rng))
    end
    local zone = zoneRow and zoneRow.zone or "body"
    local target = zoneRow and zoneRow.owner or hit.entity
    local amount = Gunplay.shotDamage(row, hit.distance or 0, zone)
    row.lastHitKey = Registry.key(target)
    if not Health.get(target) then
        target:Call("onDamaged", amount, zone)
        return
    end
    if Health.isDead(target) then
        return
    end
    local died = Health.applyDamage(target, amount, owner)
    target:Call("onHit", amount, zone, hit.distance or 0)
    target:Call("onHealthChanged", Health.getHealth(target), Health.getMaxHealth(target))
    if Registry.player then
        Registry.player.hitEvent = died and "kill" or (zone == "head" and "head" or "hit")
    end
end

local function fireShot(row, input, owner)
    local spread = rad(Gunplay.spreadDegrees(row, {
        adsBlend = row.adsBlend,
        moveFraction = input.moveFraction,
        airborne = input.airborne,
        bloom = row.bloom,
    }))
    local dx, dy, dz = Gunplay.spreadDirection(input.dx, input.dy, input.dz, spread,
                                               Gunplay.rngNext(row.rng), Gunplay.rngNext(row.rng))
    row.bloom = Gunplay.addBloom(row.bloom, row)

    if row.now - row.lastFireTime > math.max(0.3, 2 / math.max(row.roundsPerSecond or 1, 0.1)) then
        row.shotIndex = 0
    end
    row.shotIndex = row.shotIndex + 1
    row.lastFireTime = row.now
    local recoilScale = Gunplay.lerp(1, row.adsRecoilScale or 1, row.adsBlend)
    -- The same seed and shot number always give the same recoil, so a string can be learned.
    local random = Gunplay.seededRandom(row.recoilSeed or 1, row.shotIndex)
    local pitchDeg, yawDeg = Gunplay.recoilForShot(row.pattern, row.shotIndex, row.recoilJitter, row.recoilJitter,
                                                   random)
    local pitchKick = rad(pitchDeg) * recoilScale
    local yawKick = rad(yawDeg) * recoilScale
    row.recoilPitchAccum = row.recoilPitchAccum + pitchKick
    row.recoilYawAccum = row.recoilYawAccum + yawKick
    if Registry.player then
        -- Facing yaw grows to the left, so a rightward kick lowers it.
        Registry.player.pendingPitch = (Registry.player.pendingPitch or 0) + pitchKick
        Registry.player.pendingYaw = (Registry.player.pendingYaw or 0) - yawKick
    end

    -- Kick values are the peak displacement one shot gives a resting spring (metres and degrees).
    local omega, zeta = row.kickFrequency or 16, row.kickDampingRatio or 0.68
    local visual = Gunplay.lerp(1, row.adsKickScale or 0.45, row.adsBlend)
    local function kick(spring, peak)
        spring.v = spring.v + Gunplay.impulseForPeak(peak, omega, zeta) * visual
    end
    kick(row.kickBackSpring, -(row.kickBack or 0))
    kick(row.kickUpSpring, row.kickUp or 0)
    kick(row.kickRiseSpring, rad(row.kickRise))
    kick(row.kickSideSpring, rad(row.kickSide) * (random() * 2 - 1))
    kick(row.kickRollSpring, rad(row.kickRoll) * (random() * 2 - 1))
    if Registry.player then
        Registry.player.cameraImpulses = Registry.player.cameraImpulses or {}
        table.insert(Registry.player.cameraImpulses, {
            pitch = rad(row.cameraKickPitch) * visual,
            yaw = rad(row.cameraKickYaw) * (random() * 2 - 1) * visual,
            omega = row.cameraKickFrequency or 24,
            zeta = row.cameraKickDampingRatio or 0.6,
        })
    end

    playShot(row)
    showFlash(row)
    if row.hitscan then
        applyHit(row, Hitscan.trace(input.ox, input.oy, input.oz, dx, dy, dz, row.range, owner), owner)
    end
    this:Call("onFire")
end

local function updateRecoilRecovery(row, input, dt)
    row.recoilPitchAccum = Gunplay.absorbCounterInput(row.recoilPitchAccum, input.lookPitchDelta or 0)
    if row.now - row.lastFireTime < (row.recoilRecoveryDelay or 0) then
        return
    end
    local rate = rad(row.recoilRecoveryDegreesPerSecond)
    local pitchDelta
    pitchDelta, row.recoilPitchAccum = Gunplay.recoverRecoil(row.recoilPitchAccum, rate, dt)
    local yawDelta
    yawDelta, row.recoilYawAccum = Gunplay.recoverRecoil(row.recoilYawAccum, rate, dt)
    if Registry.player then
        Registry.player.pendingPitch = (Registry.player.pendingPitch or 0) + pitchDelta
        Registry.player.pendingYaw = (Registry.player.pendingYaw or 0) - yawDelta
    end
end

-- Pose in the camera's view frame: x forward, y up, z right; rotation x roll, y yaw (left), z pitch (up).
local function updatePose(row, input, dt)
    local ads = row.adsBlend
    local motion = input.motionScale or 1
    local omega, zeta = row.kickFrequency or 16, row.kickDampingRatio or 0.68
    local back = Gunplay.dampedSpring(row.kickBackSpring, 0, omega, zeta, dt)
    local up = Gunplay.dampedSpring(row.kickUpSpring, 0, omega, zeta, dt)
    local rise = Gunplay.dampedSpring(row.kickRiseSpring, 0, omega, zeta, dt)
    local side = Gunplay.dampedSpring(row.kickSideSpring, 0, omega, zeta, dt)
    local roll = Gunplay.dampedSpring(row.kickRollSpring, 0, omega, zeta, dt)

    -- Look inertia: the weapon trails the camera's turn rate, clamped and settled by a spring.
    local swayScale = (row.swayAmount or 1) * Gunplay.lerp(1, row.adsSwayScale or 0.25, ads)
    local lag, maxSway = row.swayLag or 0.014, rad(row.swayMaxDegrees or 2)
    local swayOmega, swayZeta = row.swayFrequency or 13, row.swayDampingRatio or 0.75
    local swayYaw = Gunplay.dampedSpring(row.swayYawSpring,
        Gunplay.clamp(-(input.lookYawRate or 0) * lag, -maxSway, maxSway) * swayScale, swayOmega, swayZeta, dt)
    local swayPitch = Gunplay.dampedSpring(row.swayPitchSpring,
        Gunplay.clamp(-(input.lookPitchRate or 0) * lag, -maxSway, maxSway) * swayScale, swayOmega, swayZeta, dt)

    -- Active idle: breathing that quickens after sprinting plus slow seeded noise.
    row.exertion = Gunplay.approach(row.exertion, input.sprinting and 1 or 0, dt / (input.sprinting and 0.8 or 2.5))
    row.idleTime = row.idleTime + dt
    row.breathPhase = row.breathPhase + 2 * math.pi * (row.idleBreathRate or 0.22) * (1 + 0.8 * row.exertion) * dt
    local idleScale = Gunplay.lerp(1, row.idleAdsScale or 0.2, ads) * (1 + row.exertion) * motion
    local breath = math.sin(row.breathPhase)
    local seed = row.recoilSeed or 1
    local idlePitch = rad(row.idleAmount) * idleScale * (0.6 * breath + 0.4 * Gunplay.smoothNoise(seed, row.idleTime * 0.35))
    local idleYaw = rad(row.idleAmount) * idleScale * 0.7 * Gunplay.smoothNoise(seed + 17, row.idleTime * 0.3)
    local idleY = 0.0015 * idleScale * breath

    -- Movement: the weapon trails velocity on a spring (so starts, stops and strafe reversals overshoot),
    -- rolls into strafes, and dips on landings and footfalls.
    local moveScale = motion * Gunplay.lerp(1, 0.3, ads)
    local moveOmega = row.moveFrequency or 9
    local maxOffset = row.moveMaxOffset or 0.02
    local moveLag = row.moveLag or 0.003
    local moveX = Gunplay.dampedSpring(row.moveXSpring,
        Gunplay.clamp(-(input.localForwardSpeed or 0) * moveLag, -maxOffset, maxOffset) * moveScale, moveOmega, 0.7, dt)
    local moveZ = Gunplay.dampedSpring(row.moveZSpring,
        Gunplay.clamp(-(input.localRightSpeed or 0) * moveLag, -maxOffset, maxOffset) * moveScale, moveOmega, 0.7, dt)
    if (input.landingSpeed or 0) > 0 then
        local dip = math.min(0.04, input.landingSpeed * (row.landKick or 0.006))
        row.moveYSpring.v = row.moveYSpring.v - Gunplay.impulseForPeak(dip, moveOmega, 0.55) * motion
    end
    local bobScale = (row.bobAmount or 1) * (input.moveFraction or 0) * Gunplay.lerp(1, 0.15, ads) * motion
    if input.footfall then
        row.moveYSpring.v = row.moveYSpring.v
            - Gunplay.impulseForPeak((row.footfallDip or 0.004) * bobScale, moveOmega, 0.55)
    end
    local moveY = Gunplay.dampedSpring(row.moveYSpring, 0, moveOmega, 0.55, dt)
    local strafeRoll = -rad(row.strafeRoll) * moveZ / math.max(maxOffset, 1e-4)
    -- Footfalls land at whole multiples of pi, where the figure-eight is lowest.
    local phase = input.bobPhase or 0
    local bobUp = 0.006 * bobScale * math.cos(2 * phase)
    local bobRight = math.sin(phase) * 0.010 * bobScale

    local reloadDip = 0
    if (row.reloadTotal or 0) > 0 and (row.reloadRemaining or 0) > 0 then
        reloadDip = math.sin(math.pi * Gunplay.clamp(row.reloadElapsed / row.reloadTotal, 0, 1))
    end
    local sprint = Gunplay.smoothstep(row.sprintBlend)
    local lowered = 1 - Gunplay.smoothstep(row.raise)

    local tiltRoll = -rad(30) * reloadDip
    local tiltPitch = -rad(15) * reloadDip - rad(40) * lowered
    local reach = SHOULDER_REACH * math.sin(tiltPitch)
    local hipX, hipY, hipZ = row.rest.x, row.rest.y, row.rest.z
    local adsPitch, adsYaw = rad(row.sightPitch), rad(row.sightYaw)
    local sightUp, sightRight = row.sightUp or 0, row.sightRight or 0
    local sightX = -math.sin(adsPitch) * sightUp
    local adsX = (row.adsForward or hipX) - (math.cos(adsYaw) * sightX + math.sin(adsYaw) * sightRight)
    local adsY = -math.cos(adsPitch) * sightUp
    local adsZ = -(-math.sin(adsYaw) * sightX + math.cos(adsYaw) * sightRight)
    local x = Gunplay.lerp(hipX, adsX, ads) + back + moveX - 0.04 * sprint + SHOULDER_REACH * (math.cos(tiltPitch) - 1)
    local y = Gunplay.lerp(hipY, adsY, ads) + up + moveY + idleY - bobUp - 0.06 * sprint + reach * math.cos(tiltRoll)
    local z = Gunplay.lerp(hipZ, adsZ, ads) + moveZ + bobRight - 0.05 * sprint + reach * math.sin(tiltRoll)
    local rotRoll = Gunplay.lerp(row.restRot.x, 0, ads) + roll + strafeRoll + rad(25) * sprint + tiltRoll
    local rotYaw = Gunplay.lerp(row.restRot.y, adsYaw, ads) + side + swayYaw + idleYaw + rad(30) * sprint
    local rotPitch = Gunplay.lerp(row.restRot.z, adsPitch, ads) + rise + swayPitch + idlePitch - rad(12) * sprint
        + tiltPitch
    Transform.setPosition(x, y, z)
    Transform.setRotation(rotRoll, rotYaw, rotPitch)
end

-- Called by the player every frame with the aim ray; further per-frame input is in Registry.player.input.
function tick(dt, held, pressed, reload, ox, oy, oz, dx, dy, dz, owner)
    local row = Weapon.get(this)
    if not row or not row.equipped then
        return
    end
    local input = (Registry.player and Registry.player.input) or {}
    input.ox, input.oy, input.oz, input.dx, input.dy, input.dz = ox, oy, oz, dx, dy, dz
    local isPressed = pressed == true or pressed == 1

    -- Sprinting cuts a reload; events already done (a magazine already inserted) stay done.
    if input.sprinting then
        Logic.interruptReload(row)
    end
    advanceReload(row, dt)
    if input.lowering then
        row.raise = 1 - (input.lowerProgress or 0)
    else
        row.raise = math.min(1, row.raise + dt / math.max(row.raiseSeconds or 0.01, 0.01))
    end
    row.sprintBlend = Gunplay.approach(row.sprintBlend, input.sprinting and 1 or 0, dt / 0.18)
    row.fireBlockedRemaining = Gunplay.fireBlock(row.fireBlockedRemaining, input.sprinting,
                                                 row.sprintToFireSeconds, dt)
    local wantAds = input.aimHeld and not input.sprinting and (row.reloadRemaining or 0) <= 0 and row.raise >= 1
    row.adsLinear = Gunplay.approach(row.adsLinear, wantAds and 1 or 0, dt / math.max(row.adsSeconds or 0.22, 0.01))
    local curves = Gunplay.adsCurves(row.adsLinear)
    row.adsBlend = curves.weapon
    row.adsFovBlend = curves.fov
    row.bloom = Gunplay.decayBloom(row.bloom, row, dt)

    if reload == true or reload == 1 then
        startReload(row)
    end
    -- A press shortly before the sprint-to-fire or raise block ends fires as soon as it ends.
    if isPressed and row.fireBlockedRemaining > 0 then
        row.bufferedPress = row.fireBlockedRemaining <= (row.fireBufferSeconds or 0.1)
    elseif row.bufferedPress and row.fireBlockedRemaining <= 0 then
        isPressed = true
        row.bufferedPress = false
    end
    local shots = Logic.tickFire(row, dt, held, isPressed)
    if shots == 0 and isPressed and (row.chamber or 0) <= 0 and (row.reloadRemaining or 0) <= 0 then
        if not startReload(row) then
            playOptional(row.emptySound, 0.8)
        end
    end
    for _ = 1, shots do
        if not Logic.spendShot(row) then
            startReload(row)
            break
        end
        fireShot(row, input, owner)
    end
    if shots > 0 and Logic.loadedRounds(row) <= 0 then
        startReload(row)
    end
    updateRecoilRecovery(row, input, dt)
    updatePose(row, input, dt)
end

function equip()
    local row = Weapon.get(this)
    if row then
        row.equipped = true
        row.flashes = Scene.findAllWithPrefix(this:GetName() .. "_Flash_")
        row.raise = 0
        row.adsLinear = 0
        row.adsBlend = 0
        row.fireBlockedRemaining = math.max(row.fireBlockedRemaining or 0, row.raiseSeconds or 0)
        Registry.activeWeaponId = row.key
    end
    setPartsVisible(true)
end

function unequip()
    local row = Weapon.get(this)
    if row then
        row.equipped = false
        hideFlashes(row)
        Logic.interruptReload(row)
        row.bufferedPress = false
        row.recoilPitchAccum = 0
        row.recoilYawAccum = 0
        if Registry.activeWeaponId == row.key then
            Registry.activeWeaponId = nil
        end
    end
    setPartsVisible(false)
end

function onAwake()
    Weapon.register(this, {})
    setPartsVisible(false)
end

function onDestroy()
    Weapon.unregister(this)
end

return Weapon
