-- Ledge detection and movement for climbing onto (mantle) or over (vault) obstacles in front of the player.
local Mantle = {}

local function ray(x, y, z, dx, dy, dz, distance)
    return Physics.raycast(x, y, z, dx, dy, dz, distance, nil, true)
end

-- feet: the player's feet; fx, fz: the planar direction to climb; options: reach, radius, minHeight, maxHeight,
-- vaultMaxHeight, vaultDepth, standHeight. Returns a plan with kind "mantle" or "vault" and the points of its
-- path, or nil when there is no ledge that can be climbed. Rays skip the calling script's own entity.
function Mantle.find(feet, fx, fz, options)
    local reach = options.reach or 0.8
    local minHeight = options.minHeight or 0.4
    local maxHeight = options.maxHeight or 2.0
    local standHeight = options.standHeight or 1.8

    local wall = nil
    for _, height in ipairs({ 0.3, 0.75, 1.25 }) do
        local hit = ray(feet.x, feet.y + height, feet.z, fx, 0, fz, reach)
        if hit and hit.point and hit.normal and math.abs(hit.normal.y) < 0.5 then
            wall = hit
            break
        end
    end
    if not wall then
        return nil
    end

    -- The top surface just inside the wall face.
    local inX, inZ = wall.point.x + fx * 0.15, wall.point.z + fz * 0.15
    local fromY = feet.y + maxHeight + 0.3
    local top = ray(inX, fromY, inZ, 0, -1, 0, maxHeight + 0.3 - minHeight + 0.05)
    if not top or not top.point or not top.normal or top.normal.y < 0.7 or (top.distance or 0) < 0.05 then
        return nil
    end
    local topY = top.point.y
    local height = topY - feet.y
    if height < minHeight or height > maxHeight then
        return nil
    end
    -- The way over the edge must be clear for the body.
    if ray(feet.x, topY + 0.25, feet.z, fx, 0, fz, (wall.distance or reach) + 0.6) then
        return nil
    end

    local start = { x = feet.x, y = feet.y, z = feet.z }
    local rise = { x = feet.x, y = topY + 0.05, z = feet.z }
    if height <= (options.vaultMaxHeight or 1.1) then
        local depth = options.vaultDepth or 0.9
        local farX, farZ = wall.point.x + fx * depth, wall.point.z + fz * depth
        local far = ray(farX, topY + 0.3, farZ, 0, -1, 0, height + 1.5)
        local clear = not ray(feet.x, topY + 0.25, feet.z, fx, 0, fz, (wall.distance or reach) + depth + 0.3)
        if clear and far and far.point and far.point.y < topY - 0.3 then
            return {
                kind = "vault",
                height = height,
                points = { start, rise, { x = farX, y = topY + 0.05, z = farZ },
                           { x = farX + fx * 0.3, y = far.point.y, z = farZ + fz * 0.3 } },
                times = { 0.35, 0.7, 1.0 },
            }
        end
    end

    local onTop = { x = inX + fx * 0.3, y = topY, z = inZ + fz * 0.3 }
    if ray(onTop.x, topY + 0.05, onTop.z, 0, 1, 0, standHeight) then
        return nil
    end
    return {
        kind = "mantle",
        height = height,
        points = { start, rise, onTop },
        times = { 0.6, 1.0 },
    }
end

-- Position along the plan's path at progress t (0 to 1); each leg is eased.
function Mantle.position(plan, t)
    t = math.min(1, math.max(0, t))
    local previous = 0
    for i, mark in ipairs(plan.times) do
        if t <= mark or i == #plan.times then
            local span = math.max(mark - previous, 1e-4)
            local u = math.min(1, (t - previous) / span)
            u = u * u * (3 - 2 * u)
            local a, b = plan.points[i], plan.points[i + 1]
            return { x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u, z = a.z + (b.z - a.z) * u }
        end
        previous = mark
    end
    return plan.points[#plan.points]
end

-- Seconds a plan takes: a base time plus more for higher ledges.
function Mantle.duration(plan, baseSeconds, secondsPerMetre)
    return baseSeconds + secondsPerMetre * math.max(0, plan.height)
end

return Mantle
