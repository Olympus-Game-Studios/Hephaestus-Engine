local Health = require("FPS.Health")

Expose.number("maxHealth", 60)
Expose.bool("dead", false)

function onAwake()
    Health.attach(this, { maxHealth = maxHealth })
    dead = false
end

function onDeath(instigator)
    dead = true
    engine_log("[FPS.Target] died")
end

function onDestroy()
    Health.detach(this)
end
