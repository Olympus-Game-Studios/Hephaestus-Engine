local Gunplay = {}

local function clamp(value, low, high)
    if value < low then
        return low
    end
    if value > high then
        return high
    end
    return value
end

Gunplay.clamp = clamp

function Gunplay.approach(current, target, maxStep)
    if current < target then
        return math.min(target, current + maxStep)
    end
    return math.max(target, current - maxStep)
end

function Gunplay.smoothstep(t)
    t = clamp(t, 0, 1)
    return t * t * (3 - 2 * t)
end

function Gunplay.lerp(a, b, t)
    return a + (b - a) * t
end

-- Damped spring; substeps keep stiff springs stable when a frame is long.
function Gunplay.springStep(state, target, stiffness, damping, dt)
    local steps = math.max(1, math.ceil(dt * 240))
    local h = dt / steps
    for _ = 1, steps do
        local accel = stiffness * (target - state.x) - damping * state.v
        state.v = state.v + accel * h
        state.x = state.x + state.v * h
    end
    return state.x
end

function Gunplay.newSpring()
    return { x = 0, v = 0 }
end

-- Cone half angle in degrees. info: adsBlend 0..1, moveFraction 0..1, airborne, bloom (degrees).
function Gunplay.spreadDegrees(weapon, info)
    local hip = (weapon.hipSpread or 0) + (weapon.moveSpread or 0) * clamp(info.moveFraction or 0, 0, 1)
        + (info.airborne and (weapon.airSpread or 0) or 0) + (info.bloom or 0)
    hip = math.min(hip, weapon.maxSpread or hip)
    local ads = (weapon.adsSpread or 0) + (info.bloom or 0) * (weapon.adsBloomScale or 0)
    return Gunplay.lerp(hip, ads, clamp(info.adsBlend or 0, 0, 1))
end

function Gunplay.addBloom(bloom, weapon)
    return math.min(weapon.maxBloom or 0, bloom + (weapon.bloomPerShot or 0))
end

function Gunplay.decayBloom(bloom, weapon, dt)
    return math.max(0, bloom - (weapon.bloomDecay or 0) * dt)
end

-- "pitch,yaw;pitch,yaw;..." in degrees per shot of a string. Positive pitch kicks up, positive yaw drifts right.
function Gunplay.parsePattern(text)
    local pattern = {}
    for pitch, yaw in string.gmatch(text or "", "([%-%d%.]+)%s*,%s*([%-%d%.]+)") do
        pattern[#pattern + 1] = { pitch = tonumber(pitch), yaw = tonumber(yaw) }
    end
    return pattern
end

-- The last pattern entry repeats once a string runs past it. random returns [0, 1).
function Gunplay.recoilForShot(pattern, shotIndex, jitterPitch, jitterYaw, random)
    random = random or math.random
    local base = pattern[math.min(math.max(shotIndex, 1), #pattern)] or { pitch = 0, yaw = 0 }
    return base.pitch + (random() * 2 - 1) * (jitterPitch or 0),
        base.yaw + (random() * 2 - 1) * (jitterYaw or 0)
end

-- Recoil the player pulled against is spent, so recovery never drags the aim past where they held it.
function Gunplay.absorbCounterInput(accumulated, inputDelta)
    if accumulated > 0 and inputDelta < 0 then
        return math.max(0, accumulated + inputDelta)
    end
    if accumulated < 0 and inputDelta > 0 then
        return math.min(0, accumulated + inputDelta)
    end
    return accumulated
end

-- Returns the aim change that recovers accumulated recoil this frame and the new accumulated value.
function Gunplay.recoverRecoil(accumulated, ratePerSecond, dt)
    local recovered = Gunplay.approach(accumulated, 0, ratePerSecond * dt)
    return recovered - accumulated, recovered
end

-- Full damage to falloffStart, then linear down to minMultiplier at falloffEnd and beyond.
function Gunplay.falloffMultiplier(distance, falloffStart, falloffEnd, minMultiplier)
    if distance <= falloffStart then
        return 1
    end
    if falloffEnd <= falloffStart or distance >= falloffEnd then
        return minMultiplier
    end
    local t = (distance - falloffStart) / (falloffEnd - falloffStart)
    return Gunplay.lerp(1, minMultiplier, t)
end

function Gunplay.zoneMultiplier(zone, weapon)
    if zone == "head" then
        return weapon.headMultiplier or 1.5
    end
    if zone == "limb" then
        return weapon.limbMultiplier or 0.9
    end
    return 1
end

function Gunplay.shotDamage(weapon, distance, zone)
    local falloff = Gunplay.falloffMultiplier(distance, weapon.falloffStart or 1e9, weapon.falloffEnd or 1e9,
                                              weapon.falloffMinMultiplier or 1)
    return (weapon.damage or 0) * falloff * Gunplay.zoneMultiplier(zone, weapon)
end

local function normalize(x, y, z)
    local length = math.sqrt(x * x + y * y + z * z)
    if length < 1e-9 then
        return 1, 0, 0
    end
    return x / length, y / length, z / length
end

-- Uniform over the cone's cross-section disc; u1 and u2 are in [0, 1).
function Gunplay.spreadDirection(dx, dy, dz, halfAngleRadians, u1, u2)
    dx, dy, dz = normalize(dx, dy, dz)
    if halfAngleRadians <= 0 then
        return dx, dy, dz
    end
    local ux, uy, uz = 0, 1, 0
    if math.abs(dy) > 0.99 then
        ux, uy, uz = 1, 0, 0
    end
    local rx, ry, rz = normalize(dy * uz - dz * uy, dz * ux - dx * uz, dx * uy - dy * ux)
    local vx, vy, vz = ry * dz - rz * dy, rz * dx - rx * dz, rx * dy - ry * dx
    local radius = math.tan(halfAngleRadians) * math.sqrt(u1)
    local angle = 2 * math.pi * u2
    local a, b = radius * math.cos(angle), radius * math.sin(angle)
    return normalize(dx + rx * a + vx * b, dy + ry * a + vy * b, dz + rz * a + vz * b)
end

-- Seconds of fire block left after sprinting stops or a weapon is raised.
function Gunplay.fireBlock(current, sprinting, sprintToFireSeconds, dt)
    if sprinting then
        return math.max(current, sprintToFireSeconds or 0)
    end
    return math.max(0, current - dt)
end

-- Sound cues of a reload that cross from elapsed-dt to elapsed. cues: { { time, sound, emptyOnly } }.
function Gunplay.reloadCuesCrossed(cues, before, after, empty)
    local crossed = {}
    for _, cue in ipairs(cues) do
        if (not cue.emptyOnly or empty) and cue.time > before and cue.time <= after then
            crossed[#crossed + 1] = cue.sound
        end
    end
    return crossed
end

return Gunplay
