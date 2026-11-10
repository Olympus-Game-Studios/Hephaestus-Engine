-- The in-game pause menu: the Pause action (Escape, gamepad Start) freezes the game (Time.setScale(0)) and shows
-- Resume, Settings, Restart, Quit to menu (when a menu scene is set) and Quit game. The player script calls
-- PauseMenu.update every frame.
local Menu = require("FPS.Menu")
local Settings = require("FPS.Settings")

local PauseMenu = { menu = nil, open = false }

-- Forget the menu; its UI screen does not survive a scene change.
function PauseMenu.reset()
    PauseMenu.menu = nil
    PauseMenu.open = false
end

local function close()
    PauseMenu.open = false
    if PauseMenu.menu then
        Menu.setVisible(PauseMenu.menu, false)
    end
    Settings.save()
    Time.setScale(1)
    UI.setCursorMode(CursorMode.Game)
end

local function mainPage(options)
    local items = {
        { label = "RESUME", action = close },
        { label = "SETTINGS", action = function() Menu.push(PauseMenu.menu, Settings.page()) end },
        { label = "RESTART", action = function() close(); Scene.reload() end },
    }
    if options.mainMenuScene and options.mainMenuScene ~= "" then
        items[#items + 1] = { label = "QUIT TO MENU", action = function() close(); Scene.load(options.mainMenuScene) end }
    end
    items[#items + 1] = { label = "QUIT GAME", action = function() Settings.save(); Application.quit() end }
    return { title = "PAUSED", items = items }
end

local function openMenu(options)
    if not PauseMenu.menu then
        PauseMenu.menu = Menu.new("PauseMenu", { backdrop = 0.6, y = 260,
                                                 hints = "ENTER  SELECT        A / D  ADJUST        ESC  BACK" })
    end
    Menu.setPage(PauseMenu.menu, mainPage(options))
    Menu.setVisible(PauseMenu.menu, true)
    PauseMenu.open = true
    Time.setScale(0)
    UI.setCursorMode(CursorMode.UI)
end

-- options: mainMenuScene (a scene path for "Quit to menu"), enabled (false keeps the menu shut).
function PauseMenu.update(options)
    if PauseMenu.open then
        if Menu.update(PauseMenu.menu) == "back" then
            close()
        end
    elseif Input.isActionPressed("Pause") and options.enabled ~= false then
        openMenu(options)
    end
    return PauseMenu.open
end

return PauseMenu
