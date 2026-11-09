local Logic = {}

function Logic.clampPitch(pitch, minRadians, maxRadians)
    if pitch < minRadians then
        return minRadians
    end
    if pitch > maxRadians then
        return maxRadians
    end
    return pitch
end

-- Facing yaw is Transform.rotation.y / Character.setFacingYaw.
-- ActiveCameraSync maps parent yaw θ to render yaw -θ. Planar forward matches
-- RenderForward(-yaw, 0) = (cos(yaw), 0, -sin(yaw)).
function Logic.yawBasis(yaw)
    return {
        forward = { x = math.cos(yaw), y = 0, z = -math.sin(yaw) },
        right = { x = math.sin(yaw), y = 0, z = math.cos(yaw) },
    }
end

function Logic.aimRay(feet, eyeHeight, yaw, pitch)
    local cosPitch = math.cos(pitch)
    local origin = {
        x = feet.x,
        y = feet.y + eyeHeight,
        z = feet.z,
    }
    local direction = {
        x = math.cos(yaw) * cosPitch,
        y = math.sin(pitch),
        z = -math.sin(yaw) * cosPitch,
    }
    return origin, direction
end

function Logic.sprintAllowed(forwardAxis, crouched)
    return (forwardAxis or 0) > 0.5 and not crouched
end

function Logic.tickFire(state, dt, held, pressed)
    state.now = (state.now or 0) + dt
    state.nextFire = state.nextFire or 0
    local isHeld = held == true or held == 1
    local isPressed = pressed == true or pressed == 1
    if (state.reloadRemaining or 0) > 0 or (state.magazine or 0) <= 0
        or (state.fireBlockedRemaining or 0) > 0
        or (state.fireMode == "auto" and not isHeld) then
        state.nextFire = math.max(state.nextFire, state.now)
        return 0
    end
    local rps = state.roundsPerSecond or 1
    if rps <= 0 then
        rps = 1
    end
    local interval = 1 / rps
    local shots = 0
    if state.fireMode == "auto" then
        while isHeld and state.now >= state.nextFire and shots < 4 do
            shots = shots + 1
            state.nextFire = state.nextFire + interval
        end
        if shots >= 4 and state.now >= state.nextFire then
            state.nextFire = state.now + interval
        end
    elseif isPressed and state.now >= state.nextFire then
        shots = 1
        state.nextFire = state.now + interval
    end
    return shots
end

function Logic.tryStartReload(state)
    if (state.reloadRemaining or 0) > 0 then
        return false
    end
    if (state.magazine or 0) >= (state.magazineSize or 0) then
        return false
    end
    if (state.reserve or 0) <= 0 then
        return false
    end
    local seconds = state.reloadSeconds or 0
    if (state.magazine or 0) <= 0 and state.reloadEmptySeconds then
        seconds = state.reloadEmptySeconds
    end
    state.reloadRemaining = seconds
    state.reloadTotal = seconds
    state.reloadFromEmpty = (state.magazine or 0) <= 0
    return true
end

function Logic.advanceReload(state, dt)
    if (state.reloadRemaining or 0) <= 0 then
        return false
    end
    state.reloadRemaining = state.reloadRemaining - dt
    if state.reloadRemaining > 0 then
        return false
    end
    state.reloadRemaining = 0
    local space = (state.magazineSize or 0) - (state.magazine or 0)
    local moved = math.min(space, state.reserve or 0)
    state.magazine = (state.magazine or 0) + moved
    state.reserve = (state.reserve or 0) - moved
    return true
end

function Logic.spendShot(state)
    if (state.magazine or 0) <= 0 then
        return false
    end
    state.magazine = state.magazine - 1
    return true
end

function Logic.wrapSlot(index, count, delta)
    if count <= 0 then
        return 1
    end
    local nextIndex = ((index - 1 + delta) % count) + 1
    if nextIndex < 1 then
        nextIndex = nextIndex + count
    end
    return nextIndex
end

function Logic.selectSlot(index, count)
    if index < 1 or index > count then
        return nil
    end
    return index
end

function Logic.pickInteract(hit, entries, fallbackMax)
    if not hit or not hit.key then
        return nil
    end
    local entry = entries[hit.key]
    if not entry or entry.enabled == false then
        return nil
    end
    local maxDistance = entry.maxDistance or fallbackMax or 2.5
    if (hit.distance or 0) > maxDistance then
        return nil
    end
    return hit.key
end

function Logic.createHealth(maxHealth)
    return {
        maxHealth = maxHealth or 100,
        health = maxHealth or 100,
        dead = false,
        invulnerable = false,
        deathCount = 0,
    }
end

function Logic.applyDamage(state, amount)
    if not state or state.dead or state.invulnerable or (amount or 0) <= 0 then
        return false
    end
    state.health = state.health - amount
    if state.health <= 0 then
        state.health = 0
        if not state.dead then
            state.dead = true
            state.deathCount = (state.deathCount or 0) + 1
            return true
        end
    end
    return false
end

function Logic.heal(state, amount)
    if not state or state.dead or (amount or 0) <= 0 then
        return
    end
    state.health = math.min(state.maxHealth or 0, (state.health or 0) + amount)
end

return Logic
