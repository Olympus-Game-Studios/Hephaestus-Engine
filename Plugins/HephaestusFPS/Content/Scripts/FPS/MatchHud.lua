-- Team deathmatch screens built from code: the kill feed in the top right corner and the scoreboard (both teams'
-- soldiers with their kills and deaths). TeamDeathmatch.lua drives them; Hud's wave text shows the score and clock.
local Menu = require("FPS.Menu")

local MatchHud = {}

local FEED_ROWS = 5
local FEED_WIDTH = 560
local FEED_ROW_HEIGHT = 34
local FEED_SIZE = 20
local BOARD_WIDTH = 1040
local BOARD_ROWS = 8
local BOARD_ROW_HEIGHT = 40
local COLUMN_WIDTH = 500

MatchHud.teamColors = {
    { r = 0.35, g = 0.66, b = 1.0, a = 1.0 },
    { r = 1.0, g = 0.32, b = 0.24, a = 1.0 },
}

local function place(widget, anchor, x, y, w, h)
    UI.setAnchor(widget, anchor)
    UI.setPosition(widget, x, y)
    UI.setSize(widget, w, h)
end

local function text(parent, x, y, w, h, size, bold, color, align)
    local widget = UI.createText(parent, "")
    place(widget, "TopLeft", x, y, w, h)
    UI.setStyle(widget, { textColor = color or Menu.theme.text, backgroundColor = Menu.theme.clear, fontSize = size,
                          fontBold = bold, fontFamily = Menu.theme.font, textAlignment = align or "left",
                          cornerRadius = 0 })
    UI.setTextAlignment(widget, align or "left")
    return widget
end

local function panel(parent, anchor, x, y, w, h, color)
    local widget = UI.createPanel(parent)
    place(widget, anchor, x, y, w, h)
    UI.setBackgroundColor(widget, color.r, color.g, color.b, color.a)
    return widget
end

local function setColor(widget, color)
    UI.setTextColor(widget, color.r, color.g, color.b, color.a)
end

local function width(content, size, bold)
    local w = UI.measureText(content, size, bold, Menu.theme.font)
    return w or (#content * size * 0.55)
end

-- Builds the screens; teamNames is { "BLUE", "RED" }. Call again after a scene change.
function MatchHud.mount(teamNames)
    MatchHud.teamNames = teamNames
    MatchHud.screen = UI.createScreen("MatchHud")
    local root = UI.getRoot(MatchHud.screen)

    MatchHud.feed = panel(root, "TopRight", -FEED_WIDTH - 32, 150, FEED_WIDTH, FEED_ROWS * FEED_ROW_HEIGHT,
                          Menu.theme.clear)
    MatchHud.feedRows = {}
    for i = 1, FEED_ROWS do
        local y = (i - 1) * FEED_ROW_HEIGHT
        MatchHud.feedRows[i] = {
            back = panel(MatchHud.feed, "TopLeft", 0, y, 0, FEED_ROW_HEIGHT - 4, Menu.theme.tile),
            killer = text(MatchHud.feed, 0, y, 0, FEED_ROW_HEIGHT - 4, FEED_SIZE, true),
            word = text(MatchHud.feed, 0, y, 0, FEED_ROW_HEIGHT - 4, FEED_SIZE - 2, false, Menu.theme.muted),
            victim = text(MatchHud.feed, 0, y, 0, FEED_ROW_HEIGHT - 4, FEED_SIZE, true),
        }
    end

    local height = 120 + BOARD_ROWS * BOARD_ROW_HEIGHT
    MatchHud.board = panel(root, "TopCenter", -BOARD_WIDTH / 2, 170, BOARD_WIDTH, height, Menu.theme.tile)
    MatchHud.columns = {}
    for team = 1, 2 do
        local x = 20 + (team - 1) * COLUMN_WIDTH
        local color = MatchHud.teamColors[team]
        local column = {
            title = text(MatchHud.board, x, 18, 300, 44, 30, true, color),
            score = text(MatchHud.board, x + COLUMN_WIDTH - 140, 18, 120, 44, 30, true, color, "right"),
            rule = panel(MatchHud.board, "TopLeft", x, 66, COLUMN_WIDTH - 20, 3, color),
            kHead = text(MatchHud.board, x + COLUMN_WIDTH - 200, 74, 80, 30, 16, true, Menu.theme.muted, "right"),
            dHead = text(MatchHud.board, x + COLUMN_WIDTH - 110, 74, 80, 30, 16, true, Menu.theme.muted, "right"),
            rows = {},
        }
        UI.setText(column.kHead, "KILLS")
        UI.setText(column.dHead, "DEATHS")
        for i = 1, BOARD_ROWS do
            local y = 108 + (i - 1) * BOARD_ROW_HEIGHT
            column.rows[i] = {
                mark = panel(MatchHud.board, "TopLeft", x - 8, y, COLUMN_WIDTH - 4, BOARD_ROW_HEIGHT - 4,
                             { r = color.r, g = color.g, b = color.b, a = 0.22 }),
                name = text(MatchHud.board, x, y, 300, BOARD_ROW_HEIGHT - 4, 22, false),
                kills = text(MatchHud.board, x + COLUMN_WIDTH - 200, y, 80, BOARD_ROW_HEIGHT - 4, 22, true, nil, "right"),
                deaths = text(MatchHud.board, x + COLUMN_WIDTH - 110, y, 80, BOARD_ROW_HEIGHT - 4, 22, false, nil,
                              "right"),
            }
        end
        MatchHud.columns[team] = column
    end
    UI.setVisible(MatchHud.board, false)
end

-- entries: kill feed entries, oldest first (see Match.recentFeed); the newest is at the bottom.
function MatchHud.setFeed(entries)
    if not MatchHud.feedRows then
        return
    end
    local first = math.max(1, #entries - FEED_ROWS + 1)
    for i, row in ipairs(MatchHud.feedRows) do
        local entry = entries[first + i - 1]
        local show = entry ~= nil
        for _, widget in pairs(row) do
            UI.setVisible(widget, show)
        end
        if show then
            local killer = entry.killer or ""
            local word = entry.killer and "killed" or "died"
            local kw = killer ~= "" and width(killer, FEED_SIZE, true) or 0
            local ww = width(word, FEED_SIZE - 2, false)
            local vw = width(entry.victim, FEED_SIZE, true)
            local gap = 12
            local right = FEED_WIDTH - 14
            local vx = right - vw
            local wx = vx - gap - ww
            local kx = wx - (killer ~= "" and gap + kw or 0)
            local y = (i - 1) * FEED_ROW_HEIGHT
            UI.setText(row.victim, entry.victim)
            setColor(row.victim, MatchHud.teamColors[entry.victimTeam] or Menu.theme.text)
            place(row.victim, "TopLeft", vx, y, vw + 4, FEED_ROW_HEIGHT - 4)
            UI.setText(row.word, word)
            place(row.word, "TopLeft", wx, y, ww + 4, FEED_ROW_HEIGHT - 4)
            UI.setText(row.killer, killer)
            setColor(row.killer, MatchHud.teamColors[entry.killerTeam or 1] or Menu.theme.text)
            place(row.killer, "TopLeft", kx, y, kw + 4, FEED_ROW_HEIGHT - 4)
            place(row.back, "TopLeft", kx - 14, y, right + 14 - (kx - 14), FEED_ROW_HEIGHT - 4)
        end
    end
end

-- Shows (or hides) the scoreboard: standings is { team1 list, team2 list } (see Match.standings), scores the team
-- scores, highlightId the soldier to mark (the player). x moves it sideways from the centre.
function MatchHud.setScoreboard(visible, standings, scores, highlightId, x)
    if not MatchHud.board then
        return
    end
    UI.setVisible(MatchHud.board, visible)
    if not visible then
        return
    end
    UI.setPosition(MatchHud.board, -BOARD_WIDTH / 2 + (x or 0), 170)
    for team = 1, 2 do
        local column = MatchHud.columns[team]
        UI.setText(column.title, (MatchHud.teamNames and MatchHud.teamNames[team]) or ("TEAM " .. team))
        UI.setText(column.score, tostring(scores[team] or 0))
        local list = standings[team] or {}
        for i, row in ipairs(column.rows) do
            local player = list[i]
            UI.setVisible(row.name, player ~= nil)
            UI.setVisible(row.kills, player ~= nil)
            UI.setVisible(row.deaths, player ~= nil)
            UI.setVisible(row.mark, player ~= nil and player.id == highlightId)
            if player then
                UI.setText(row.name, player.name)
                UI.setText(row.kills, tostring(player.kills))
                UI.setText(row.deaths, tostring(player.deaths))
            end
        end
    end
end

function MatchHud.setVisible(visible)
    if MatchHud.screen then
        UI.setScreenVisible(MatchHud.screen, visible)
    end
end

-- Forget the screens; they do not survive a scene change.
function MatchHud.reset()
    MatchHud.screen, MatchHud.feed, MatchHud.feedRows, MatchHud.board, MatchHud.columns = nil, nil, nil, nil, nil
end

return MatchHud
