-- Holds a weapon mesh in a character's hands: the grip at one hand socket and the barrel (+X) pointing at the
-- other. Put it on a child of an entity with a Character whose asset has both sockets.
Expose.string("gripSocket", "RightHand")
Expose.string("supportSocket", "LeftHand")
Expose.number("gripForward", 0.13)
Expose.number("gripUp", 0.02)
Expose.string("dropPrefab", "")

local owner = nil
local dropped = false

function onAwake()
    owner = this:GetParent()
    dropped = false
end

-- Grip and barrel direction from the sockets, in the owner's space (world = false) or the world.
local function hold(world)
    local character = owner and owner:IsValid() and owner:GetCharacter()
    if not character then
        return nil
    end
    local grip = world and character:GetSocket(gripSocket) or character:GetSocketLocal(gripSocket)
    local support = world and character:GetSocket(supportSocket) or character:GetSocketLocal(supportSocket)
    if not grip or not support then
        return nil
    end
    local g, s = grip.position, support.position
    local dx, dy, dz = s.x - g.x, s.y - g.y, s.z - g.z
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-4 then
        return nil
    end
    dx, dy, dz = dx / length, dy / length, dz / length
    local yaw = math.atan(-dz, dx)
    local pitch = math.asin(math.max(-1, math.min(1, dy)))
    -- The weapon's +Y after turning by yaw and pitch (rotation order Rx * Ry * Rz with no X).
    local ux, uy, uz = -math.sin(pitch) * math.cos(yaw), math.cos(pitch), math.sin(pitch) * math.sin(yaw)
    return { x = g.x + dx * gripForward + ux * gripUp, y = g.y + dy * gripForward + uy * gripUp,
             z = g.z + dz * gripForward + uz * gripUp }, yaw, pitch
end

function onLateUpdate()
    if dropped then
        return
    end
    local position, yaw, pitch = hold(false)
    if position then
        Transform.setPosition(position.x, position.y, position.z)
        Transform.setRotation(0, yaw, pitch)
    end
end

-- Lets go of the weapon: spawns dropPrefab where it is, thrown with vx, vy, vz and removed after lifeSeconds
-- (the prefab's own lifetime when nil), and hides the held one.
function dropWeapon(vx, vy, vz, lifeSeconds)
    if dropped or dropPrefab == "" then
        return
    end
    local position, yaw, pitch = hold(true)
    if not position then
        return
    end
    dropped = true
    Mesh.setVisible(false)
    local vars = { vx = vx or 0, vy = vy or 0, vz = vz or 0 }
    if lifeSeconds then
        vars.lifeSeconds = lifeSeconds
    end
    Scene.instantiate(dropPrefab, { position = position, rotation = { x = 0, y = math.deg(yaw), z = math.deg(pitch) },
                                    vars = vars })
end
