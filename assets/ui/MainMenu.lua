-- MainMenu.lua
-- Controller script for MainMenu.hui
-- Handles button events and menu logic

-- UI asset path (define at top for readability and easy refactoring)
local MAIN_MENU_UI = "assets/ui/MainMenu.hui"

local menuHandle = nil

function showMainMenu()
    local handle, err = display.ui(MAIN_MENU_UI)
    if not handle then
        print("[MainMenu] Failed to load UI: " .. tostring(err))
        return
    end

    menuHandle = handle
    UI.setCursorMode(CursorMode.UI)

    local btnStart = UI.getWidget(handle, "btn_start")
    if btnStart then
        UI.setOnClick(btnStart, function()
            print("[MainMenu] Start Game clicked")
            destroy.ui(menuHandle)
            menuHandle = nil
            UI.setCursorMode(CursorMode.Game)
        end)
    end

    local btnSettings = UI.getWidget(handle, "btn_settings")
    if btnSettings then
        UI.setOnClick(btnSettings, function()
            print("[MainMenu] Settings clicked")
        end)
    end

    local btnQuit = UI.getWidget(handle, "btn_quit")
    if btnQuit then
        UI.setOnClick(btnQuit, function()
            print("[MainMenu] Quit clicked")
            Application.quit()
        end)
    end
end

function hideMainMenu()
    if menuHandle then
        destroy.ui(menuHandle)
        menuHandle = nil
        UI.setCursorMode(CursorMode.Game)
    end
end
