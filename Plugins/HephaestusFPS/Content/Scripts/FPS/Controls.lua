-- Rebindable controls: the Controls page of the settings menu and the saved bindings. Each row is an input action,
-- or one direction of a movement axis, with two keyboard or mouse slots and a gamepad button slot. Choose a slot and
-- press the new key or button; a control already used by another row moves there in exchange. Delete clears a slot.
-- FPS.Settings saves the bindings in its settings file (Controls.serialize) and applies them at startup
-- (Controls.apply); Controls.changed is called after every change. Escape, F1 and F2 stay reserved for the menus.
local Menu = require("FPS.Menu")

local Controls = {}

Controls.rows = {
    { label = "MOVE FORWARD", name = "MoveForward", sign = 1 },
    { label = "MOVE BACK", name = "MoveForward", sign = -1 },
    { label = "MOVE LEFT", name = "MoveRight", sign = -1 },
    { label = "MOVE RIGHT", name = "MoveRight", sign = 1 },
    { label = "JUMP", name = "Jump" },
    { label = "SPRINT", name = "Sprint" },
    { label = "CROUCH", name = "Crouch" },
    { label = "PRONE", name = "Prone" },
    { label = "LEAN LEFT", name = "LeanLeft" },
    { label = "LEAN RIGHT", name = "LeanRight" },
    { label = "FIRE", name = "Fire" },
    { label = "AIM DOWN SIGHTS", name = "Aim" },
    { label = "RELOAD", name = "Reload" },
    { label = "INTERACT", name = "Interact" },
    { label = "NEXT WEAPON", name = "NextWeapon" },
    { label = "PREVIOUS WEAPON", name = "PreviousWeapon" },
    { label = "WEAPON 1", name = "Weapon1" },
    { label = "WEAPON 2", name = "Weapon2" },
    { label = "WEAPON 3", name = "Weapon3" },
    { label = "WEAPON 4", name = "Weapon4" },
    { label = "WEAPON 5", name = "Weapon5" },
    { label = "FRAG GRENADE", name = "ThrowFrag" },
    { label = "SMOKE GRENADE", name = "ThrowSmoke" },
    { label = "SCOPE ZOOM IN", name = "ZoomIn" },
    { label = "SCOPE ZOOM OUT", name = "ZoomOut" },
    { label = "SCOREBOARD", name = "Scoreboard" },
}

Controls.reserved = { ESCAPE = true, F1 = true, F2 = true }

local PAD = 3
local ROW_HEIGHT = 40
local LABEL_WIDTH = 330
local CELL_WIDTH = 220
local GAP = 12
local MAX_LINES = 24

local KINDS = {
    keyboard = "key", mouse_button = "mouse", gamepad_button = "pad", gamepad_axis = "stick",
    mouse_delta = "mousemove", mouse_wheel = "wheel",
}
local TYPES = {}
for type, kind in pairs(KINDS) do
    TYPES[kind] = type
end

local PAD_NAMES = {
    SOUTH = "A", EAST = "B", WEST = "X", NORTH = "Y", LEFT_BUMPER = "LB", RIGHT_BUMPER = "RB", BACK = "BACK",
    START = "START", GUIDE = "GUIDE", LEFT_STICK = "LS", RIGHT_STICK = "RS", DPAD_UP = "D-PAD UP",
    DPAD_DOWN = "D-PAD DOWN", DPAD_LEFT = "D-PAD LEFT", DPAD_RIGHT = "D-PAD RIGHT",
}
local MOUSE_NAMES = { LEFT = "LEFT MOUSE", RIGHT = "RIGHT MOUSE", MIDDLE = "MIDDLE MOUSE" }

-- The rows whose action or axis the game's input map has.
local function available()
    local list = {}
    for _, row in ipairs(Controls.rows) do
        if Input.getBindings(row.name) then
            list[#list + 1] = row
        end
    end
    return list
end

local function isPointer(binding)
    return binding.type == "keyboard" or binding.type == "mouse_button"
end

-- Whether a binding of row.name is one of this row's slots: a key or mouse button (of the row's direction on an
-- axis), or for an action, a gamepad button.
local function belongs(row, binding)
    if row.sign then
        return isPointer(binding) and ((binding.scale or 1) < 0) == (row.sign < 0)
    end
    return isPointer(binding) or binding.type == "gamepad_button"
end

-- The row's three slots (nil where empty): two keys or mouse buttons, then a gamepad button.
function Controls.slots(row)
    local slots = {}
    local pointer = 0
    for _, binding in ipairs(Input.getBindings(row.name) or {}) do
        if belongs(row, binding) then
            if isPointer(binding) and pointer < 2 then
                pointer = pointer + 1
                slots[pointer] = binding
            elseif binding.type == "gamepad_button" and not slots[PAD] then
                slots[PAD] = binding
            end
        end
    end
    return slots
end

local function setSlots(row, slots)
    local list = {}
    for i = 1, PAD do
        local binding = slots[i]
        if binding then
            list[#list + 1] = { type = binding.type, control = binding.control, scale = row.sign or 1 }
        end
    end
    for _, binding in ipairs(Input.getBindings(row.name) or {}) do
        if not belongs(row, binding) then
            list[#list + 1] = binding
        end
    end
    local ok, err = Input.setBindings(row.name, list)
    if not ok then
        engine_log("[FPS.Controls] " .. tostring(err))
    end
end

local function sameControl(a, b)
    return a ~= nil and b ~= nil and a.type == b.type and a.control == b.control
end

-- Puts control in slot `slot` of `row`. Another row (or slot) already using it gets this slot's old control in
-- exchange. Returns the label of the row it came from, if any.
function Controls.assign(row, slot, control)
    local rows = available()
    local previous = Controls.slots(row)[slot]
    local from = nil
    for _, other in ipairs(rows) do
        local slots = Controls.slots(other)
        for i = 1, PAD do
            if sameControl(slots[i], control) and not (other == row and i == slot) then
                slots[i] = previous
                setSlots(other, slots)
                from = other.label
            end
        end
    end
    local slots = Controls.slots(row)
    slots[slot] = control
    setSlots(row, slots)
    return from
end

function Controls.clear(row, slot)
    local slots = Controls.slots(row)
    slots[slot] = nil
    setSlots(row, slots)
end

function Controls.save()
    if Controls.changed then
        Controls.changed()
    end
end

-- "key:SPACE, pad:SOUTH": each binding as kind:CONTROL, with *scale when it is not 1.
function Controls.encode(list)
    local parts = {}
    for _, binding in ipairs(list) do
        local scale = binding.scale or 1
        parts[#parts + 1] = (KINDS[binding.type] or binding.type) .. ":" .. binding.control
            .. (scale ~= 1 and string.format("*%g", scale) or "")
    end
    return table.concat(parts, ", ")
end

-- The bindings in text written by Controls.encode, or nil and a message.
function Controls.decode(text)
    local list = {}
    for part in string.gmatch(text or "", "[^,]+") do
        local kind, control, scale = part:match("^%s*([%a_]+)%s*:%s*([%w_]+)%s*%*?%s*(%-?[%d%.]*)%s*$")
        if not kind or not TYPES[kind] then
            return nil, "'" .. part:match("^%s*(.-)%s*$") .. "' is not kind:CONTROL"
        end
        list[#list + 1] = { type = TYPES[kind], control = control:upper(), scale = tonumber(scale) or 1 }
    end
    return list
end

-- The bindings of every row's action or axis, for a settings file: { { key, value, comment } }.
function Controls.serialize()
    local entries, seen = {}, {}
    for _, row in ipairs(available()) do
        if not seen[row.name] then
            seen[row.name] = true
            entries[#entries + 1] = { key = row.name, value = Controls.encode(Input.getBindings(row.name)) }
        end
    end
    return entries
end

-- Applies { name = text } written by Controls.serialize; a name the game does not have, or text that does not
-- read, is logged and leaves that action as it was. Returns how many were applied.
function Controls.apply(section, source)
    local applied = 0
    for name, text in pairs(section or {}) do
        local list, err = Controls.decode(text)
        local ok = false
        if list and Input.getBindings(name) then
            ok, err = Input.setBindings(name, list)
        elseif list then
            err = "the game has no action or axis with that name"
        end
        if ok then
            applied = applied + 1
        else
            engine_log(string.format("[FPS.Controls] %s: %s = %s ignored: %s", source or "settings", name,
                                     tostring(text), tostring(err)))
        end
    end
    return applied
end

function Controls.resetToDefaults()
    for _, row in ipairs(available()) do
        Input.setBindings(row.name, Input.getBindings(row.name, true))
    end
    Controls.save()
end

function Controls.describe(binding)
    if not binding then
        return ""
    end
    if binding.type == "mouse_button" then
        return MOUSE_NAMES[binding.control] or ("MOUSE " .. tostring((tonumber(binding.control) or 0) + 1))
    end
    if binding.type == "gamepad_button" then
        return PAD_NAMES[binding.control] or binding.control
    end
    return (binding.control:gsub("_", " "))
end

-- The page ----------------------------------------------------------------------------------------------------
local view = { menu = nil }

local function place(widget, x, y, w, h)
    UI.setAnchor(widget, "TopLeft")
    UI.setPosition(widget, x, y)
    UI.setSize(widget, w, h)
end

local function text(parent, x, y, w, h, size, bold, color, align)
    local widget = UI.createText(parent, "")
    place(widget, x, y, w, h)
    UI.setStyle(widget, { textColor = color or Menu.theme.text, backgroundColor = Menu.theme.clear, fontSize = size,
                          fontBold = bold, fontFamily = Menu.theme.font, textAlignment = align or "left",
                          cornerRadius = 0 })
    UI.setTextAlignment(widget, align or "left")
    return widget
end

local function panel(parent, x, y, w, h, color)
    local widget = UI.createPanel(parent)
    place(widget, x, y, w, h)
    UI.setBackgroundColor(widget, color.r, color.g, color.b, color.a)
    return widget
end

local function cellX(menu, column)
    return menu.x + LABEL_WIDTH + (column - 1) * (CELL_WIDTH + GAP)
end

local function hints(capturing)
    Menu.setHints(view.menu, capturing and "PRESS A KEY, MOUSE BUTTON OR PAD BUTTON        ESC  CANCEL"
        or "ENTER  REBIND        DELETE  CLEAR        ARROWS  MOVE        ESC  BACK")
end

local function refresh()
    local menu = view.menu
    local ui = menu.controls
    local rows = view.rows
    local count = #rows + 1
    local lines = math.max(3, math.min(#ui.lines, math.floor((menu.backY - 50 - ui.top) / ROW_HEIGHT)))
    view.lines = lines
    view.selected = math.max(1, math.min(view.selected, count + 1))
    local key = view.selected * 10 + view.column
    if view.tickKey and view.tickKey ~= key then
        Menu.tick(menu)
    end
    view.tickKey = key
    Menu.selectBack(menu, view.selected == count + 1)
    if view.selected <= count and view.selected < view.first then
        view.first = view.selected
    elseif view.selected <= count and view.selected >= view.first + lines then
        view.first = view.selected - lines + 1
    end
    view.first = math.max(1, math.min(view.first, count - lines + 1))
    UI.setSize(ui.backdrop, ui.width, 42 + lines * ROW_HEIGHT + 44)
    UI.setPosition(ui.status, menu.x, ui.top + lines * ROW_HEIGHT + 6)
    local scrolling = count > lines
    UI.setVisible(ui.scrollTrack, scrolling)
    UI.setVisible(ui.scrollThumb, scrolling)
    if scrolling then
        local track = lines * ROW_HEIGHT - 6
        UI.setSize(ui.scrollTrack, 3, track)
        UI.setPosition(ui.scrollThumb, ui.scrollX, ui.top + track * (view.first - 1) / count)
        UI.setSize(ui.scrollThumb, 3, track * lines / count)
    end
    local theme = Menu.theme
    for i, line in ipairs(ui.lines) do
        local index = view.first + i - 1
        local row = i <= lines and rows[index] or nil
        local isReset = i <= lines and index == count
        local shown = row ~= nil or isReset
        UI.setVisible(line.label, shown)
        UI.setText(line.label, isReset and "RESET TO DEFAULTS" or (row and row.label or ""))
        local slots = row and Controls.slots(row) or {}
        for column, cell in ipairs(line.cells) do
            local visible = row ~= nil
            local selected = index == view.selected and (isReset or column == view.column)
            local capturing = view.capture and view.capture.row == row and view.capture.column == column
            UI.setVisible(cell.back, visible)
            UI.setVisible(cell.text, visible)
            UI.setVisible(cell.button, visible)
            if visible then
                UI.setText(cell.text, capturing and "PRESS..." or Controls.describe(slots[column]))
                local back = (selected or capturing) and theme.accent or theme.tile
                UI.setBackgroundColor(cell.back, back.r, back.g, back.b, back.a)
                local ink = (selected or capturing) and theme.ink or theme.text
                UI.setTextColor(cell.text, ink.r, ink.g, ink.b, ink.a)
            end
        end
        local resetSelected = isReset and index == view.selected
        local labelInk = resetSelected and theme.accent or theme.text
        UI.setTextColor(line.label, labelInk.r, labelInk.g, labelInk.b, labelInk.a)
        UI.setVisible(line.resetButton, isReset)
    end
    UI.setText(ui.status, view.status or "")
end

local function startCapture(index, column)
    local row = view.rows[index]
    if not row then
        Controls.resetToDefaults()
        view.status = "Controls reset to defaults"
        refresh()
        return
    end
    view.capture = { row = row, column = column, wait = true }
    view.status = nil
    hints(true)
    refresh()
end

local function finishCapture(control)
    local capture = view.capture
    if not control then
        view.capture = nil
        hints(false)
        refresh()
        return
    end
    local wantsPad = capture.column == PAD
    if control.type == "keyboard" and Controls.reserved[control.control] then
        view.status = Controls.describe(control) .. " is kept for the menus"
    elseif wantsPad ~= (control.type == "gamepad_button") then
        view.status = wantsPad and "Press a gamepad button" or "Press a key or mouse button"
        refresh()
        return
    else
        local from = Controls.assign(capture.row, capture.column, control)
        view.status = from and string.format("%s moved from %s", Controls.describe(control), from) or nil
        Controls.save()
    end
    view.capture = nil
    hints(false)
    refresh()
end

local function build(menu)
    local root = menu.root
    local ui = { lines = {} }
    local top = menu.y + 34
    local visible = MAX_LINES
    local width = LABEL_WIDTH + PAD * CELL_WIDTH + (PAD - 1) * GAP + 36
    ui.top, ui.width = top, width
    ui.backdrop = panel(root, menu.barX - 6, menu.y - 8, width, 42 + visible * ROW_HEIGHT + 44,
                        { r = 0, g = 0, b = 0, a = 0.55 })
    ui.scrollX = cellX(menu, PAD) + CELL_WIDTH + 12
    ui.scrollTrack = panel(root, ui.scrollX, top, 3, 0, Menu.theme.track)
    ui.scrollThumb = panel(root, ui.scrollX, top, 3, 0, Menu.theme.accent)
    ui.heads = {}
    for column, title in ipairs({ "PRIMARY", "SECONDARY", "GAMEPAD" }) do
        ui.heads[column] = text(root, cellX(menu, column) + 12, menu.y, CELL_WIDTH - 12, 28, 16, true, Menu.theme.muted)
        UI.setText(ui.heads[column], title)
    end
    for i = 1, visible do
        local y = top + (i - 1) * ROW_HEIGHT
        local line = { cells = {} }
        line.label = text(root, menu.x, y, LABEL_WIDTH - 10, ROW_HEIGHT - 6, 20, true)
        line.resetButton = UI.createButton(root, "")
        place(line.resetButton, menu.x - 8, y, LABEL_WIDTH, ROW_HEIGHT - 6)
        for _, state in ipairs({ "normal", "hovered", "pressed", "disabled" }) do
            UI.setButtonStyle(line.resetButton, state, { textColor = Menu.theme.clear, backgroundColor = Menu.theme.clear })
        end
        UI.setOnHover(line.resetButton, function(hovered)
            if hovered and view.visible and not view.capture then
                view.selected = view.first + i - 1
                refresh()
            end
        end)
        UI.setOnClick(line.resetButton, function()
            if view.visible and not view.capture and view.first + i - 1 == #view.rows + 1 then
                startCapture(#view.rows + 1, 1)
            end
        end)
        for column = 1, PAD do
            local x = cellX(menu, column)
            local cell = {
                back = panel(root, x, y, CELL_WIDTH, ROW_HEIGHT - 6, Menu.theme.tile),
                text = text(root, x + 12, y, CELL_WIDTH - 24, ROW_HEIGHT - 6, 18, true, nil, "left"),
                button = UI.createButton(root, ""),
            }
            place(cell.button, x, y, CELL_WIDTH, ROW_HEIGHT - 6)
            for _, state in ipairs({ "normal", "hovered", "pressed", "disabled" }) do
                UI.setButtonStyle(cell.button, state, { textColor = Menu.theme.clear, backgroundColor = Menu.theme.clear })
            end
            UI.setOnHover(cell.button, function(hovered)
                if hovered and view.visible and not view.capture then
                    view.selected, view.column = view.first + i - 1, column
                    refresh()
                end
            end)
            UI.setOnClick(cell.button, function()
                if view.visible and not view.capture then
                    view.selected, view.column = view.first + i - 1, column
                    startCapture(view.selected, column)
                end
            end)
            line.cells[column] = cell
        end
        ui.lines[i] = line
    end
    ui.status = text(root, menu.x, top + visible * ROW_HEIGHT + 6, 900, 30, 18, false, Menu.theme.muted)
    ui.all = { ui.backdrop, ui.heads[1], ui.heads[2], ui.heads[3], ui.status, ui.scrollTrack, ui.scrollThumb }
    for _, line in ipairs(ui.lines) do
        ui.all[#ui.all + 1] = line.label
        ui.all[#ui.all + 1] = line.resetButton
        for _, cell in ipairs(line.cells) do
            ui.all[#ui.all + 1] = cell.back
            ui.all[#ui.all + 1] = cell.text
            ui.all[#ui.all + 1] = cell.button
        end
    end
    return ui
end

local custom = {}

function custom.show(menu)
    view.menu = menu
    menu.controls = menu.controls or build(menu)
    view.rows = available()
    view.selected, view.column, view.first, view.capture, view.status = 1, 1, 1, nil, nil
    view.tickKey = nil
    view.visible = true
    view.savedHints = menu.hintText
    for _, widget in ipairs(menu.controls.all) do
        UI.setVisible(widget, true)
    end
    hints(false)
    refresh()
end

function custom.hide(menu)
    view.visible = false
    view.capture = nil
    if menu.controls then
        for _, widget in ipairs(menu.controls.all) do
            UI.setVisible(widget, false)
        end
    end
    Menu.setHints(menu, view.savedHints)
end

function custom.update(menu)
    if view.backY ~= menu.backY then
        view.backY = menu.backY
        refresh()
    end
    if view.capture then
        if view.capture.wait then
            view.capture.wait = false
            return nil
        end
        local control = Input.getPressedControl()
        if control then
            if control.type == "keyboard" and control.control == "ESCAPE" then
                finishCapture(nil)
            else
                finishCapture(control)
            end
        end
        return nil
    end
    local count = #view.rows + 2
    local wheel = Input.getScrollDelta().y or 0
    if wheel ~= 0 and view.lines and count - 1 > view.lines then
        view.first = math.max(1, math.min(view.first - (wheel > 0 and 1 or -1), count - view.lines))
        if view.selected < count then
            view.selected = math.max(view.first, math.min(view.selected, view.first + view.lines - 1))
        end
        refresh()
    end
    if Input.isActionPressed("MenuUp") then
        view.selected = (view.selected - 2) % count + 1
        refresh()
    elseif Input.isActionPressed("MenuDown") then
        view.selected = view.selected % count + 1
        refresh()
    elseif Input.isActionPressed("MenuLeft") then
        view.column = math.max(1, view.column - 1)
        refresh()
    elseif Input.isActionPressed("MenuRight") then
        view.column = math.min(PAD, view.column + 1)
        refresh()
    elseif Input.isActionPressed("MenuSelect") then
        if view.selected == count then
            return "back"
        end
        startCapture(view.selected, view.column)
    elseif Input.isKeyPressed("DELETE") and view.rows[view.selected] then
        Controls.clear(view.rows[view.selected], view.column)
        Controls.save()
        refresh()
    elseif Input.isActionPressed("MenuBack") then
        return "back"
    end
    return nil
end

function Controls.page()
    return { title = "CONTROLS", custom = custom }
end

return Controls
