local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Gunplay = require("FPS.Gunplay")
local Health = require("FPS.Health")
local Interactable = require("FPS.Interactable")
local Hud = require("FPS.Hud")
local Impacts = require("FPS.Impacts")
local ThrowPreview = require("FPS.ThrowPreview")

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
Expose.number("eyeBlendMetersPerSecond", 8)
Expose.number("sprintMultiplier", 1.6)
Expose.number("strafeSpeedScale", 0.85)
Expose.number("backpedalSpeedScale", 0.7)
Expose.number("crouchSpeedScale", 0.6)
Expose.number("landingSlowScale", 0.6)
Expose.number("landingSlowSeconds", 0.3)
Expose.number("landingSlowMinSpeed", 4)
Expose.number("fieldOfView", 80)
Expose.number("fullSpreadMoveSpeed", 5.0)
Expose.number("interactDistance", 2.5)
Expose.number("maxHealth", 100)
Expose.bool("hudEnabled", true)
Expose.bool("showCrosshair", true)
Expose.bool("showHealth", true)
Expose.bool("showAmmo", true)
Expose.bool("showPrompt", true)
Expose.string("hudDocument", "plugin://com.hephaestus.fps/UI/FPSHud.hui")
Expose.bool("presentationEnabled", true)
Expose.number("viewBobScale", 1.0)
Expose.number("cameraShakeScale", 1.0)
Expose.string("footstepSounds", "")
Expose.number("footstepStride", 2.1)
Expose.number("footstepVolume", 0.5)
Expose.string("impactDecalPrefabs", "")
Expose.number("maxImpactDecals", 64)
Expose.string("impactDustPrefab", "")
Expose.string("impactSparksPrefab", "")
Expose.string("fragGrenadePrefab", "")
Expose.string("smokeGrenadePrefab", "")
Expose.number("fragGrenades", 2)
Expose.number("smokeGrenades", 2)
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
local pitch = 0
local lookEnabled = true
local inputEnabled = true
local eyeHeight = 1.62
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
local fallSpeed = 0
local footfallThisFrame = false
local stepCount = 0
local lastKnownHealth = 0
local sprinting = false
local swap = nil
local stepDistance = 0
local stepIndex = 0
local footsteps = {}
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
        local dYaw = -(mx * mouseSensitivityX + stickX * stickSensitivityX * dt) * invertXScale * adsScale
        lookPitchDelta = -(my * mouseSensitivityY + stickY * stickSensitivityY * dt) * invertYScale * adsScale
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
    local shake = presentationEnabled and Gunplay.clamp(cameraShakeScale, 0, 1) or 0
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
    Camera.setRotation(pitch + cameraKickPitch.x, cameraKickYaw.x, 0)
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
        cameraObject:SetFov(Gunplay.lerp(fieldOfView, row and row.adsFov or fieldOfView, ads))
    end
end

function onAwake()
    Registry.ensureSession()
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
    cameraKickPitch = { x = 0, v = 0 }
    cameraKickYaw = { x = 0, v = 0 }
    footsteps = parseList(footstepSounds)
    Impacts.configure(parseList(impactDecalPrefabs), maxImpactDecals,
                      { dust = impactDustPrefab, sparks = impactSparksPrefab })
    commands = { fireHeld = false, firePressed = false, reload = false, interact = false }
    Health.attach(this, { maxHealth = maxHealth })
    Registry.setPlayer(this, {
        yaw = 0,
        pitch = 0,
        lookEnabled = true,
        inputEnabled = true,
        pendingPitch = 0,
        pendingYaw = 0,
        bob = 0,
        input = {},
    })
    eyeHeight = standEyeHeight
    lastKnownHealth = Health.getHealth(this)
    Camera.setActive(true)
    Player.setLookEnabled(true)
    Hud.mount(hudDocument, hudEnabled)
end

function onUpdate(dt)
    lastDt = dt
    applyLook(dt)
    Character.setFacingYaw(yaw)
    applyCameraRotation()
    Registry.player.yaw = yaw
    Registry.player.pitch = pitch
    local row = activeWeaponRow()
    if lookEnabled and inputEnabled then
        local basis = Logic.yawBasis(yaw)
        local forward = Input.getAxis("MoveForward") or 0
        local right = Input.getAxis("MoveRight") or 0
        Character.move(basis.forward.x * forward + basis.right.x * right,
                       basis.forward.z * forward + basis.right.z * right)
        if Input.isActionPressed("Jump") then
            Character.jump()
        end
        local crouchHeld = Input.isActionDown("Crouch")
        Character.setCrouched(crouchHeld)
        local crouched = crouchHeld or Character.isCrouched() or Character.isStandBlocked()
        commands.fireHeld = Input.isActionDown("Fire")
        commands.firePressed = Input.isActionPressed("Fire")
        commands.aimHeld = Input.isActionDown("Aim")
        -- Firing or aiming ends a sprint; the weapon then waits its sprint-to-fire time.
        sprinting = Logic.sprintAllowed(forward, crouched) and Input.isActionDown("Sprint")
            and not commands.fireHeld and not commands.aimHeld
        local adsMove = row and Gunplay.lerp(1, row.adsMoveMultiplier or 1, row.adsBlend or 0) or 1
        local scale = sprinting and sprintMultiplier
            or adsMove * Logic.directionSpeedScale(forward, right, strafeSpeedScale, backpedalSpeedScale)
                * (crouched and crouchSpeedScale or 1)
        if sinceHardLanding then
            sinceHardLanding = sinceHardLanding + dt
            scale = scale * Logic.landingSpeedScale(sinceHardLanding, landingSlowSeconds, landingSlowScale)
        end
        Character.setSpeedMultiplier(scale)
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
        commands.fireHeld = false
        commands.firePressed = false
        commands.aimHeld = false
        commands.reload = false
        commands.interact = false
        commands.cancelThrow = true
    end
end

function onLateUpdate(dt)
    local crouched = Character.isCrouched() or Character.isStandBlocked()
    local target = crouched and crouchEyeHeight or standEyeHeight
    local speed = eyeBlendMetersPerSecond * dt
    if eyeHeight < target then
        eyeHeight = math.min(target, eyeHeight + speed)
    elseif eyeHeight > target then
        eyeHeight = math.max(target, eyeHeight - speed)
    end
    Camera.setPosition(0, eyeHeight, 0)

    local feet = Transform.getPosition()
    local origin, direction = Logic.aimRay(feet, eyeHeight, yaw, pitch)

    local vel = Character.getVelocity()
    local grounded = Character.isGrounded()
    local speedXZ = vel and math.sqrt((vel.x or 0) ^ 2 + (vel.z or 0) ^ 2) or 0
    local player = Registry.player
    updateFootsteps(speedXZ, grounded, dt)
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
            aimHeld = commands.aimHeld and swap == nil and throw == nil,
            sprinting = sprinting,
            moveFraction = Gunplay.clamp(speedXZ / math.max(fullSpreadMoveSpeed, 0.1), 0, 1),
            airborne = not grounded,
            lookPitchDelta = lookPitchDelta,
            lookYawRate = lookYawRate,
            lookPitchRate = lookPitchRate,
            localForwardSpeed = localForward,
            localRightSpeed = localRight,
            landingSpeed = landingSpeed,
            footfall = footfallThisFrame,
            motionScale = presentationEnabled and Gunplay.clamp(viewBobScale, 0, 1) or 0,
            -- Footfalls land on whole multiples of pi.
            bobPhase = math.pi * (stepCount + stepDistance / math.max(footstepStride, 0.1)),
            lowering = swap ~= nil or throw ~= nil,
            lowerProgress = swap and row and
                Gunplay.clamp(1 - swap.remaining / math.max(row.lowerSeconds or 0.01, 0.01), 0, 1) or throwLowered(),
        }
    end
    local weaponEntity = activeWeaponEntity()
    if weaponEntity then
        local blocked = swap ~= nil or throw ~= nil
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
end

function onHealthChanged(current, maximum)
    if current < lastKnownHealth then
        Hud.flashDamage()
    end
    lastKnownHealth = current
end

function setHudSuppressed(suppressed)
    Hud.setSuppressed(suppressed)
end

function onDeath()
    Player.setInputEnabled(false)
    Hud.setSuppressed(true)
end

function onDestroy()
    Health.detach(this)
    Hud.unmount()
    Registry.clearPlayer()
end

return Player
