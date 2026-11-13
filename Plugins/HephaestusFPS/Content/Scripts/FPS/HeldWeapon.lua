-- Holds a weapon mesh in a character's hands: the grip at one hand socket and the barrel (+X) pointing at the
-- other, or along where the character faces (alongFacing). Put it on a child of an entity with a Character whose
-- asset has both sockets.
local Registry = require("FPS.Registry")

Expose.string("gripSocket", "RightHand")
Expose.string("supportSocket", "LeftHand")
Expose.number("gripForward", 0.13)
Expose.number("gripUp", 0.02)
Expose.number("gripSide", 0) -- metres across the barrel towards the left, from the grip hand into its palm
Expose.bool("alongFacing", false) -- barrel where the character faces, tilted up or down as its hands are
Expose.number("forwardYawDegrees", -90) -- the character's forward in its own space: -90 is +Z, as Mixamo faces
Expose.string("dropPrefab", "")
Expose.number("muzzleForward", 0.45) -- from the weapon's origin to the end of the barrel

local owner = nil
local ownerKey = nil
local dropped = false
local droppedCopy = nil

function onAwake()
    owner = this:GetParent()
    ownerKey = owner and Registry.key(owner) or nil
    dropped = false
    droppedCopy = nil
end

-- Where the weapon goes in the owner's space: its origin, and the yaw and pitch that turn its +X along the barrel.
local function holdLocal()
    local character = owner and owner:IsValid() and owner:GetCharacter()
    if not character then
        return nil
    end
    local grip = character:GetSocketLocal(gripSocket)
    local support = character:GetSocketLocal(supportSocket)
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
    local yaw = alongFacing and math.rad(forwardYawDegrees) or math.atan(-dz, dx)
    local pitch = math.asin(math.max(-1, math.min(1, dy)))
    -- The barrel and the weapon's +Y and left after turning by yaw and pitch (rotation order Rx * Ry * Rz, no X).
    local cp, sp, cy, sy = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw)
    local fx, fy, fz = cp * cy, sp, -cp * sy
    local ux, uy, uz = -sp * cy, cp, sp * sy
    local lx, lz = -sy, -cy
    return { x = g.x + fx * gripForward + ux * gripUp + lx * gripSide, y = g.y + fy * gripForward + uy * gripUp,
             z = g.z + fz * gripForward + uz * gripUp + lz * gripSide }, yaw, pitch
end

-- The same hold in the world, through the owner's position and yaw.
local function hold(world)
    local position, yaw, pitch = holdLocal()
    if not position or not world then
        return position, yaw, pitch
    end
    local transform = owner:GetTransform()
    local at = transform:GetWorldPosition()
    local turn = transform:GetRotation().y
    local c, s = math.cos(turn), math.sin(turn)
    return { x = at.x + position.x * c + position.z * s, y = at.y + position.y,
             z = at.z - position.x * s + position.z * c }, yaw + turn, pitch
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
    -- Where shots leave the barrel, for the owner's muzzle flash and tracers.
    local world, worldYaw, worldPitch = hold(true)
    if world and ownerKey then
        local dx = math.cos(worldPitch) * math.cos(worldYaw)
        local dy = math.sin(worldPitch)
        local dz = -math.cos(worldPitch) * math.sin(worldYaw)
        Registry.muzzles[ownerKey] = { x = world.x + dx * muzzleForward, y = world.y + dy * muzzleForward,
                                         z = world.z + dz * muzzleForward, dx = dx, dy = dy, dz = dz }
    end
end

-- Lets go of the weapon: spawns dropPrefab where it is, thrown with vx, vy, vz, and hides the held one. The copy is
-- removed after lifeSeconds, or with this weapon (and its owner) when lifeSeconds is nil.
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
    vars.lifeSeconds = lifeSeconds or 1e9
    droppedCopy = Scene.instantiate(dropPrefab, { position = position, rotation = { x = 0, y = math.deg(yaw), z = math.deg(pitch) },
                                    vars = vars })
end

-- Puts the weapon away for good: hidden, no longer held, and never dropped. Scripts cannot destroy one node of a
-- prefab instance on its own (Scene.destroy takes the whole instance), so a character swapping weapons stows this one.
function stowWeapon()
    dropped = true
    Mesh.setVisible(false)
    if ownerKey then
        Registry.muzzles[ownerKey] = nil
    end
end

function onDestroy()
    if ownerKey then
        Registry.muzzles[ownerKey] = nil
    end
    if droppedCopy and droppedCopy:IsValid() then
        Scene.destroy(droppedCopy)
    end
end
