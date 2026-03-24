local menu = {
    screenName = "MainMenu",
    screen = 0,
    root = 0,
    panel = 0,
    title = 0,
    subtitle = 0,
    buttonList = 0,
    buttons = {},
    statusText = 0,
    visible = false,
    selectedIndex = 1,
    showOptionsHint = false
}

local buttonLabels = {
    "Start Game",
    "Options",
    "Quit"
}

local keyState = {
    up = false,
    w = false,
    down = false,
    s = false,
    enter = false,
    space = false,
    escape = false
}

local function keyPressedNow(key, stateField)
    local isDown = Input and Input.isKeyPressed(key) or false
    local justPressed = isDown and not keyState[stateField]
    keyState[stateField] = isDown and true or false
    return justPressed
end

local function setStatus(text)
    if menu.statusText ~= 0 then
        UI.setText(menu.statusText, text)
    end
end

local function refreshButtonLabels()
    for index, button in ipairs(menu.buttons) do
        local prefix = "  "
        if index == menu.selectedIndex then
            prefix = "> "
        end

        UI.setText(button, prefix .. buttonLabels[index])
    end
end

local function showMenu()
    menu.visible = true
    UI.setScreenVisible(menu.screenName, true)
    UI.setScreenActive(menu.screenName, true)
    UI.setCursorMode(CursorMode.UI)
    setStatus("Click a button or use keyboard (W/S + Enter).")
    engine_log("[MainMenu] Opened")
end

local function hideMenu()
    menu.visible = false
    UI.setScreenVisible(menu.screenName, false)
    UI.setScreenActive(menu.screenName, false)
    UI.setCursorMode(CursorMode.Game)
    engine_log("[MainMenu] Hidden")
end

local function activateCurrentSelection()
    local currentLabel = buttonLabels[menu.selectedIndex]

    if currentLabel == "Start Game" then
        setStatus("Starting test run. Press ESC to open the menu again.")
        engine_log("[MainMenu] Start Game selected")
        hideMenu()
        return
    end

    if currentLabel == "Options" then
        menu.showOptionsHint = not menu.showOptionsHint
        if menu.showOptionsHint then
            setStatus("Options placeholder: wire settings or scene loading here.")
        else
            setStatus("Use Up/Down or W/S, then Enter or Space.")
        end
        engine_log("[MainMenu] Options selected")
        return
    end

    if currentLabel == "Quit" then
        setStatus("Quit requested...")
        engine_log("[MainMenu] Quit selected")
        Application.quit()
    end
end

local function moveSelection(step)
    local count = #buttonLabels
    menu.selectedIndex = ((menu.selectedIndex - 1 + step) % count) + 1
    refreshButtonLabels()
    setStatus("Selected: " .. buttonLabels[menu.selectedIndex])
end

local function onStartGameClicked()
    setStatus("Starting test run. Press ESC to open the menu again.")
    engine_log("[MainMenu] Start Game clicked")
    hideMenu()
end

local function onOptionsClicked()
    menu.showOptionsHint = not menu.showOptionsHint
    if menu.showOptionsHint then
        setStatus("Options placeholder: wire settings or scene loading here.")
    else
        setStatus("Click a button or use keyboard navigation.")
    end
    engine_log("[MainMenu] Options clicked")
end

local function onQuitClicked()
    setStatus("Quit requested...")
    engine_log("[MainMenu] Quit clicked")
    Application.quit()
end

local function buildUI()
    menu.screen = UI.createScreen(menu.screenName)
    menu.root = UI.getRoot(menu.screen)

    if menu.root == 0 then
        engine_log("[MainMenu] ERROR: Root canvas is invalid")
        return false
    end

    menu.panel = UI.createPanel(menu.root)
    if menu.panel == 0 then
        engine_log("[MainMenu] ERROR: Failed to create panel")
        return false
    end

    UI.setPosition(menu.panel, 64, 64)
    UI.setSize(menu.panel, 560, 360)
    UI.setZOrder(menu.panel, 50)

    menu.title = UI.createText(menu.panel, "Hephaestus Main Menu")
    UI.setPosition(menu.title, 24, 24)
    UI.setSize(menu.title, 512, 40)

    menu.subtitle = UI.createText(menu.panel, "Click buttons or use keyboard (W/S + Enter)")
    UI.setPosition(menu.subtitle, 24, 64)
    UI.setSize(menu.subtitle, 512, 28)

    menu.buttonList = UI.createVerticalBox(menu.panel)
    UI.setPosition(menu.buttonList, 24, 110)
    UI.setSize(menu.buttonList, 512, 180)
    UI.setSpacing(menu.buttonList, 12)

    local clickHandlers = {
        onStartGameClicked,
        onOptionsClicked,
        onQuitClicked
    }

    for index, label in ipairs(buttonLabels) do
        local button = UI.createButton(menu.buttonList, label)
        UI.setSize(button, 512, 52)
        table.insert(menu.buttons, button)

        if UI.setOnClick and clickHandlers[index] then
            UI.setOnClick(button, clickHandlers[index])
        end
    end

    menu.statusText = UI.createText(menu.panel, "")
    UI.setPosition(menu.statusText, 24, 308)
    UI.setSize(menu.statusText, 512, 28)

    refreshButtonLabels()
    return true
end

function onAwake()
    if not UI or not UI.isAvailable() then
        engine_log("[MainMenu] UI API unavailable")
        return
    end

    if not buildUI() then
        return
    end

    showMenu()
    engine_log("[MainMenu] Ready - supports mouse clicks and keyboard navigation")
end

function onUpdate(deltaTime)
    local _ = deltaTime

    local upArrowPressed = keyPressedNow("UP", "up")
    local wPressed = keyPressedNow("W", "w")
    local downArrowPressed = keyPressedNow("DOWN", "down")
    local sPressed = keyPressedNow("S", "s")
    local enterPressed = keyPressedNow("ENTER", "enter")
    local spacePressed = keyPressedNow("SPACE", "space")
    local escapePressed = keyPressedNow("ESCAPE", "escape")
    local upPressed = upArrowPressed or wPressed
    local downPressed = downArrowPressed or sPressed
    local activatePressed = enterPressed or spacePressed

    if escapePressed then
        if menu.visible then
            hideMenu()
        else
            showMenu()
        end
    end

    if not menu.visible then
        return
    end

    if upPressed then
        moveSelection(-1)
    end

    if downPressed then
        moveSelection(1)
    end

    if activatePressed then
        activateCurrentSelection()
    end
end

function onDestroy()
    if UI and UI.isAvailable() then
        UI.setCursorMode(CursorMode.Game)
    end
end
