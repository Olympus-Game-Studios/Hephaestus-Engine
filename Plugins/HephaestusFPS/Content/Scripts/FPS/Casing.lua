-- An ejected shell casing: launched with vx, vy, vz and a tumble once its body exists, removed after lifeSeconds.
Expose.number("vx", 0)
Expose.number("vy", 0)
Expose.number("vz", 0)
Expose.number("spin", 25)
Expose.number("lifeSeconds", 4)

local launched = false
local age = 0

function onAwake()
    launched = false
    age = 0
end

function onUpdate(dt)
    if not launched and Physics.hasBody() then
        Physics.setLinearVelocity(vx, vy, vz)
        Physics.setAngularVelocity((math.random() * 2 - 1) * spin, (math.random() * 2 - 1) * spin, spin)
        launched = true
    end
    age = age + dt
    if age >= lifeSeconds then
        Scene.destroy(this)
        lifeSeconds = math.huge
    end
end
