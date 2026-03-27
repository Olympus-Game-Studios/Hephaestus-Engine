# Lua display / destroy API Reference

## Overview

The `display` and `destroy` global tables provide a clean, high-level API for loading and unloading `.hui` UI assets at runtime from Lua scripts. They wrap the underlying HUI parser and UI system with managed lifecycle tracking, duplicate prevention, and a current-UI stack.

**Key properties:**
- Simple two-function API: `display.ui()` and `destroy.ui()`
- Automatic lifecycle management — all UI is cleaned up when play mode stops
- Duplicate prevention — re-displaying the same path replaces the previous instance
- Current-UI stack — `destroy.ui("current")` targets the most recently displayed UI
- Clear error reporting on every failure path

---

## API Reference

### display.ui(path)

Loads a `.hui` file, builds the UI widget tree, adds it to the active UI system, and makes it visible immediately.

**Signature:**

```lua
local handle, err = display.ui(path)
```

**Parameters:**

| Name | Type | Description |
|---|---|---|
| `path` | string | Path to a `.hui` file relative to the project root (or absolute). |

**Returns:**

| Value | Type | Description |
|---|---|---|
| `handle` | integer or nil | A UI handle on success, `nil` on failure. |
| `err` | string or nil | `nil` on success, a descriptive error string on failure. |

**Behavior:**

1. Validates the path is non-empty and has a `.hui` extension.
2. Resolves the path against project search paths.
3. If the same normalized path is already active, the old instance is destroyed first (replace semantics).
4. Parses the `.hui` JSON and builds the widget tree.
5. Adds the screen to the UI system (visible and active).
6. Registers all widget handles for `UI.getWidget()` lookups.
7. If the `.hui` specifies a `controller` Lua script, it is loaded automatically.
8. Pushes the handle onto the current-UI stack.

**Example:**

```lua
local MAIN_MENU = "assets/ui/MainMenu.hui"

local ui, err = display.ui(MAIN_MENU)
if not ui then
    print("Error: " .. tostring(err))
    return
end
```

---

### destroy.ui(target)

Destroys an active UI instance, removing it from the screen and freeing associated resources.

**Signature:**

```lua
local success = destroy.ui(target)
```

**Parameters:**

| Name | Type | Description |
|---|---|---|
| `target` | integer, string, or nil | A handle from `display.ui()`, or `"current"` / `"all"`. |

**Returns:**

| Value | Type | Description |
|---|---|---|
| `success` | boolean | `true` if something was destroyed, `false` otherwise. |

**Target values:**

| Target | Description |
|---|---|
| `handle` (integer) | Destroys the specific UI instance returned by `display.ui()`. |
| `"current"` | Destroys the most recently displayed UI (top of the stack). |
| `"all"` | Destroys every active managed UI instance. |
| `nil` | Prints a warning and returns `false`. |

**Behavior:**

1. Removes the screen from the UI system.
2. Unreferences all Lua callbacks (onClick, onHover) bound to widgets in that screen.
3. Cleans up widget handle mappings.
4. Removes the instance from the current-UI stack and path tracking.
5. Destroying an already-destroyed handle returns `false` (safe no-op).

**Examples:**

```lua
-- Destroy by handle
local ui = display.ui("assets/ui/HUD.hui")
destroy.ui(ui)

-- Destroy the most recent UI
destroy.ui("current")

-- Destroy everything
destroy.ui("all")
```

---

## Lifecycle Rules

### Duplicate prevention

Calling `display.ui()` with a path that is already active destroys the previous instance before loading the new one. This means calling `display.ui("assets/ui/HUD.hui")` twice does not create two overlapping copies — the first is replaced.

### Simulation stop cleanup

When play mode stops (editor) or the game shuts down, **all** managed HUI instances are automatically destroyed. Scripts do not need to manually clean up in `onDestroy`.

### Safe destruction

- Calling `destroy.ui(handle)` on an already-destroyed handle returns `false` with no side effects.
- Calling `destroy.ui("current")` when no UI is active prints a warning and returns `false`.
- Passing `nil` prints a clear error directing the user to use a handle or `"current"`.

### Repeated display calls

```lua
-- First call loads and displays the UI
local ui = display.ui("assets/ui/Menu.hui")

-- Second call with the same path: destroys the old one, creates a fresh instance
ui = display.ui("assets/ui/Menu.hui")
```

This is safe and intentional for cases like hot-reloading a UI during development.

---

## Current-UI Stack

The system maintains a stack of displayed UIs (most-recent-last). This enables layered menus:

```lua
local menu = display.ui("assets/ui/MainMenu.hui")
local settings = display.ui("assets/ui/Settings.hui")

-- "current" is now Settings
destroy.ui("current")  -- destroys Settings
-- "current" is now MainMenu
destroy.ui("current")  -- destroys MainMenu
```

The stack is purely for the `"current"` keyword. Each handle remains independently valid regardless of stack position.

---

## Integration with .hui Controllers

If a `.hui` file specifies a `"controller"` field:

```json
{
    "version": 1,
    "controller": "MainMenu.lua",
    "root": { ... }
}
```

Then `display.ui()` automatically loads the referenced Lua script after building the UI. The controller path is resolved relative to the `.hui` file's directory first, then falls back to the project search paths.

The controller script can define setup functions that accept the screen handle, or it can use global state established by the calling script.

---

## Combining with UI.getWidget

After `display.ui()` returns a handle, use `UI.getWidget()` to look up widgets by ID and bind behavior:

```lua
local HUD_PATH = "assets/ui/HUD.hui"

local hud, err = display.ui(HUD_PATH)
if not hud then
    print("HUD load failed: " .. tostring(err))
    return
end

local healthBar = UI.getWidget(hud, "health_bar")
local scoreText = UI.getWidget(hud, "score_text")

if scoreText then
    UI.setText(scoreText, "Score: 0")
end
```

All existing `UI.*` mutation functions (`setText`, `setVisible`, `setBackgroundColor`, `setOnClick`, etc.) work with widget handles from `.hui` screens loaded via `display.ui()`.

---

## Recommended Script Pattern

Define UI paths as local constants at the top of scripts for readability and easy refactoring:

```lua
-- Define UI assets at the top
local MAIN_MENU_UI = "assets/ui/MainMenu.hui"
local PAUSE_MENU_UI = "assets/ui/PauseMenu.hui"
local HUD_UI = "assets/ui/HUD.hui"

local menuHandle = nil
local hudHandle = nil

function onAwake()
    menuHandle = display.ui(MAIN_MENU_UI)

    local startBtn = UI.getWidget(menuHandle, "btn_start")
    if startBtn then
        UI.setOnClick(startBtn, function()
            destroy.ui(menuHandle)
            menuHandle = nil

            hudHandle = display.ui(HUD_UI)
            UI.setCursorMode(CursorMode.Game)
        end)
    end
end

function onUpdate(dt)
    if Input.isKeyPressed(256) then  -- Escape
        if hudHandle then
            display.ui(PAUSE_MENU_UI)
            UI.setCursorMode(CursorMode.UI)
        end
    end
end
```

**Why this pattern matters:**
- Paths are defined once, not scattered through logic.
- Handles are stored in local variables for explicit lifecycle control.
- `nil` checks prevent operations on already-destroyed UI.
- Easy to add/remove/rename UI assets by changing one line.

---

## Error Messages

| Error | Cause | Fix |
|---|---|---|
| `"File path is empty"` | Empty string passed to `display.ui()`. | Pass a valid `.hui` path. |
| `"Expected a .hui file, got: '...'"` | Non-`.hui` extension. | Ensure the path ends in `.hui`. |
| `"File not found: '...'"` | Path doesn't resolve to an existing file. | Check the path relative to the project root. |
| `"UI system not available"` | Called before UI system is initialized. | Ensure the UI system is active (play mode). |
| `"No current UI to destroy"` | `destroy.ui("current")` with no active UI. | Check handle validity before destroying. |
| `"Cannot destroy nil"` | Passed `nil` to `destroy.ui()`. | Use a saved handle or `"current"`. |
| JSON/parse errors | Malformed `.hui` file. | Check the `.hui` file for syntax errors. |

All errors are printed to the console log with a `[display.ui]` or `[destroy.ui]` prefix.

---

## Future Expansion

The handle returned by `display.ui()` is designed to support future extensions without breaking the existing API:

```lua
-- Planned (not yet implemented):
-- ui:show()
-- ui:hide()
-- ui:setVisible(bool)
-- ui:getWidget(id)
-- ui:bind(id, event, func)
```

These would be added as methods on a handle userdata type in a future release, maintaining backward compatibility with the current integer-handle approach.
