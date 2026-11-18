-- The developer menu: the DevMenu action (F2) opens God Mode, Infinite Ammo, Infinite Grenades, Spawn Enemy and
-- Start Wave. The player script only uses it when Application.isDevelopment() is true (Editor Play and Development
-- builds), so Release games never reach it. Like the in-game menu it frees the mouse and does not pause the game.
-- The player calls DevMenu.update every frame.
local Menu = require("FPS.Menu")
local Registry = require("FPS.Registry")
local Health = require("FPS.Health")
local Logic = require("FPS.Logic")
local Hud = require("FPS.Hud")

local DevMenu = {
    menu = nil,
    open = false,
    suppressFire = false,
    cheats = { god = false, ammo = false, grenades = false },
    wave = nil, -- a stand-in wave on a level without a wave director: { number, toSpawn, timer }
    waveNumber = 0,
}

local WAVE_SPAWN_SECONDS = 1.0
local WAVE_MIN_DISTANCE = 15
local WAVE_MAX_DISTANCE = 60
local SPAWN_AHEAD = 8

-- Forget the menu and switch every cheat off; its UI screen does not survive a scene change.
function DevMenu.reset()
    DevMenu.menu = nil
    DevMenu.open = false
    DevMenu.suppressFire = false
    DevMenu.cheats = { god = false, ammo = false, grenades = false }
    DevMenu.wave = nil
    DevMenu.waveNumber = 0
end

function DevMenu.anyCheat()
    local c = DevMenu.cheats
    return c.god or c.ammo or c.grenades
end

local function notice(text)
    Hud.showPickup(text, 1.0, 1.0, 1.0)
end

local function playerFeet()
    local row = Registry.player
    local transform = row and row.entity and row.entity:IsValid() and row.entity:GetTransform()
    return transform and transform:GetWorldPosition()
end

local function spawnEnemy(prefab, at, feet)
    local yaw = math.deg(math.atan(-(feet.z - at.z), feet.x - at.x))
    return Scene.instantiate(prefab, { position = at, rotation = { x = 0, y = yaw + 90, z = 0 },
                                       vars = { huntOnSpawn = true } })
end

local function spawnAhead(prefab)
    local feet = playerFeet()
    if prefab == "" or not feet then
        notice("No enemy prefab set")
        return
    end
    local forward = Logic.yawBasis(Registry.player.yaw or 0).forward
    local spot = Nav.closestPoint({ x = feet.x + forward.x * SPAWN_AHEAD, y = feet.y,
                                    z = feet.z + forward.z * SPAWN_AHEAD })
    if not spot then
        notice("No navmesh here")
        return
    end
    if spawnEnemy(prefab, spot, feet) then
        notice("Enemy spawned")
    end
end

-- A navmesh point between WAVE_MIN_DISTANCE and WAVE_MAX_DISTANCE from the player, or nil.
local function waveSpawnPoint(feet)
    for _ = 1, 40 do
        local point = Nav.randomPoint(feet, WAVE_MAX_DISTANCE)
        if point then
            local dx, dz = point.x - feet.x, point.z - feet.z
            local distance = math.sqrt(dx * dx + dz * dz)
            if distance >= WAVE_MIN_DISTANCE and distance <= WAVE_MAX_DISTANCE then
                return point
            end
        end
    end
    return nil
end

local function startWave(prefab)
    local director = Registry.waveDirector
    if director and director:IsValid() then
        director:Call("devStartWave")
        return
    end
    if prefab == "" then
        notice("No enemy prefab set")
        return
    end
    DevMenu.waveNumber = DevMenu.waveNumber + 1
    DevMenu.wave = { toSpawn = 2 + DevMenu.waveNumber, timer = WAVE_SPAWN_SECONDS, prefab = prefab }
    Hud.showBanner(string.format("Wave %d", DevMenu.waveNumber), "Developer wave", 2.5)
end

local function updateWave(dt)
    local wave = DevMenu.wave
    if not wave then
        return
    end
    wave.timer = wave.timer + dt
    if wave.timer < WAVE_SPAWN_SECONDS then
        return
    end
    wave.timer = 0
    local feet = playerFeet()
    local point = feet and waveSpawnPoint(feet)
    if point then
        spawnEnemy(wave.prefab, point, feet)
    end
    wave.toSpawn = wave.toSpawn - 1
    if wave.toSpawn <= 0 then
        DevMenu.wave = nil
    end
end

-- Keeps the cheats in force: no damage, full magazines and reserves, and a full load of grenades.
local function applyCheats()
    local row = Registry.player
    if not row or not row.entity then
        return
    end
    local health = Health.get(row.entity)
    if health and health.invulnerable ~= DevMenu.cheats.god then
        Health.setInvulnerable(row.entity, DevMenu.cheats.god)
    end
    if DevMenu.cheats.ammo then
        for _, key in ipairs(Registry.weaponOrder) do
            local weapon = Registry.weapons[key]
            local size = weapon and weapon.magazineSize or 0
            if size > 0 and (weapon.magazine or 0) < size - 1 then
                weapon.magazine = size - 1
                weapon.chamber = 1
            end
            if weapon and weapon.maxReserve and (weapon.reserve or 0) < weapon.maxReserve then
                weapon.reserve = weapon.maxReserve
            end
        end
    end
    if DevMenu.cheats.grenades and row.grenades and row.maxGrenades then
        for _, which in ipairs({ "frag", "smoke" }) do
            row.grenades[which] = math.max(row.grenades[which] or 0, row.maxGrenades[which] or 0)
        end
    end
end

local function close()
    DevMenu.open = false
    if DevMenu.menu then
        Menu.setVisible(DevMenu.menu, false)
    end
    DevMenu.suppressFire = true
    UI.setCursorMode(CursorMode.Game)
end

local function toggle(label, key)
    return { label = label, kind = "choice", options = { { false, "OFF" }, { true, "ON" } },
             get = function() return DevMenu.cheats[key] end,
             set = function(v) DevMenu.cheats[key] = v end }
end

local function page(options)
    return {
        title = "DEVELOPER",
        items = {
            toggle("GOD MODE", "god"),
            toggle("INFINITE AMMO", "ammo"),
            toggle("INFINITE GRENADES", "grenades"),
            { label = "SPAWN ENEMY", action = function() spawnAhead(options.enemyPrefab or "") end },
            { label = "START WAVE", action = function() startWave(options.enemyPrefab or "") end },
            { label = "CLOSE", action = close },
        },
    }
end

-- options: enabled (false keeps the menu shut), enemyPrefab (what Spawn Enemy and a stand-in wave spawn).
-- Returns whether the menu is open.
function DevMenu.update(options, dt)
    if DevMenu.suppressFire and not Input.isActionDown("Fire") then
        DevMenu.suppressFire = false
    end
    applyCheats()
    updateWave(dt or 0)
    Hud.setDevIndicator(DevMenu.anyCheat())
    if DevMenu.open then
        if Menu.update(DevMenu.menu) == "back" or Input.isActionPressed("DevMenu") then
            close()
        end
    elseif Input.isActionPressed("DevMenu") and options.enabled ~= false then
        if not DevMenu.menu then
            DevMenu.menu = Menu.new("DevMenu", { backdrop = 0.45, y = 260,
                hints = "ENTER  SELECT        A / D  TOGGLE        ESC  BACK        F2  CLOSE" })
        end
        Menu.setPage(DevMenu.menu, page(options))
        Menu.setVisible(DevMenu.menu, true)
        DevMenu.open = true
        UI.setCursorMode(CursorMode.UI)
    end
    return DevMenu.open
end

return DevMenu
