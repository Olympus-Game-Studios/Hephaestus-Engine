-- Holds a weapon mesh in a character's hands: the grip at one hand socket and the barrel (+X) pointing at the
-- other. Put it on a child of an entity with a Character whose asset has both sockets.
Expose.string("gripSocket", "RightHand")
Expose.string("supportSocket", "LeftHand")
Expose.number("gripForward", 0.13)
Expose.number("gripUp", 0.02)

local owner = nil

function onAwake()
    owner = this:GetParent()
end

function onLateUpdate()
    local character = owner and owner:IsValid() and owner:GetCharacter()
    if not character then
        return
    end
    local grip = character:GetSocketLocal(gripSocket)
    local support = character:GetSocketLocal(supportSocket)
    if not grip or not support then
        return
    end
    local g, s = grip.position, support.position
    local dx, dy, dz = s.x - g.x, s.y - g.y, s.z - g.z
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-4 then
        return
    end
    dx, dy, dz = dx / length, dy / length, dz / length
    local yaw = math.atan(-dz, dx)
    local pitch = math.asin(math.max(-1, math.min(1, dy)))
    -- The weapon's +Y after turning by yaw and pitch (rotation order Rx * Ry * Rz with no X).
    local ux, uy, uz = -math.sin(pitch) * math.cos(yaw), math.cos(pitch), math.sin(pitch) * math.sin(yaw)
    Transform.setPosition(g.x + dx * gripForward + ux * gripUp, g.y + dy * gripForward + uy * gripUp,
                          g.z + dz * gripForward + uz * gripUp)
    Transform.setRotation(0, yaw, pitch)
end
