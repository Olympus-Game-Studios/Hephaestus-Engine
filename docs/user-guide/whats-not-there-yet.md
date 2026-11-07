---
title: What's not there yet
description: The honest list of features that are missing, unfinished or only partly hooked up.
order: 20
---

# What's not there yet

Hephaestus is young, and I'd rather you know its gaps up front than discover them halfway through a project. Everything below is something that doesn't exist yet, or exists in the editor but doesn't fully do its job. This list will shrink.

## Rendering and materials

- **Material graphs aren't per pixel yet.** The graph is worked out once into PBR values, so nodes like Noise, Fresnel, Time, World Position and UV distortion don't affect what you see. The Material Editor marks them **(no effect yet)** and warns when a graph uses one. [Details](materials.md#what-the-graph-cant-do-yet).
- **No true transparency.** Opacity Mask cuts holes, but glass and water style blending isn't supported.
- **No changing materials from scripts.**
- **No post processing** like bloom, ambient occlusion, fog or depth of field, and no anti aliasing.
- **No LODs, instancing or streaming** for very large worlds.
- **Visual quality tiers and dynamic GI without Probe Volumes** are being built but aren't in this release. [Details](lighting.md#coming-soon-quality-tiers-and-gi-without-volumes).
- **Ray traced shadows skip animated characters.**

## Physics

- No joints, ragdolls or vehicles.
- Raycasts are the only physics query (no overlap or shape casts).
- There's no way to draw a Rigid Body's collision shape in the viewport yet. A Mesh Collider's **Debug Draw Bounds** does draw its box.
- No coyote time or jump buffering in the Character Controller; script them if you want them.

## Animation

- Clip playback and crossfades only. No animation graph, blend spaces, root motion or inverse kinematics.

## Audio

- All sound is 2D, with no distance falloff or panning.
- No mixer, buses or effects.
- No sound at all on macOS.

## Game UI

- No text fields, sliders, checkboxes or scrolling lists.
- No keyboard or gamepad navigation between buttons.

## Input

- On macOS, gamepads work in packaged games but not while playing in the editor. On Windows they work in both.
- One gamepad only, and no in game rebinding or input contexts.

## Scripting

- C++ is the only other scripting language that ships, and it's Windows only. Its API doesn't reach everything Lua can yet (game UI, `onAnimationFinished`, `onEnable`/`onDisable` and character controller scripts are Lua only), and the Code panel's debugger is Lua only. See [Scripting with C++](scripting-cpp.md).
- Lua scripts aren't sandboxed. Only run scripts you trust.

## World and gameplay systems

- No save game system. `Scene.reload()` restarts a scene but doesn't keep progress.
- No loading more than one scene at once, or streaming parts of a level in and out.
- No terrain tools, particles or visual effects.
- No navigation meshes or built in AI.
- No networking or multiplayer.
- No localisation.

## Editor

- **Nested prefabs and prefab variants** aren't supported.
- The Character Editor can't show bone names in its viewport.
- No light theme, HDR output, exposure control or shadow quality setting yet.
- **Plugins need C++** and the source headers from GitHub. Content only plugins aren't supported.

## Platforms

- Windows is the main platform. macOS works but is newer and less tested.
- No Linux, console or mobile support, for the editor or for games.

Found something that isn't on this list? Let us know, it probably should be.
