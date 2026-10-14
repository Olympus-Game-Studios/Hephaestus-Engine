local Interactable = require("FPS.Interactable")
local Health = require("FPS.Health")
local Registry = require("FPS.Registry")
Expose.string("prompt", "Pick up")
Expose.number("healAmount", 25)
Expose.number("ammoAmount", 12)

function onAwake()
    Interactable.register(this, { prompt = prompt, maxDistance = 2.5, enabled = true })
end

function onInteract(player)
    if player then
        Health.heal(player, healAmount)
        local weapon = Registry.activeWeapon()
        if weapon then
            weapon.reserve = (weapon.reserve or 0) + ammoAmount
        end
    end
    Interactable.unregister(this)
    Scene.destroy(this)
end

function onDestroy()
    Interactable.unregister(this)
end
