-- Ledge detection and movement for climbing onto (mantle) or over (vault) obstacles in front of the player.
local Mantle = {}

local function ray(x, y, z, dx, dy, dz, distance)
    return Physics.raycast(x, y, z, dx, dy, dz, distance, nil, true)
end

-- feet: the player's feet; fx, fz: the planar direction to climb; options: reach, radius, minHeight, maxHeight,
-- vaultMaxHeight, vaultDepth (deepest obstacle vaulted), standHeight (headroom needed on top of a mantle, over a
-- vault and where it lands), vaultOnly. Returns a plan with kind "mantle" or "vault" and the points of its path, or
-- nil when there is no ledge that can be climbed (or, with vaultOnly, none that can be vaulted). Rays skip the calling
-- script's own entity.
function Mantle.find(feet, fx, fz, options)
    local reach = options.reach or 0.8
    local radius = options.radius or 0.3
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
    if height <= (options.vaultMaxHeight or 1.1) then
        -- The obstacle must end within vaultDepth of its face, with floor below its top under the whole body there.
        local depth = options.vaultDepth or 1.0
        local wallDistance = wall.distance or reach
        local function floorAt(distance)
            local hit = ray(feet.x + fx * distance, topY + 0.3, feet.z + fz * distance, 0, -1, 0, height + 1.5)
            return hit and hit.point and hit.point.y < topY - 0.3 and hit.point.y or nil
        end
        -- Down beyond the far edge at about 45 degrees, and never less than the body's radius.
        local across = wallDistance + depth + math.max(radius, height)
        local landY = floorAt(across)
        local edgeY = floorAt(wallDistance + depth)
        local clear = not ray(feet.x, topY + 0.25, feet.z, fx, 0, fz, across)
            and not ray(feet.x, topY + standHeight - 0.05, feet.z, fx, 0, fz, across)
        local landX, landZ = feet.x + fx * across, feet.z + fz * across
        if clear and landY and edgeY and not ray(landX, landY + 0.05, landZ, 0, 1, 0, standHeight - 0.05) then
            -- Up to the top as the body reaches the face, across, then down beyond the far edge.
            local onFace = math.max(0, wallDistance - radius)
            local beyond = wallDistance + depth
            return {
                kind = "vault",
                height = height,
                linear = true,
                points = { start,
                           { x = feet.x + fx * onFace, y = topY + 0.05, z = feet.z + fz * onFace },
                           { x = feet.x + fx * beyond, y = topY + 0.05, z = feet.z + fz * beyond },
                           { x = landX, y = landY, z = landZ } },
            }
        end
    end
    if options.vaultOnly then
        return nil
    end

    local rise = { x = feet.x, y = topY + 0.05, z = feet.z }
    local onTop = { x = inX + fx * 0.3, y = topY, z = inZ + fz * 0.3 }
    if ray(onTop.x, topY + 0.05, onTop.z, 0, 1, 0, standHeight) then
        return nil
    end
    -- A thin wall too tall to vault has no top to stand on.
    local under = ray(onTop.x, topY + 0.3, onTop.z, 0, -1, 0, 0.4)
    if not under or not under.point or math.abs(under.point.y - topY) > 0.05 then
        return nil
    end
    return {
        kind = "mantle",
        height = height,
        points = { start, rise, onTop },
        times = { 0.6, 1.0 },
    }
end

-- Total length of a plan's path in metres.
function Mantle.length(plan)
    local total = 0
    for i = 2, #plan.points do
        local a, b = plan.points[i - 1], plan.points[i]
        total = total + math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2 + (b.z - a.z) ^ 2)
    end
    return total
end

-- Position along the plan's path at progress t (0 to 1). A mantle eases each leg; a linear plan (a vault) moves at
-- a steady speed along its whole path so it keeps the player's momentum.
function Mantle.position(plan, t)
    t = math.min(1, math.max(0, t))
    if plan.linear then
        local along = t * Mantle.length(plan)
        for i = 2, #plan.points do
            local a, b = plan.points[i - 1], plan.points[i]
            local leg = math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2 + (b.z - a.z) ^ 2)
            if along <= leg or i == #plan.points then
                local u = leg > 1e-6 and math.min(1, along / leg) or 1
                return { x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u, z = a.z + (b.z - a.z) * u }
            end
            along = along - leg
        end
    end
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

-- Seconds a vault takes: its path at the speed the player came in with, kept between minSeconds and maxSeconds.
function Mantle.vaultDuration(plan, speed, minSeconds, maxSeconds)
    return math.min(maxSeconds, math.max(minSeconds, Mantle.length(plan) / math.max(speed, 0.1)))
end

return Mantle
