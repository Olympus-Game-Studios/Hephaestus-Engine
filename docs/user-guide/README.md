---
title: Hephaestus user guide
description: Everything you need to go from installing Hephaestus on Steam to shipping your first game.
order: 0
---

# Hephaestus user guide

Welcome! This guide is for people who've grabbed Hephaestus from Steam and want to make games with it. You don't need to build the engine from source, know C++ or have used another engine before. If you have, great, a lot of this will feel familiar.

I've tried to keep it honest. Hephaestus is young and moving quickly, so wherever a feature is half built or missing I'll say so rather than let you find out the hard way. There's a whole page on that too: [What's not there yet](whats-not-there-yet.md).

## Start here

1. [Installing Hephaestus](installing.md) covers what you need, what Steam puts on your machine and where logs end up.
2. [The launcher and projects](launcher-and-projects.md) walks through making your first project and what's inside the folder.
3. [A tour of the editor](editor-tour.md) explains the header, panels, modes and the shortcuts worth learning on day one.

## Building your game

- [Content Browser and assets](content-browser.md): importing models, textures and sound, and keeping references intact when you move things around.
- [Scenes, entities and components](scenes-and-entities.md): how a level is put together, placing things, snapping and prefabs.
- [Materials](materials.md): PBR materials and the node based Material Editor.
- [Lighting](lighting.md): lights, shadows, skies and global illumination with probe volumes.
- [Physics](physics.md): rigid bodies, colliders, layers, triggers and the character controller.
- [Characters and animation](characters-and-animation.md): importing rigged characters and playing animations.
- [Audio](audio.md): sound sources and playing sounds from scripts.

## Making it play

- [Scripting with Lua](scripting-lua.md): lifecycle callbacks, Inspector variables, the code editor and debugger.
- [Lua API reference](lua-api.md): every table and function the engine gives your scripts.
- [Input](input.md): actions, axes, keyboard, mouse and gamepads.
- [Game UI](game-ui.md): menus and HUDs with the UI Editor and `.hui` files.
- [Other languages through plugins](other-languages.md): C#, Python and friends.

## Extending and shipping

- [Plugins](plugins.md): what ships with Hephaestus and how plugins work.
- [Packaging and shipping](packaging.md): turning your project into a game other people can play, plus the licence basics.
- [What's not there yet](whats-not-there-yet.md): the honest list.
