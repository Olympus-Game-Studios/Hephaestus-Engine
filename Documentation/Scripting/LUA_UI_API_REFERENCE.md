# Lua UI API Reference (HephaestusUI)

This reference documents the runtime UI API exposed to Lua through the global `UI` table.

Availability:
- API is available when Lua scripting is enabled and `LuaScriptEngine::SetUIBindings(...)` has been wired by the runtime target.

Namespace:
- Global table: `UI`

## 1. Concepts

The API uses integer handles:
- Screen handles: returned by screen creation/get operations
- Element handles: returned when creating widgets or querying roots

Handles are runtime-only:
- Valid only for current session/play run
- Invalidated if screens are cleared or removed

## 2. System/Screen API

### UI.isAvailable()

Signature:
```lua
local ok = UI.isAvailable()
```

Returns:
- `boolean`

Meaning:
- `true` when `UISystem`, `UIContext`, and `UIRenderer` are bound to Lua.

### UI.createSimpleMenu()

Signature:
```lua
local screenHandle = UI.createSimpleMenu()
```

Returns:
- `integer` screen handle
- `0` on failure

Behavior:
- Creates the built-in `MainMenu` screen if missing
- Reuses existing `MainMenu` if already present

### UI.createScreen(name)

Signature:
```lua
local screenHandle = UI.createScreen("HUD")
```

Parameters:
- `name: string`

Returns:
- `integer` screen handle
- `0` on failure

Behavior:
- Creates a new screen with that name if it does not exist
- Returns existing screen handle if it already exists

### UI.getRoot(screenHandle)

Signature:
```lua
local rootElement = UI.getRoot(screenHandle)
```

Returns:
- `integer` element handle for the screen's root canvas
- `0` on failure

### UI.clearScreens()

Signature:
```lua
UI.clearScreens()
```

Effect:
- Removes all runtime UI screens
- Invalidates previously returned handles

### UI.getScreenCount()

Signature:
```lua
local count = UI.getScreenCount()
```

Returns:
- `integer`

### UI.setScreenVisible(screenName, visible)

Signature:
```lua
local ok = UI.setScreenVisible("HUD", true)
```

Returns:
- `boolean`

### UI.setScreenActive(screenName, active)

Signature:
```lua
local ok = UI.setScreenActive("HUD", true)
```

Returns:
- `boolean`

### UI.setViewportSize(width, height)

Signature:
```lua
local ok = UI.setViewportSize(1920, 1080)
```

Returns:
- `boolean`

### UI.getDrawCommandCount()

Signature:
```lua
local commands = UI.getDrawCommandCount()
```

Returns:
- `integer`

Use case:
- Debug/validation of UI command generation.

## 3. Widget Creation API

Each create call takes a parent element handle and returns a new element handle.

### UI.createPanel(parentHandle)
```lua
local panel = UI.createPanel(parent)
```

### UI.createText(parentHandle, text)
```lua
local label = UI.createText(parent, "Hello")
```

### UI.createButton(parentHandle, label)
```lua
local button = UI.createButton(parent, "Play")
```

### UI.createImage(parentHandle, imageId)
```lua
local image = UI.createImage(parent, "textures/ui/logo.png")
```

### UI.createVerticalBox(parentHandle)
```lua
local vbox = UI.createVerticalBox(parent)
```

### UI.createHorizontalBox(parentHandle)
```lua
local hbox = UI.createHorizontalBox(parent)
```

Return contract for all creates:
- `integer` element handle on success
- `0` on failure

## 4. Widget Property API

### UI.setPosition(elementHandle, x, y)
```lua
UI.setPosition(element, 32, 64)
```

### UI.setSize(elementHandle, width, height)
```lua
UI.setSize(element, 300, 80)
```

### UI.setVisible(elementHandle, visible)
```lua
UI.setVisible(element, true)
```

### UI.setOpacity(elementHandle, opacity)
```lua
UI.setOpacity(element, 0.85)
```

### UI.setZOrder(elementHandle, zOrder)
```lua
UI.setZOrder(element, 10)
```

### UI.setText(elementHandle, text)
```lua
UI.setText(labelOrButton, "New Text")
```

Supported element types:
- `UIText`
- `UIButton` (updates internal label)

### UI.setImage(elementHandle, imageId)
```lua
UI.setImage(imageElement, "textures/ui/new_icon.png")
```

Supported element type:
- `UIImage`

### UI.setSpacing(elementHandle, spacing)
```lua
UI.setSpacing(vboxOrHbox, 12)
```

Supported element types:
- `UIVerticalBox`
- `UIHorizontalBox`

Return contract for setters:
- `boolean` success/failure

### UI.setOnClick(buttonHandle, callback)

Register a click callback for a button element. When the button is clicked (mouse press and release on the same button), the callback function is invoked.

```lua
local button = UI.createButton(parent, "Click Me")
UI.setOnClick(button, function()
    print("Button was clicked!")
end)
```

Parameters:
- `buttonHandle: integer` — handle returned by `UI.createButton`
- `callback: function` — Lua function to call when clicked

Returns:
- `boolean` — `true` on success, `false` if handle is invalid or not a button

Notes:
- Only one callback per button; setting a new callback replaces the previous one
- Callbacks are automatically cleaned up when play mode stops
- The button must be a `UIButton` element (not text, panel, etc.)

## 5. Cursor Mode API

### UI.setCursorMode(mode)

Switch between gameplay and UI cursor modes. This is the recommended high-level API for managing cursor lock/visibility when transitioning between gameplay and UI screens (pause menus, inventories, dialogue, etc.).

Signature:
```lua
local ok = UI.setCursorMode("ui")
```

Parameters:
- `mode: string` — `"ui"` or `"game"` (case-insensitive)

You can also use the `CursorMode` constants table:
```lua
UI.setCursorMode(CursorMode.UI)
UI.setCursorMode(CursorMode.Game)
```

Returns:
- `boolean` — `true` on success

Behavior when `"ui"`:
- Cursor becomes visible and unlocked
- Mouse input routes to UI widgets
- In the Qt editor, the editor camera controller is suppressed so it does not interfere
- In the GLFW runtime, cursor is set to `GLFW_CURSOR_NORMAL`

Behavior when `"game"`:
- Cursor is hidden and captured
- Mouse input routes to gameplay systems (camera look, etc.)
- In the Qt editor, the viewport captures the cursor
- In the GLFW runtime, cursor is set to `GLFW_CURSOR_DISABLED` with raw mouse motion

### UI.getCursorMode()

Query the current cursor mode.

Signature:
```lua
local mode = UI.getCursorMode()
```

Returns:
- `string` — `"ui"` or `"game"`

### CursorMode Constants

A global `CursorMode` table is registered with named constants:
- `CursorMode.UI` — string `"ui"`
- `CursorMode.Game` — string `"game"`

### Typical Usage Pattern

```lua
local function showPauseMenu()
    UI.setScreenVisible("PauseMenu", true)
    UI.setScreenActive("PauseMenu", true)
    UI.setCursorMode(CursorMode.UI)
end

local function hidePauseMenu()
    UI.setScreenVisible("PauseMenu", false)
    UI.setScreenActive("PauseMenu", false)
    UI.setCursorMode(CursorMode.Game)
end
```

## 6. Complete Example

```lua
if not UI.isAvailable() then
    return
end

local screen = UI.createScreen("PauseMenu")
local root = UI.getRoot(screen)

local panel = UI.createPanel(root)
UI.setPosition(panel, 620, 220)
UI.setSize(panel, 680, 520)
UI.setZOrder(panel, 5)

local title = UI.createText(panel, "Paused")
UI.setPosition(title, 24, 20)
UI.setSize(title, 632, 48)

local vbox = UI.createVerticalBox(panel)
UI.setPosition(vbox, 24, 96)
UI.setSize(vbox, 632, 360)
UI.setSpacing(vbox, 10)

local resumeBtn = UI.createButton(vbox, "Resume")
UI.setSize(resumeBtn, 632, 64)

local settingsBtn = UI.createButton(vbox, "Settings")
UI.setSize(settingsBtn, 632, 64)

local quitBtn = UI.createButton(vbox, "Quit")
UI.setSize(quitBtn, 632, 64)

UI.setScreenVisible("PauseMenu", true)
UI.setScreenActive("PauseMenu", true)
```

## 7. Error Handling Guidance

Recommended pattern:

```lua
local function assertHandle(h, msg)
    if h == 0 then
        error(msg)
    end
    return h
end

local screen = assertHandle(UI.createScreen("HUD"), "Failed to create HUD screen")
```

## 8. More Functions

Sections 2 to 4 cover the core calls. The `UI` table also has the following; each takes the element handle first. For exact argument lists see the bindings in `engine/Scripting/LuaScriptEngine.cpp` (`RegisterUIBindings`) and the examples in [HUI_FORMAT_REFERENCE.md](../Features/HUI_FORMAT_REFERENCE.md).

- **Grids:** `UI.createGrid(parent)`, `UI.setGridColumns`, `UI.setGridSpacing`
- **Layout:** `UI.setPadding`, `UI.setMargin`, `UI.setMinSize`, `UI.setMaxSize`, `UI.setAnchor` (`UIAnchor.*`), `UI.setAlignment` (`UIAlign.*`), `UI.setChildAlignment`
- **State:** `UI.setEnabled`, `UI.setFocused`
- **Style:** `UI.setBackgroundColor`, `UI.setBorderColor`, `UI.setBorderWidth`, `UI.setCornerRadius`, `UI.setTextColor`, `UI.setFontSize`, `UI.setFontBold`, `UI.setFontItalic`, `UI.setTextAlignment`, `UI.setTextShadow`, `UI.setWordWrap`, `UI.setStyle`, `UI.setButtonStyle`, `UI.registerThemeStyle`, `UI.applyThemeStyle`
- **Images:** `UI.setImageScaleMode` (`UIScaleMode.*`), `UI.setImageTint`
- **Events:** `UI.setOnHover(element, function(isHovered) end)` alongside `UI.setOnClick`
- **`.hui` files:** `UI.loadHUI(path)` and `UI.getWidget(handle, id)`; prefer the managed `display.ui` / `destroy.ui` described in [LUA_DISPLAY_DESTROY_API.md](./LUA_DISPLAY_DESTROY_API.md)

## 9. Current Limitations

Not yet exposed in Lua:
- Widget removal (destroy whole screens with `destroy.ui` or `UI.clearScreens`)
- Child enumeration

Not in HephaestusUI at all yet: text input fields, sliders, checkboxes, scrolling lists and keyboard or gamepad focus navigation.
