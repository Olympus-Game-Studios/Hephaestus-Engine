-- PauseMenu.lua
-- Toggles a pause overlay on Escape, tracks play time, and wires button events.

local PAUSE_MENU_UI = "assets/ui/PauseMenu.hui"
local MAIN_MENU_UI  = "assets/ui/MainMenu.hui"

local pauseHandle = nil
local paused = false
local playTime = 0

function onAwake()
    print("[PauseMenu] Script attached – press Escape to open pause menu")
    paused = false
    pauseHandle = nil
    playTime = 0
end

function onUpdate(dt)
    if not paused then
        playTime = playTime + dt
    end

    if Input.isKeyPressed(256) then
        print("[PauseMenu] Escape pressed, paused=" .. tostring(paused))
        if paused then
            resumeGame()
        else
            pauseGame()
        end
    end
end

function pauseGame()
    if pauseHandle then return end

    print("[PauseMenu] Loading UI: " .. PAUSE_MENU_UI)
    local handle, err = display.ui(PAUSE_MENU_UI)
    if not handle then
        print("[PauseMenu] ERROR – Failed to load: " .. tostring(err))
        return
    end

    print("[PauseMenu] UI loaded, handle=" .. tostring(handle))
    pauseHandle = handle
    paused = true
    UI.setCursorMode(CursorMode.UI)

    -- Update the play-time label with the current session time
    local lblPlaytime = UI.getWidget(handle, "lbl_playtime")
    if lblPlaytime then
        local minutes = math.floor(playTime / 60)
        local seconds = math.floor(playTime % 60)
        UI.setText(lblPlaytime, string.format("Play time: %d:%02d", minutes, seconds))
    end

    -- Resume button — close the pause menu and return to gameplay
    local btnResume = UI.getWidget(handle, "btn_resume")
    if btnResume then
        UI.setOnClick(btnResume, function()
            resumeGame()
        end)
    end

    -- Main Menu button — destroy pause menu then show the main menu
    local btnMainMenu = UI.getWidget(handle, "btn_main_menu")
    if btnMainMenu then
        UI.setOnClick(btnMainMenu, function()
            destroy.ui(pauseHandle)
            pauseHandle = nil
            paused = false
            playTime = 0
            display.ui(MAIN_MENU_UI)
        end)
    end

    -- Quit button
    local btnQuit = UI.getWidget(handle, "btn_quit")
    if btnQuit then
        UI.setOnClick(btnQuit, function()
            Application.quit()
        end)
    end
end

function resumeGame()
    if pauseHandle then
        destroy.ui(pauseHandle)
        pauseHandle = nil
    end
    paused = false
    UI.setCursorMode(CursorMode.Game)
end
