local frames = 0

function onAwake()
    engine_log("STAGE6_LUA_AWAKE")
    display.ui("assets/ui/Portability.hui")
end

function onUpdate()
    frames = frames + 1
    if frames == 120 then
        engine_log("STAGE6_PORTABILITY_COMPLETE")
        Application.quit()
    end
end

function onDestroy()
end
