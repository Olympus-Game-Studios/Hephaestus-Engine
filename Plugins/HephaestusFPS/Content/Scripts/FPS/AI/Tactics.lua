-- Squad tactics maths: where to go to flank a threat and how hard to throw a grenade to land on a point.
local Tactics = {}

-- Points `distance` metres from the threat, turned `angles` degrees (signed, about +Y) away from the line from the
-- threat to the enemy, nearest turn first. Going to one of them puts the enemy beside the threat's cover.
function Tactics.flankCandidates(enemy, threat, distance, angles)
    local bx, bz = enemy.x - threat.x, enemy.z - threat.z
    local length = math.sqrt(bx * bx + bz * bz)
    if length < 1e-3 then
        return {}
    end
    bx, bz = bx / length, bz / length
    local out = {}
    for _, degrees in ipairs(angles) do
        local a = math.rad(degrees)
        local c, s = math.cos(a), math.sin(a)
        out[#out + 1] = { x = threat.x + (bx * c - bz * s) * distance, y = threat.y,
                          z = threat.z + (bx * s + bz * c) * distance }
    end
    return out
end

-- Horizontal distance a throw covers before it comes down to height h (relative to the start), integrated the way
-- the physics does for a body: gravity, then damping, then movement, at 60 steps a second.
local function flightDistance(speed, cosA, sinA, h, gravity, damping)
    local vx, vy, x, y, dt = speed * cosA, speed * sinA, 0, 0, 1 / 60
    local keep = 1 - damping * dt
    for _ = 1, 1200 do
        vy = (vy - gravity * dt) * keep
        vx = vx * keep
        x, y = x + vx * dt, y + vy * dt
        if vy < 0 and y <= h then
            return x
        end
    end
    return x
end

-- The launch velocity that lands a throw from `from` on `to` at `angleDegrees` above the horizontal, with linear
-- damping (per second, as on a Rigid Body). nil when the angle cannot reach it.
function Tactics.throwVelocity(from, to, angleDegrees, gravity, damping)
    local dx, dz = to.x - from.x, to.z - from.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 1e-3 then
        return nil
    end
    local h = to.y - from.y
    local angle = math.rad(angleDegrees)
    local cosA, sinA, tanA = math.cos(angle), math.sin(angle), math.tan(angle)
    local drop = d * tanA - h
    if drop <= 0 then
        return nil
    end
    local speed = math.sqrt(gravity * d * d / (2 * cosA * cosA * drop))
    damping = damping or 0
    for _ = 1, 6 do
        local reached = flightDistance(speed, cosA, sinA, h, gravity, damping)
        if reached < 1e-3 or math.abs(reached - d) < 0.05 then
            break
        end
        speed = speed * math.sqrt(d / reached)
    end
    local horizontal = speed * cosA
    return dx / d * horizontal, speed * math.sin(angle), dz / d * horizontal
end

return Tactics
