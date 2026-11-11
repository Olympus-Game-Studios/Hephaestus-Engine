local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Gunplay = require("FPS.Gunplay")
local Health = require("FPS.Health")
local Interactable = require("FPS.Interactable")
local Hud = require("FPS.Hud")
local GrenadeWarning = require("FPS.GrenadeWarning")
local DamageDirection = require("FPS.DamageDirection")
local Tracers = require("FPS.Tracers")
local Settings = require("FPS.Settings")
local PauseMenu = require("FPS.PauseMenu")
-- Only Editor Play and Development builds load the developer menu.
local DevMenu = Application and Application.isDevelopment and Application.isDevelopment() and require("FPS.DevMenu") or nil
local Impacts = require("FPS.Impacts")
local ThrowPreview = require("FPS.ThrowPreview")
local Mantle = require("FPS.Mantle")
local BulletSounds = require("FPS.BulletSounds")

Expose.number("mouseSensitivityX", 0.002)
Expose.number("mouseSensitivityY", 0.002)
Expose.number("stickSensitivityX", 1.5)
Expose.number("stickSensitivityY", 1.5)
Expose.bool("invertX", false)
Expose.bool("invertY", false)
Expose.number("pitchMinDegrees", -89)
Expose.number("pitchMaxDegrees", 89)
Expose.number("standEyeHeight", 1.62)
Expose.number("crouchEyeHeight", 0.95)
Expose.bool("leanEnabled", true)
Expose.number("leanDistance", 0.35)
Expose.number("leanRollDegrees", 10)
Expose.number("leanSeconds", 0.15)
Expose.bool("proneEnabled", true)
Expose.number("proneEyeHeight", 0.35)
Expose.number("proneEyeBlendMetersPerSecond", 2.5)
Expose.number("proneSpeedScale", 0.25)
Expose.number("eyeBlendMetersPerSecond", 8)
Expose.number("sprintMultiplier", 1.6)
Expose.number("strafeSpeedScale", 0.85)
Expose.number("backpedalSpeedScale", 0.7)
Expose.number("crouchSpeedScale", 0.6)
Expose.number("landingSlowScale", 0.6)
Expose.number("landingSlowSeconds", 0.3)
Expose.number("landingSlowMinSpeed", 4)
Expose.bool("tacticalSprintEnabled", true)
Expose.number("tacticalSprintMultiplier", 1.9)
Expose.number("tacticalSprintSeconds", 2.5)
Expose.number("tacticalSprintRechargeSeconds", 4)
Expose.number("tacticalSprintFovBoost", 4)
Expose.bool("slideEnabled", true)
Expose.number("slideBoost", 1.15)
Expose.number("slideSeconds", 0.8)
Expose.number("slideCooldown", 0.6)
Expose.number("slideSteer", 1.5)
Expose.number("slideFovBoost", 6)
Expose.number("slideRollDegrees", 4)
Expose.number("slideCameraHeight", 0.55) -- eye height while sliding, eased in and out with the slide (0 keeps crouch height)
-- The player's own legs, seen while sliding: a character prefab with FPS/SlideLegs.lua ("" for none) and where it
-- sits relative to the camera and the player's facing.
Expose.string("slideLegsPrefab", "")
Expose.number("slideLegsForward", -0.1)
Expose.number("slideLegsRight", -0.12)
Expose.number("slideLegsBelowEye", 1.24) -- metres from the camera down to the model's origin, so the hips stay under it
Expose.number("slideLegsYawDegrees", 90)
Expose.number("slideLegsDrop", 0.45) -- how far below their place they rise from and drop back to
Expose.number("slideLegsPullBackAtDown", 0.15) -- metres back when looking straight down, scaled by pitch (the waist is cut)
Expose.bool("mantleEnabled", true)
Expose.number("mantleReach", 0.8)
Expose.number("mantleMaxHeight", 2.0)
Expose.number("vaultMaxHeight", 1.1)
Expose.number("mantleSeconds", 0.3)
Expose.number("mantleSecondsPerMetre", 0.2)
Expose.number("fieldOfView", 80)
Expose.number("fullSpreadMoveSpeed", 5.0)
Expose.number("interactDistance", 2.5)
Expose.number("maxHealth", 100)
Expose.integer("team", 1) -- soldiers on another team fight the player
Expose.bool("hudEnabled", true)
Expose.bool("showCrosshair", true)
Expose.bool("showHealth", true)
Expose.bool("showAmmo", true)
Expose.bool("showPrompt", true)
Expose.string("hudDocument", "plugin://com.hephaestus.fps/UI/FPSHud.hui")
Expose.bool("pauseMenuEnabled", true)
Expose.bool("pauseMenuRestart", true)
Expose.bool("pauseFreezesTime", false)
Expose.string("devEnemyPrefab", "") -- what the developer menu (F2, development builds) spawns
Expose.string("mainMenuScene", "")
Expose.bool("restartEnabled", true)
Expose.number("restartDelaySeconds", 1.5)
Expose.string("restartPrompt", "Press Space to restart")
Expose.bool("grenadeWarningEnabled", true)
Expose.string("grenadeWarningIcon", "")
Expose.string("grenadeWarningArrowPattern", "")
Expose.integer("grenadeWarningArrowSteps", 24)
Expose.number("grenadeWarningRange", 10)
Expose.number("grenadeWarningRadius", 160)
Expose.number("grenadeWarningIconSize", 66)
Expose.bool("damageIndicatorEnabled", true)
Expose.number("damageIndicatorSeconds", 1.5)
Expose.number("damageIndicatorRadius", 120)
Expose.number("damageIndicatorSize", 40)
Expose.bool("presentationEnabled", true)
Expose.number("viewBobScale", 1.0)
Expose.number("cameraShakeScale", 1.0)
Expose.string("footstepSounds", "")
Expose.number("footstepStride", 2.1)
Expose.number("footstepVolume", 0.5)
Expose.string("bulletHitSounds", "") -- played at random when an enemy bullet hits the player
Expose.number("bulletHitVolume", 0.9)
Expose.string("nearMissSounds", "") -- played at random where an enemy bullet passes close by
Expose.number("nearMissVolume", 0.8)
Expose.number("nearMissRadius", 2.5) -- how close to the head a miss is heard, in metres
Expose.number("nearMissInterval", 0.2) -- the least time between two near-miss sounds, in seconds
Expose.string("envImpactSounds", "") -- played at random where an enemy bullet hits the level near the player
Expose.number("envImpactVolume", 0.8)
Expose.number("envImpactRadius", 5) -- how close to the head an impact is heard, in metres
Expose.number("envImpactInterval", 0.12) -- the least time between two impact sounds, in seconds
Expose.string("impactDecalPrefabs", "")
Expose.number("maxImpactDecals", 64)
Expose.string("impactDustPrefab", "")
Expose.string("impactSparksPrefab", "")
Expose.string("fragGrenadePrefab", "")
Expose.string("smokeGrenadePrefab", "")
Expose.number("fragGrenades", 2)
Expose.number("smokeGrenades", 2)
Expose.number("maxFragGrenades", 4) -- grenade pickups fill up to these
Expose.number("maxSmokeGrenades", 4)
Expose.number("throwSpeed", 15)
Expose.number("throwLift", 0.2)
Expose.number("throwSeconds", 0.6)
Expose.number("throwReleaseFraction", 0.4)
Expose.string("grenadeArmSound", "")
Expose.string("grenadeThrowSound", "")
Expose.number("grenadeSoundVolume", 1.0)
Expose.bool("throwPreviewEnabled", true)
Expose.string("throwPreviewDotPrefab", "")
Expose.string("throwPreviewMarkerPrefab", "")
Expose.number("throwPreviewDots", 32)
Expose.number("throwPreviewStartDistance", 1.5)
Expose.number("throwPreviewSeconds", 3)
Expose.number("throwPreviewDamping", 0.1)
Expose.number("throwPreviewBounces", 3)
Expose.number("throwPreviewRestitution", 0.2)
Expose.number("throwPreviewFriction", 0.55)

local Player = {}
local yaw = 0
local warningClock = 0
local endedFor = nil
local pitch = 0
local lookEnabled = true
local inputEnabled = true
local eyeHeight = 1.62
local viewHeight = 1.62 -- eyeHeight lowered towards slideCameraHeight while sliding; where the camera and aim sit
local lastDt = 1 / 60
local commands = { fireHeld = false, firePressed = false, reload = false, interact = false }
local discoverTries = 0
local lookPitchDelta = 0
local lookYawRate = 0
local lookPitchRate = 0
local cameraKickPitch = { x = 0, v = 0 }
local cameraKickYaw = { x = 0, v = 0 }
local cameraKickOmega, cameraKickZeta = 24, 0.6
local wasGrounded = true
local sinceHardLanding = nil
local slide = nil
local slideCooldownRemaining = 0
local slideBlend = 0
local slideLegs = nil
local tactical = false
local tacticalCharge = 0
local tacticalBlend = 0
local sinceSprintEnded = math.huge
local mantle = nil
local proneWanted = false
local leanTarget = 0
local lean = 0
local leanOffset = 0
local fallSpeed = 0
local footfallThisFrame = false
local stepCount = 0
local lastKnownHealth = 0
local sprinting = false
local swap = nil
local stepDistance = 0
local stepIndex = 0
local footsteps = {}
local bulletSounds = { hits = {}, misses = {}, impacts = {}, last = {}, missAt = -math.huge, impactAt = -math.huge }
local cameraObject = nil
local grenades = { frag = 0, smoke = 0 }
local throw = nil

local function degToRad(value)
    return value * math.pi / 180
end

function Player.setLookEnabled(enabled)
    lookEnabled = enabled == true
    if lookEnabled then
        UI.setCursorMode("game")
    else
        UI.setCursorMode("ui")
        Character.setSpeedMultiplier(1)
        Character.move(0, 0)
    end
end

function Player.setInputEnabled(enabled)
    inputEnabled = enabled == true
    if not inputEnabled then
        Player.setLookEnabled(false)
    end
end

function Player.yaw()
    return yaw
end

function Player.pitch()
    return pitch
end

local function activeWeaponRow()
    return Registry.activeWeapon()
end

local function applyLook(dt)
    local mouse = Input.getMouseDelta()
    local mx = (mouse and mouse.x) or 0
    local my = (mouse and mouse.y) or 0
    lookPitchDelta = 0
    lookYawRate = 0
    lookPitchRate = 0
    local row = activeWeaponRow()
    local player = Registry.player
    if player then
        -- Recoil and its recovery move the real aim, not only the gun model.
        pitch = pitch + (player.pendingPitch or 0)
        yaw = yaw + (player.pendingYaw or 0)
        player.pendingPitch = 0
        player.pendingYaw = 0
    end
    if lookEnabled and inputEnabled then
        local adsScale = row and Gunplay.lerp(1, row.adsSensitivityScale or 1, row.adsFovBlend or 0) or 1
        local invertXScale = invertX and -1 or 1
        local invertYScale = invertY and -1 or 1
        local stickX = Input.getAxis("LookStickX") or 0
        local stickY = Input.getAxis("LookStickY") or 0
        local sensitivity = Settings.values.sensitivity
        local dYaw = -(mx * mouseSensitivityX + stickX * stickSensitivityX * dt) * invertXScale * adsScale * sensitivity
        lookPitchDelta = -(my * mouseSensitivityY + stickY * stickSensitivityY * dt) * invertYScale * adsScale
            * sensitivity
        yaw = yaw + dYaw
        pitch = pitch + lookPitchDelta
        if dt > 1e-4 then
            lookYawRate = dYaw / dt
            lookPitchRate = lookPitchDelta / dt
        end
    end
    pitch = Logic.clampPitch(pitch, degToRad(pitchMinDegrees), degToRad(pitchMaxDegrees))
end

local function discoverWeapons()
    if #Registry.weaponOrder > 0 then
        return true
    end
    discoverTries = discoverTries + 1
    return discoverTries > 8
end

local function activeWeaponEntity()
    local row = activeWeaponRow()
    return row and row.entity or nil
end

local function equipNow(index)
    local previous = activeWeaponEntity()
    if previous then
        previous:Call("unequip")
    end
    local key = Registry.weaponOrder[index]
    local row = Registry.weapons[key]
    if row and row.entity then
        row.entity:Call("equip")
    end
end

-- The current weapon lowers for its lowerSeconds before the next one is raised.
local function requestSlot(index)
    local selected = Logic.selectSlot(index, #Registry.weaponOrder)
    if not selected then
        return
    end
    local active = activeWeaponRow()
    if active and Registry.weaponOrder[selected] == active.key then
        return
    end
    if not active then
        equipNow(selected)
        return
    end
    swap = { index = selected, remaining = active.lowerSeconds or 0 }
end

local function activeSlot()
    local active = activeWeaponRow()
    if active then
        for i, key in ipairs(Registry.weaponOrder) do
            if key == active.key then
                return i
            end
        end
    end
    return 1
end

local function parseList(text)
    local list = {}
    for item in string.gmatch(text or "", "[^;]+") do
        list[#list + 1] = item
    end
    return list
end

-- Counts strides even without footstep sounds, because the weapon bob and footfall dips follow them.
local function updateFootsteps(speedXZ, grounded, dt)
    footfallThisFrame = false
    if not grounded or speedXZ < 0.5 then
        return
    end
    stepDistance = stepDistance + speedXZ * dt
    if stepDistance >= footstepStride then
        stepDistance = stepDistance - footstepStride
        stepCount = stepCount + 1
        footfallThisFrame = true
        if #footsteps > 0 then
            stepIndex = stepIndex % #footsteps + 1
            if Audio and Audio.isAvailable and Audio.isAvailable() then
                Audio.play(footsteps[stepIndex], { volume = footstepVolume, bus = "sfx", priority = 1,
                                                   pitchJitter = 0.05, volumeJitter = 0.15 })
            end
        end
    end
end

-- Presentation-only camera kick: it moves the view, never where shots go.
local function updateCameraKick(dt)
    local player = Registry.player
    local shake = presentationEnabled and Gunplay.clamp(cameraShakeScale * Settings.values.cameraShake, 0, 1) or 0
    if player and player.cameraImpulses then
        for _, impulse in ipairs(player.cameraImpulses) do
            cameraKickOmega, cameraKickZeta = impulse.omega or cameraKickOmega, impulse.zeta or cameraKickZeta
            cameraKickPitch.v = cameraKickPitch.v
                + Gunplay.impulseForPeak(impulse.pitch or 0, cameraKickOmega, cameraKickZeta) * shake
            cameraKickYaw.v = cameraKickYaw.v
                + Gunplay.impulseForPeak(impulse.yaw or 0, cameraKickOmega, cameraKickZeta) * shake
        end
        player.cameraImpulses = nil
    end
    Gunplay.dampedSpring(cameraKickPitch, 0, cameraKickOmega, cameraKickZeta, dt)
    Gunplay.dampedSpring(cameraKickYaw, 0, cameraKickOmega, cameraKickZeta, dt)
end

local function applyCameraRotation()
    local motion = presentationEnabled and Gunplay.clamp(viewBobScale * Settings.values.viewBob, 0, 1) or 0
    local leanRoll = math.rad(leanRollDegrees) * leanOffset / math.max(leanDistance, 1e-3)
    Camera.setRotation(pitch + cameraKickPitch.x, cameraKickYaw.x,
                       -math.rad(slideRollDegrees) * slideBlend * motion - leanRoll)
end

-- Plain trigger volumes are skipped so a large sensor cannot hide the interactable inside it.
local function interactionHit(origin, direction)
    local ignored = { this }
    for _ = 1, 4 do
        local hit = Physics.raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
                                    interactDistance, "Character", false, ignored)
        if not hit or not hit.sensor or not hit.entity or Registry.interactables[Registry.key(hit.entity)] then
            return hit
        end
        ignored[#ignored + 1] = hit.entity
    end
    return nil
end

local function playThrowSound(path)
    if path ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(path, { volume = grenadeSoundVolume, bus = "sfx", priority = 2, pitchJitter = 0.03 })
    end
end

-- Where a throw leaves the hand and how fast: beside the eye along the aim, lifted a little, plus the player's
-- own velocity.
local function throwLaunch(origin, direction)
    local fx, fy, fz = direction.x, direction.y + throwLift, direction.z
    local length = math.sqrt(fx * fx + fy * fy + fz * fz)
    fx, fy, fz = fx / length, fy / length, fz / length
    local rx, rz = -direction.z, direction.x
    local side = math.sqrt(rx * rx + rz * rz)
    if side > 1e-4 then
        rx, rz = rx / side * 0.2, rz / side * 0.2
    end
    local vel = Character.getVelocity() or {}
    local position = { x = origin.x + direction.x * 0.5 + rx, y = origin.y + direction.y * 0.5 - 0.15,
                       z = origin.z + direction.z * 0.5 + rz }
    local velocity = { x = fx * throwSpeed + (vel.x or 0), y = fy * throwSpeed + (vel.y or 0),
                       z = fz * throwSpeed + (vel.z or 0) }
    return position, velocity
end

local function releaseGrenade(kind, origin, direction)
    local position, velocity = throwLaunch(origin, direction)
    Scene.instantiate(kind == "smoke" and smokeGrenadePrefab or fragGrenadePrefab, {
        position = position,
        vars = { vx = velocity.x, vy = velocity.y, vz = velocity.z, thrownByPlayer = true },
    })
end

-- Pressing a throw key arms a grenade and lowers the weapon over the wind-up. While the key is held the
-- preview shows where the throw will land; letting go throws it and raises the weapon again. Fire and ADS
-- are blocked throughout and a reload is cut. The grenade is only spent when it leaves the hand.
local function updateThrow(dt, origin, direction, row)
    if commands.cancelThrow and throw and not throw.recover then
        ThrowPreview.hide()
        throw = nil
    end
    commands.cancelThrow = false
    if not throw and commands.throwKind and not swap then
        local kind = commands.throwKind
        local path = kind == "smoke" and smokeGrenadePrefab or fragGrenadePrefab
        if path ~= "" and grenades[kind] > 0 then
            throw = { kind = kind, windup = 0, recover = nil }
            playThrowSound(grenadeArmSound)
            if row then
                Logic.interruptReload(row)
            end
        end
    end
    commands.throwKind = nil
    if not throw then
        return
    end
    if throw.recover then
        throw.recover = throw.recover + dt / math.max(throwSeconds * (1 - throwReleaseFraction), 0.01)
        if throw.recover >= 1 then
            throw = nil
        end
        return
    end
    throw.windup = math.min(1, throw.windup + dt / math.max(throwSeconds * throwReleaseFraction, 0.01))
    if throw.windup >= 1 and not commands[throw.kind .. "Held"] then
        grenades[throw.kind] = grenades[throw.kind] - 1
        releaseGrenade(throw.kind, origin, direction)
        playThrowSound(grenadeThrowSound)
        ThrowPreview.hide()
        throw.recover = 0
    elseif throwPreviewEnabled then
        local position, velocity = throwLaunch(origin, direction)
        local points, marks = ThrowPreview.predict(position, velocity, {
            damping = throwPreviewDamping, maxSeconds = throwPreviewSeconds, bounces = throwPreviewBounces,
            restitution = throwPreviewRestitution, friction = throwPreviewFriction,
        })
        ThrowPreview.show(points, marks, { dotPrefab = throwPreviewDotPrefab, markerPrefab = throwPreviewMarkerPrefab,
                                           dotCount = throwPreviewDots, startDistance = throwPreviewStartDistance })
    end
end

-- How far the weapon is lowered for a throw, 0 to 1, eased at both ends.
local function throwLowered()
    if not throw then
        return 0
    end
    local t = throw.recover and 1 - math.min(throw.recover, 1) or throw.windup
    return t * t * (3 - 2 * t)
end

local function updateFieldOfView(row)
    if not cameraObject then
        local cameraEntity = Scene.GetActiveCamera()
        cameraObject = cameraEntity and cameraEntity:GetCamera() or nil
    end
    if cameraObject then
        local ads = row and row.adsFovBlend or 0
        local base = Settings.fieldOfView() + slideFovBoost * slideBlend + tacticalSprintFovBoost * tacticalBlend
        cameraObject:SetFov(Gunplay.lerp(base, row and row.adsFov or base, ads))
    end
end

function onAwake()
    Registry.ensureSession()
    Tracers.reset()
    local rotation = Transform.getRotation()
    yaw = rotation.y
    pitch = 0
    inputEnabled = true
    discoverTries = 0
    sprinting = false
    swap = nil
    cameraObject = nil
    grenades = { frag = math.max(0, math.floor(fragGrenades)), smoke = math.max(0, math.floor(smokeGrenades)) }
    throw = nil
    ThrowPreview.reset()
    stepDistance = 0
    stepCount = 0
    wasGrounded = true
    fallSpeed = 0
    sinceHardLanding = nil
    slide = nil
    slideCooldownRemaining = 0
    slideBlend = 0
    proneWanted = false
    leanTarget = 0
    lean = 0
    leanOffset = 0
    tactical = false
    tacticalCharge = tacticalSprintSeconds
    tacticalBlend = 0
    sinceSprintEnded = math.huge
    mantle = nil
    cameraKickPitch = { x = 0, v = 0 }
    cameraKickYaw = { x = 0, v = 0 }
    footsteps = parseList(footstepSounds)
    bulletSounds = { hits = parseList(bulletHitSounds), misses = parseList(nearMissSounds),
                     impacts = parseList(envImpactSounds), last = {}, missAt = -math.huge, impactAt = -math.huge }
    Impacts.configure(parseList(impactDecalPrefabs), maxImpactDecals,
                      { dust = impactDustPrefab, sparks = impactSparksPrefab })
    commands = { fireHeld = false, firePressed = false, reload = false, interact = false }
    Health.attach(this, { maxHealth = maxHealth })
    Registry.setPlayer(this, {
        team = team,
        name = "You",
        yaw = 0,
        pitch = 0,
        lookEnabled = true,
        inputEnabled = true,
        pendingPitch = 0,
        pendingYaw = 0,
        bob = 0,
        input = {},
        grenades = grenades,
        maxGrenades = { frag = math.max(0, math.floor(maxFragGrenades)), smoke = math.max(0, math.floor(maxSmokeGrenades)) },
    })
    eyeHeight = standEyeHeight
    viewHeight = standEyeHeight
    endedFor = nil
    Settings.gameFieldOfView = fieldOfView
    Settings.load()
    PauseMenu.reset()
    if DevMenu then
        DevMenu.reset()
    end
    lastKnownHealth = Health.getHealth(this)
    Camera.setActive(true)
    Player.setLookEnabled(true)
    Hud.mount(hudDocument, hudEnabled)
end

-- After death or a won game: wait restartDelaySeconds, then offer a restart on Jump (Scene.reload). A match
-- (Registry.matchDirector) ends with its own screen instead.
local function updateGameEnd(dt)
    if Registry.matchDirector then
        return
    end
    if not endedFor and Registry.gameOver then
        endedFor = 0
    end
    if not endedFor or not restartEnabled then
        return
    end
    endedFor = endedFor + dt
    if endedFor >= restartDelaySeconds then
        local title = Hud.banner and Hud.banner.title or ""
        if not Hud.banner or Hud.banner.subtitle ~= restartPrompt then
            Hud.showBanner(title, restartPrompt, nil)
        end
        if Input.isActionPressed("Jump") then
            endedFor = nil
            Scene.reload()
        end
    end
end

function onUpdate(dt)
    lastDt = dt
    if slideLegsPrefab ~= "" and not slideLegs then
        local feet = Transform.getPosition()
        slideLegs = Scene.instantiate(slideLegsPrefab, {
            position = { x = feet.x, y = feet.y, z = feet.z },
            vars = { forwardOffset = slideLegsForward, rightOffset = slideLegsRight, belowEye = slideLegsBelowEye,
                     yawOffsetDegrees = slideLegsYawDegrees, hiddenDrop = slideLegsDrop,
                     pullBackAtDown = slideLegsPullBackAtDown },
        }) or false
    end
    Registry.clock = (Registry.clock or 0) + dt
    Tracers.update(dt)
    updateGameEnd(dt)
    -- Escape is both Back and Pause: closing the developer menu with it must not open the in-game menu too.
    local devWasOpen = DevMenu ~= nil and DevMenu.open
    local devOpen = DevMenu and DevMenu.update({ enabled = not PauseMenu.open and not Registry.gameOver,
                                                enemyPrefab = devEnemyPrefab }, dt) or false
    PauseMenu.update({ enabled = pauseMenuEnabled and not Registry.gameOver and not devOpen and not devWasOpen,
                       mainMenuScene = mainMenuScene, allowRestart = pauseMenuRestart, freezeTime = pauseFreezesTime })
    local menuOpen = PauseMenu.open or devOpen
    local holdFire = PauseMenu.suppressFire or (DevMenu ~= nil and DevMenu.suppressFire)
    -- With the game frozen: no look, movement or firing (mouse look does not depend on dt).
    if Time.getScale() == 0 then
        Character.move(0, 0)
        return
    end
    -- The menu takes the mouse and keys; the game goes on, so this player just stands still meanwhile.
    if not menuOpen then
        applyLook(dt)
    end
    Character.setFacingYaw(yaw)
    applyCameraRotation()
    Registry.player.yaw = yaw
    Registry.player.pitch = pitch
    -- What enemies read to judge how visible the player is.
    local velocity = Character.getVelocity()
    Registry.player.speed = velocity and math.sqrt((velocity.x or 0) ^ 2 + (velocity.z or 0) ^ 2) or 0
    Registry.player.stance = Character.isProne() and "prone" or (Character.isCrouched() and "crouched" or "standing")
    Registry.player.eyeHeight = viewHeight
    Registry.player.slideBlend = slideBlend
    local row = activeWeaponRow()
    if lookEnabled and inputEnabled and not menuOpen then
        local basis = Logic.yawBasis(yaw)
        local forward = Input.getAxis("MoveForward") or 0
        local right = Input.getAxis("MoveRight") or 0
        local wishX = basis.forward.x * forward + basis.right.x * right
        local wishZ = basis.forward.z * forward + basis.right.z * right
        local crouchHeld = Input.isActionDown("Crouch")
        local jumpPressed = Input.isActionPressed("Jump")
        -- Prone toggles; Crouch, Jump or Sprint get up from it (Jump stands without jumping).
        if proneEnabled and Input.isActionPressed("Prone") then
            proneWanted = not proneWanted
        elseif proneWanted and (Input.isActionPressed("Crouch") or jumpPressed or Input.isActionPressed("Sprint")) then
            proneWanted = false
        end
        Character.setProne(proneEnabled and proneWanted)
        local prone = Character.isProne()
        if prone then
            jumpPressed = false
        end
        commands.fireHeld = Input.isActionDown("Fire") and not holdFire
        commands.firePressed = Input.isActionPressed("Fire") and not holdFire
        commands.aimHeld = Input.isActionDown("Aim")
        slideCooldownRemaining = math.max(0, slideCooldownRemaining - dt)
        -- Pressing crouch while sprinting on the ground drops into a slide along the way the player is moving.
        if not slide and slideEnabled and sprinting and Input.isActionPressed("Crouch") and Character.isGrounded()
            and slideCooldownRemaining <= 0 then
            local vel = Character.getVelocity() or {}
            local vx, vz = vel.x or 0, vel.z or 0
            local length = math.sqrt(vx * vx + vz * vz)
            if length < 0.5 then
                vx, vz, length = basis.forward.x, basis.forward.z, 1
            end
            local runScale = tactical and tacticalSprintMultiplier or sprintMultiplier
            slide = { elapsed = 0, dirX = vx / length, dirZ = vz / length, startScale = runScale * slideBoost }
        end
        -- Jumping (or holding jump in the air) towards a ledge climbs onto it or vaults over it instead.
        if not mantle and mantleEnabled and forward > 0.3 and not prone
            and (jumpPressed or (not Character.isGrounded() and Input.isActionDown("Jump"))) then
            local plan = Mantle.find(Transform.getPosition(), basis.forward.x, basis.forward.z, {
                reach = mantleReach, maxHeight = mantleMaxHeight, vaultMaxHeight = vaultMaxHeight,
            })
            if plan then
                mantle = { plan = plan, elapsed = 0, seconds = Mantle.duration(plan, mantleSeconds, mantleSecondsPerMetre) }
                slide = nil
                jumpPressed = false
            end
        end
        if mantle then
            mantle.elapsed = mantle.elapsed + dt
            local p = Mantle.position(mantle.plan, mantle.elapsed / mantle.seconds)
            Character.move(0, 0)
            Character.setCrouched(false)
            Character.teleport(p.x, p.y, p.z)
            sprinting = false
            if mantle.elapsed >= mantle.seconds then
                mantle = nil
            end
        elseif slide then
            slide.elapsed = slide.elapsed + dt
            -- The slide keeps its heading; move input only turns it slowly.
            local steer = math.min(1, slideSteer * dt)
            local dx, dz = slide.dirX + wishX * steer, slide.dirZ + wishZ * steer
            local length = math.sqrt(dx * dx + dz * dz)
            if length > 1e-4 then
                slide.dirX, slide.dirZ = dx / length, dz / length
            end
            Character.move(slide.dirX, slide.dirZ)
            Character.setCrouched(true)
            Character.setSpeedMultiplier(Logic.slideSpeedScale(slide.elapsed, slideSeconds, slide.startScale,
                                                               crouchSpeedScale))
            sprinting = false
            if jumpPressed or slide.elapsed >= slideSeconds or not Character.isGrounded() then
                slide = nil
                slideCooldownRemaining = slideCooldown
                if jumpPressed then
                    Character.setCrouched(false)
                    Character.jump()
                end
            end
        else
            Character.move(wishX, wishZ)
            if jumpPressed then
                Character.jump()
            end
            Character.setCrouched(crouchHeld)
            local crouched = crouchHeld or Character.isCrouched() or Character.isStandBlocked()
            -- Firing or aiming ends a sprint; the weapon then waits its sprint-to-fire time.
            local wasSprinting = sprinting
            sprinting = Logic.sprintAllowed(forward, crouched or prone) and Input.isActionDown("Sprint")
                and not commands.fireHeld and not commands.aimHeld
            -- Double-tapping Sprint while running starts a tactical sprint, which lasts while its charge does.
            if wasSprinting and not sprinting then
                sinceSprintEnded = 0
            elseif not sprinting then
                sinceSprintEnded = sinceSprintEnded + dt
            end
            if sprinting and Input.isActionPressed("Sprint") and (wasSprinting or sinceSprintEnded <= 0.3)
                and tacticalSprintEnabled and tacticalCharge >= tacticalSprintSeconds * 0.25 then
                tactical = true
            end
            tactical, tacticalCharge = Logic.stepTacticalSprint(tactical and sprinting, tacticalCharge,
                                                                tacticalSprintSeconds, tacticalSprintRechargeSeconds, dt)
            local adsMove = row and Gunplay.lerp(1, row.adsMoveMultiplier or 1, row.adsBlend or 0) or 1
            local scale = sprinting and (tactical and tacticalSprintMultiplier or sprintMultiplier)
                or adsMove * Logic.directionSpeedScale(forward, right, strafeSpeedScale, backpedalSpeedScale)
                    * (prone and proneSpeedScale or (crouched and crouchSpeedScale or 1))
            if sinceHardLanding then
                sinceHardLanding = sinceHardLanding + dt
                scale = scale * Logic.landingSpeedScale(sinceHardLanding, landingSlowSeconds, landingSlowScale)
            end
            Character.setSpeedMultiplier(scale)
        end
        if slide or mantle then
            tactical = false
        end
        -- Leaning holds while its key is down, except while running, sliding or climbing.
        leanTarget = 0
        if leanEnabled and not sprinting and not slide and not mantle then
            leanTarget = (Input.isActionDown("LeanRight") and 1 or 0) - (Input.isActionDown("LeanLeft") and 1 or 0)
        end
        slideBlend = Gunplay.approach(slideBlend, slide and 1 or 0, dt / 0.12)
        tacticalBlend = Gunplay.approach(tacticalBlend, tactical and 1 or 0, dt / 0.25)
        commands.reload = Input.isActionPressed("Reload")
        commands.interact = Input.isActionPressed("Interact")
        if Input.isActionPressed("NextWeapon") then
            commands.next = true
        end
        if Input.isActionPressed("PreviousWeapon") then
            commands.prev = true
        end
        for slot = 1, 4 do
            if Input.isActionPressed("Weapon" .. tostring(slot)) then
                commands.slot = slot
            end
        end
        commands.fragHeld = Input.isActionDown("ThrowFrag")
        commands.smokeHeld = Input.isActionDown("ThrowSmoke")
        if Input.isActionPressed("ThrowFrag") then
            commands.throwKind = "frag"
        elseif Input.isActionPressed("ThrowSmoke") then
            commands.throwKind = "smoke"
        end
    else
        Character.move(0, 0)
        Character.setSpeedMultiplier(1)
        sprinting = false
        slide = nil
        mantle = nil
        tactical = false
        leanTarget = 0
        commands.fireHeld = false
        commands.firePressed = false
        commands.aimHeld = false
        commands.reload = false
        commands.interact = false
        commands.cancelThrow = true
    end
end

function onLateUpdate(dt)
    local prone = Character.isProne()
    local crouched = Character.isCrouched() or Character.isStandBlocked()
    local target = prone and proneEyeHeight or (crouched and crouchEyeHeight or standEyeHeight)
    -- Going down to or up from prone is slower than crouching.
    local lying = prone or eyeHeight < crouchEyeHeight - 0.01
    local speed = (lying and proneEyeBlendMetersPerSecond or eyeBlendMetersPerSecond) * dt
    if eyeHeight < target then
        eyeHeight = math.min(target, eyeHeight + speed)
    elseif eyeHeight > target then
        eyeHeight = math.max(target, eyeHeight - speed)
    end
    viewHeight = eyeHeight
    if slideCameraHeight > 0 and slideBlend > 0 then
        local eased = slideBlend * slideBlend * (3 - 2 * slideBlend)
        viewHeight = eyeHeight + (math.min(slideCameraHeight, eyeHeight) - eyeHeight) * eased
    end
    local feet = Transform.getPosition()
    lean = Gunplay.approach(lean, leanTarget, dt / math.max(leanSeconds, 0.01))
    leanOffset = Logic.leanOffset(lean, leanDistance, function(side, distance)
        local right = Logic.yawBasis(yaw).right
        local hit = Physics.raycast(feet.x, feet.y + viewHeight, feet.z, right.x * side, 0, right.z * side,
                                    distance, nil, true)
        return hit and hit.distance or nil
    end)
    -- The player turns with yaw, so the camera's local +z is the player's right.
    Camera.setPosition(0, viewHeight, leanOffset)

    local origin, direction = Logic.aimRay(feet, viewHeight, yaw, pitch)
    local leanRight = Logic.yawBasis(yaw).right
    origin.x, origin.z = origin.x + leanRight.x * leanOffset, origin.z + leanRight.z * leanOffset

    local vel = Character.getVelocity()
    local grounded = Character.isGrounded()
    local speedXZ = vel and math.sqrt((vel.x or 0) ^ 2 + (vel.z or 0) ^ 2) or 0
    local player = Registry.player
    updateFootsteps(speedXZ, grounded and not slide, dt)
    local landingSpeed = 0
    if not grounded then
        fallSpeed = math.max(fallSpeed, -((vel and vel.y) or 0))
    elseif not wasGrounded then
        landingSpeed = fallSpeed
        if landingSpeed >= landingSlowMinSpeed then
            sinceHardLanding = 0
        end
        fallSpeed = 0
        if player and landingSpeed > 2 then
            player.cameraImpulses = player.cameraImpulses or {}
            table.insert(player.cameraImpulses, { pitch = -math.min(math.rad(1.2), landingSpeed * math.rad(0.12)) })
        end
    end
    wasGrounded = grounded
    local basis = Logic.yawBasis(yaw)
    local localForward = vel and ((vel.x or 0) * basis.forward.x + (vel.z or 0) * basis.forward.z) or 0
    local localRight = vel and ((vel.x or 0) * basis.right.x + (vel.z or 0) * basis.right.z) or 0

    if not discoverWeapons() then
        return
    end
    if not activeWeaponRow() and not swap and #Registry.weaponOrder > 0 then
        equipNow(1)
    end
    if commands.next then
        requestSlot(Logic.wrapSlot(activeSlot(), #Registry.weaponOrder, 1))
        commands.next = false
    end
    if commands.prev then
        requestSlot(Logic.wrapSlot(activeSlot(), #Registry.weaponOrder, -1))
        commands.prev = false
    end
    if commands.slot then
        requestSlot(commands.slot)
        commands.slot = nil
    end
    if swap then
        swap.remaining = swap.remaining - dt
        if swap.remaining <= 0 then
            equipNow(swap.index)
            swap = nil
        end
    end

    local targetRow = Interactable.query(interactionHit(origin, direction), interactDistance)
    if commands.interact and targetRow and targetRow.entity then
        targetRow.entity:Call("onInteract", this)
        commands.interact = false
    end

    local row = activeWeaponRow()
    updateThrow(lastDt, origin, direction, row)
    if player then
        player.input = {
            aimHeld = commands.aimHeld and swap == nil and throw == nil and mantle == nil,
            sprinting = sprinting,
            tacticalSprint = tactical,
            prone = Character.isProne(),
            vx = vel and vel.x or 0,
            vy = vel and vel.y or 0,
            vz = vel and vel.z or 0,
            moveFraction = Gunplay.clamp(speedXZ / math.max(fullSpreadMoveSpeed, 0.1), 0, 1),
            airborne = not grounded,
            lookPitchDelta = lookPitchDelta,
            lookYawRate = lookYawRate,
            lookPitchRate = lookPitchRate,
            localForwardSpeed = localForward,
            localRightSpeed = localRight,
            landingSpeed = landingSpeed,
            footfall = footfallThisFrame,
            motionScale = presentationEnabled and Gunplay.clamp(viewBobScale * Settings.values.viewBob, 0, 1) or 0,
            -- Footfalls land on whole multiples of pi.
            bobPhase = math.pi * (stepCount + stepDistance / math.max(footstepStride, 0.1)),
            lowering = swap ~= nil or throw ~= nil or mantle ~= nil,
            lowerProgress = swap and row and
                Gunplay.clamp(1 - swap.remaining / math.max(row.lowerSeconds or 0.01, 0.01), 0, 1)
                or (mantle and math.sin(math.pi * math.min(1, mantle.elapsed / mantle.seconds)) * 0.7)
                or throwLowered(),
        }
    end
    local weaponEntity = activeWeaponEntity()
    if weaponEntity then
        local blocked = swap ~= nil or throw ~= nil or mantle ~= nil
        weaponEntity:Call("tick", lastDt, (commands.fireHeld and not blocked) and 1 or 0,
                          (commands.firePressed and not blocked) and 1 or 0,
                          commands.reload and 1 or 0, origin.x, origin.y, origin.z,
                          direction.x, direction.y, direction.z, this)
    end
    commands.firePressed = false
    commands.reload = false
    updateFieldOfView(row)
    updateCameraKick(lastDt)
    applyCameraRotation()

    if player and player.hitEvent then
        Hud.showMarker(player.hitEvent)
        player.hitEvent = nil
    end
    Hud.refresh({
        enabled = hudEnabled,
        showCrosshair = showCrosshair,
        showHealth = showHealth,
        showAmmo = showAmmo,
        showPrompt = showPrompt,
        health = Health.getHealth(this),
        maxHealth = Health.getMaxHealth(this),
        weapon = Registry.weaponSnapshot(weaponEntity),
        prompt = Interactable.prompt(),
        adsBlend = row and row.adsBlend or 0,
        grenades = (fragGrenadePrefab ~= "" or smokeGrenadePrefab ~= "") and grenades or nil,
        dt = lastDt,
    })
    warningClock = warningClock + lastDt
    local warnings = {}
    if grenadeWarningEnabled then
        local live = {}
        for _, g in pairs(Registry.liveGrenades) do
            live[#live + 1] = g
        end
        warnings = GrenadeWarning.compute(Transform.getPosition(), yaw, Health.getHealth(this), live,
                                          grenadeWarningRange, Hud.MAX_GRENADE_WARNINGS)
    end
    Hud.showGrenadeWarnings(warnings, { icon = grenadeWarningIcon, arrowPattern = grenadeWarningArrowPattern,
                                        arrowSteps = math.max(1, grenadeWarningArrowSteps),
                                        radius = grenadeWarningRadius, iconSize = grenadeWarningIconSize,
                                        time = warningClock })
    local hits = damageIndicatorEnabled and DamageDirection.compute(Transform.getPosition(), yaw, Registry.playerHits,
        Registry.clock or 0, damageIndicatorSeconds, Hud.MAX_DAMAGE_ARROWS) or {}
    Hud.showDamageDirections(hits, { arrowPattern = grenadeWarningArrowPattern,
                                     arrowSteps = math.max(1, grenadeWarningArrowSteps),
                                     radius = damageIndicatorRadius, size = damageIndicatorSize })
end

function onHealthChanged(current, maximum)
    if current < lastKnownHealth then
        Hud.flashDamage()
    end
    lastKnownHealth = current
end

-- Called by enemies for every shot at the player. A hit plays a hit sound. A miss that strikes the level near the
-- head plays an impact where it struck; otherwise one that passes close plays a ricochet where it passed, so one
-- bullet never makes both. Each kind plays at most once per its interval.
local function playRandom(kind, list, options)
    local index = BulletSounds.pick(#list, bulletSounds.last[kind], math.random())
    if index then
        bulletSounds.last[kind] = index
        Audio.play(list[index], options)
    end
end

function onIncomingShot(ox, oy, oz, dx, dy, dz, length, hit, hitSurface)
    if not (Audio and Audio.isAvailable and Audio.isAvailable()) then
        return
    end
    if hit then
        playRandom("hit", bulletSounds.hits, { volume = bulletHitVolume, bus = "sfx", priority = 4, pitchJitter = 0.04,
                                               volumeJitter = 0.08 })
        return
    end
    local clock = Registry.clock or 0
    local feet = Transform.getPosition()
    local head = { x = feet.x, y = feet.y + viewHeight, z = feet.z }
    if hitSurface and #bulletSounds.impacts > 0 then
        local at = { x = ox + dx * length, y = oy + dy * length, z = oz + dz * length }
        local ax, ay, az = at.x - head.x, at.y - head.y, at.z - head.z
        if ax * ax + ay * ay + az * az <= envImpactRadius * envImpactRadius then
            if clock - bulletSounds.impactAt >= envImpactInterval then
                bulletSounds.impactAt = clock
                playRandom("impact", bulletSounds.impacts, { volume = envImpactVolume, bus = "sfx", priority = 3,
                                                             pitchJitter = 0.06, volumeJitter = 0.1, position = at,
                                                             minDistance = 1, maxDistance = 30 })
            end
            return
        end
    end
    if #bulletSounds.misses > 0 and clock - bulletSounds.missAt >= nearMissInterval then
        local at = BulletSounds.nearMiss({ x = ox, y = oy, z = oz }, dx, dy, dz, length, head, nearMissRadius)
        if at then
            bulletSounds.missAt = clock
            playRandom("miss", bulletSounds.misses, { volume = nearMissVolume, bus = "sfx", priority = 3,
                                                      pitchJitter = 0.06, volumeJitter = 0.1, position = at,
                                                      minDistance = 1, maxDistance = 25 })
        end
    end
end

function setHudSuppressed(suppressed)
    Hud.setSuppressed(suppressed)
end

function onDeath(instigator)
    Player.setInputEnabled(false)
    Hud.setSuppressed(true)
    Hud.setDeathFade(true)
    Hud.showBanner("You died", "", nil)
    -- In a match the player comes back (see respawn) instead of the game ending.
    local director = Registry.matchDirector
    if director and director:IsValid() then
        director:Call("onCombatantDeath", this, instigator)
        return
    end
    Registry.gameOver = Registry.gameOver or "dead"
end

-- Brings the player back for a match: full health, ammo and grenades, standing at (x, y, z) facing facingYaw.
function respawn(x, y, z, facingYaw)
    Health.revive(this)
    Character.setProne(false)
    Character.setCrouched(false)
    Character.teleport(x, y, z)
    yaw, pitch = facingYaw or yaw, 0
    slide, mantle, throw, proneWanted, tactical, sprinting = nil, nil, nil, false, false, false
    grenades.frag = math.max(0, math.floor(fragGrenades))
    grenades.smoke = math.max(0, math.floor(smokeGrenades))
    for _, row in pairs(Registry.weapons) do
        row.reserve = row.maxReserve or row.reserve
        row.reloadRemaining = 0
        Logic.fillWeapon(row)
    end
    lastKnownHealth = Health.getHealth(this)
    Hud.setDeathFade(false)
    Hud.setSuppressed(false)
    Hud.clearBanner()
    Player.setInputEnabled(true)
    Player.setLookEnabled(true)
end

function onDestroy()
    if slideLegs and slideLegs:IsValid() then
        Scene.destroy(slideLegs)
    end
    Health.detach(this)
    Hud.unmount()
    Registry.clearPlayer()
end

return Player
