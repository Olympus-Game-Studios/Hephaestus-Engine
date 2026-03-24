local ui = {
    screen = 0,
    root = 0,
    panel = 0,
    title = 0,
    menu = 0,
    resumeButton = 0,
    optionsButton = 0,
    exitButton = 0,
    visible = false,
    escWasDown = false
}

function buildUI()
    ui.screen = UI.createScreen("PauseMenu")
    ui.root = UI.getRoot(ui.screen)

    if ui.root == 0 then
        print("[PauseUI] ERROR: Root canvas is invalid")
        return
    end

    ui.panel = UI.createPanel(ui.root)
    if ui.panel == 0 then
        print("[PauseUI] ERROR: Failed to create panel")
        return
    end

    UI.setPosition(ui.panel, 32, 32)
    UI.setSize(ui.panel, 420, 320)
    UI.setZOrder(ui.panel, 20)

    ui.title = UI.createText(ui.panel, "Paused")
    UI.setPosition(ui.title, 24, 24)
    UI.setSize(ui.title, 372, 40)

    ui.menu = UI.createVerticalBox(ui.panel)
    UI.setPosition(ui.menu, 24, 76)
    UI.setSize(ui.menu, 372, 220)
    UI.setSpacing(ui.menu, 12)

    ui.resumeButton = UI.createButton(ui.menu, "Resume")
    UI.setSize(ui.resumeButton, 372, 60)

    ui.optionsButton = UI.createButton(ui.menu, "Options")
    UI.setSize(ui.optionsButton, 372, 60)

    ui.exitButton = UI.createButton(ui.menu, "Exit")
    UI.setSize(ui.exitButton, 372, 60)
end

local function showPauseMenu()
    ui.visible = true
    UI.setScreenVisible("PauseMenu", true)
    UI.setScreenActive("PauseMenu", true)
    UI.setCursorMode(CursorMode.UI)
    print("[PauseUI] Menu opened  (cursor: UI mode)")
end

local function hidePauseMenu()
    ui.visible = false
    UI.setScreenVisible("PauseMenu", false)
    UI.setScreenActive("PauseMenu", false)
    UI.setCursorMode(CursorMode.Game)
    print("[PauseUI] Menu closed  (cursor: Game mode)")
end

function onAwake()
    if not UI or not UI.isAvailable() then
        print("[PauseUI] API unavailable")
        return
    end

    buildUI()
    ui.escWasDown = (Input and Input.isKeyPressed("ESCAPE")) and true or false

    -- Start with the pause menu visible so the UI is immediately shown
    showPauseMenu()
    print("[PauseUI] Pause menu ready (press ESC to toggle)")
end

function onUpdate(deltaTime)
    local escDown = Input and Input.isKeyPressed("ESCAPE")
    if escDown and not ui.escWasDown then
        if ui.visible then
            hidePauseMenu()
        else
            showPauseMenu()
        end
    end
    ui.escWasDown = escDown and true or false
end