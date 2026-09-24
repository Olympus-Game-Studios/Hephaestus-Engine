-- HUIRuntimeTest.lua
-- Controller for HUIRuntimeTest.hui (packaged Vulkan runtime verification screen).

local handle = nil
local reported = false

function onAwake()
    local created, err = display.ui("assets/ui/HUIRuntimeTest.hui")
    if not created then
        engine_log("HUI_RUNTIME_TEST_FAILED " .. tostring(err))
        return
    end
    handle = created
    engine_log("HUI_RUNTIME_TEST_LOADED")
    UI.setCursorMode(CursorMode.UI)

    local btnPlay = UI.getWidget(handle, "btn_play")
    if btnPlay then
        UI.setOnClick(btnPlay, function()
            engine_log("HUI_RUNTIME_TEST_PLAY_CLICKED")
        end)
    end

    local btnQuit = UI.getWidget(handle, "btn_quit")
    if btnQuit then
        UI.setOnClick(btnQuit, function()
            engine_log("HUI_RUNTIME_TEST_QUIT_CLICKED")
            Application.quit()
        end)
    end
end

function onUpdate()
    if not reported then
        local count = UI.getDrawCommandCount()
        if count and count > 0 then
            engine_log("HUI_RUNTIME_TEST_DRAW_COMMANDS " .. tostring(count))
            reported = true
        end
    end
end