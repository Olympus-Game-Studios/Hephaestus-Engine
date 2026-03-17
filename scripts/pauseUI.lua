local ui = {
    screen = 0,
    root = 0,
    panel = 0,
    title = 0,
    menu = 0,
    playButton = 0,
    optionsButton = 0,
    exitButton = 0,
    visible = true
}

function buildUI()
    ui.screen = UI.createScreen("CustomMenu")
    ui.root = UI.getRoot(ui.screen)

    ui.panel = UI.createPanel(ui.root)
    UI.setPosition(ui.panel, 640, 180)
    UI.setSize(ui.panel, 640, 520)
    UI.setZOrder(ui.panel, 20)

    ui.title = UI.createText(ui.panel, "My Custom Menu")
    UI.setPosition(ui.title, 24, 24)
    UI.setSize(ui.title, 592, 48)

    ui.menu = UI.createVerticalBox(ui.panel)
    UI.setPosition(ui.menu, 24, 96)
    UI.setSize(ui.menu, 592, 360)
    UI.setSpacing(ui.menu, 12)

    ui.playButton = UI.createButton(ui.menu, "Play")
    UI.setSize(ui.playButton, 592, 64)

    ui.optionsButton = UI.createButton(ui.menu, "Options")
    UI.setSize(ui.optionsButton, 592, 64)

    ui.exitButton = UI.createButton(ui.menu, "Exit")
    UI.setSize(ui.exitButton, 592, 64)

    UI.setScreenVisible("CustomMenu", true)
    UI.setScreenActive("CustomMenu", true)
end

function onAwake()
    if not UI or not UI.isAvailable() then
        print("[UI] API unavailable")
        return
    end

    buildUI()
    print("[UI] Draw commands: " .. UI.getDrawCommandCount())
end

function onUpdate(deltaTime)
    -- Example toggle with Escape
    if Input and Input.isKeyPressed("ESCAPE") then
        ui.visible = not ui.visible
        UI.setScreenVisible("CustomMenu", ui.visible)
    end
end