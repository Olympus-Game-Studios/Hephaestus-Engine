-- A scrolling credits page for FPS.Menu: Credits.page(sections), shown in order and followed by the engine and the
-- libraries the game player ships with (the "Game" rows of the engine's third-party notices). A section is
--   { heading = "AUDIO", entries = { { role = "MUSIC", names = { "..." }, note = "..." }, ... } }
-- where each entry shows its role in small muted capitals, its names below in larger text and its note (a link or
-- licence) in small muted text under them; any of the three may be left out. The text holds at the top, scrolls up to
-- the end, holds and starts again. MenuUp and MenuDown (or the mouse wheel) scroll by hand within the same range (the
-- first line never goes below the top, the last never above the bottom) and the automatic scroll carries on from there
-- after a few seconds. Enter, Escape or BACK leave it. Reduce Motion (Menu.reducedMotion) turns the automatic scroll off.
local Menu = require("FPS.Menu")

local Credits = {}

Credits.engine = {
    { heading = "ENGINE", entries = {
        { role = "BUILT WITH", names = { "Hephaestus Engine" } },
        { role = "GRAPHICS", names = { "Vulkan" } },
    } },
    { heading = "THIRD-PARTY LIBRARIES", entries = {
        { role = "SCRIPTING", names = { "Lua 5.4" }, note = "MIT" },
        { role = "GPU MEMORY", names = { "Vulkan Memory Allocator" }, note = "MIT" },
        { role = "PHYSICS", names = { "Jolt Physics" }, note = "MIT" },
        { role = "ENTITY COMPONENT SYSTEM", names = { "EnTT" }, note = "MIT" },
        { role = "WINDOWS AND INPUT", names = { "GLFW" }, note = "zlib" },
        { role = "JSON", names = { "JSON for Modern C++ (nlohmann/json)" }, note = "MIT" },
        { role = "FBX IMPORT", names = { "ufbx" }, note = "MIT or public domain" },
        { role = "IMAGES AND FONTS", names = { "stb_image and stb_truetype" }, note = "MIT or public domain" },
        { role = "AUDIO", names = { "miniaudio" }, note = "MIT No Attribution" },
        { role = "NAVIGATION", names = { "Recast & Detour" }, note = "zlib" },
        { role = "TYPEFACE", names = { "Roboto" }, note = "Apache 2.0" },
        { note = "Full licence texts are in ThirdPartyNotices.txt beside the game" },
    } },
}

local SPEED = 45 -- units a second
local HOLD_SECONDS = 4 -- after scrolling by hand
local END_HOLD = 3 -- at the end, before starting again
local OPEN_HOLD = 2 -- at the start
local WIDTH = 960
local STEP = 108 -- how far one press of Up or Down, or one wheel notch, scrolls
local FADE = 48 -- units at the top and bottom where lines fade in and out
local POOL = 40
-- Each kind of line: font size, height taken, bold, colour key in Menu.theme.
local KINDS = {
    heading = { size = 26, height = 44, bold = true, color = "accent" },
    role = { size = 16, height = 24, bold = true, color = "muted" },
    name = { size = 24, height = 34, bold = false, color = "text" },
    note = { size = 16, height = 24, bold = false, color = "muted" },
}
local ENTRY_GAP, SECTION_GAP = 22, 56

local view = {}

-- Splits text at spaces so each piece fits `width`; a single word wider than that stays whole.
local function wrap(text, kind, width)
    local out, line = {}, nil
    for word in text:gmatch("%S+") do
        local candidate = line and (line .. " " .. word) or word
        if line and (UI.measureText(candidate, kind.size, kind.bold, Menu.theme.font) or 0) > width then
            out[#out + 1] = line
            line = word
        else
            line = candidate
        end
    end
    out[#out + 1] = line or ""
    return out
end

-- Flattens the sections into placed lines { text, y, kind } and the total height.
local function layout(sections, width)
    local lines, y = {}, 0
    local function add(text, kindName)
        local kind = KINDS[kindName]
        for _, piece in ipairs(wrap(text, kind, width)) do
            lines[#lines + 1] = { text = piece, y = y, kind = kind }
            y = y + kind.height
        end
    end
    for s, section in ipairs(sections) do
        if s > 1 then
            y = y + SECTION_GAP
        end
        if section.heading and section.heading ~= "" then
            add(section.heading, "heading")
            y = y + 8
        end
        for e, entry in ipairs(section.entries or {}) do
            if e > 1 then
                y = y + ENTRY_GAP
            end
            if entry.role then
                add(entry.role, "role")
            end
            for _, name in ipairs(entry.names or {}) do
                add(name, "name")
            end
            if entry.note then
                add(entry.note, "note")
            end
        end
    end
    return lines, y
end

local function screenWidth()
    local width = UI.getLayoutSize and UI.getLayoutSize() or nil
    return (width and width > 0) and width or 1920
end

local function build(menu)
    local root = menu.root
    local ui = { pool = {} }
    ui.backdrop = UI.createPanel(root)
    UI.setAnchor(ui.backdrop, "TopLeft")
    UI.setBackgroundColor(ui.backdrop, 0, 0, 0, 0.55)
    for i = 1, POOL do
        local widget = UI.createText(root, "")
        UI.setAnchor(widget, "TopLeft")
        UI.setStyle(widget, { textColor = Menu.theme.text, backgroundColor = Menu.theme.clear,
                              fontSize = KINDS.name.size, fontBold = false, fontFamily = Menu.theme.font,
                              textAlignment = "center", cornerRadius = 0 })
        UI.setTextAlignment(widget, "center")
        ui.pool[i] = widget
    end
    return ui
end

-- The scroll range: at the least the first line sits at the top of the view (just inside the fade), at the most
-- the last line sits at its bottom. Text shorter than the view never scrolls.
local function minOffset()
    return view.region.height - FADE
end

local function maxOffset()
    return math.max(minOffset(), view.length + FADE)
end

-- Places the pool over the lines in view at the current scroll offset, fading them at the region's edges.
local function draw()
    local ui, region = view.ui, view.region
    local used = 0
    for _, line in ipairs(view.lines) do
        local kind = line.kind
        local y = region.top + region.height - view.offset + line.y
        if y + kind.height > region.top and y < region.top + region.height and used < POOL then
            used = used + 1
            local widget = ui.pool[used]
            local color = Menu.theme[kind.color]
            UI.setText(widget, line.text)
            UI.setFontSize(widget, kind.size)
            UI.setFontBold(widget, kind.bold)
            UI.setTextColor(widget, color.r, color.g, color.b, color.a)
            UI.setPosition(widget, region.x, y)
            UI.setSize(widget, region.width, kind.height)
            local edge = math.min(y - region.top, region.top + region.height - (y + kind.height))
            UI.setOpacity(widget, math.max(0, math.min(1, edge / FADE)))
            UI.setVisible(widget, true)
        end
    end
    for i = used + 1, POOL do
        UI.setVisible(ui.pool[i], false)
    end
end

-- Fits the region between the title and BACK and lays the text out for it (again when the UI size changes).
local function fit(menu)
    local width = math.min(WIDTH, screenWidth() - 2 * menu.x)
    local x = math.floor((screenWidth() - width) / 2)
    local top = menu.y
    view.region = { x = x, top = top, width = width, height = math.max(3 * FADE, menu.backY - 24 - top) }
    view.lines, view.length = layout(view.sections, width)
    view.backY = menu.backY
    if view.offset then
        view.offset = math.max(minOffset(), math.min(maxOffset(), view.offset))
    end
    UI.setPosition(view.ui.backdrop, x - 24, top - 12)
    UI.setSize(view.ui.backdrop, width + 48, view.region.height + 24)
end

local custom = {}

function custom.show(menu)
    menu.creditsUi = menu.creditsUi or build(menu)
    view.ui = menu.creditsUi
    UI.setVisible(view.ui.backdrop, true)
    view.offset = nil
    fit(menu)
    view.offset = minOffset()
    view.hold = OPEN_HOLD
    view.savedHints = menu.hintText
    Menu.setHints(menu, "UP / DOWN  SCROLL        ENTER / ESC  BACK")
    Menu.selectBack(menu, true)
    draw()
end

function custom.hide(menu)
    if view.ui then
        UI.setVisible(view.ui.backdrop, false)
        for _, widget in ipairs(view.ui.pool) do
            UI.setVisible(widget, false)
        end
    end
    Menu.setHints(menu, view.savedHints)
end

function custom.update(menu)
    if view.backY ~= menu.backY then
        fit(menu)
    end
    local dt = Time.unscaledDeltaTime()
    local step = (Input.isActionPressed("MenuDown") and 1 or 0) - (Input.isActionPressed("MenuUp") and 1 or 0)
    local wheel = Input.getScrollDelta().y or 0
    if wheel ~= 0 then
        step = step + (wheel > 0 and -1 or 1)
    end
    if step ~= 0 then
        local before = view.offset
        view.offset = math.max(minOffset(), math.min(maxOffset(), view.offset + step * STEP))
        view.hold = HOLD_SECONDS
        if view.offset ~= before then
            Menu.tick(menu)
        end
    elseif view.hold > 0 then
        view.hold = view.hold - dt
    elseif not Menu.reducedMotion then
        if view.offset >= maxOffset() then
            view.offset, view.hold = minOffset(), OPEN_HOLD
        else
            view.offset = math.min(maxOffset(), view.offset + SPEED * dt)
            if view.offset >= maxOffset() then
                view.hold = END_HOLD
            end
        end
    end
    draw()
    if Input.isActionPressed("MenuSelect") or Input.isActionPressed("MenuBack") then
        return "back"
    end
    return nil
end

function Credits.page(sections)
    view.sections = {}
    for _, section in ipairs(sections or {}) do
        view.sections[#view.sections + 1] = section
    end
    for _, section in ipairs(Credits.engine) do
        view.sections[#view.sections + 1] = section
    end
    return { title = "CREDITS", custom = custom }
end

return Credits
