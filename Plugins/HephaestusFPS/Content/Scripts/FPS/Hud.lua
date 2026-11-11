local Hud = {
    handle = nil,
    widgets = {},
    loggedError = false,
    damageFlashRemaining = 0,
    markerRemaining = 0,
    markerKind = nil,
    suppressed = false,
}

function Hud.mount(path, enabled)
    if not enabled or not path or path == "" then
        return
    end
    if not UI or not UI.isAvailable or not UI.isAvailable() then
        return
    end
    local handle, err = display.ui(path)
    if not handle then
        if not Hud.loggedError then
            engine_log("[FPS.Hud] display.ui failed: " .. tostring(err))
            Hud.loggedError = true
        end
        return
    end
    Hud.handle = handle
    Hud.damageFlashRemaining = 0
    Hud.suppressed = false
    Hud.widgets.Crosshair = UI.getWidget(handle, "Crosshair")
    Hud.widgets.HealthText = UI.getWidget(handle, "HealthText")
    Hud.widgets.AmmoText = UI.getWidget(handle, "AmmoText")
    Hud.widgets.WeaponName = UI.getWidget(handle, "WeaponName")
    Hud.widgets.GrenadeText = UI.getWidget(handle, "GrenadeText")
    Hud.widgets.Prompt = UI.getWidget(handle, "Prompt")
    Hud.widgets.DamageFlash = UI.getWidget(handle, "DamageFlash")
    Hud.widgets.HitMarker = UI.getWidget(handle, "HitMarker")
    Hud.widgets.KillMarker = UI.getWidget(handle, "KillMarker")
    Hud.widgets.Banner = UI.getWidget(handle, "Banner")
    Hud.widgets.BannerSub = UI.getWidget(handle, "BannerSub")
    Hud.widgets.WaveText = UI.getWidget(handle, "WaveText")
    Hud.widgets.DeathFade = UI.getWidget(handle, "DeathFade")
    Hud.widgets.PickupText = UI.getWidget(handle, "PickupText")
    Hud.pickup = nil
    Hud.banner = nil
    Hud.waveText = nil
    Hud.deathFade = false
    Hud.warnings = {}
    for i = 1, Hud.MAX_GRENADE_WARNINGS do
        Hud.warnings[i] = { icon = UI.getWidget(handle, "GrenadeWarning" .. i),
                            arrow = UI.getWidget(handle, "GrenadeArrow" .. i) }
    end
    Hud.damageArrows = {}
    for i = 1, Hud.MAX_DAMAGE_ARROWS do
        Hud.damageArrows[i] = { arrow = UI.getWidget(handle, "DamageArrow" .. i) }
    end
end

function Hud.unmount()
    if Hud.handle and destroy and destroy.ui then
        destroy.ui(Hud.handle)
    end
    Hud.handle = nil
    Hud.widgets = {}
    Hud.warnings = {}
    Hud.damageFlashRemaining = 0
    Hud.suppressed = false
end

Hud.MAX_GRENADE_WARNINGS = 4
Hud.warnings = {}
Hud.MAX_DAMAGE_ARROWS = 4
Hud.PICKUP_SECONDS = 1.8
Hud.damageArrows = {}
local WARNING_COLORS = {
    near = { 1.0, 1.0, 1.0 },
    harmful = { 1.0, 0.55, 0.1 },
    lethal = { 0.95, 0.12, 0.08 },
}

-- Grenade warnings from GrenadeWarning.compute: an icon on a ring round the crosshair towards each grenade, with an
-- arrow just outside it pointing the same way. style: icon, arrowPattern (a format with the frame number),
-- arrowSteps, radius and iconSize (pixels; the arrow is 0.75 of the icon), time (for the lethal pulse).
function Hud.showGrenadeWarnings(list, style)
    if not Hud.handle then
        return
    end
    local show = not Hud.suppressed and style.icon ~= "" and style.arrowPattern ~= ""
    for i, slot in ipairs(Hud.warnings) do
        local warning = show and list[i] or nil
        if slot.icon and slot.arrow then
            UI.setVisible(slot.icon, warning ~= nil)
            UI.setVisible(slot.arrow, warning ~= nil)
        end
        if warning and slot.icon and slot.arrow then
            local s, c = math.sin(warning.angle), math.cos(warning.angle)
            UI.setPosition(slot.icon, s * style.radius, -c * style.radius)
            local arrowSize = style.iconSize * 0.75
            if slot.iconSize ~= style.iconSize then
                UI.setSize(slot.icon, style.iconSize, style.iconSize)
                UI.setSize(slot.arrow, arrowSize, arrowSize)
                slot.iconSize = style.iconSize
            end
            local arrowRadius = style.radius + (style.iconSize + arrowSize) * 0.5
            UI.setPosition(slot.arrow, s * arrowRadius, -c * arrowRadius)
            local turn = (warning.angle / (2 * math.pi)) % 1
            local frame = math.floor(turn * style.arrowSteps + 0.5) % style.arrowSteps
            local arrowImage = string.format(style.arrowPattern, frame)
            if slot.arrowImage ~= arrowImage then
                UI.setImage(slot.arrow, arrowImage)
                slot.arrowImage = arrowImage
            end
            if slot.iconImage ~= style.icon then
                UI.setImage(slot.icon, style.icon)
                slot.iconImage = style.icon
            end
            local color = WARNING_COLORS[warning.level] or WARNING_COLORS.near
            local alpha = warning.level == "lethal" and (0.75 + 0.25 * math.sin((style.time or 0) * 14)) or 1
            UI.setImageTint(slot.icon, color[1], color[2], color[3], alpha)
            UI.setImageTint(slot.arrow, color[1], color[2], color[3], alpha)
        end
    end
end

-- Damage directions from DamageDirection.compute: a red arrow on a ring round the crosshair pointing towards where
-- each recent hit came from, fading out. style: arrowPattern and arrowSteps (the grenade warning's arrow images),
-- radius and size in pixels.
function Hud.showDamageDirections(list, style)
    if not Hud.handle then
        return
    end
    local show = not Hud.suppressed and style.arrowPattern ~= ""
    for i, slot in ipairs(Hud.damageArrows) do
        local hit = show and list[i] or nil
        if slot.arrow then
            UI.setVisible(slot.arrow, hit ~= nil)
        end
        if hit and slot.arrow then
            local s, c = math.sin(hit.angle), math.cos(hit.angle)
            UI.setPosition(slot.arrow, s * style.radius, -c * style.radius)
            if slot.size ~= style.size then
                UI.setSize(slot.arrow, style.size, style.size)
                slot.size = style.size
            end
            local turn = (hit.angle / (2 * math.pi)) % 1
            local image = string.format(style.arrowPattern, math.floor(turn * style.arrowSteps + 0.5) % style.arrowSteps)
            if slot.image ~= image then
                UI.setImage(slot.arrow, image)
                slot.image = image
            end
            UI.setImageTint(slot.arrow, 0.95, 0.12, 0.08, hit.alpha)
        end
    end
end

-- A large message in the upper middle of the screen with an optional line under it, for `seconds` (nil: until
-- replaced or cleared). Shown even while the rest of the HUD is suppressed, so it works on the death screen.
function Hud.showBanner(title, subtitle, seconds)
    Hud.banner = { title = title or "", subtitle = subtitle or "", remaining = seconds }
end

-- A short line above the prompt, such as "+50 Health", in the given colour; it fades out after a moment.
function Hud.showPickup(text, r, g, b)
    Hud.pickup = { text = text, remaining = Hud.PICKUP_SECONDS, r = r or 1, g = g or 1, b = b or 1 }
end

function Hud.clearBanner()
    Hud.banner = nil
end

-- Wave progress at the top of the screen; nil hides it.
function Hud.setWaveText(text)
    Hud.waveText = text
end

-- Darkens the screen behind the banner, for the death screen.
function Hud.setDeathFade(on)
    Hud.deathFade = on == true
end

local function refreshMessages(dt)
    local banner = Hud.banner
    if banner and banner.remaining then
        banner.remaining = banner.remaining - dt
        if banner.remaining <= 0 then
            Hud.banner = nil
            banner = nil
        end
    end
    local w = Hud.widgets
    if w.Banner then
        UI.setVisible(w.Banner, banner ~= nil)
        if banner then
            UI.setText(w.Banner, banner.title)
        end
    end
    if w.BannerSub then
        UI.setVisible(w.BannerSub, banner ~= nil and banner.subtitle ~= "")
        if banner then
            UI.setText(w.BannerSub, banner.subtitle)
        end
    end
    if w.WaveText then
        UI.setVisible(w.WaveText, Hud.waveText ~= nil and not Hud.suppressed)
        if Hud.waveText then
            UI.setText(w.WaveText, Hud.waveText)
        end
    end
    if w.DeathFade then
        UI.setVisible(w.DeathFade, Hud.deathFade)
    end
    local pickup = Hud.pickup
    if pickup then
        pickup.remaining = pickup.remaining - dt
        if pickup.remaining <= 0 then
            Hud.pickup, pickup = nil, nil
        end
    end
    if w.PickupText then
        UI.setVisible(w.PickupText, pickup ~= nil and not Hud.suppressed)
        if pickup then
            UI.setText(w.PickupText, pickup.text)
            UI.setTextColor(w.PickupText, pickup.r, pickup.g, pickup.b, math.min(1, pickup.remaining / 0.4))
        end
    end
end

function Hud.flashDamage()
    Hud.damageFlashRemaining = 0.3
end

-- kind is "hit", "head" or "kill"; a kill marker lasts longer than a hit.
function Hud.showMarker(kind)
    Hud.markerKind = kind
    Hud.markerRemaining = kind == "kill" and 0.45 or 0.18
end

function Hud.setSuppressed(suppressed)
    Hud.suppressed = suppressed == true
end

function Hud.refresh(info)
    if not Hud.handle then
        return
    end
    info = info or {}
    refreshMessages(info.dt or 0)
    local enabled = info.enabled and not Hud.suppressed
    local function vis(widget, on)
        if widget then
            UI.setVisible(widget, on and true or false)
        end
    end
    vis(Hud.widgets.Crosshair, enabled and info.showCrosshair and (info.adsBlend or 0) < 0.5)
    vis(Hud.widgets.HealthText, enabled and info.showHealth)
    vis(Hud.widgets.AmmoText, enabled and info.showAmmo)
    vis(Hud.widgets.WeaponName, enabled and info.showAmmo)
    vis(Hud.widgets.Prompt, enabled and info.showPrompt and info.prompt ~= nil)

    Hud.markerRemaining = math.max(0, Hud.markerRemaining - (info.dt or 0))
    local marking = enabled and Hud.markerRemaining > 0
    vis(Hud.widgets.HitMarker, marking and Hud.markerKind ~= "kill")
    vis(Hud.widgets.KillMarker, marking and Hud.markerKind == "kill")
    if marking and Hud.widgets.HitMarker and Hud.markerKind ~= "kill" then
        UI.setTextColor(Hud.widgets.HitMarker, 1.0, Hud.markerKind == "head" and 0.8 or 1.0,
                        Hud.markerKind == "head" and 0.2 or 1.0, 1.0)
    end

    Hud.damageFlashRemaining = math.max(0, Hud.damageFlashRemaining - (info.dt or 0))
    if Hud.widgets.DamageFlash then
        local flashing = enabled and Hud.damageFlashRemaining > 0
        UI.setVisible(Hud.widgets.DamageFlash, flashing)
        if flashing then
            UI.setOpacity(Hud.widgets.DamageFlash, 0.65 * Hud.damageFlashRemaining / 0.3)
        end
    end

    if Hud.widgets.HealthText and enabled and info.showHealth then
        -- Damage can be fractional; rounding up never shows 0 while alive.
        UI.setText(Hud.widgets.HealthText, string.format("%d / %d", math.ceil(info.health or 0),
                                                         math.ceil(info.maxHealth or 0)))
    end
    local weapon = info.weapon
    if Hud.widgets.AmmoText then
        if weapon and enabled and info.showAmmo then
            UI.setText(Hud.widgets.AmmoText, string.format("%d / %d", weapon.magazine or 0, weapon.reserve or 0))
        else
            vis(Hud.widgets.AmmoText, false)
        end
    end
    if Hud.widgets.WeaponName then
        if weapon and enabled and info.showAmmo then
            UI.setText(Hud.widgets.WeaponName, weapon.displayName or "")
        else
            vis(Hud.widgets.WeaponName, false)
        end
    end
    if Hud.widgets.GrenadeText then
        local grenades = info.grenades
        vis(Hud.widgets.GrenadeText, enabled and info.showAmmo and grenades ~= nil)
        if grenades and enabled and info.showAmmo then
            UI.setText(Hud.widgets.GrenadeText, string.format("Frag %d   Smoke %d", grenades.frag or 0,
                                                              grenades.smoke or 0))
        end
    end
    if Hud.widgets.Prompt then
        if enabled and info.prompt and info.showPrompt then
            UI.setText(Hud.widgets.Prompt, info.prompt)
        end
    end
    if info.enabled and FPS_DEBUG then
        engine_log(string.format("[FPS.Hud] health=%s ammo=%s",
            tostring(info.health),
            weapon and string.format("%s/%s", tostring(weapon.magazine), tostring(weapon.reserve)) or "none"))
    end
end

return Hud
