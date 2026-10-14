local Interactable = require("FPS.Interactable")

Expose.bool("pressed", false)
Expose.string("prompt", "Press Interact")
Expose.string("doorName", "FPSDoor")

function onAwake()
    Interactable.register(this, { prompt = prompt, maxDistance = 2.5, enabled = true })
end

function onInteract(player)
    pressed = not pressed
    for _, door in ipairs(Scene.findAllWithPrefix(doorName)) do
        if door:GetName() == doorName then
            door:Call("onInteract", player)
        end
    end
    engine_log("[FPS.Button] toggled pressed=" .. tostring(pressed))
end

function onDestroy()
    Interactable.unregister(this)
end
