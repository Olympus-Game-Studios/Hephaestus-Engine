-- Smoke clouds that block sight. A cloud is { x, y, z, age, life }: its centre, seconds since it burst and how long
-- it lasts. It swells to full size over GROW_SECONDS and shrinks away over the last FADE_SECONDS.
local Smoke = {}

Smoke.MAX_RADIUS = 4.0
Smoke.GROW_SECONDS = 3.0
Smoke.FADE_SECONDS = 4.0
Smoke.CORE = 0.85 -- sight is blocked inside this fraction of the radius; the thin edge lets it through

function Smoke.radius(cloud)
    local grow = math.min(1, math.max(0, cloud.age / Smoke.GROW_SECONDS))
    local fade = math.min(1, math.max(0, (cloud.life - cloud.age) / Smoke.FADE_SECONDS))
    return Smoke.MAX_RADIUS * math.sqrt(grow) * fade
end

-- Distance from point c to the segment a-b.
local function segmentDistance(a, b, c)
    local abx, aby, abz = b.x - a.x, b.y - a.y, b.z - a.z
    local length2 = abx * abx + aby * aby + abz * abz
    local t = 0
    if length2 > 1e-9 then
        t = math.max(0, math.min(1, ((c.x - a.x) * abx + (c.y - a.y) * aby + (c.z - a.z) * abz) / length2))
    end
    local px, py, pz = a.x + abx * t - c.x, a.y + aby * t - c.y, a.z + abz * t - c.z
    return math.sqrt(px * px + py * py + pz * pz)
end

-- Whether the line of sight from a to b passes through the thick part of any cloud (clouds: any table of clouds).
function Smoke.blocks(a, b, clouds)
    for _, cloud in pairs(clouds or {}) do
        local core = Smoke.radius(cloud) * Smoke.CORE
        if core > 0 and segmentDistance(a, b, cloud) < core then
            return true
        end
    end
    return false
end

return Smoke
