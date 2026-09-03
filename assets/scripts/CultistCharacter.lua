-- Cultist / YBot character controller.
-- Assign this path on CharacterComponent (Run Controller), not LuaScriptComponent.
-- Clip ids must match the imported .hpcharacter clip.id values. Rename clips in
-- the Character Editor after importing Mixamo YBot, or change the constants below.

local CLIP_IDLE = "Idle"
local CLIP_WALK = "Walk"
local CLIP_RUN = "Run"
local CLIP_SHOOT = "Shoot"
local CLIP_HURT = "Hurt"
local CLIP_DEATH = "Death"

Expose.number("WalkThreshold", 0.1)
Expose.number("RunThreshold", 4.0)
Expose.number("LocomotionBlend", 0.12)

local animator = nil
local lastPos = nil
local shootStarted = false
local hurtStarted = false
local deathStarted = false
local attacking = false
local hurt = false
local dead = false

local function horizontalSpeed(dt)
    if Physics.hasBody() then
        local vel = Physics.getLinearVelocity()
        if vel then
            return math.sqrt(vel.x * vel.x + vel.z * vel.z)
        end
    end
    local transform = this:GetTransform()
    if not transform then
        return 0.0
    end
    local pos = transform:GetPosition()
    if not lastPos or dt <= 0.0 then
        lastPos = pos
        return 0.0
    end
    local dx = pos.x - lastPos.x
    local dz = pos.z - lastPos.z
    lastPos = pos
    return math.sqrt(dx * dx + dz * dz) / dt
end

function onAwake()
    animator = this:GetAnimator()
    local transform = this:GetTransform()
    if transform then
        lastPos = transform:GetPosition()
    end
end

-- AI entry points. Call these once via this:CallController, not every frame.
function BeginShoot()
    if dead or hurt then
        return
    end
    attacking = true
end

function BeginHurt()
    if dead then
        return
    end
    hurt = true
    attacking = false
    shootStarted = false
end

function BeginDeath()
    dead = true
    attacking = false
    hurt = false
    shootStarted = false
    hurtStarted = false
end

function onUpdate(dt)
    if not animator then
        animator = this:GetAnimator()
        if not animator then
            return
        end
    end

    if dead then
        if not deathStarted then
            deathStarted = true
            animator:Play(CLIP_DEATH)
        end
        return
    end

    if hurt then
        if not hurtStarted then
            hurtStarted = true
            animator:Play(CLIP_HURT)
        end
        return
    end

    if attacking then
        if not shootStarted then
            shootStarted = true
            animator:Play(CLIP_SHOOT)
        end
        return
    end

    local speed = horizontalSpeed(dt)
    if speed >= RunThreshold then
        animator:CrossFade(CLIP_RUN, LocomotionBlend)
    elseif speed >= WalkThreshold then
        animator:CrossFade(CLIP_WALK, LocomotionBlend)
    else
        animator:CrossFade(CLIP_IDLE, LocomotionBlend)
    end
end

function onAnimationFinished(name)
    if name == CLIP_SHOOT then
        attacking = false
        shootStarted = false
    elseif name == CLIP_HURT then
        hurt = false
        hurtStarted = false
    end
end
