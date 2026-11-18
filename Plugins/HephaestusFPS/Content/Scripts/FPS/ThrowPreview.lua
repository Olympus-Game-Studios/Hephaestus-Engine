-- Aiming preview for thrown grenades: a dotted arc, a small marker at each bounce and a marker where the
-- throw comes to rest.
local Impacts = require("FPS.Impacts")

local ThrowPreview = {}

local dots = {}
local markers = {}

local function dot(ax, ay, az, bx, by, bz)
    return ax * bx + ay * by + az * bz
end

-- Steps the flight the way the physics integrates a body (gravity, then damping, then position), raycasting
-- each step. At a surface the velocity is split along the hit normal: the normal part is reflected and scaled
-- by restitution, and friction takes friction * (1 + restitution) * normal speed off the sliding part, as a
-- contact impulse does. Each contact hit at markSpeed or faster is a bounce; after `bounces` of them, or once
-- the throw slows below stopSpeed on ground, the prediction ends. bounces = 0 stops at the first contact.
-- Returns the sampled points, the marks (hits with point, normal and distance; the last is where the throw ends
-- up, or where it is after maxSeconds if it has touched something by then) and whether it came to rest. Rays skip the calling script's own entity and trigger volumes.
function ThrowPreview.predict(origin, velocity, options)
    options = options or {}
    local gravity = options.gravity or 9.81
    local dt = options.step or (1 / 60)
    local keep = math.max(0, 1 - (options.damping or 0) * dt)
    local bounces = math.max(0, math.floor(options.bounces or 0))
    local restitution = options.restitution or 0
    local friction = options.friction or 0
    local markSpeed = options.markSpeed or 1.0
    local stopSpeed = options.stopSpeed or 0.3
    local x, y, z = origin.x, origin.y, origin.z
    local vx, vy, vz = velocity.x, velocity.y, velocity.z
    local points = { { x = x, y = y, z = z } }
    local marks = {}
    local travelled = 0
    local lastNormal = nil
    for _ = 1, math.ceil((options.maxSeconds or 3) / dt - 1e-6) do
        vy = vy - gravity * dt
        vx, vy, vz = vx * keep, vy * keep, vz * keep
        local dx, dy, dz = vx * dt, vy * dt, vz * dt
        local length = math.sqrt(dx * dx + dy * dy + dz * dz)
        local hit = nil
        if length > 1e-6 then
            hit = Physics.raycast(x, y, z, dx / length, dy / length, dz / length, length, nil, true)
        end
        if hit and hit.point and hit.normal then
            local n = hit.normal
            local p = hit.point
            lastNormal = n
            travelled = travelled + (hit.distance or 0)
            points[#points + 1] = { x = p.x, y = p.y, z = p.z }
            local vn = dot(vx, vy, vz, n.x, n.y, n.z)
            if vn < 0 then
                local mark = { point = p, normal = n, distance = travelled }
                if #marks == 0 or -vn >= markSpeed then
                    marks[#marks + 1] = mark
                    if #marks > bounces then
                        return points, marks, false
                    end
                end
                local tx, ty, tz = vx - vn * n.x, vy - vn * n.y, vz - vn * n.z
                local slide = math.sqrt(tx * tx + ty * ty + tz * tz)
                local scale = slide > 1e-6 and math.max(0, 1 - friction * (1 + restitution) * -vn / slide) or 0
                local out = -vn * restitution
                vx, vy, vz = tx * scale + n.x * out, ty * scale + n.y * out, tz * scale + n.z * out
                if n.y > 0.7 and math.sqrt(vx * vx + vy * vy + vz * vz) < stopSpeed then
                    local last = marks[#marks]
                    local dx2, dz2 = last.point.x - p.x, last.point.z - p.z
                    if dx2 * dx2 + dz2 * dz2 < 0.25 then
                        marks[#marks] = mark
                    else
                        marks[#marks + 1] = mark
                    end
                    return points, marks, true
                end
            end
            x, y, z = p.x + n.x * 0.005, p.y + n.y * 0.005, p.z + n.z * 0.005
        else
            x, y, z = x + dx, y + dy, z + dz
            travelled = travelled + length
            points[#points + 1] = { x = x, y = y, z = z }
        end
    end
    if #marks > 0 and lastNormal then
        local last = marks[#marks]
        local mark = { point = { x = x, y = y, z = z }, normal = lastNormal, distance = travelled }
        local dx, dz = last.point.x - x, last.point.z - z
        if dx * dx + dz * dz < 0.25 then
            marks[#marks] = mark
        else
            marks[#marks + 1] = mark
        end
    end
    return points, marks, false
end

local function alive(entity)
    return entity ~= nil and entity:IsValid()
end

-- Forgets spawned preview entities without destroying them, for a new play session.
function ThrowPreview.reset()
    dots = {}
    markers = {}
end

local function place(entity, p, rotation, scale)
    local transform = entity and entity.GetTransform and entity:GetTransform()
    if not transform then
        return
    end
    transform:SetPosition(p.x, p.y, p.z)
    if rotation then
        transform:SetRotation(rotation.x, rotation.y, rotation.z)
    end
    if scale then
        transform:SetScale(scale.x, scale.y, scale.z)
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

-- options: dotPrefab, markerPrefab, dotCount, startDistance, bounceMarkerScale. Dots are spaced evenly along
-- the path from startDistance metres out, so the ones next to the hand do not crowd the view. The last mark gets
-- the full-size marker and earlier bounces get it scaled by bounceMarkerScale.
function ThrowPreview.show(points, marks, options)
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
    local shown = (options.markerPrefab and options.markerPrefab ~= "") and #marks or 0
    for i = 1, shown do
        local hit = marks[i]
        local n = hit.normal
        local lift = Impacts.surfaceOffset(hit.distance) + 0.01
        local p = { x = hit.point.x + n.x * lift, y = hit.point.y + n.y * lift, z = hit.point.z + n.z * lift }
        local degrees = Impacts.rotationForNormal(n.x, n.y, n.z)
        if not (markers[i] and alive(markers[i].entity)) then
            markers[i] = { entity = Scene.instantiate(options.markerPrefab, { position = p, rotation = degrees }) }
            local transform = markers[i].entity and markers[i].entity:GetTransform()
            markers[i].scale = transform and transform:GetScale() or { x = 1, y = 1, z = 1 }
        end
        local full = markers[i].scale
        local k = i == shown and 1 or (options.bounceMarkerScale or 0.5)
        place(markers[i].entity, p, { x = math.rad(degrees.x), y = math.rad(degrees.y), z = math.rad(degrees.z) },
              { x = full.x * k, y = full.y, z = full.z * k })
    end
    for i = #markers, shown + 1, -1 do
        if alive(markers[i] and markers[i].entity) then
            Scene.destroy(markers[i].entity)
        end
        markers[i] = nil
    end
end

function ThrowPreview.hide()
    for _, entity in ipairs(dots) do
        if alive(entity) then
            Scene.destroy(entity)
        end
    end
    dots = {}
    for _, m in ipairs(markers) do
        if alive(m.entity) then
            Scene.destroy(m.entity)
        end
    end
    markers = {}
end

return ThrowPreview
