local Hud = {
    handle = nil,
    widgets = {},
    loggedError = false,
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
    Hud.widgets.Crosshair = UI.getWidget(handle, "Crosshair")
    Hud.widgets.HealthText = UI.getWidget(handle, "HealthText")
    Hud.widgets.AmmoText = UI.getWidget(handle, "AmmoText")
    Hud.widgets.WeaponName = UI.getWidget(handle, "WeaponName")
    Hud.widgets.Prompt = UI.getWidget(handle, "Prompt")
end

function Hud.unmount()
    Hud.handle = nil
    Hud.widgets = {}
end

function Hud.refresh(info)
    if not Hud.handle then
        return
    end
    info = info or {}
    local function vis(widget, on)
        if widget then
            UI.setVisible(widget, on and true or false)
        end
    end
    vis(Hud.widgets.Crosshair, info.enabled and info.showCrosshair)
    vis(Hud.widgets.HealthText, info.enabled and info.showHealth)
    vis(Hud.widgets.AmmoText, info.enabled and info.showAmmo)
    vis(Hud.widgets.WeaponName, info.enabled and info.showAmmo)
    vis(Hud.widgets.Prompt, info.enabled and info.showPrompt and info.prompt ~= nil)

    if Hud.widgets.HealthText and info.showHealth then
        UI.setText(Hud.widgets.HealthText, string.format("%d / %d", info.health or 0, info.maxHealth or 0))
    end
    local weapon = info.weapon
    if Hud.widgets.AmmoText then
        if weapon and info.showAmmo then
            UI.setText(Hud.widgets.AmmoText, string.format("%d / %d", weapon.magazine or 0, weapon.reserve or 0))
        else
            vis(Hud.widgets.AmmoText, false)
        end
    end
    if Hud.widgets.WeaponName then
        if weapon and info.showAmmo then
            UI.setText(Hud.widgets.WeaponName, weapon.displayName or "")
        else
            vis(Hud.widgets.WeaponName, false)
        end
    end
    if Hud.widgets.Prompt then
        if info.prompt and info.showPrompt then
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
