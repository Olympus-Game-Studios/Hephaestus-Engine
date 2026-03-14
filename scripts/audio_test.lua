-- Audio system smoke test for Hephaestus.
-- Attach this script to any entity with a LuaScriptComponent.
-- Expected test assets (replace paths if your files differ):
--   assets/audio/theme.mp3
--   assets/audio/click.wav

local musicHandle = 0
local wasOneDown = false
local wasPDown = false
local wasSDown = false
local wasRDown = false

local function pressedNow(key, wasDown)
    local down = Input.isKeyPressed(key)
    local justPressed = down and not wasDown
    return down, justPressed
end

function onAwake()
    if not Audio.isAvailable() then
        engine_log("[AudioTest] Audio backend not available on this platform/build")
        return
    end

    musicHandle = Audio.play("assets/audio/theme.mp3", true, 0.35)
    if musicHandle == 0 then
        engine_log("[AudioTest] Failed to start looping music. Check file path.")
    else
        engine_log("[AudioTest] Music started. Controls: 2=one-shot, P=pause/resume, S=stop, R=restart")
    end
end

function onUpdate(deltaTime)
    local oneDown
    oneDown, oneJustPressed = pressedNow("2", wasOneDown)
    wasOneDown = oneDown
    if oneJustPressed then
        local sfx = Audio.play("assets/audio/click.wav", false, 1.0)
        if sfx == 0 then
            engine_log("[AudioTest] One-shot failed. Missing assets/audio/click.wav?")
        end
    end

    local pDown
    pDown, pJustPressed = pressedNow("P", wasPDown)
    wasPDown = pDown
    if pJustPressed and musicHandle ~= 0 then
        if Audio.isPlaying(musicHandle) then
            Audio.pause(musicHandle)
            engine_log("[AudioTest] Music paused")
        else
            Audio.resume(musicHandle)
            engine_log("[AudioTest] Music resumed")
        end
    end

    local sDown
    sDown, sJustPressed = pressedNow("S", wasSDown)
    wasSDown = sDown
    if sJustPressed and musicHandle ~= 0 then
        Audio.stop(musicHandle)
        musicHandle = 0
        engine_log("[AudioTest] Music stopped")
    end

    local rDown
    rDown, rJustPressed = pressedNow("R", wasRDown)
    wasRDown = rDown
    if rJustPressed then
        if musicHandle ~= 0 then
            Audio.stop(musicHandle)
        end
        musicHandle = Audio.play("assets/audio/theme.mp3", true, 0.35)
        if musicHandle ~= 0 then
            engine_log("[AudioTest] Music restarted")
        end
    end

    local _ = deltaTime
end

function onDestroy()
    Audio.stopAll()
end
