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

-- Speed scale for the direction of the move input: 1 straight ahead, strafeScale sideways and backScale straight
-- back, blended by the angle so diagonals fall in between.
function Logic.directionSpeedScale(forwardAxis, rightAxis, strafeScale, backScale)
    local f, r = forwardAxis or 0, rightAxis or 0
    local length = math.sqrt(f * f + r * r)
    if length < 1e-4 then
        return 1
    end
    local along = f / length
    local towards = along >= 0 and 1 or backScale
    return strafeScale + (towards - strafeScale) * along * along
end

-- Speed scale through a slide: from startScale, easing down to endScale by the end of duration.
function Logic.slideSpeedScale(elapsed, duration, startScale, endScale)
    local t = duration > 0 and math.min(1, math.max(0, elapsed / duration)) or 1
    return startScale + (endScale - startScale) * t * t * (3 - 2 * t)
end

-- Speed scale after a hard landing: starts at slowScale and eases back to 1 over seconds.
function Logic.landingSpeedScale(elapsed, seconds, slowScale)
    if not elapsed or seconds <= 0 or elapsed >= seconds then
        return 1
    end
    local t = math.max(0, elapsed) / seconds
    return slowScale + (1 - slowScale) * t * t * (3 - 2 * t)
end

-- Rounds the weapon holds: the magazine plus a chambered round.
function Logic.loadedRounds(state)
    return (state.magazine or 0) + (state.chamber or 0)
end

-- A fresh weapon holds magazineSize rounds: one chambered and the rest in the magazine.
function Logic.fillWeapon(state)
    local size = state.magazineSize or 0
    state.chamber = size > 0 and 1 or 0
    state.magazine = math.max(0, size - 1)
end

function Logic.tickFire(state, dt, held, pressed)
    state.now = (state.now or 0) + dt
    state.nextFire = state.nextFire or 0
    local isHeld = held == true or held == 1
    local isPressed = pressed == true or pressed == 1
    if (state.reloadRemaining or 0) > 0 or (state.chamber or 0) <= 0
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

-- "tactical" swaps a magazine with a round chambered, "empty" also chambers one, "chamber" only racks
-- the action when a magazine is in but the chamber is empty (an empty reload interrupted after insert).
function Logic.reloadKind(state)
    local chamber = state.chamber or 0
    local magazine = state.magazine or 0
    if (state.reloadRemaining or 0) > 0 then
        return nil
    end
    if chamber <= 0 and magazine > 0 then
        return "chamber"
    end
    if (state.reserve or 0) <= 0 or magazine >= (state.magazineSize or 0) then
        return nil
    end
    return chamber > 0 and "tactical" or "empty"
end

-- Event times in seconds from the start of the action. Weapons without authored times get fixed fractions.
local function reloadPlan(state, kind)
    if kind == "tactical" then
        local t = state.tacticalReload or {}
        local seconds = t.seconds or state.reloadSeconds or 0
        return seconds, { { name = "detach", time = t.detach or seconds * 0.2 },
                          { name = "insert", time = t.insert or seconds * 0.55 } }
    end
    local e = state.emptyReload or {}
    local seconds = e.seconds or state.reloadEmptySeconds or state.reloadSeconds or 0
    local chamberTime = e.chamber or seconds * 0.8
    if kind == "chamber" then
        local lead = math.min(0.2, chamberTime)
        return seconds - chamberTime + lead, { { name = "chamber", time = lead } }
    end
    return seconds, { { name = "detach", time = e.detach or seconds * 0.2 },
                      { name = "insert", time = e.insert or seconds * 0.5 },
                      { name = "chamber", time = chamberTime } }
end

function Logic.tryStartReload(state)
    local kind = Logic.reloadKind(state)
    if not kind then
        return false
    end
    local seconds, events = reloadPlan(state, kind)
    state.reload = { kind = kind, events = events, nextEvent = 1 }
    state.reloadRemaining = seconds
    state.reloadTotal = seconds
    state.reloadElapsed = 0
    state.reloadFromEmpty = kind ~= "tactical"
    return true
end

local function applyReloadEvent(state, name)
    if name == "insert" then
        -- Rounds left in the old magazine go back to the reserve.
        state.reserve = (state.reserve or 0) + (state.magazine or 0)
        local taken = math.min(state.magazineSize or 0, state.reserve)
        state.magazine = taken
        state.reserve = state.reserve - taken
    elseif name == "chamber" and (state.chamber or 0) <= 0 and (state.magazine or 0) > 0 then
        state.magazine = state.magazine - 1
        state.chamber = 1
    end
end

-- Returns whether the action finished and the names of the events it crossed, in order.
function Logic.advanceReload(state, dt)
    local reload = state.reload
    if not reload or (state.reloadRemaining or 0) <= 0 then
        return false, {}
    end
    state.reloadElapsed = (state.reloadElapsed or 0) + dt
    local crossed = {}
    while reload.events[reload.nextEvent] and reload.events[reload.nextEvent].time <= state.reloadElapsed do
        local event = reload.events[reload.nextEvent]
        applyReloadEvent(state, event.name)
        crossed[#crossed + 1] = event.name
        reload.nextEvent = reload.nextEvent + 1
    end
    state.reloadRemaining = state.reloadTotal - state.reloadElapsed
    if state.reloadRemaining > 0 then
        return false, crossed
    end
    while reload.events[reload.nextEvent] do
        applyReloadEvent(state, reload.events[reload.nextEvent].name)
        crossed[#crossed + 1] = reload.events[reload.nextEvent].name
        reload.nextEvent = reload.nextEvent + 1
    end
    state.reload = nil
    state.reloadRemaining = 0
    return true, crossed
end

-- Stops an action where it is. Events already crossed stay applied, so an empty reload cut after
-- the insert leaves the chamber empty and the next reload is a "chamber" action.
function Logic.interruptReload(state)
    if not state.reload then
        return false
    end
    state.reload = nil
    state.reloadRemaining = 0
    return true
end

-- Fires the chambered round and feeds the next one from the magazine.
function Logic.spendShot(state)
    if (state.chamber or 0) <= 0 then
        return false
    end
    state.chamber = 0
    if (state.magazine or 0) > 0 then
        state.magazine = state.magazine - 1
        state.chamber = 1
    end
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
