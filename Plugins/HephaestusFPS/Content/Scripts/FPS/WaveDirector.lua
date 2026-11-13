-- Runs waves of enemies: a countdown, then each wave's enemies spawn one by one at spawn points away from the
-- player and come looking for them; when all are dead the next wave follows a short break, and clearing the last
-- wave wins. Put it on any entity in a level with a navmesh. The player script offers the restart afterwards.
local Registry = require("FPS.Registry")
local Health = require("FPS.Health")
local Hud = require("FPS.Hud")
local Waves = require("FPS.Waves")

Expose.string("enemyPrefab", "")
Expose.string("waves", "3;5;7")
Expose.string("spawnPoints", "")
Expose.number("startDelaySeconds", 4)
Expose.number("breakSeconds", 6)
Expose.number("spawnIntervalSeconds", 1.5)
Expose.number("minSpawnDistance", 15)

local plan = {}
local points = {}
local wave = 0
local phase = "starting"
local timer = 0
local toSpawn = 0
local spawned = {}
local rng = 1

local function random()
    rng = (rng * 1103515245 + 12345) % 2147483648
    return rng / 2147483648
end

local function playerFeet()
    local row = Registry.player
    local transform = row and row.entity and row.entity:IsValid() and row.entity:GetTransform()
    return transform and transform:GetWorldPosition()
end

local function aliveCount()
    local alive = 0
    for _, enemy in ipairs(spawned) do
        if enemy:IsValid() and not Health.isDead(enemy) then
            alive = alive + 1
        end
    end
    return alive
end

local function showProgress()
    Hud.setWaveText(string.format("Wave %d / %d   Enemies %d", wave, #plan, aliveCount() + toSpawn))
end

local function spawnOne()
    local feet = playerFeet()
    local point = Waves.pickSpawn(points, feet, minSpawnDistance, random)
    if not point then
        return
    end
    local spot = Nav.closestPoint({ x = point.x, y = feet and feet.y or 0, z = point.z }) or
                 { x = point.x, y = 0, z = point.z }
    local yaw = feet and math.deg(math.atan(-(feet.z - spot.z), feet.x - spot.x)) or 0
    local enemy = Scene.instantiate(enemyPrefab, {
        position = spot,
        rotation = { x = 0, y = yaw + 90, z = 0 },
        vars = { huntOnSpawn = true, difficulty = plan[wave].difficulty },
    })
    if enemy then
        spawned[#spawned + 1] = enemy
    end
end

local function startWave(index)
    wave = index
    toSpawn = plan[wave].count
    spawned = {}
    phase = "spawning"
    timer = 0
    Hud.showBanner(string.format("Wave %d", wave), #plan > 1 and string.format("of %d", #plan) or "", 2.5)
end

function onAwake()
    Registry.ensureSession() -- opening it later would clear waveDirector
    Registry.loadoutActive = true
    Registry.waveDirector = this
    plan = Waves.parse(waves)
    points = Waves.parsePoints(spawnPoints)
    wave, toSpawn, spawned, timer = 0, 0, {}, 0
    rng = (Registry.key(this) or 1) * 48271 % 2147483647 + 1
    phase = (enemyPrefab ~= "" and #plan > 0 and #points > 0) and "starting" or "idle"
    if phase == "idle" then
        engine_log("[FPS.WaveDirector] needs an enemyPrefab, waves and spawnPoints")
    end
end

function onUpdate(dt)
    if phase == "idle" or phase == "done" or Registry.gameOver == "dead" then
        return
    end
    timer = timer + dt
    if phase == "starting" then
        if timer >= startDelaySeconds then
            startWave(1)
        end
    elseif phase == "spawning" then
        if timer >= spawnIntervalSeconds or #spawned == 0 then
            timer = 0
            spawnOne()
            toSpawn = toSpawn - 1
            if toSpawn <= 0 then
                phase = "fighting"
            end
        end
    elseif phase == "fighting" then
        if aliveCount() == 0 then
            timer = 0
            if wave >= #plan then
                phase = "done"
                Registry.gameOver = "victory"
                Hud.showBanner("All waves cleared", "", nil)
                Hud.setWaveText(nil)
                return
            end
            phase = "break"
            Hud.showBanner(string.format("Wave %d cleared", wave), "", breakSeconds)
        end
    elseif phase == "break" then
        if timer >= breakSeconds then
            startWave(wave + 1)
        end
    end
    if phase ~= "starting" and phase ~= "done" then
        showProgress()
    end
end

-- The developer menu's Start Wave: the next wave starts now, adding to any enemies still fighting.
function devStartWave()
    if phase == "idle" or phase == "done" or #plan == 0 then
        return
    end
    wave = math.min(wave + 1, #plan)
    toSpawn = toSpawn + plan[wave].count
    phase = "spawning"
    timer = 0
    Hud.showBanner(string.format("Wave %d", wave), #plan > 1 and string.format("of %d", #plan) or "", 2.5)
end

function onDestroy()
    if Registry.waveDirector == this then
        Registry.waveDirector = nil
    end
end
