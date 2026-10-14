-- Verification fixture: authored transform drives the kinematic body before physics.
local start
local time = 0

function onAwake()
    start = Transform.getPosition()
    time = 0
end

function onUpdate(dt)
    time = time + dt
    Transform.setPosition(start.x, start.y, start.z + 2 * math.sin(time * 0.25))
end
