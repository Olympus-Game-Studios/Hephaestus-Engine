local menu = {
    screenName = "MainMenu",
    screen = 0,
    root = 0,
    overlay = 0,
    panel = 0,
    title = 0,
    subtitle = 0,
    buttonList = 0,
    buttons = {},
    statusText = 0,
    visible = false,
    selectedIndex = 1
}

local buttonDefs = {
    { label = "Start Game",  action = "start" },
    { label = "Options",     action = "options" },
    { label = "Credits",     action = "credits" },
    { label = "Quit",        action = "quit" }
}

local keyState = {
    up = false, w = false,
    down = false, s = false,
    enter = false, space = false,
    escape = false
}

-- Theme colors
local colors = {
    panelBg       = { r = 0.06, g = 0.06, b = 0.09, a = 0.94 },
    panelBorder   = { r = 0.22, g = 0.24, b = 0.32, a = 0.7 },
    overlayBg     = { r = 0.0,  g = 0.0,  b = 0.0,  a = 0.5 },
    buttonNormalBg   = { r = 0.12, g = 0.13, b = 0.18, a = 1.0 },
    buttonNormalBdr  = { r = 0.28, g = 0.30, b = 0.38, a = 1.0 },
    buttonHoverBg    = { r = 0.18, g = 0.22, b = 0.35, a = 1.0 },
    buttonHoverBdr   = { r = 0.35, g = 0.45, b = 0.75, a = 1.0 },
    buttonPressBg    = { r = 0.08, g = 0.10, b = 0.16, a = 1.0 },
    buttonFocusBdr   = { r = 0.4,  g = 0.6,  b = 1.0,  a = 1.0 },
    buttonDisabledBg = { r = 0.10, g = 0.10, b = 0.12, a = 0.5 },
    textWhite     = { r = 1.0,  g = 1.0,  b = 1.0,  a = 1.0 },
    textDim       = { r = 0.6,  g = 0.6,  b = 0.65, a = 1.0 },
    textDisabled  = { r = 0.4,  g = 0.4,  b = 0.4,  a = 1.0 },
    shadowColor   = { r = 0.0,  g = 0.0,  b = 0.0,  a = 0.4 }
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

local function updateButtonFocus()
    for index, btn in ipairs(menu.buttons) do
        UI.setFocused(btn, index == menu.selectedIndex)
    end
end

local function showMenu()
    menu.visible = true
    UI.setScreenVisible(menu.screenName, true)
    UI.setScreenActive(menu.screenName, true)
    UI.setCursorMode(CursorMode.UI)
    updateButtonFocus()
    setStatus("Navigate with W/S or arrows. Enter to select.")
    engine_log("[MainMenu] Opened")
end

local function hideMenu()
    menu.visible = false
    UI.setScreenVisible(menu.screenName, false)
    UI.setScreenActive(menu.screenName, false)
    UI.setCursorMode(CursorMode.Game)
    engine_log("[MainMenu] Hidden")
end

local function activateAction(action)
    if action == "start" then
        setStatus("Starting game...")
        engine_log("[MainMenu] Start Game selected")
        hideMenu()
    elseif action == "options" then
        setStatus("Options panel coming soon.")
        engine_log("[MainMenu] Options selected")
    elseif action == "credits" then
        setStatus("Hephaestus Engine - Built with care.")
        engine_log("[MainMenu] Credits selected")
    elseif action == "quit" then
        setStatus("Quitting...")
        engine_log("[MainMenu] Quit selected")
        Application.quit()
    end
end

local function moveSelection(step)
    local count = #buttonDefs
    menu.selectedIndex = ((menu.selectedIndex - 1 + step) % count) + 1
    updateButtonFocus()
    setStatus("Selected: " .. buttonDefs[menu.selectedIndex].label)
end

local function styleButton(btn)
    UI.setCornerRadius(btn, 8)

    UI.setButtonStyle(btn, "normal", {
        backgroundColor = colors.buttonNormalBg,
        borderColor     = colors.buttonNormalBdr,
        borderWidth     = 1,
        cornerRadius    = 8,
        textColor       = colors.textWhite,
        fontSize        = 16,
        textAlignment   = "center",
        padding         = 10
    })
    UI.setButtonStyle(btn, "hovered", {
        backgroundColor = colors.buttonHoverBg,
        borderColor     = colors.buttonHoverBdr,
        borderWidth     = 1.5,
        cornerRadius    = 8,
        textColor       = colors.textWhite,
        fontSize        = 16,
        textAlignment   = "center",
        padding         = 10
    })
    UI.setButtonStyle(btn, "pressed", {
        backgroundColor = colors.buttonPressBg,
        borderColor     = colors.buttonNormalBdr,
        borderWidth     = 1,
        cornerRadius    = 8,
        textColor       = colors.textDim,
        fontSize        = 16,
        textAlignment   = "center",
        padding         = 10
    })
    UI.setButtonStyle(btn, "focused", {
        backgroundColor = colors.buttonNormalBg,
        borderColor     = colors.buttonFocusBdr,
        borderWidth     = 2,
        cornerRadius    = 8,
        textColor       = colors.textWhite,
        fontSize        = 16,
        textAlignment   = "center",
        padding         = 10
    })
    UI.setButtonStyle(btn, "disabled", {
        backgroundColor = colors.buttonDisabledBg,
        borderColor     = { r = 0.15, g = 0.15, b = 0.18, a = 0.5 },
        borderWidth     = 1,
        cornerRadius    = 8,
        textColor       = colors.textDisabled,
        fontSize        = 16,
        textAlignment   = "center",
        padding         = 10
    })
end

local function buildUI()
    menu.screen = UI.createScreen(menu.screenName)
    menu.root = UI.getRoot(menu.screen)

    if menu.root == 0 then
        engine_log("[MainMenu] ERROR: Root canvas is invalid")
        return false
    end

    -- Semi-transparent overlay behind the panel
    menu.overlay = UI.createPanel(menu.root)
    UI.setAnchor(menu.overlay, UIAnchor.Stretch)
    UI.setStyle(menu.overlay, {
        backgroundColor = colors.overlayBg,
        borderWidth     = 0,
        cornerRadius    = 0
    })
    UI.setZOrder(menu.overlay, 10)

    -- Main panel, centered
    menu.panel = UI.createPanel(menu.root)
    UI.setSize(menu.panel, 480, 480)
    UI.setAnchor(menu.panel, UIAnchor.Center)
    UI.setPadding(menu.panel, 32)
    UI.setZOrder(menu.panel, 50)
    UI.setStyle(menu.panel, {
        backgroundColor = colors.panelBg,
        borderColor     = colors.panelBorder,
        borderWidth     = 1,
        cornerRadius    = 16
    })

    -- Title
    menu.title = UI.createText(menu.panel, "Hephaestus")
    UI.setSize(menu.title, 0, 48)
    UI.setFontSize(menu.title, 32)
    UI.setFontBold(menu.title, true)
    UI.setTextAlignment(menu.title, "center")
    UI.setTextColor(menu.title, 1.0, 1.0, 1.0, 1.0)
    UI.setTextShadow(menu.title, true, 0.0, 0.0, 0.0, 0.4, 2, 2)
    UI.setPosition(menu.title, 0, 0)

    -- Subtitle
    menu.subtitle = UI.createText(menu.panel, "Main Menu")
    UI.setSize(menu.subtitle, 0, 28)
    UI.setFontSize(menu.subtitle, 14)
    UI.setTextAlignment(menu.subtitle, "center")
    UI.setTextColor(menu.subtitle, 0.6, 0.6, 0.7, 1.0)
    UI.setPosition(menu.subtitle, 0, 48)

    -- Vertical button list, centered in panel
    menu.buttonList = UI.createVerticalBox(menu.panel)
    UI.setPosition(menu.buttonList, 0, 100)
    UI.setSize(menu.buttonList, 0, 280)
    UI.setSpacing(menu.buttonList, 10)
    UI.setChildAlignment(menu.buttonList, "center")
    UI.setAnchor(menu.buttonList, UIAnchor.Stretch)

    for index, def in ipairs(buttonDefs) do
        local btn = UI.createButton(menu.buttonList, def.label)
        UI.setSize(btn, 320, 50)
        styleButton(btn)
        table.insert(menu.buttons, btn)

        if UI.setOnClick then
            local action = def.action
            UI.setOnClick(btn, function()
                menu.selectedIndex = index
                updateButtonFocus()
                activateAction(action)
            end)
        end

        if UI.setOnHover then
            UI.setOnHover(btn, function(entered)
                if entered then
                    menu.selectedIndex = index
                    updateButtonFocus()
                end
            end)
        end
    end

    -- Status text at the bottom of the panel
    menu.statusText = UI.createText(menu.panel, "")
    UI.setPosition(menu.statusText, 0, 400)
    UI.setSize(menu.statusText, 0, 24)
    UI.setFontSize(menu.statusText, 12)
    UI.setTextAlignment(menu.statusText, "center")
    UI.setTextColor(menu.statusText, 0.5, 0.5, 0.55, 1.0)
    UI.setWordWrap(menu.statusText, true)

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
    engine_log("[MainMenu] Ready - styled menu with keyboard + mouse support")
end

function onUpdate(deltaTime)
    local _ = deltaTime

    local upPressed    = keyPressedNow("UP", "up")    or keyPressedNow("W", "w")
    local downPressed  = keyPressedNow("DOWN", "down") or keyPressedNow("S", "s")
    local activatePressed = keyPressedNow("ENTER", "enter") or keyPressedNow("SPACE", "space")
    local escapePressed   = keyPressedNow("ESCAPE", "escape")

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
        activateAction(buttonDefs[menu.selectedIndex].action)
    end
end

function onDestroy()
    if UI and UI.isAvailable() then
        UI.setCursorMode(CursorMode.Game)
    end
end
