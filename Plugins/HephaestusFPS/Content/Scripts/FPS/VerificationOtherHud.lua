local handle

function onAwake()
    handle = display.ui("plugin://com.hephaestus.fps/UI/FPSVerificationOtherHud.hui")
end

function onDestroy()
    if handle then
        destroy.ui(handle)
        handle = nil
    end
end
