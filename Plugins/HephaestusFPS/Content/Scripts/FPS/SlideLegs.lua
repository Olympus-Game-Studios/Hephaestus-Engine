-- The player's own legs, shown only while they slide so the slide can be seen from first person. Put it on a
-- character with the legs model whose MeshRenderer has castShadows off (the player has no body shadow) and no collider
-- (so shots pass through); Player.lua spawns it from slideLegsPrefab and sets the tuning below. The legs follow the
-- player's feet and yaw in the slide pose, rise into view as the slide starts and drop away as it ends (following
-- the player's slide blend), then hide.
local Registry = require("FPS.Registry")

-- Metres along the player's facing from the camera to the model's origin. Looking down moves the legs back
-- by up to pullBackAtDown (all of it when looking straight down), just enough that the cut at the waist stays out of view.
Expose.number("forwardOffset", -0.1)
Expose.number("pullBackAtDown", 0.15)
Expose.number("rightOffset", -0.12)
Expose.number("belowEye", 1.24) -- metres from the camera down to the model's origin; its hips sit 0.94 m above the origin
Expose.number("yawOffsetDegrees", 90) -- turns the model to face the way the player faces
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
    local down = math.max(0, math.min(1, -(player.pitch or 0) / (math.pi / 2)))
    local forward = forwardOffset - pullBackAtDown * down
    Transform.setPosition(feet.x + fx * forward + rx * rightOffset,
                          feet.y + (player.eyeHeight or 0) - belowEye - hiddenDrop * (1 - eased),
                          feet.z + fz * forward + rz * rightOffset)
    Transform.setRotation(0, yaw + math.rad(yawOffsetDegrees), 0)
end
