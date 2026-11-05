---
title: Game UI
description: Building menus and HUDs in the UI Editor, showing them from Lua and wiring up buttons.
order: 14
---

# Game UI

Menus, HUDs, health bars, pause screens: all of that is built with **HephaestusUI**. You lay a screen out visually in the **UI Editor**, it's saved as a `.hui` file and a Lua script shows it and makes the buttons do things. Layout in one place, logic in another, which keeps both easier to change.

## The UI Editor

Create a layout with **New UI File** in the Content Browser (they live in `assets/ui/`), then double click it.

- **Palette** on the left has the widgets: **Panel**, **Label**, **Button**, **Image**, **Vertical Stack**, **Horizontal Stack** and **Grid**. Double click one to add it to the selected container.
- **Hierarchy** shows the widget tree. Stacks and grids lay out their children for you, so you don't have to position every button by hand.
- **The canvas** in the middle, with snapping to a grid and to guides, plus zoom and a preview resolution so you can check how it looks on different screens.
- **Details** on the right edits the selected widget: its **ID**, position and size, **Anchor** (which corner or edge it sticks to when the screen size changes), alignment, colours, text, font size, corner radius, opacity and a separate look for **Hovered** and **Pressed** buttons.
- **Arrange** has z order and grouping (`Ctrl+G` to group). `Ctrl+D` duplicates, arrow keys nudge.
- **Preview** lets you interact with the UI as it'll run.
- The **Controller** field under the canvas links a Lua script to this layout.

**Give every widget your script needs to touch a clear ID**, like `HealthText` or `StartButton`. That's how your script finds it.

## Showing UI from a script

Use `display.ui` with the path to the layout. You get back a handle, or `nil` and an error message:

```lua
local hud

function onAwake()
    local err
    hud, err = display.ui("assets/ui/Hud.hui")
    if not hud then
        print("Couldn't show the HUD: " .. tostring(err))
    end
end

function onDestroy()
    destroy.ui(hud)
end
```

If the layout has a controller script, it loads automatically. Showing the same layout twice replaces the first one rather than stacking two copies. `destroy.ui("current")` removes the most recently shown UI and `destroy.ui("all")` clears everything. Any UI still on screen is cleaned up when you stop playing.

## Talking to widgets

Look widgets up by their ID with `UI.getWidget`, then change them or react to them:

```lua
local hud = display.ui("assets/ui/Hud.hui")
local healthText = UI.getWidget(hud, "HealthText")
local startButton = UI.getWidget(hud, "StartButton")

UI.setText(healthText, "Health: 100")

UI.setOnClick(startButton, function()
    destroy.ui(hud)
    UI.setCursorMode(CursorMode.Game)
end)

UI.setOnHover(startButton, function(isHovered)
    UI.setFontBold(startButton, isHovered)
end)
```

There's a whole set of `UI.set...` functions for visibility, colours, fonts, sizes and layout, and you can even build screens entirely from code with `UI.createScreen`, `UI.createPanel`, `UI.createButton` and friends. They're all in the [Lua API reference](lua-api.md#ui).

Remember to free the mouse with `UI.setCursorMode(CursorMode.UI)` when a menu opens, and capture it again with `CursorMode.Game` when it closes.

## What's not there yet

HephaestusUI covers panels, text, buttons, images, stacks and grids. It doesn't have text input fields, sliders, checkboxes, scrolling lists or keyboard and gamepad navigation between buttons yet, so menus are mouse driven for now.
