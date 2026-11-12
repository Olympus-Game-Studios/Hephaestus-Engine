-- The player's own legs, shown only while they slide so the slide can be seen from first person. Put it on a
-- character with the legs model whose MeshRenderer has drawOverWorld on (no clipping into walls) and castShadows off
-- (the player has no body shadow), and no collider
-- (so shots pass through); Player.lua spawns it as a child of the player (so it moves with the player's drawn,
-- physics-interpolated position, as the camera does) from slideLegsPrefab and sets the tuning below. The legs sit in the
-- slide pose and rise into view as the slide starts; as it ends they play exitClip (the slide pose turned down about the
-- hips) in step with the player's slide blend and stay put as the camera rises, so they sink out of view, then hide.
-- Through a vault they play vaultClip (from hanging out of view, a swing across the view and over) in step with it,
-- cross-fading from and back to the slide pose when the vault comes out of a slide. A mantle out of a slide hides them at once.
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
Expose.string("exitClip", "LegsSlideExit") -- "" just drops the legs away
Expose.number("exitClipSeconds", 0.3) -- the exit clip's length
Expose.string("vaultClip", "LegsVault") -- "" shows no legs while vaulting
Expose.number("vaultClipSeconds", 0.4) -- the vault clip's length
Expose.number("blendSeconds", 0.1) -- cross-fade between the slide and vault clips

local shown = false
local mode = nil -- the clip the legs show: "slide", "exit" or "vault"
local exitHeight = 0 -- where the legs stay while they tuck away; the camera rising must not carry them up with it

function onAwake()
    Mesh.setVisible(false)
end

function onUpdate(dt)
    local player = Registry.player
    local blend = player and player.entity and player.entity:IsValid() and (player.slideBlend or 0) or 0
    local vaulting = player and player.vaultProgress ~= nil and vaultClip ~= ""
    local visible = (blend > 0.02 or vaulting) and not player.climbing
    if visible ~= shown then
        shown = visible
        Mesh.setVisible(visible)
    end
    if not visible then
        mode = nil
        return
    end
    local animator = this:GetAnimator()
    local exiting = not vaulting and not player.sliding and exitClip ~= ""
    local wanted = vaulting and "vault" or (exiting and "exit" or "slide")
    local eased = vaulting and 1 or blend * blend * (3 - 2 * blend)
    local height = (player.eyeHeight or 0) - belowEye - hiddenDrop * (1 - eased)
    -- The scrubbed clips play on (a paused clip would hold a cross-fade still) and are set to their place each frame.
    if wanted ~= mode and animator then
        local clip = ({ slide = slideClip, exit = exitClip, vault = vaultClip })[wanted]
        if (mode == "slide" and wanted == "vault") or (mode == "vault" and wanted == "slide") then
            animator:CrossFade(clip, blendSeconds)
        else
            animator:Play(clip)
        end
        mode = wanted
        exitHeight = height
    end
    if animator and mode == "exit" then
        animator:SetTime((1 - blend) * exitClipSeconds)
        height = exitHeight
    elseif animator and mode == "vault" then
        animator:SetTime(player.vaultProgress * vaultClipSeconds)
    end
    local down = math.max(0, math.min(1, -(player.pitch or 0) / (math.pi / 2)))
    local forward = forwardOffset - pullBackAtDown * down
    -- In the player's own space (it carries their yaw): +X is forward, +Z is to their right.
    Transform.setPosition(forward, height, rightOffset)
    Transform.setRotation(0, math.rad(yawOffsetDegrees), 0)
end
