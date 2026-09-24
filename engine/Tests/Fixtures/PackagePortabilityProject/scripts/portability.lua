local frames = 0
local uiHandle = nil
local reported = false

function onAwake()
    engine_log("STAGE6_LUA_AWAKE")
    local handle, err = display.ui("assets/ui/Portability.hui")
    if not handle then
        engine_log("STAGE6_HUI_LOAD_FAILED " .. tostring(err))
        return
    end
    uiHandle = handle
    engine_log("STAGE6_HUI_LOADED")
    UI.setCursorMode(CursorMode.UI)
end

function onUpdate()
    frames = frames + 1

    -- Verify the packaged Vulkan backend produced draw commands and that the
    -- widgets are reachable through the Lua API (rendering + input routing use
    -- the same tree).
    if uiHandle and not reported then
        local count = UI.getDrawCommandCount()
        if count and count > 0 then
            engine_log("STAGE6_HUI_DRAW_COMMANDS " .. tostring(count))
            local button = UI.getWidget(uiHandle, "btn_click")
            local image = UI.getWidget(uiHandle, "image")
            local title = UI.getWidget(uiHandle, "title")
            engine_log("STAGE6_HUI_WIDGET_BUTTON " .. tostring(button ~= nil))
            engine_log("STAGE6_HUI_WIDGET_IMAGE " .. tostring(image ~= nil))
            engine_log("STAGE6_HUI_WIDGET_TITLE " .. tostring(title ~= nil))
            if button then
                UI.setOnClick(button, function()
                    engine_log("STAGE6_HUI_BUTTON_CLICKED")
                end)
            end
            reported = true
        end
    end

    if frames == 120 then
        engine_log("STAGE6_PORTABILITY_COMPLETE")
        Application.quit()
    end
end

function onDestroy()
end