---
title: Audio
description: Audio Source components and playing sounds from Lua.
order: 10
---

# Audio

Sound in Hephaestus is simple for now: you can play sound effects and music, loop them and set their volume. Supported formats are **WAV** and **MP3**.

## Audio Source

Add an **Audio Source** (from **Create** in the viewport, or as a component) and pick a clip. Its settings:

- **Play On Start**: start playing as soon as the game does. Perfect for music and ambience.
- **Loop**: keep playing forever.
- **Volume**: from 0 to 1.

You can also drag a sound from the Content Browser straight into the viewport.

## Playing sounds from Lua

For anything triggered by gameplay (footsteps, gunshots, a door creaking), play it from a script:

```lua
-- A one shot sound
Audio.play("assets/audio/pickup.wav")

-- Looping music at 40% volume, keeping the handle to control it later
local music = Audio.play("assets/audio/theme.mp3", true, 0.4)

Audio.setVolume(music, 0.2)
Audio.pause(music)
Audio.resume(music)
Audio.stop(music)
```

All the functions are in the [Lua API reference](lua-api.md#audio).

## Limitations

- **Every sound is 2D.** Sounds don't get quieter with distance or pan left and right based on where they are. The Audio Source's position doesn't affect how it sounds yet.
- There's no mixer, no buses and no effects.
- **Sound only plays on Windows.** The macOS build is silent for now.

A proper audio engine with 3D sound is on the list. Until then, you can fake distance a little by setting volume from a script based on how far the player is from the source.
