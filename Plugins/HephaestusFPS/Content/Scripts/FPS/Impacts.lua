-- Bullet hole decals: prefabs whose surface faces +Y, placed on the hit surface. The oldest are removed first.
local Impacts = {}

local prefabs = {}
local maxDecals = 64
local live = {}
local count = 0

local function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end

function Impacts.configure(prefabList, maximum)
    prefabs = prefabList or {}
    maxDecals = math.max(0, math.floor(maximum or 64))
    live = {}
    count = 0
end

-- Euler angles in degrees (x, y, z, composed as the engine does: Rx * Ry * Rz) that turn +Y onto the normal.
function Impacts.rotationForNormal(nx, ny, nz)
    local length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length < 1e-6 then
        return { x = 0, y = 0, z = 0 }
    end
    nx, ny, nz = nx / length, ny / length, nz / length
    local zAngle = math.asin(clamp(-nx, -1, 1))
    local xAngle = math.atan(nz, ny)
    return { x = math.deg(xAngle), y = 0, z = math.deg(zAngle) }
end

-- Lifts the decal off the surface by more the further away it is, so depth precision never hides it.
function Impacts.surfaceOffset(distance)
    return 0.002 + 0.0002 * (distance or 0)
end

function Impacts.spawn(hit, pick)
    if #prefabs == 0 or maxDecals == 0 or not hit or not hit.point or not hit.normal then
        return nil
    end
    local n = hit.normal
    local offset = Impacts.surfaceOffset(hit.distance)
    local index = math.min(#prefabs, math.floor((pick or 0) * #prefabs) + 1)
    local decal = Scene.instantiate(prefabs[index], {
        position = { x = hit.point.x + n.x * offset, y = hit.point.y + n.y * offset, z = hit.point.z + n.z * offset },
        rotation = Impacts.rotationForNormal(n.x, n.y, n.z),
    })
    if not decal then
        return nil
    end
    count = count + 1
    live[count] = decal
    local oldest = count - maxDecals
    if oldest >= 1 and live[oldest] then
        if live[oldest]:IsValid() then
            Scene.destroy(live[oldest])
        end
        live[oldest] = nil
    end
    return decal
end

return Impacts
