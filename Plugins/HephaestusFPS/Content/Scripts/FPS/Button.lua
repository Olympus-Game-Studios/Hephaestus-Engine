local Interactable = require("FPS.Interactable")

Expose.bool("pressed", false)
Expose.string("prompt", "Press Interact")

function onAwake()
    Interactable.register(this, { prompt = prompt, maxDistance = 2.5, enabled = true })
end

function onInteract(player)
    pressed = not pressed
    engine_log("[FPS.Button] toggled pressed=" .. tostring(pressed))
end

function onDestroy()
    Interactable.unregister(this)
end
