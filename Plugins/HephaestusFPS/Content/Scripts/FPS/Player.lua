local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Health = require("FPS.Health")
local Interactable = require("FPS.Interactable")
local Hud = require("FPS.Hud")

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
Expose.number("interactDistance", 2.5)
Expose.number("maxHealth", 100)
Expose.bool("hudEnabled", true)
Expose.bool("showCrosshair", true)
Expose.bool("showHealth", true)
Expose.bool("showAmmo", true)
Expose.bool("showPrompt", true)
Expose.string("hudDocument", "plugin://com.hephaestus.fps/UI/FPSHud.hui")
Expose.bool("presentationEnabled", true)

local Player = {}
local yaw = 0
local pitch = 0
local lookEnabled = true
local inputEnabled = true
local eyeHeight = 1.62
local lastDt = 1 / 60
local commands = { fireHeld = false, firePressed = false, reload = false, interact = false }
local discoverTries = 0
local lastLookX = 0
local lastLookY = 0
local lastKnownHealth = 0

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

local function applyLook(dt)
    local mouse = Input.getMouseDelta()
    local mx = (mouse and mouse.x) or 0
    local my = (mouse and mouse.y) or 0
    lastLookX = mx
    lastLookY = my
    if not lookEnabled or not inputEnabled then
        return
    end
    local invertXScale = invertX and -1 or 1
    local invertYScale = invertY and -1 or 1
    yaw = yaw - mx * mouseSensitivityX * invertXScale
    pitch = pitch - my * mouseSensitivityY * invertYScale
    local stickX = Input.getAxis("LookStickX") or 0
    local stickY = Input.getAxis("LookStickY") or 0
    yaw = yaw - stickX * stickSensitivityX * invertXScale * dt
    pitch = pitch - stickY * stickSensitivityY * invertYScale * dt
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
    local row = Registry.activeWeapon()
    return row and row.entity or nil
end

local function equipIndex(index)
    local count = #Registry.weaponOrder
    local selected = Logic.selectSlot(index, count)
    if not selected then
        return
    end
    local previous = activeWeaponEntity()
    if previous then
        previous:Call("unequip")
    end
    local key = Registry.weaponOrder[selected]
    local row = Registry.weapons[key]
    if row and row.entity then
        row.entity:Call("equip")
    end
end

function onAwake()
    Registry.ensureSession()
    local rotation = Transform.getRotation()
    yaw = rotation.y
    pitch = 0
    inputEnabled = true
    discoverTries = 0
    commands = { fireHeld = false, firePressed = false, reload = false, interact = false }
    Health.attach(this, { maxHealth = maxHealth })
    Registry.setPlayer(this, {
        yaw = 0,
        pitch = 0,
        lookEnabled = true,
        inputEnabled = true,
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
    Camera.setRotation(pitch, 0, 0)
    Registry.player.yaw = yaw
    Registry.player.pitch = pitch
    local wishX, wishZ = 0, 0
    if lookEnabled and inputEnabled then
        local basis = Logic.yawBasis(yaw)
        local forward = Input.getAxis("MoveForward") or 0
        local right = Input.getAxis("MoveRight") or 0
        wishX = basis.forward.x * forward + basis.right.x * right
        wishZ = basis.forward.z * forward + basis.right.z * right
        Character.move(wishX, wishZ)
        if Input.isActionPressed("Jump") then
            Character.jump()
        end
        local crouchHeld = Input.isActionDown("Crouch")
        Character.setCrouched(crouchHeld)
        local crouched = crouchHeld or Character.isCrouched() or Character.isStandBlocked()
        if Logic.sprintAllowed(forward, crouched) and Input.isActionDown("Sprint") then
            Character.setSpeedMultiplier(sprintMultiplier)
        else
            Character.setSpeedMultiplier(1)
        end
        commands.fireHeld = Input.isActionDown("Fire")
        commands.firePressed = Input.isActionPressed("Fire")
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
    else
        Character.move(0, 0)
        Character.setSpeedMultiplier(1)
        commands.fireHeld = false
        commands.firePressed = false
        commands.reload = false
        commands.interact = false
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
    Camera.setRotation(pitch, 0, 0)

    local feet = Transform.getPosition()
    local origin, direction = Logic.aimRay(feet, eyeHeight, yaw, pitch)

    if presentationEnabled then
        local vel = Character.getVelocity()
        local speedXZ = 0
        if vel and Character.isGrounded() then
            speedXZ = math.sqrt((vel.x or 0) ^ 2 + (vel.z or 0) ^ 2)
        end
        local bob = math.sin((Registry.player and Registry.player.bob or 0) + dt * 8) * math.min(speedXZ, 5) * 0.002
        if Registry.player then
            Registry.player.bob = (Registry.player.bob or 0) + dt * speedXZ
        end
        -- Viewmodel offsets live on the weapon entity inside Weapon.tick / recoil.
        Registry.player.bobOffset = bob
        Registry.player.lastLookX = lastLookX
    end

    if not discoverWeapons() then
        return
    end
    if not Registry.activeWeapon() and #Registry.weaponOrder > 0 then
        equipIndex(1)
    end
    if commands.next then
        local current = 1
        local active = Registry.activeWeapon()
        if active then
            for i, key in ipairs(Registry.weaponOrder) do
                if key == active.key then
                    current = i
                    break
                end
            end
        end
        equipIndex(Logic.wrapSlot(current, #Registry.weaponOrder, 1))
        commands.next = false
    end
    if commands.prev then
        local current = 1
        local active = Registry.activeWeapon()
        if active then
            for i, key in ipairs(Registry.weaponOrder) do
                if key == active.key then
                    current = i
                    break
                end
            end
        end
        equipIndex(Logic.wrapSlot(current, #Registry.weaponOrder, -1))
        commands.prev = false
    end
    if commands.slot then
        equipIndex(commands.slot)
        commands.slot = nil
    end

    local hit = Physics.raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
                                interactDistance, "Character", false)
    local targetRow = Interactable.query(hit, interactDistance)
    if commands.interact and targetRow and targetRow.entity then
        targetRow.entity:Call("onInteract", this)
        commands.interact = false
    end

    local weaponEntity = activeWeaponEntity()
    if weaponEntity then
        weaponEntity:Call("tick", lastDt, commands.fireHeld and 1 or 0, commands.firePressed and 1 or 0,
                          commands.reload and 1 or 0, origin.x, origin.y, origin.z,
                          direction.x, direction.y, direction.z, this)
    end
    commands.firePressed = false
    commands.reload = false

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
