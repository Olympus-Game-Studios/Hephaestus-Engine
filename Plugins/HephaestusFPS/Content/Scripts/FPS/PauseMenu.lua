-- The in-game menu: the Pause action (Escape, F1, gamepad Start) opens Resume, Settings, Restart (when allowed), Quit to
-- menu (when a menu scene is set) and Quit game. The game keeps running (ready for multiplayer) unless freezeTime
-- is set; the player script ignores its own input while the menu is open. It calls PauseMenu.update every frame.
local Menu = require("FPS.Menu")
local Settings = require("FPS.Settings")

-- suppressFire: after the menu closes, Fire is ignored until it is released (so clicking Resume does not shoot).
local PauseMenu = { menu = nil, open = false, froze = false, suppressFire = false }

-- Forget the menu; its UI screen does not survive a scene change.
function PauseMenu.reset()
    PauseMenu.menu = nil
    PauseMenu.open = false
    PauseMenu.froze = false
    PauseMenu.suppressFire = false
end

local function close()
    PauseMenu.open = false
    if PauseMenu.menu then
        Menu.setVisible(PauseMenu.menu, false)
    end
    Settings.save()
    if PauseMenu.froze then
        Time.setScale(1)
        PauseMenu.froze = false
    end
    PauseMenu.suppressFire = true
    UI.setCursorMode(CursorMode.Game)
end

local function mainPage(options)
    local items = {
        { label = "RESUME", action = close },
        { label = "SETTINGS", action = function() Menu.push(PauseMenu.menu, Settings.page()) end },
    }
    if options.allowRestart ~= false then
        items[#items + 1] = { label = "RESTART", action = function() close(); Scene.reload() end }
    end
    if options.mainMenuScene and options.mainMenuScene ~= "" then
        items[#items + 1] = { label = "QUIT TO MENU", action = function() close(); Scene.load(options.mainMenuScene) end }
    end
    items[#items + 1] = { label = "QUIT GAME", action = function() Settings.save(); Application.quit() end }
    return { title = options.freezeTime and "PAUSED" or "MENU", items = items }
end

local function openMenu(options)
    if not PauseMenu.menu then
        PauseMenu.menu = Menu.new("PauseMenu", { backdrop = 0.6, y = 260,
                                                 hints = "ENTER  SELECT        A / D  ADJUST        ESC  BACK        F1  CLOSE" })
    end
    Menu.setPage(PauseMenu.menu, mainPage(options))
    Menu.setVisible(PauseMenu.menu, true)
    PauseMenu.open = true
    if options.freezeTime then
        Time.setScale(0)
        PauseMenu.froze = true
    end
    UI.setCursorMode(CursorMode.UI)
end

-- options: mainMenuScene (a scene path for "Quit to menu"), enabled (false keeps the menu shut), allowRestart
-- (false hides Restart, as in multiplayer), freezeTime (true stops the game while the menu is open).
function PauseMenu.update(options)
    if PauseMenu.suppressFire and not Input.isActionDown("Fire") then
        PauseMenu.suppressFire = false
    end
    if PauseMenu.open then
        if Menu.update(PauseMenu.menu) == "back" then
            close()
        elseif Input.isActionPressed("Pause") and not Input.isActionPressed("MenuBack") then
            -- F1 (or Start) closes from any page; Escape also means Back, which steps out one page at a time.
            close()
        end
    elseif Input.isActionPressed("Pause") and options.enabled ~= false then
        openMenu(options)
    end
    return PauseMenu.open
end

return PauseMenu
