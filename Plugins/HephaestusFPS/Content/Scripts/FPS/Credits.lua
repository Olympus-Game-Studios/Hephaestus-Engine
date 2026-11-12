-- A scrolling credits page for FPS.Menu: Credits.page(sections) where sections is { { heading = "MUSIC", lines =
-- { "...", ... } }, ... }, shown in order and followed by the engine and the libraries the game player ships with
-- (the "Game" rows of the engine's third-party notices). It scrolls up by itself and starts again after the end;
-- MenuUp and MenuDown (or the mouse wheel) scroll by hand and hold the automatic scroll for a few seconds. Enter, Escape
-- or BACK leave it. Reduce Motion (Menu.reducedMotion) turns the automatic scroll off.
local Menu = require("FPS.Menu")

local Credits = {}

Credits.engine = {
    { heading = "BUILT WITH", lines = { "Hephaestus Engine", "Rendered with Vulkan" } },
    { heading = "ENGINE LIBRARIES", lines = {
        "Lua 5.4 - MIT",
        "Vulkan Memory Allocator - MIT",
        "Jolt Physics - MIT",
        "EnTT - MIT",
        "GLFW - zlib",
        "JSON for Modern C++ (nlohmann/json) - MIT",
        "ufbx - MIT or public domain",
        "stb_image and stb_truetype - MIT or public domain",
        "miniaudio - MIT No Attribution",
        "Recast & Detour - zlib",
        "Roboto - Apache 2.0",
        "Full licence texts are in ThirdPartyNotices.txt beside the game",
    } },
}

local SPEED = 45 -- pixels a second
local HOLD_SECONDS = 4 -- after scrolling by hand
local LOOP_PAUSE = 2 -- at the end, before starting again
local OPEN_HOLD = 2 -- seconds the first lines stay put when the page opens
local WIDTH = 960
local HEADING_SIZE, LINE_SIZE = 24, 22
local LINE_GAP, SECTION_GAP = 36, 32
local FADE = 60 -- pixels at the top and bottom where lines fade in and out
local POOL = 40

local view = {}

-- Splits text at spaces so each piece fits `width` at `size`; a single word wider than that stays whole.
local function wrap(text, size, bold, width)
    local out, line = {}, nil
    for word in text:gmatch("%S+") do
        local candidate = line and (line .. " " .. word) or word
        if line and (UI.measureText(candidate, size, bold, Menu.theme.font) or 0) > width then
            out[#out + 1] = line
            line = word
        else
            line = candidate
        end
    end
    out[#out + 1] = line or ""
    return out
end

-- Flattens the sections into placed lines: { text, y, size, bold, accent }.
local function layout(sections, width)
    local lines, y = {}, 0
    for s, section in ipairs(sections) do
        if s > 1 then
            y = y + SECTION_GAP
        end
        if section.heading and section.heading ~= "" then
            lines[#lines + 1] = { text = section.heading, y = y, size = HEADING_SIZE, bold = true, accent = true }
            y = y + LINE_GAP + 6
        end
        for _, entry in ipairs(section.lines or {}) do
            for _, piece in ipairs(wrap(entry, LINE_SIZE, false, width)) do
                lines[#lines + 1] = { text = piece, y = y, size = LINE_SIZE, bold = false }
                y = y + LINE_GAP
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
        UI.setStyle(widget, { textColor = Menu.theme.text, backgroundColor = Menu.theme.clear, fontSize = LINE_SIZE,
                              fontBold = false, fontFamily = Menu.theme.font, textAlignment = "center",
                              cornerRadius = 0 })
        UI.setTextAlignment(widget, "center")
        ui.pool[i] = widget
    end
    return ui
end

-- Places the pool over the lines in view at the current scroll offset, fading them at the region's edges.
local function draw()
    local ui, region = view.ui, view.region
    local used = 0
    for _, line in ipairs(view.lines) do
        local y = region.top + region.height - view.offset + line.y
        if y + LINE_GAP > region.top and y < region.top + region.height and used < POOL then
            used = used + 1
            local widget = ui.pool[used]
            local color = line.accent and Menu.theme.accent or Menu.theme.text
            UI.setText(widget, line.text)
            UI.setFontSize(widget, line.size)
            UI.setFontBold(widget, line.bold)
            UI.setTextColor(widget, color.r, color.g, color.b, color.a)
            UI.setPosition(widget, region.x, y)
            UI.setSize(widget, region.width, LINE_GAP)
            local edge = math.min(y - region.top, region.top + region.height - (y + LINE_GAP))
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
    view.region = { x = x, top = top, width = width, height = math.max(LINE_GAP, menu.backY - 24 - top) }
    view.lines, view.length = layout(view.sections, width)
    view.backY = menu.backY
    UI.setPosition(view.ui.backdrop, x - 24, top - 12)
    UI.setSize(view.ui.backdrop, width + 48, view.region.height + 24)
end

-- The furthest the text scrolls: its end has left the top of the region.
local function maxOffset()
    return view.length + view.region.height
end

local custom = {}

function custom.show(menu)
    menu.creditsUi = menu.creditsUi or build(menu)
    view.ui = menu.creditsUi
    UI.setVisible(view.ui.backdrop, true)
    fit(menu)
    -- Starts with the first lines at the top (below the fade), so the screen is never empty when it opens.
    view.offset = math.max(0, math.min(view.region.height - FADE, maxOffset()))
    view.hold, view.pause = OPEN_HOLD, 0
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
        view.offset = math.max(0, math.min(maxOffset(), view.offset + step * LINE_GAP * 3))
        view.hold, view.pause = HOLD_SECONDS, 0
        if view.offset ~= before then
            Menu.tick(menu)
        end
    elseif view.hold > 0 then
        view.hold = view.hold - dt
    elseif not Menu.reducedMotion then
        if view.offset >= maxOffset() then
            view.pause = view.pause + dt
            if view.pause >= LOOP_PAUSE then
                view.offset, view.pause = 0, 0
            end
        else
            view.offset = math.min(maxOffset(), view.offset + SPEED * dt)
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
