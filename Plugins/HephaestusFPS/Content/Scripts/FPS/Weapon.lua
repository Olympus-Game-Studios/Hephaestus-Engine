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
Expose.number("kickBack", 0.035)
Expose.number("kickRise", 2.5)
Expose.number("kickRoll", 1.2)
Expose.number("kickStiffness", 260)
Expose.number("kickDamping", 22)
Expose.number("swayAmount", 1.0)
Expose.number("bobAmount", 1.0)
Expose.number("sprintToFireSeconds", 0.22)
Expose.number("raiseSeconds", 0.45)
Expose.number("lowerSeconds", 0.3)
Expose.string("fireSound", "")
Expose.number("fireVolume", 0.8)
Expose.string("reloadSound", "")
Expose.number("reloadSoundTime", 0.05)
Expose.string("reloadPart2Sound", "")
Expose.number("reloadPart2Time", 0.9)
Expose.string("boltSound", "")
Expose.number("boltTime", 1.45)
Expose.string("emptySound", "")
Expose.string("viewModelPrefab", "")
Expose.string("worldModelPrefab", "")

local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Gunplay = require("FPS.Gunplay")
local Hitscan = require("FPS.Hitscan")
local Health = require("FPS.Health")

local Weapon = {}
local SHOULDER_REACH = 0.4 -- metres from the grip back to the shoulders in the view frame

local TUNING = {
    "displayName", "slot", "fireMode", "roundsPerSecond", "damage", "range", "falloffStart", "falloffEnd",
    "falloffMinMultiplier", "headMultiplier", "limbMultiplier", "magazineSize", "reloadSeconds",
    "reloadEmptySeconds", "adsSeconds", "adsFov", "adsMoveMultiplier", "adsSensitivityScale", "adsForward",
    "sightUp", "sightRight", "hipSpread", "moveSpread", "airSpread", "maxSpread", "adsSpread", "adsBloomScale",
    "bloomPerShot", "maxBloom", "bloomDecay", "recoilPitch", "recoilYaw", "recoilJitter", "adsRecoilScale",
    "recoilRecoveryDelay", "recoilRecoveryDegreesPerSecond", "kickBack", "kickRise", "kickRoll",
    "kickStiffness", "kickDamping", "swayAmount", "bobAmount", "sprintToFireSeconds", "raiseSeconds",
    "lowerSeconds", "fireSound", "fireVolume", "emptySound",
}

local function rad(value)
    return (value or 0) * math.pi / 180
end

-- options override the script's exposed variables, which are globals of this script's environment.
local function setting(options, name, fallback)
    if options[name] ~= nil then
        return options[name]
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
        kickRiseSpring = Gunplay.newSpring(),
        kickRollSpring = Gunplay.newSpring(),
        swayYawSpring = Gunplay.newSpring(),
        swayPitchSpring = Gunplay.newSpring(),
        lastHitKey = nil,
    }
    for _, name in ipairs(TUNING) do
        row[name] = setting(options, name, nil)
    end
    row.reserve = setting(options, "reserveAmmo", 48)
    row.magazine = row.magazineSize or 12
    row.hitscan = setting(options, "hitscan", true) ~= false
    row.pattern = Gunplay.parsePattern(setting(options, "recoilPattern", ""))
    if #row.pattern == 0 then
        row.pattern = { { pitch = row.recoilPitch or 1, yaw = row.recoilYaw or 0 } }
    end
    row.reloadCues = {
        { time = setting(options, "reloadSoundTime", 0.05), sound = setting(options, "reloadSound", "") },
        { time = setting(options, "reloadPart2Time", 0.9), sound = setting(options, "reloadPart2Sound", "") },
        { time = setting(options, "boltTime", 1.45), sound = setting(options, "boltSound", ""), emptyOnly = true },
    }
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

local function playOptional(path, volume)
    if path and path ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(path, false, volume or 1.0)
    end
end

local function setPartsVisible(visible)
    for _, part in ipairs(Scene.findAllWithPrefix(this:GetName() .. "_Part_")) do
        part:Call("setWeaponPartVisible", visible)
    end
end

local function startReload(row)
    if Logic.tryStartReload(row) then
        row.reloadElapsed = 0
        return true
    end
    return false
end

local function advanceReload(row, dt)
    if (row.reloadRemaining or 0) <= 0 then
        return
    end
    local before = row.reloadElapsed
    row.reloadElapsed = before + dt
    for _, sound in ipairs(Gunplay.reloadCuesCrossed(row.reloadCues, before, row.reloadElapsed, row.reloadFromEmpty)) do
        playOptional(sound, 0.9)
    end
    Logic.advanceReload(row, dt)
end

local function applyHit(row, hit, owner)
    if not hit or not hit.entity then
        return
    end
    local zoneRow = Registry.hitZones[Registry.key(hit.entity)]
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
    local dx, dy, dz = Gunplay.spreadDirection(input.dx, input.dy, input.dz, spread, math.random(), math.random())
    row.bloom = Gunplay.addBloom(row.bloom, row)

    if row.now - row.lastFireTime > math.max(0.3, 2 / math.max(row.roundsPerSecond or 1, 0.1)) then
        row.shotIndex = 0
    end
    row.shotIndex = row.shotIndex + 1
    row.lastFireTime = row.now
    local recoilScale = Gunplay.lerp(1, row.adsRecoilScale or 1, row.adsBlend)
    local pitchDeg, yawDeg = Gunplay.recoilForShot(row.pattern, row.shotIndex, row.recoilJitter, row.recoilJitter)
    local pitchKick = rad(pitchDeg) * recoilScale
    local yawKick = rad(yawDeg) * recoilScale
    row.recoilPitchAccum = row.recoilPitchAccum + pitchKick
    row.recoilYawAccum = row.recoilYawAccum + yawKick
    if Registry.player then
        -- Facing yaw grows to the left, so a rightward kick lowers it.
        Registry.player.pendingPitch = (Registry.player.pendingPitch or 0) + pitchKick
        Registry.player.pendingYaw = (Registry.player.pendingYaw or 0) - yawKick
    end

    local visual = Gunplay.lerp(1, 0.45, row.adsBlend)
    row.kickBackSpring.v = row.kickBackSpring.v - (row.kickBack or 0) * 25 * visual
    row.kickRiseSpring.v = row.kickRiseSpring.v + rad(row.kickRise) * 25 * visual
    row.kickRollSpring.v = row.kickRollSpring.v + rad(row.kickRoll) * 25 * visual * (math.random() * 2 - 1)

    playOptional(row.fireSound, row.fireVolume)
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
    local stiffness, damping = row.kickStiffness or 260, row.kickDamping or 22
    local back = Gunplay.springStep(row.kickBackSpring, 0, stiffness, damping, dt)
    local rise = Gunplay.springStep(row.kickRiseSpring, 0, stiffness, damping, dt)
    local roll = Gunplay.springStep(row.kickRollSpring, 0, stiffness, damping, dt)
    local swayScale = (row.swayAmount or 1) * Gunplay.lerp(1, 0.25, ads)
    local swayYaw = Gunplay.springStep(row.swayYawSpring,
                                       Gunplay.clamp((input.lookX or 0) * 0.0025, -0.08, 0.08) * swayScale,
                                       120, 16, dt)
    local swayPitch = Gunplay.springStep(row.swayPitchSpring,
                                         Gunplay.clamp((input.lookY or 0) * 0.0025, -0.08, 0.08) * swayScale,
                                         120, 16, dt)

    local bobScale = (row.bobAmount or 1) * (input.moveFraction or 0) * Gunplay.lerp(1, 0.15, ads)
    local phase = input.bobPhase or 0
    local bobUp = math.abs(math.sin(phase)) * 0.012 * bobScale
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
    local adsX, adsY, adsZ = row.adsForward or hipX, -(row.sightUp or 0), -(row.sightRight or 0)
    local x = Gunplay.lerp(hipX, adsX, ads) + back - 0.04 * sprint + SHOULDER_REACH * (math.cos(tiltPitch) - 1)
    local y = Gunplay.lerp(hipY, adsY, ads) - bobUp - 0.06 * sprint + reach * math.cos(tiltRoll)
    local z = Gunplay.lerp(hipZ, adsZ, ads) + bobRight - 0.05 * sprint + reach * math.sin(tiltRoll)
    local rotRoll = Gunplay.lerp(row.restRot.x, 0, ads) + roll + rad(25) * sprint + tiltRoll
    local rotYaw = Gunplay.lerp(row.restRot.y, 0, ads) + swayYaw + rad(30) * sprint
    local rotPitch = Gunplay.lerp(row.restRot.z, 0, ads) + rise - swayPitch - rad(12) * sprint + tiltPitch
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
    row.adsBlend = Gunplay.smoothstep(row.adsLinear)
    row.bloom = Gunplay.decayBloom(row.bloom, row, dt)

    if reload == true or reload == 1 then
        startReload(row)
    end
    local shots = Logic.tickFire(row, dt, held, pressed)
    if shots == 0 and isPressed and (row.magazine or 0) <= 0 and (row.reloadRemaining or 0) <= 0 then
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
    if shots > 0 and (row.magazine or 0) <= 0 then
        startReload(row)
    end
    updateRecoilRecovery(row, input, dt)
    updatePose(row, input, dt)
end

function equip()
    local row = Weapon.get(this)
    if row then
        row.equipped = true
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
        row.reloadRemaining = 0
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
