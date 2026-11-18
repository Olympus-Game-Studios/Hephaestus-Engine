-- Wave plans and spawn choice for WaveDirector.lua.
local Waves = {}

-- "count[:difficulty];count[:difficulty]" -> { { count, difficulty } }, skipping empty or zero entries.
-- An empty difficulty means the global level (FPS.AI.Difficulty).
function Waves.parse(text)
    local out = {}
    for entry in string.gmatch(text or "", "[^;]+") do
        local count, level = entry:match("^%s*(%d+)%s*:?%s*([%w_]*)%s*$")
        count = tonumber(count)
        if count and count > 0 then
            out[#out + 1] = { count = count, difficulty = level or "" }
        end
    end
    return out
end

-- "x,z;x,z" -> { { x, z } }.
function Waves.parsePoints(text)
    local out = {}
    for x, z in string.gmatch(text or "", "(-?[%d%.]+)%s*,%s*(-?[%d%.]+)") do
        out[#out + 1] = { x = tonumber(x), z = tonumber(z) }
    end
    return out
end

-- A spawn point at least minDistance from the player, chosen with random() in [0, 1); the farthest point when
-- none is far enough. nil without points.
function Waves.pickSpawn(points, player, minDistance, random)
    if #points == 0 then
        return nil
    end
    local far, farthest, farthestDistance = {}, nil, -1
    for _, p in ipairs(points) do
        local d = player and math.sqrt((p.x - player.x) ^ 2 + (p.z - player.z) ^ 2) or math.huge
        if d >= minDistance then
            far[#far + 1] = p
        end
        if d > farthestDistance then
            farthest, farthestDistance = p, d
        end
    end
    if #far == 0 then
        return farthest
    end
    return far[math.min(#far, math.floor(random() * #far) + 1)]
end

return Waves
