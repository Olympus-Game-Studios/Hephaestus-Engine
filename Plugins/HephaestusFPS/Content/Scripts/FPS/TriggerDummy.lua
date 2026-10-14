local Health = require("FPS.Health")

Expose.number("maxHealth", 1)
Expose.number("playerDamage", 20)
Expose.bool("playerDamageApplied", false)
Expose.bool("damaged", false)

function onAwake()
    Health.attach(this, { maxHealth = maxHealth })
    damaged = false
    playerDamageApplied = false
end

function onDamaged(amount)
    damaged = true
end

function onDeath()
    damaged = true
end

function onDestroy()
    Health.detach(this)
end

function onTriggerEnter(other)
    engine_log("[FPS.Trigger] Enter")
    if other and not playerDamageApplied and Health.get(other) then
        playerDamageApplied = true
        Health.applyDamage(other, playerDamage, this)
    end
end

function onTriggerExit(other)
    engine_log("[FPS.Trigger] Exit")
end
