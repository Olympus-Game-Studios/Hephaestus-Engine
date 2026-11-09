---
title: Audio
description: Audio Source components, 3D sound, buses and playing sounds from Lua.
order: 10
---

# Audio

Hephaestus mixes as many sounds at once as you need: overlapping gunshots, footsteps and music all play together. Sounds can be 2D (the same in both ears, right for music, UI and the player's own weapon) or 3D (quieter with distance and panned to the side they come from, heard from the active camera). Supported formats are **WAV**, **MP3** and **FLAC**. Sound plays through WASAPI on Windows and Core Audio on macOS.

Each file is loaded and decoded the first time it plays and kept in memory, so playing it again never reads the disk.

## Audio Source

Add an **Audio Source** (from **Create** in the viewport, or as a component) and pick a clip. Its settings:

- **Play On Start**: start playing as soon as the game does. Perfect for music and ambience.
- **Loop**: keep playing forever.
- **Volume**: from 0 to 1.
- **Bus**: which volume group it belongs to (see below).
- **3D**: hear it from the entity's position. It follows the entity if it moves.
- **Min Distance**: inside this many metres it plays at full volume.
- **Max Distance**: beyond this many metres it is silent. In between it gets quieter with distance.

You can also drag a sound from the Content Browser straight into the viewport.

## Buses

Every sound plays on a bus, and each bus has its own volume: `sfx` (the default), `weapons`, `music` and `ui`, all under `master`. Turn a whole group down from a script, for example for a settings menu:

```lua
Audio.setBusVolume("music", 0.5)
Audio.setBusVolume("master", 0.8)
```

## Playing sounds from Lua

For anything triggered by gameplay (footsteps, gunshots, a door creaking), play it from a script:

```lua
-- A one shot sound
Audio.play("assets/audio/pickup.wav")

-- Looping music at 40% volume, keeping the handle to control it later
local music = Audio.play("assets/audio/theme.mp3", true, 0.4)

if music ~= 0 then             -- 0 means the sound couldn't be played
    Audio.setVolume(music, 0.2)
    Audio.pause(music)
    Audio.resume(music)
    Audio.stop(music)
end
```

Pass a table instead of `loop` and `volume` for more control:

```lua
-- A 3D explosion on the sfx bus, a little different every time
local p = Transform.getPosition()
Audio.play("assets/audio/explosion.wav", {
    position = { x = p.x, y = p.y, z = p.z },
    minDistance = 3, maxDistance = 120,
    volume = 0.9, pitchJitter = 0.05, volumeJitter = 0.1,
    priority = 5,
})
```

`pitchJitter` and `volumeJitter` vary each play by up to that fraction, so repeated sounds don't sound like a recording. Up to 48 sounds play at once. When that is full, a new sound replaces the quietest playing sound of the lowest `priority`, and is dropped if every playing sound has a higher priority than it.

All the functions are in the [Lua API reference](lua-api.md#audio).

## Limitations

- No effects yet (reverb, echo, filters), so a room and an open field sound the same.
- 3D sound is distance and left/right panning only: no Doppler, and sounds behind you are not muffled.
