local Health = require("FPS.Health")

Expose.number("maxHealth", 1)
Expose.bool("damaged", false)

function onAwake()
    Health.attach(this, { maxHealth = maxHealth })
    damaged = false
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
