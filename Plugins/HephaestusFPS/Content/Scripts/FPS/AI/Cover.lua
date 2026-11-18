-- Cover search on the navmesh. A cover spot hides a standing enemy from the threat; its peek spot is a step to the
-- side from which the enemy can see and shoot the threat. Enemies without a crouch animation hide standing up, so
-- cover has to be at least head high.
local Cover = {}

-- blocked(from, to) says whether something solid is between two points.
local function hiddenAt(point, height, threatEye, blocked)
    return blocked({ x = point.x, y = point.y + height, z = point.z }, threatEye)
end

-- The spot beside a cover point, one step left or right across the threat direction, from which the threat can be
-- seen; nil when neither side works.
function Cover.peekSpot(point, threatEye, options, blocked, closestPoint)
    local dx, dz = threatEye.x - point.x, threatEye.z - point.z
    local length = math.sqrt(dx * dx + dz * dz)
    if length < 1e-3 then
        return nil
    end
    local rx, rz = -dz / length, dx / length
    local step = options.peekStep or 1.0
    for _, side in ipairs({ 1, -1 }) do
        local candidate = closestPoint({ x = point.x + rx * step * side, y = point.y, z = point.z + rz * step * side })
        if candidate and not hiddenAt(candidate, options.eyeHeight or 1.55, threatEye, blocked) then
            return candidate
        end
    end
    return nil
end

-- The best cover near position from a threat whose eye is at threatEye, or nil.
-- options: radius, samples, eyeHeight, minThreatDistance, peekStep; randomPoint(center, radius), closestPoint(p) and
-- blocked(from, to) come from the navmesh and physics. Spots with a peek spot win; nearer is better.
function Cover.find(position, threatEye, options, randomPoint, closestPoint, blocked)
    local best, bestScore = nil, -math.huge
    for _ = 1, options.samples or 12 do
        local point = randomPoint(position, options.radius or 10)
        if point then
            local tx, tz = threatEye.x - point.x, threatEye.z - point.z
            local threatDistance = math.sqrt(tx * tx + tz * tz)
            if threatDistance >= (options.minThreatDistance or 5)
                and hiddenAt(point, 1.0, threatEye, blocked) and hiddenAt(point, options.eyeHeight or 1.55, threatEye, blocked) then
                local peek = Cover.peekSpot(point, threatEye, options, blocked, closestPoint)
                local travel = math.sqrt((point.x - position.x) ^ 2 + (point.z - position.z) ^ 2)
                local score = (peek and 10 or 0) - travel
                if score > bestScore then
                    best, bestScore = { point = point, peek = peek }, score
                end
            end
        end
    end
    return best
end

return Cover
