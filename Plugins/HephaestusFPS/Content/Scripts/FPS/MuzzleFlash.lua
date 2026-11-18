-- A muzzle flash quad under a weapon, named <Weapon>_Flash_<anything>. Hidden until the weapon fires.
local remaining = 0
local baseScale = nil

function onAwake()
    baseScale = Transform.getScale()
    Mesh.setVisible(false)
end

-- roll spins the flash about the barrel (radians); scale multiplies its authored size.
function showFlash(seconds, roll, scale)
    remaining = seconds or 0.035
    local s = scale or 1
    Transform.setRotation(roll or 0, 0, math.pi / 2)
    Transform.setScale(baseScale.x * s, baseScale.y * s, baseScale.z * s)
    Mesh.setVisible(true)
end

function hideFlash()
    remaining = 0
    Mesh.setVisible(false)
end

function onUpdate(dt)
    if remaining > 0 then
        remaining = remaining - dt
        if remaining <= 0 then
            Mesh.setVisible(false)
        end
    end
end
