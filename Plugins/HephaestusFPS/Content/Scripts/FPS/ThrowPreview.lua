-- Aiming preview for thrown grenades: a dotted arc and a marker where the throw first touches a surface.
local Impacts = require("FPS.Impacts")

local ThrowPreview = {}

local dots = {}
local marker = nil

-- Steps the flight the way the physics integrates a body (gravity, then damping, then position) and stops at the
-- first surface the path crosses. Returns the sampled points and the raycast hit, or nil when nothing is reached
-- within maxSeconds. Rays skip the calling script's own entity and trigger volumes.
function ThrowPreview.predict(origin, velocity, options)
    options = options or {}
    local gravity = options.gravity or 9.81
    local dt = options.step or (1 / 60)
    local keep = math.max(0, 1 - (options.damping or 0) * dt)
    local x, y, z = origin.x, origin.y, origin.z
    local vx, vy, vz = velocity.x, velocity.y, velocity.z
    local points = { { x = x, y = y, z = z } }
    for _ = 1, math.ceil((options.maxSeconds or 3) / dt - 1e-6) do
        vy = vy - gravity * dt
        vx, vy, vz = vx * keep, vy * keep, vz * keep
        local dx, dy, dz = vx * dt, vy * dt, vz * dt
        local length = math.sqrt(dx * dx + dy * dy + dz * dz)
        if length > 1e-6 then
            local hit = Physics.raycast(x, y, z, dx / length, dy / length, dz / length, length, nil, true)
            if hit and hit.point then
                points[#points + 1] = { x = hit.point.x, y = hit.point.y, z = hit.point.z }
                return points, hit
            end
        end
        x, y, z = x + dx, y + dy, z + dz
        points[#points + 1] = { x = x, y = y, z = z }
    end
    return points, nil
end

local function alive(entity)
    return entity ~= nil and entity:IsValid()
end

-- Forgets spawned preview entities without destroying them, for a new play session.
function ThrowPreview.reset()
    dots = {}
    marker = nil
end

local function place(entity, p, rotation)
    local transform = entity and entity.GetTransform and entity:GetTransform()
    if not transform then
        return
    end
    transform:SetPosition(p.x, p.y, p.z)
    if rotation then
        transform:SetRotation(rotation.x, rotation.y, rotation.z)
    end
end

-- The point at distance along the path, measured from its start.
local function pointAt(points, lengths, distance)
    for i = 2, #points do
        if lengths[i] >= distance then
            local span = lengths[i] - lengths[i - 1]
            local t = span > 1e-6 and (distance - lengths[i - 1]) / span or 0
            local a, b = points[i - 1], points[i]
            return { x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t, z = a.z + (b.z - a.z) * t }
        end
    end
    return points[#points]
end

-- options: dotPrefab, markerPrefab, dotCount, startDistance. Dots are spaced evenly along the path from
-- startDistance metres out, so the ones next to the hand do not crowd the view.
function ThrowPreview.show(points, hit, options)
    local count = math.max(0, math.floor(options.dotCount or 0))
    if options.dotPrefab and options.dotPrefab ~= "" then
        local lengths = { 0 }
        for i = 2, #points do
            local a, b = points[i - 1], points[i]
            lengths[i] = lengths[i - 1] + math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2 + (b.z - a.z) ^ 2)
        end
        local total = lengths[#lengths]
        local first = math.min(options.startDistance or 0, total * 0.5)
        for i = 1, count do
            local p = pointAt(points, lengths, first + (total - first) * (i - 1) / math.max(count - 1, 1))
            if not alive(dots[i]) then
                dots[i] = Scene.instantiate(options.dotPrefab, { position = p })
            end
            place(dots[i], p)
        end
    end
    if hit and hit.normal and options.markerPrefab and options.markerPrefab ~= "" then
        local n = hit.normal
        local lift = Impacts.surfaceOffset(hit.distance) + 0.01
        local p = { x = hit.point.x + n.x * lift, y = hit.point.y + n.y * lift, z = hit.point.z + n.z * lift }
        local degrees = Impacts.rotationForNormal(n.x, n.y, n.z)
        if not alive(marker) then
            marker = Scene.instantiate(options.markerPrefab, { position = p, rotation = degrees })
        end
        place(marker, p, { x = math.rad(degrees.x), y = math.rad(degrees.y), z = math.rad(degrees.z) })
    else
        if alive(marker) then
            Scene.destroy(marker)
        end
        marker = nil
    end
end

function ThrowPreview.hide()
    for _, dot in ipairs(dots) do
        if alive(dot) then
            Scene.destroy(dot)
        end
    end
    dots = {}
    if alive(marker) then
        Scene.destroy(marker)
    end
    marker = nil
end

return ThrowPreview
