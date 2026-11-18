-- Tracers: a glowing streak that flies a shot's path. Each one moves at its speed with gravity, stops at maxDistance
-- or where cast() says it hits, and is drawn as a thin stretched mesh (the tracer prefab) behind its head.
-- Tracers.update(dt) runs them all; the player script calls it every frame.
local Tracers = { live = {} }

Tracers.LENGTH = 3.5 -- metres of streak behind the head
Tracers.WIDTH = 0.05

-- Forgets every tracer (their entities go with the scene).
function Tracers.reset()
    Tracers.live = {}
end

-- A tracer's flight without drawing: { x, y, z, vx, vy, vz, gravity, maxDistance, travelled }.
function Tracers.newFlight(x, y, z, dx, dy, dz, speed, gravity, maxDistance)
    return { x = x, y = y, z = z, vx = dx * speed, vy = dy * speed, vz = dz * speed, gravity = gravity or 0,
             maxDistance = maxDistance, travelled = 0, tail = { x = x, y = y, z = z } }
end

-- Moves the flight on by dt. cast(x, y, z, dx, dy, dz, length) returns the distance to a hit along that segment or
-- nil. Returns true once the tracer has landed or gone its full distance.
function Tracers.advance(flight, dt, cast)
    flight.vy = flight.vy - flight.gravity * dt
    local sx, sy, sz = flight.vx * dt, flight.vy * dt, flight.vz * dt
    local length = math.sqrt(sx * sx + sy * sy + sz * sz)
    if length < 1e-6 then
        return false
    end
    local dx, dy, dz = sx / length, sy / length, sz / length
    local step = math.min(length, flight.maxDistance - flight.travelled)
    local done = step < length
    local hitDistance = cast and cast(flight.x, flight.y, flight.z, dx, dy, dz, step) or nil
    if hitDistance and hitDistance < step then
        step, done = hitDistance, true
    end
    flight.x, flight.y, flight.z = flight.x + dx * step, flight.y + dy * step, flight.z + dz * step
    flight.travelled = flight.travelled + step
    flight.dx, flight.dy, flight.dz = dx, dy, dz
    return done or flight.travelled >= flight.maxDistance - 1e-4
end

local function draw(tracer)
    local f = tracer.flight
    local length = math.min(Tracers.LENGTH, f.travelled)
    if length < 1e-3 or not f.dx then
        return
    end
    local transform = tracer.entity:GetTransform()
    transform:SetPosition(f.x - f.dx * length * 0.5, f.y - f.dy * length * 0.5, f.z - f.dz * length * 0.5)
    -- The streak's long axis is the mesh's +X, turned by yaw then pitch (Rx * Ry * Rz with no X).
    transform:SetRotation(0, math.atan(-f.dz, f.dx), math.asin(math.max(-1, math.min(1, f.dy))))
    transform:SetScale(length, Tracers.WIDTH, Tracers.WIDTH)
end

-- Starts a tracer from (x, y, z) along the unit direction (dx, dy, dz). options: speed, gravity, maxDistance, cast.
function Tracers.spawn(prefab, x, y, z, dx, dy, dz, options)
    if not prefab or prefab == "" then
        return
    end
    -- The prefab is authored tiny, so nothing shows before the first draw sizes it.
    local entity = Scene.instantiate(prefab, { position = { x = x, y = y, z = z } })
    if not entity then
        return
    end
    Tracers.live[#Tracers.live + 1] = {
        entity = entity,
        flight = Tracers.newFlight(x, y, z, dx, dy, dz, options.speed, options.gravity, options.maxDistance),
        cast = options.cast,
    }
end

function Tracers.update(dt)
    for i = #Tracers.live, 1, -1 do
        local tracer = Tracers.live[i]
        if not tracer.entity:IsValid() then
            table.remove(Tracers.live, i)
        elseif tracer.landed then
            Scene.destroy(tracer.entity)
            table.remove(Tracers.live, i)
        else
            -- Shown for one frame where it lands, then removed.
            tracer.landed = Tracers.advance(tracer.flight, dt, tracer.cast)
            draw(tracer)
        end
    end
end

return Tracers
