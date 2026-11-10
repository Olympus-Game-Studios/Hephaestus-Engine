-- What an enemy can see and how quickly it notices the player.
local Perception = {}

local CLOSE_RANGE = 3 -- within this, the player is noticed in any direction

-- Whether a target at (dx, dz) from the eye is inside the view cone around the facing (fx, fz).
function Perception.inView(fx, fz, dx, dz, fovDegrees, distance)
    if distance <= CLOSE_RANGE then
        return true
    end
    local length = math.sqrt(dx * dx + dz * dz)
    if length < 1e-4 then
        return true
    end
    local cosAngle = (fx * dx + fz * dz) / length
    return cosAngle >= math.cos(math.rad(fovDegrees * 0.5))
end

-- Awareness gained per second while the player is visible: full rate up close, a quarter at the edge of sight
-- range, less for a crouched or prone player and more for a moving one.
function Perception.awarenessRate(distance, settings, stance, moving)
    local closeness = math.max(0.25, math.min(1.4, 1.4 - distance / math.max(settings.sightRange, 1)))
    local stanceScale = stance == "prone" and 0.45 or (stance == "crouched" and 0.7 or 1)
    return settings.awarenessPerSecond * closeness * stanceScale * (moving and 1.3 or 1)
end

-- Whether a ray from eye reaches point without hitting anything but the target (or its hit zones).
-- ignore lists the looker's own entities; isTarget(entity) says whether a hit belongs to the target.
function Perception.clearLine(eye, point, ignore, isTarget)
    local dx, dy, dz = point.x - eye.x, point.y - eye.y, point.z - eye.z
    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance < 1e-3 then
        return true
    end
    local hit = Physics.raycast(eye.x, eye.y, eye.z, dx / distance, dy / distance, dz / distance, distance, nil,
                                true, ignore)
    return not hit or not hit.entity or isTarget(hit.entity) or (hit.distance or 0) >= distance - 0.2
end

-- Visibility of the target from eye: in range, in the view cone and with a clear line to its head or chest.
function Perception.canSee(eye, facingX, facingZ, head, chest, settings, ignore, isTarget)
    local dx, dz = chest.x - eye.x, chest.z - eye.z
    local distance = math.sqrt(dx * dx + (chest.y - eye.y) ^ 2 + dz * dz)
    if distance > settings.sightRange then
        return false, distance
    end
    if not Perception.inView(facingX, facingZ, dx, dz, settings.fovDegrees, distance) then
        return false, distance
    end
    local visible = Perception.clearLine(eye, head, ignore, isTarget) or Perception.clearLine(eye, chest, ignore, isTarget)
    return visible, distance
end

-- The newest noise this listener can hear and has not heard before, or nil. Returns the noise and the newest id.
function Perception.hear(noises, lastHeardId, x, y, z, hearingScale)
    local best = nil
    local newest = lastHeardId
    for _, noise in ipairs(noises) do
        if noise.id > lastHeardId then
            newest = math.max(newest, noise.id)
            local d = math.sqrt((noise.x - x) ^ 2 + (noise.y - y) ^ 2 + (noise.z - z) ^ 2)
            if d <= noise.radius * hearingScale and (not best or noise.id > best.id) then
                best = noise
            end
        end
    end
    return best, newest
end

return Perception
