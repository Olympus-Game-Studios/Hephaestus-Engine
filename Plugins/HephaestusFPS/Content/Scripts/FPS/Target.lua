local Health = require("FPS.Health")

Expose.number("maxHealth", 60)
Expose.bool("dead", false)

function onAwake()
    Health.attach(this, { maxHealth = maxHealth })
    dead = false
end

function onDeath(instigator)
    dead = true
    Mesh.setVisible(false)
    engine_log("[FPS.Target] died")
end

function onHealthChanged(current, maximum)
    engine_log(string.format("[FPS.Target] health=%s/%s", tostring(current), tostring(maximum)))
end

function onDestroy()
    Health.detach(this)
end
