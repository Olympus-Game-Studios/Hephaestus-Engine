-- Team deathmatch with bots: the player and teamSize - 1 friendly soldiers (team 1) against teamSize enemy soldiers
-- (team 2). A kill scores for the killer's team, friendly fire is off, and the first team to scoreLimit or the one
-- ahead when the time runs out wins. The dead come back after respawnSeconds at one of their team's spawn points,
-- away from the other team. Hold Scoreboard (Tab) for the scoreboard. The match ends on a screen offering Play
-- again and Quit to menu. Put it on any entity in a level with a navmesh and the player; set the player's
-- restartEnabled off, since this screen replaces its restart prompt.
local Registry = require("FPS.Registry")
local Hud = require("FPS.Hud")
local Match = require("FPS.Match")
local MatchHud = require("FPS.MatchHud")
local Menu = require("FPS.Menu")
local Waves = require("FPS.Waves")
local Perception = require("FPS.AI.Perception")
local Health = require("FPS.Health")

Expose.string("friendlyPrefab", "")
Expose.string("enemyPrefab", "")
Expose.integer("teamSize", 6) -- soldiers per team, the player included on team 1
Expose.integer("scoreLimit", 50)
Expose.number("timeLimitMinutes", 10)
Expose.number("respawnSeconds", 5)
Expose.string("friendlySpawns", "") -- "x,z;x,z": where team 1 comes back
Expose.string("enemySpawns", "")
Expose.number("minSpawnDistance", 15) -- spawn points closer than this to the other team are avoided
Expose.string("routePoints", "") -- "x,z;x,y,z": lane and high-ground spots soldiers sometimes head for between fights
Expose.number("routeChance", 0.5) -- chance a soldier with no enemy in sight goes to a route point first
Expose.string("friendlyNames", "Hale;Ortiz;Becker;Novak;Reyes;Quinn;Marsh")
Expose.string("enemyNames", "Volkov;Kane;Sato;Mercer;Drago;Lund;Brandt")
Expose.string("teamNames", "BLUE;RED")
Expose.string("mainMenuScene", "")
Expose.number("feedSeconds", 6) -- how long a kill stays in the feed
Expose.number("nameTagHeight", 2.1) -- metres above a teammate's feet
Expose.number("nameTagFadeStart", 15) -- teammates' names fade out between these distances, in metres
Expose.number("nameTagFadeEnd", 45)

local state = nil
local soldiers = {} -- id -> { id, team, name, prefab, entity, key, respawnAt }
local byKey = {}
local spawns = {}
local phase = "waiting"
local endMenu = nil
local rng = 1
local shownCountdown = nil

local PLAYER = "player"

local function random()
    rng = (rng * 1103515245 + 12345) % 2147483648
    return rng / 2147483648
end

local function split(text)
    local list = {}
    for item in string.gmatch(text or "", "[^;]+") do
        list[#list + 1] = item:match("^%s*(.-)%s*$")
    end
    return list
end

local function centre(points)
    local x, z = 0, 0
    for _, p in ipairs(points) do
        x, z = x + p.x, z + p.z
    end
    return #points > 0 and { x = x / #points, z = z / #points } or { x = 0, z = 0 }
end

-- "x,z;x,y,z" -> { {x, y, z} }; y defaults to 0.
local function parseRoutes(text)
    local out = {}
    for item in string.gmatch(text or "", "[^;]+") do
        local n = {}
        for v in string.gmatch(item, "-?[%d%.]+") do
            n[#n + 1] = tonumber(v)
        end
        if #n == 2 then
            out[#out + 1] = { x = n[1], y = 0, z = n[2] }
        elseif #n == 3 then
            out[#out + 1] = { x = n[1], y = n[2], z = n[3] }
        end
    end
    return out
end

local function enemiesOf(team)
    local list = {}
    for _, row in pairs(Registry.combatants) do
        local transform = row.team ~= team and row.entity and row.entity:IsValid() and row.entity:GetTransform()
        local p = transform and transform:GetWorldPosition()
        if p then
            list[#list + 1] = p
        end
    end
    return list
end

-- Where a soldier of `team` comes back, on the navmesh, and the yaw (radians, +X turning towards -Z) facing the
-- other team's side.
local function spawnSpot(team)
    local point = Match.pickSpawn(spawns[team], enemiesOf(team), minSpawnDistance, random)
    if not point then
        return nil
    end
    local spot = Nav.closestPoint({ x = point.x, y = 0, z = point.z }) or { x = point.x, y = 0, z = point.z }
    local towards = centre(spawns[3 - team])
    return spot, math.atan(-(towards.z - spot.z), towards.x - spot.x)
end

local function spawnSoldier(soldier)
    local spot, yaw = spawnSpot(soldier.team)
    if not spot then
        return
    end
    local entity = Scene.instantiate(soldier.prefab, {
        position = spot,
        rotation = { x = 0, y = math.deg(yaw) + 90, z = 0 },
        vars = { team = soldier.team, displayName = soldier.name, huntOnSpawn = true, huntWhenIdle = true },
    })
    soldier.respawnAt = nil
    if entity then
        soldier.entity = entity
        soldier.key = Registry.key(entity)
        byKey[soldier.key] = soldier.id
    end
end

local function respawnPlayer(soldier)
    local row = Registry.player
    local spot, yaw = spawnSpot(soldier.team)
    soldier.respawnAt = nil
    shownCountdown = nil
    if row and row.entity and row.entity:IsValid() and spot then
        row.entity:Call("respawn", spot.x, spot.y, spot.z, yaw)
    end
end

local function startMatch()
    state = Match.new({ scoreLimit = scoreLimit, timeLimitSeconds = timeLimitMinutes * 60 })
    local names = { split(friendlyNames), split(enemyNames) }
    local prefabs = { friendlyPrefab, enemyPrefab }
    local player = Registry.player
    soldiers[PLAYER] = { id = PLAYER, team = player.team or 1, name = "You", entity = player.entity, key = player.key }
    byKey[player.key] = PLAYER
    Match.join(state, PLAYER, "You", soldiers[PLAYER].team)
    for team = 1, 2 do
        local count = team == soldiers[PLAYER].team and teamSize - 1 or teamSize
        for i = 1, count do
            local id = string.format("%d-%d", team, i)
            local name = names[team][i] or string.format("%s %d", team == 1 and "Blue" or "Red", i)
            soldiers[id] = { id = id, team = team, name = name, prefab = prefabs[team] }
            Match.join(state, id, name, team)
            spawnSoldier(soldiers[id])
        end
    end
    phase = "playing"
    Hud.showBanner("Team Deathmatch", string.format("First to %d", scoreLimit), 3)
end

local function finish(winner)
    phase = "over"
    Registry.gameOver = "match"
    local mine = soldiers[PLAYER] and soldiers[PLAYER].team or 1
    local title = winner == "draw" and "DRAW" or (winner == mine and "VICTORY" or "DEFEAT")
    Hud.clearBanner()
    Hud.setDeathFade(false)
    Time.setScale(0)
    UI.setCursorMode(CursorMode.UI)
    endMenu = Menu.new("MatchEnd", { backdrop = 0.55, y = 300, hints = "ENTER  SELECT        W / S  MOVE" })
    local items = { { label = "PLAY AGAIN", action = function() Time.setScale(1); Scene.reload() end } }
    if mainMenuScene ~= "" then
        items[#items + 1] = { label = "QUIT TO MENU", action = function() Time.setScale(1); Scene.load(mainMenuScene) end }
    end
    Menu.setPage(endMenu, { title = title, items = items })
end

-- Names over the player's teammates that are on screen, in sight and near enough.
local function nameTags()
    local row = Registry.player
    local tags = {}
    if not (Camera and Camera.worldToScreen) or not row or not row.entity or not row.entity:IsValid()
        or Health.isDead(row.entity) then
        return tags
    end
    local transform = row.entity:GetTransform()
    local feet = transform and transform:GetWorldPosition()
    if not feet then
        return tags
    end
    local eye = { x = feet.x, y = feet.y + (row.eyeHeight or 1.6), z = feet.z }
    for _, soldier in pairs(soldiers) do
        local entity = soldier.entity
        if soldier.id ~= PLAYER and soldier.team == row.team and entity and entity:IsValid() then
            local p = entity:GetTransform():GetWorldPosition()
            local head = { x = p.x, y = p.y + nameTagHeight, z = p.z }
            local x, y, onScreen, depth = Camera.worldToScreen(head)
            local alpha = depth and 1 - (depth - nameTagFadeStart) / math.max(nameTagFadeEnd - nameTagFadeStart, 0.1)
            if onScreen and alpha > 0.02 and Perception.clearLine(eye, head, { row.entity }, function(hit)
                    local owner = Registry.combatantOf(hit)
                    return owner ~= nil and owner.entity == entity
                end) then
                tags[#tags + 1] = { name = soldier.name, x = x, y = y, alpha = math.min(1, alpha), team = soldier.team }
            end
        end
    end
    return tags
end

local function showScore()
    local names = MatchHud.teamNames
    Hud.setWaveText(string.format("%s  %d     %s     %d  %s", names[1], state.scores[1], Match.clock(state),
                                  state.scores[2], names[2]))
end

function onAwake()
    Registry.ensureSession()
    Registry.matchDirector = this
    Registry.friendlyFire = false
    state, soldiers, byKey, phase, endMenu, shownCountdown = nil, {}, {}, "waiting", nil, nil
    spawns = { Waves.parsePoints(friendlySpawns), Waves.parsePoints(enemySpawns) }
    local routes = parseRoutes(routePoints)
    Registry.routes = #routes > 0 and { points = routes, chance = routeChance } or nil
    rng = (Registry.key(this) or 1) * 48271 % 2147483647 + 1
    local names = split(teamNames)
    MatchHud.reset()
    MatchHud.mount({ names[1] or "BLUE", names[2] or "RED" })
    if friendlyPrefab == "" or enemyPrefab == "" or #spawns[1] == 0 or #spawns[2] == 0 then
        phase = "idle"
        engine_log("[FPS.TeamDeathmatch] needs friendlyPrefab, enemyPrefab, friendlySpawns and enemySpawns")
    end
end

function onUpdate(dt)
    if phase == "idle" then
        return
    end
    if phase == "waiting" then
        if Nav.isReady() and Registry.player and Registry.player.entity then
            startMatch()
        end
        return
    end
    if phase == "over" then
        Menu.update(endMenu)
        MatchHud.setFeed({})
        MatchHud.setNameTags({})
        MatchHud.setScoreboard(true, { Match.standings(state, 1), Match.standings(state, 2) }, state.scores, PLAYER,
                               260)
        return
    end
    local winner = Match.step(state, dt)
    for _, soldier in pairs(soldiers) do
        if soldier.respawnAt and state.elapsed >= soldier.respawnAt then
            if soldier.id == PLAYER then
                respawnPlayer(soldier)
            else
                spawnSoldier(soldier)
            end
        end
    end
    local me = soldiers[PLAYER]
    if me and me.respawnAt then
        local left = math.max(1, math.ceil(me.respawnAt - state.elapsed))
        if left ~= shownCountdown then
            shownCountdown = left
            Hud.showBanner("You died", string.format("Back in %d", left), nil)
        end
    end
    showScore()
    MatchHud.setFeed(Match.recentFeed(state, feedSeconds))
    MatchHud.setNameTags(nameTags())
    MatchHud.setScoreboard(Input.isActionDown("Scoreboard"), { Match.standings(state, 1), Match.standings(state, 2) },
                           state.scores, PLAYER)
    if winner then
        finish(winner)
    end
end

-- Called by Player.lua and Enemy.lua when they die; killer is whoever Health.applyDamage named (may be nil).
function onCombatantDeath(victim, killer)
    if phase ~= "playing" then
        return
    end
    local id = byKey[Registry.key(victim)]
    local soldier = id and soldiers[id]
    if not soldier then
        return
    end
    Match.kill(state, id, killer and byKey[Registry.key(killer)] or nil)
    if soldier.id ~= PLAYER then
        byKey[soldier.key] = nil
        soldier.entity, soldier.key = nil, nil
    end
    soldier.respawnAt = state.elapsed + respawnSeconds
end

function onDestroy()
    if Registry.matchDirector == this then
        Registry.matchDirector = nil
        Registry.routes = nil
        Registry.friendlyFire = true
    end
    MatchHud.reset()
end
