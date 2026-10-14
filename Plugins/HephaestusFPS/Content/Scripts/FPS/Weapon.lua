Expose.string("displayName", "Pistol")
Expose.string("fireMode", "semi")
Expose.number("roundsPerSecond", 5)
Expose.number("damage", 20)
Expose.number("range", 100)
Expose.number("magazineSize", 12)
Expose.number("reserveAmmo", 48)
Expose.number("reloadSeconds", 1.4)
Expose.bool("hitscan", true)
Expose.number("recoilPitch", 1.0)
Expose.number("recoilYaw", 0.3)
Expose.number("recoilRecoveryDegreesPerSecond", 12)
Expose.string("fireSound", "")
Expose.string("reloadSound", "")
Expose.string("emptySound", "")
Expose.string("viewModelPrefab", "")
Expose.string("worldModelPrefab", "")

local Registry = require("FPS.Registry")
local Logic = require("FPS.Logic")
local Hitscan = require("FPS.Hitscan")
local Health = require("FPS.Health")

local Weapon = {}

local function degreesToRadians(value)
    return (value or 0) * math.pi / 180
end

function Weapon.register(entity, options)
    Registry.ensureSession()
    options = options or {}
    local key = Registry.key(entity)
    if not key then
        return
    end
    local rest = Transform.getPosition()
    local restRot = Transform.getRotation()
    local row = {
        entity = entity,
        key = key,
        displayName = options.displayName or displayName or "Weapon",
        fireMode = options.fireMode or fireMode or "semi",
        roundsPerSecond = options.roundsPerSecond or roundsPerSecond or 5,
        damage = options.damage or damage or 20,
        range = options.range or range or 100,
        magazineSize = options.magazineSize or magazineSize or 12,
        reserve = options.reserveAmmo or reserveAmmo or 48,
        reloadSeconds = options.reloadSeconds or reloadSeconds or 1.4,
        hitscan = options.hitscan,
        recoilPitch = options.recoilPitch or recoilPitch or 1.0,
        recoilYaw = options.recoilYaw or recoilYaw or 0.3,
        recoilRecovery = options.recoilRecoveryDegreesPerSecond or recoilRecoveryDegreesPerSecond or 12,
        magazine = options.magazineSize or magazineSize or 12,
        now = 0,
        nextFire = 0,
        reloadRemaining = 0,
        equipped = false,
        kickPitch = 0,
        kickYaw = 0,
        restX = rest and rest.x or 0.15,
        restY = rest and rest.y or -0.12,
        restZ = rest and rest.z or -0.35,
        restPitch = restRot and restRot.x or 0,
        restYaw = restRot and restRot.y or 0,
        restRoll = restRot and restRot.z or 0,
        lastFireTime = -1,
        lastHitKey = nil,
        fireSound = options.fireSound or fireSound or "",
        reloadSound = options.reloadSound or reloadSound or "",
        emptySound = options.emptySound or emptySound or "",
    }
    if row.hitscan == nil then
        if hitscan == nil then
            row.hitscan = true
        else
            row.hitscan = hitscan
        end
    end
    Registry.weapons[key] = row
    local exists = false
    for _, existing in ipairs(Registry.weaponOrder) do
        if existing == key then
            exists = true
            break
        end
    end
    if not exists then
        table.insert(Registry.weaponOrder, key)
        -- Script awake order is not an authored slot order. Keep the default
        -- weapon selection deterministic in editor and packaged runtime.
        table.sort(Registry.weaponOrder)
    end
end

function Weapon.unregister(entity)
    local key = Registry.key(entity)
    if not key then
        return
    end
    Registry.weapons[key] = nil
    for i, existing in ipairs(Registry.weaponOrder) do
        if existing == key then
            table.remove(Registry.weaponOrder, i)
            break
        end
    end
    if Registry.activeWeaponId == key then
        Registry.activeWeaponId = nil
    end
end

function Weapon.get(entity)
    return Registry.weapons[Registry.key(entity)]
end

function Weapon.snapshot(entity)
    local row = Weapon.get(entity)
    if not row then
        return nil
    end
    return {
        displayName = row.displayName,
        magazine = row.magazine,
        reserve = row.reserve,
        magazineSize = row.magazineSize,
        reloading = (row.reloadRemaining or 0) > 0,
    }
end

function Weapon.active()
    local key = Registry.activeWeaponId
    return key and Registry.weapons[key] or nil
end

function Weapon.giveAmmo(entity, amount)
    local row = Weapon.get(entity)
    if not row then
        return
    end
    row.reserve = (row.reserve or 0) + (amount or 0)
end

local function playOptional(path)
    if path and path ~= "" and Audio and Audio.isAvailable and Audio.isAvailable() then
        Audio.play(path, false, 1.0)
    end
end

local function setPartsVisible(visible)
    for _, part in ipairs(Scene.findAllWithPrefix(this:GetName() .. "_Part_")) do
        part:Call("setWeaponPartVisible", visible)
    end
end

function tick(dt, held, pressed, reload, ox, oy, oz, dx, dy, dz, owner)
    local row = Weapon.get(this)
    if not row or not row.equipped then
        return
    end
    Logic.advanceReload(row, dt)
    if reload == true or reload == 1 then
        if Logic.tryStartReload(row) then
            playOptional(row.reloadSound)
        end
    end
    local shots = Logic.tickFire(row, dt, held, pressed)
    if shots == 0 and (pressed == true or pressed == 1) and (row.magazine or 0) <= 0 then
        if not Logic.tryStartReload(row) then
            playOptional(row.emptySound)
        end
        return
    end
    for _ = 1, shots do
        if not Logic.spendShot(row) then
            Logic.tryStartReload(row)
            break
        end
        playOptional(row.fireSound)
        row.lastFireTime = row.now
        row.kickPitch = row.kickPitch + degreesToRadians(row.recoilPitch)
        row.kickYaw = row.kickYaw + degreesToRadians(row.recoilYaw)
        if row.hitscan then
            local hit = Hitscan.trace(ox, oy, oz, dx, dy, dz, row.range, owner)
            if hit and hit.entity then
                row.lastHitKey = Registry.key(hit.entity)
                if not Health.get(hit.entity) then
                    hit.entity:Call("onDamaged", row.damage)
                else
                    Health.applyDamage(hit.entity, row.damage, owner)
                    hit.entity:Call("onHealthChanged", Health.getHealth(hit.entity),
                                    Health.getMaxHealth(hit.entity))
                end
            end
        end
        if this and this.Call then
            this:Call("onFire")
        end
    end
    local recover = degreesToRadians(row.recoilRecovery) * dt
    if row.kickPitch > 0 then
        row.kickPitch = math.max(0, row.kickPitch - recover)
    else
        row.kickPitch = math.min(0, row.kickPitch + recover)
    end
    if row.kickYaw > 0 then
        row.kickYaw = math.max(0, row.kickYaw - recover)
    else
        row.kickYaw = math.min(0, row.kickYaw + recover)
    end
    -- The render camera pitches around its view-local Z axis, while the camera
    -- entity's scene transform pitches around X. Convert the authored camera-
    -- space rest offset back through the scene hierarchy so the gun remains in
    -- the lower-right of the rendered view instead of orbiting out of frame.
    local player = Registry.player or {}
    local yaw = player.yaw or 0
    local pitch = player.pitch or 0
    local depth = row.restX
    local right = row.restZ
    local down = -row.restY
    local cy, sy = math.cos(yaw), math.sin(yaw)
    local cp, sp = math.cos(pitch), math.sin(pitch)
    local worldX = depth * cy * cp + right * sy + down * cy * sp
    local worldY = depth * sp - down * cp
    local worldZ = -depth * sy * cp + right * cy + down * sy * sp
    local parentX = cy * worldX - sy * worldZ
    local parentZ = sy * worldX + cy * worldZ
    local localY = cp * worldY + sp * parentZ
    local localZ = -sp * worldY + cp * parentZ
    Transform.setPosition(parentX, localY, localZ)
    Transform.setRotation(row.restPitch - pitch, row.restYaw + row.kickYaw,
                          row.restRoll + pitch + row.kickPitch)
end

function equip()
    local row = Weapon.get(this)
    if row then
        row.equipped = true
        Registry.activeWeaponId = row.key
    end
    setPartsVisible(true)
end

function unequip()
    local row = Weapon.get(this)
    if row then
        row.equipped = false
        if Registry.activeWeaponId == row.key then
            Registry.activeWeaponId = nil
        end
    end
    setPartsVisible(false)
end

function onAwake()
    Weapon.register(this, {})
    setPartsVisible(false)
end

function onDestroy()
    Weapon.unregister(this)
end

return Weapon
