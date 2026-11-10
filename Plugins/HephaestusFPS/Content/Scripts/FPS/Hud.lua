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
end

function Hud.unmount()
    if Hud.handle and destroy and destroy.ui then
        destroy.ui(Hud.handle)
    end
    Hud.handle = nil
    Hud.widgets = {}
    Hud.damageFlashRemaining = 0
    Hud.suppressed = false
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
