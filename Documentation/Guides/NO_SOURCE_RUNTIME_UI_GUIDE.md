# No-Source Runtime UI Guide (Script-Only)

This guide explains how to create and display custom UI in Hephaestus Engine when you do not have engine source code access.

Audience:
- Game/team members using the editor and Lua scripts
- Mod/script authors working against a provided engine build

Assumption:
- The engine/runtime binary already includes HephaestusUI + Lua UI bindings.

## 1. What You Can Do Without Source Access

You can build runtime UI entirely from Lua by using the global `UI` API:
- Create screens
- Create widgets (panel/text/button/image/vertical and horizontal boxes)
- Set size/position/visibility/z-order/text/image/spacing
- Show/hide active screens at runtime

You do not need to recompile engine code for any of the above.

## 2. Prerequisites

1. Lua scripting is enabled in your runtime/editor build.
2. Your project can attach and run Lua scripts on entities.
3. At runtime, `UI.isAvailable()` returns `true`.

Quick check script:

```lua
function onAwake()
    if UI and UI.isAvailable() then
        print("[UI] Runtime UI API is available")
    else
        print("[UI] Runtime UI API is NOT available in this build")
    end
end
```

If it returns false, your binary was likely built without the UI bridge wiring.

## 3. Workflow (No Engine Source Required)

1. Create a Lua script in your project scripts folder.
2. Attach the script to an entity in your scene (for example, a bootstrap/game-manager entity).
3. In `onAwake`, create your screen and widget tree.
4. In `onUpdate`, update visibility/content as needed.
5. Play the scene; iterate by editing Lua and restarting play mode.

## 4. Minimal Working Example

This creates and displays a custom pause-style menu using only Lua.

```lua
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
```

## 5. Displaying a HUD Example

Simple top-left HUD:

```lua
function createHUD()
    local screen = UI.createScreen("HUD")
    local root = UI.getRoot(screen)

    local panel = UI.createPanel(root)
    UI.setPosition(panel, 24, 24)
    UI.setSize(panel, 300, 90)

    local hp = UI.createText(panel, "HP: 100")
    UI.setPosition(hp, 12, 10)
    UI.setSize(hp, 276, 28)

    local ammo = UI.createText(panel, "Ammo: 30")
    UI.setPosition(ammo, 12, 42)
    UI.setSize(ammo, 276, 28)

    UI.setScreenVisible("HUD", true)
    UI.setScreenActive("HUD", true)

    return hp, ammo
end
```

You can later update those text handles with `UI.setText(handle, "...")`.

## 6. Typical "No Source" Best Practices

1. Use one bootstrap script per screen family (HUD, Pause, Inventory) for maintainability.
2. Cache all returned handles in Lua tables; avoid recreating UI every frame.
3. Create UI once in `onAwake`, then mutate values in `onUpdate`.
4. Use `UI.setScreenVisible` and `UI.setScreenActive` instead of destroying/recreating often.
5. Keep a clear naming convention for screens (`HUD`, `PauseMenu`, `Inventory`).

## 7. Troubleshooting

### UI.isAvailable() is false
- Your runtime build may not expose the UI bridge.
- Ask the engine provider to enable Lua UI bindings in the shipped build.

### create* returns 0
- Parent handle may be invalid.
- You may have cleared screens, invalidating handles.
- Recreate the screen and reacquire root/child handles.

### Nothing appears on screen
- Confirm the screen is visible and active.
- Confirm non-zero sizes (`UI.setSize`).
- Check z-order if overlapping (`UI.setZOrder`).

### Text/image update fails
- `UI.setText` works on text and button handles.
- `UI.setImage` works on image handles.

## 8. Current Limits in No-Source Mode

Not currently exposed through Lua API:
- Styling/color/font setters
- Widget removal API
- Click callback registration on buttons
- Navigation/focus graph

For advanced behavior, coordinate with engine developers to expose additional Lua bindings in future builds.

## 9. API Reference

Full API details and signatures:
- `Documentation/Scripting/LUA_UI_API_REFERENCE.md`

Runtime architecture reference:
- `Documentation/Features/HEPHAESTUS_UI_RUNTIME_GUIDE.md`
