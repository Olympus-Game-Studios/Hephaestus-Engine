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

function Gunplay.newSpring()
    return { x = 0, v = 0 }
end

-- Exact step of x'' = -omega^2 (x - target) - 2 zeta omega x' for a constant target, so the result
-- does not depend on how dt is split. omega is the natural frequency in rad/s, zeta the damping ratio.
function Gunplay.dampedSpring(state, target, omega, zeta, dt)
    if dt <= 0 or omega <= 0 then
        return state.x
    end
    local y0, v0 = state.x - target, state.v
    local y, v
    if math.abs(zeta - 1) < 1e-4 then
        local e = math.exp(-omega * dt)
        local b = v0 + omega * y0
        y = (y0 + b * dt) * e
        v = (v0 - omega * b * dt) * e
    elseif zeta < 1 then
        local wd = omega * math.sqrt(1 - zeta * zeta)
        local e = math.exp(-zeta * omega * dt)
        local c, s = math.cos(wd * dt), math.sin(wd * dt)
        y = e * (y0 * c + (v0 + zeta * omega * y0) / wd * s)
        v = e * (v0 * c - (zeta * omega * v0 + omega * omega * y0) / wd * s)
    else
        local root = math.sqrt(zeta * zeta - 1)
        local r1, r2 = -omega * (zeta - root), -omega * (zeta + root)
        local a = (v0 - r2 * y0) / (r1 - r2)
        local b = y0 - a
        local e1, e2 = math.exp(r1 * dt), math.exp(r2 * dt)
        y = a * e1 + b * e2
        v = a * r1 * e1 + b * r2 * e2
    end
    state.x = y + target
    state.v = v
    return state.x
end

-- Velocity to add to a resting spring so its largest displacement is peak.
function Gunplay.impulseForPeak(peak, omega, zeta)
    if omega <= 0 then
        return 0
    end
    local factor
    if math.abs(zeta - 1) < 1e-4 then
        factor = 1 / (omega * math.exp(1))
    elseif zeta < 1 then
        local root = math.sqrt(1 - zeta * zeta)
        local wd = omega * root
        local t = math.atan(root, zeta) / wd
        factor = math.exp(-zeta * omega * t) * math.sin(wd * t) / wd
    else
        local root = math.sqrt(zeta * zeta - 1)
        local r1, r2 = -omega * (zeta - root), -omega * (zeta + root)
        local t = math.log(r2 / r1) / (r1 - r2)
        factor = (math.exp(r1 * t) - math.exp(r2 * t)) / (r1 - r2)
    end
    return peak / factor
end

-- Deterministic value in [0, 1) for integer seed, index and channel.
function Gunplay.hash01(seed, index, channel)
    local h = (math.tointeger(seed) or 0) * 0x9E3779B1 + (math.tointeger(index) or 0) * 0x85EBCA77
        + (math.tointeger(channel) or 0) * 0xC2B2AE3D
    h = h & 0xFFFFFFFF
    h = h ~ (h >> 15)
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h = h ~ (h >> 12)
    h = (h * 0x297A2D39) & 0xFFFFFFFF
    h = h ~ (h >> 15)
    return h / 4294967296.0
end

-- A random function for one shot of a string: the same seed and index always give the same values.
function Gunplay.seededRandom(seed, index)
    local channel = 0
    return function()
        channel = channel + 1
        return Gunplay.hash01(seed, index, channel)
    end
end

function Gunplay.newRng(seed)
    return { seed = math.tointeger(seed) or 1, count = 0 }
end

function Gunplay.rngNext(rng)
    rng.count = rng.count + 1
    return Gunplay.hash01(rng.seed, rng.count, 7)
end

local function valueNoise(seed, t)
    local i = math.floor(t)
    local f = t - i
    local a = Gunplay.hash01(seed, math.tointeger(i), 11) * 2 - 1
    local b = Gunplay.hash01(seed, math.tointeger(i) + 1, 11) * 2 - 1
    return a + (b - a) * (f * f * (3 - 2 * f))
end

-- Smooth, continuous noise in [-1, 1] from three octaves; t is in cycles of the lowest octave.
function Gunplay.smoothNoise(seed, t)
    return 0.6 * valueNoise(seed, t) + 0.3 * valueNoise(seed + 1, t * 2.3) + 0.1 * valueNoise(seed + 2, t * 5.1)
end

-- ADS blend for each channel from the linear aim-in progress: the weapon moves on a smoothstep,
-- the field of view and look sensitivity follow a sine ease so the zoom starts and settles softly.
function Gunplay.adsCurves(linear)
    local t = clamp(linear or 0, 0, 1)
    return { weapon = t * t * (3 - 2 * t), fov = 0.5 - 0.5 * math.cos(math.pi * t) }
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

return Gunplay
