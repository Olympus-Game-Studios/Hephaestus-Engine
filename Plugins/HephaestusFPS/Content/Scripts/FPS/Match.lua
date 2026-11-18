-- Team deathmatch rules, kept free of engine calls so they can be tested: each soldier's kills and deaths, the team
-- scores, the kill feed, the clock, the winner and where the dead come back.
local Match = {}

-- options: scoreLimit, timeLimitSeconds, feedSize (kill feed entries kept).
function Match.new(options)
    options = options or {}
    return {
        scoreLimit = options.scoreLimit or 50,
        timeLimit = options.timeLimitSeconds or 600,
        feedSize = options.feedSize or 5,
        scores = { 0, 0 },
        players = {},
        feed = {},
        elapsed = 0,
        winner = nil,
    }
end

-- Adds a soldier under an id it keeps for the whole match, across respawns.
function Match.join(state, id, name, team)
    local player = state.players[id]
    if not player then
        player = { id = id, name = name, team = team, kills = 0, deaths = 0 }
        state.players[id] = player
    end
    return player
end

-- The team ahead when the score limit is reached or the time runs out: 1, 2 or "draw"; nil while it goes on.
function Match.check(state)
    if state.winner then
        return state.winner
    end
    for team = 1, 2 do
        if state.scores[team] >= state.scoreLimit then
            state.winner = team
            return team
        end
    end
    if state.elapsed >= state.timeLimit then
        local a, b = state.scores[1], state.scores[2]
        state.winner = a > b and 1 or (b > a and 2 or "draw")
    end
    return state.winner
end

-- victimId was killed by killerId (nil when no one is credited). Only a kill by the other team scores. Returns the
-- new kill feed entry, or nil once the match is over or for an unknown victim.
function Match.kill(state, victimId, killerId)
    local victim = state.players[victimId]
    if state.winner or not victim then
        return nil
    end
    victim.deaths = victim.deaths + 1
    local killer = killerId and state.players[killerId]
    if killer and killer.team ~= victim.team then
        killer.kills = killer.kills + 1
        state.scores[killer.team] = state.scores[killer.team] + 1
    else
        killer = nil
    end
    local entry = { killer = killer and killer.name, killerTeam = killer and killer.team, victim = victim.name,
                    victimTeam = victim.team, time = state.elapsed }
    table.insert(state.feed, entry)
    while #state.feed > state.feedSize do
        table.remove(state.feed, 1)
    end
    Match.check(state)
    return entry
end

function Match.step(state, dt)
    if not state.winner then
        state.elapsed = state.elapsed + dt
        Match.check(state)
    end
    return state.winner
end

-- Time left as "m:ss".
function Match.clock(state)
    local left = math.max(0, math.ceil(state.timeLimit - state.elapsed))
    return string.format("%d:%02d", math.floor(left / 60), left % 60)
end

-- A team's soldiers, best first: most kills, then fewest deaths, then by name.
function Match.standings(state, team)
    local list = {}
    for _, player in pairs(state.players) do
        if player.team == team then
            list[#list + 1] = player
        end
    end
    table.sort(list, function(a, b)
        if a.kills ~= b.kills then
            return a.kills > b.kills
        end
        if a.deaths ~= b.deaths then
            return a.deaths < b.deaths
        end
        return a.name < b.name
    end)
    return list
end

-- Kill feed entries from the last `seconds`, oldest first.
function Match.recentFeed(state, seconds)
    local list = {}
    for _, entry in ipairs(state.feed) do
        if state.elapsed - entry.time <= seconds then
            list[#list + 1] = entry
        end
    end
    return list
end

-- A spawn point ({ x, z }) for someone whose enemies stand at `enemies` (positions with x and z): one at least
-- minDistance from all of them, chosen with random() in [0, 1), or else the one furthest from the nearest enemy.
function Match.pickSpawn(points, enemies, minDistance, random)
    local safe, best, bestGap = {}, nil, -1
    for _, point in ipairs(points) do
        local gap = math.huge
        for _, enemy in ipairs(enemies) do
            gap = math.min(gap, math.sqrt((enemy.x - point.x) ^ 2 + (enemy.z - point.z) ^ 2))
        end
        if gap >= minDistance then
            safe[#safe + 1] = point
        end
        if gap > bestGap then
            best, bestGap = point, gap
        end
    end
    if #safe > 0 then
        return safe[1 + math.floor(random() * #safe)]
    end
    return best
end

return Match
