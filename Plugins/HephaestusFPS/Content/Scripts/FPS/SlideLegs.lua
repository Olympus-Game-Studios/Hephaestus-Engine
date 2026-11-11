-- The player's own legs, shown only while they slide so the slide can be seen from first person. Put it on a
-- character with the legs model whose MeshRenderer has castShadows off (the player has no body shadow) and no collider
-- (so shots pass through); Player.lua spawns it from slideLegsPrefab and sets the tuning below. The legs follow the
-- player's feet and yaw in the slide pose, rise into view as the slide starts and drop away as it ends (following
-- the player's slide blend), then hide.
local Registry = require("FPS.Registry")

-- Metres along the player's facing from their feet to the model's origin. The hips stay behind the camera so only a
-- view straight down shows the cut at the waist.
Expose.number("forwardOffset", -0.35)
Expose.number("rightOffset", -0.12)
Expose.number("heightOffset", -0.55) -- metres from the floor; the model's hips sit 0.94 m above its origin
Expose.number("yawOffsetDegrees", 90) -- turns the model to face the way the player faces
Expose.number("pitchDegrees", 0) -- tips the legs back (positive) or forward
Expose.number("hiddenDrop", 0.45) -- how far below their place the legs start and end, in metres
Expose.string("slideClip", "LegsSlide")

local shown = false

function onAwake()
    Mesh.setVisible(false)
end

function onUpdate(dt)
    local player = Registry.player
    local blend = player and player.entity and player.entity:IsValid() and (player.slideBlend or 0) or 0
    local visible = blend > 0.02
    if visible ~= shown then
        shown = visible
        Mesh.setVisible(visible)
        local animator = visible and this:GetAnimator()
        if animator then
            animator:Play(slideClip)
        end
    end
    if not visible then
        return
    end
    local transform = player.entity:GetTransform()
    local feet = transform and transform:GetWorldPosition()
    if not feet then
        return
    end
    local yaw = player.yaw or 0
    local fx, fz = math.cos(yaw), -math.sin(yaw)
    local rx, rz = -fz, fx
    local eased = blend * blend * (3 - 2 * blend)
    Transform.setPosition(feet.x + fx * forwardOffset + rx * rightOffset,
                          feet.y + heightOffset - hiddenDrop * (1 - eased),
                          feet.z + fz * forwardOffset + rz * rightOffset)
    Transform.setRotation(math.rad(pitchDegrees), yaw + math.rad(yawOffsetDegrees), 0)
end
