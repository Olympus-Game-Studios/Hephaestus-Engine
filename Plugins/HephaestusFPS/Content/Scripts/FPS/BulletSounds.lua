-- Choosing and placing the sounds of enemy bullets around the player: a hit, or a near miss heard as a ricochet
-- where the bullet passed closest to the player's head. Pure functions; Player.lua plays the sounds.
local BulletSounds = {}

-- An index in 1..count chosen with unitRandom in [0, 1), never last again when there is a choice.
function BulletSounds.pick(count, last, unitRandom)
    if count <= 0 then
        return nil
    end
    if count == 1 or not last then
        return math.min(count, math.floor(unitRandom * count) + 1)
    end
    local index = math.min(count - 1, math.floor(unitRandom * (count - 1)) + 1)
    return index >= last and index + 1 or index
end

-- Where a shot from origin along the unit direction, travelling length metres, passes closest to point, if that is
-- within radius and somewhere along the shot (not behind the muzzle or past where it stopped). Returns the point
-- and its distance, or nil.
function BulletSounds.nearMiss(origin, dx, dy, dz, length, point, radius)
    local t = (point.x - origin.x) * dx + (point.y - origin.y) * dy + (point.z - origin.z) * dz
    if t <= 0 or t >= length then
        return nil
    end
    local closest = { x = origin.x + dx * t, y = origin.y + dy * t, z = origin.z + dz * t }
    local ox, oy, oz = closest.x - point.x, closest.y - point.y, closest.z - point.z
    local distance = math.sqrt(ox * ox + oy * oy + oz * oz)
    if distance > radius then
        return nil
    end
    return closest, distance
end

return BulletSounds
