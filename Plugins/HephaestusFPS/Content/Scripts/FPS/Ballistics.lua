-- Bullets with flight time and drop: each step moves a bullet under gravity and checks the segment it covered.
local Ballistics = {}

function Ballistics.new(ox, oy, oz, dx, dy, dz, speed)
    return { x = ox, y = oy, z = oz, vx = dx * speed, vy = dy * speed, vz = dz * speed, travelled = 0, age = 0 }
end

-- Advances bullet by dt with gravity (m/s^2, downwards). cast(x, y, z, dx, dy, dz, distance, bullet) returns the
-- first hit along a segment or nil. Returns the hit with its distance measured along the whole flight, or nil.
function Ballistics.step(bullet, dt, gravity, cast)
    bullet.vy = bullet.vy - gravity * dt
    local sx, sy, sz = bullet.vx * dt, bullet.vy * dt, bullet.vz * dt
    local length = math.sqrt(sx * sx + sy * sy + sz * sz)
    bullet.age = bullet.age + dt
    if length < 1e-6 then
        return nil
    end
    local hit = cast(bullet.x, bullet.y, bullet.z, sx / length, sy / length, sz / length, length, bullet)
    if hit then
        hit.distance = bullet.travelled + (hit.distance or 0)
        bullet.travelled = hit.distance
        return hit
    end
    bullet.x, bullet.y, bullet.z = bullet.x + sx, bullet.y + sy, bullet.z + sz
    bullet.travelled = bullet.travelled + length
    return nil
end

-- Steps every bullet in list, calling onHit(bullet, hit) for each one that hits; bullets that hit, fly past
-- maxDistance or outlive maxSeconds are removed.
function Ballistics.update(list, dt, gravity, maxDistance, maxSeconds, cast, onHit)
    local i = 1
    while i <= #list do
        local bullet = list[i]
        local hit = Ballistics.step(bullet, dt, gravity, cast)
        if hit then
            onHit(bullet, hit)
        end
        if hit or bullet.travelled >= maxDistance or bullet.age >= maxSeconds then
            table.remove(list, i)
        else
            i = i + 1
        end
    end
end

return Ballistics
