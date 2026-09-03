-- Simple boomer-shooter Cultist AI. Attach as LuaScriptComponent beside CultistCharacter.lua.
-- Tells the character controller what to present via this:CallController.

Expose.number("MoveSpeed", 2.5)
Expose.number("AttackInterval", 2.0)
Expose.number("Health", 30)

local attackTimer = 0.0
local dead = false

function onAwake()
    attackTimer = AttackInterval
end

function onDamage(amount)
    if dead then
        return
    end
    Health = Health - (amount or 10)
    if Health <= 0 then
        dead = true
        this:CallController("BeginDeath")
        return
    end
    this:CallController("BeginHurt")
end

function onUpdate(dt)
    if dead then
        return
    end

    if Physics.hasBody() then
        Physics.move(0.0, 1.0, MoveSpeed)
    else
        local transform = this:GetTransform()
        if transform then
            transform:Translate(0.0, 0.0, MoveSpeed * dt)
        end
    end

    attackTimer = attackTimer - dt
    if attackTimer <= 0.0 then
        attackTimer = AttackInterval
        this:CallController("BeginShoot")
    end
end
