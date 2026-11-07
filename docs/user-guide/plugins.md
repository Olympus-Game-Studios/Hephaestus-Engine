---
title: Plugins
description: What plugins can do, the ones that come with Hephaestus, turning them on and off and making your own.
order: 18
---

# Plugins

Plugins are how Hephaestus grows without everyone being stuck with the same workflow. A plugin can add editor panels, menu commands and toolbar buttons, new kinds of assets with their own editors, project settings, content like scripts and prefabs, and even whole scripting languages.

## Turning plugins on and off

Open **Edit > Plugins...** to see every plugin Hephaestus found, with its version, author and status. Tick or untick **Enabled** to switch one on or off for this project. Changes apply after you **restart the editor**.

Plugins are picked up from two places:

- the `Plugins` folder in the Hephaestus install folder, which are available to every project, and
- a `Plugins` folder inside your project, for plugins that belong to just that project.

Which plugins are on is saved per project in `.hephaestus/plugins.json`, so your teammates get the same set.

## What comes with Hephaestus

- **Primitive Tools**: an example plugin that adds a few scene authoring commands, a panel and a custom asset type. It's mostly there to show plugin authors how things fit together.
- **C++ Scripting**: write gameplay scripts in C++. Off by default; see [Scripting with C++](scripting-cpp.md).
- **Hephaestus MCP**: lets AI assistants that speak the Model Context Protocol drive the editor (inspect the scene, play, step frames, take screenshots, package builds). It only listens on your own machine (`127.0.0.1`) and you can change its port in its settings. It's enabled by default, so switch it off in the Plugin Manager if you don't use AI tools.

## Plugins in your game

When you package a game, the **content** of every enabled plugin (scripts, prefabs, UI, meshes and so on) goes in with it, along with any scripting language the plugin provides. Editor only parts of a plugin stay behind, since they're no use to players.

Scripts can `require` modules from enabled plugins, and assets inside a plugin use paths like `plugin://com.example.tools/Prefabs/Thing.hpprefab`. If a project uses a plugin that isn't installed, packaging stops and tells you which one is missing, rather than shipping a broken game.

## Making your own

A plugin is a folder with a `.hpplugin` manifest (a small JSON file naming the plugin and what it contains) plus a native library built in C++. That means writing plugins needs a C++ compiler and the Hephaestus plugin headers, which live in the engine's source repository on GitHub, not in the Steam install. The repository has the details:

- `docs/plugins/plugin-manifest.md` for the manifest
- `docs/plugins/plugin-api.md` for the editor plugin API
- `docs/plugins/script-languages.md` for scripting language plugins
- `Plugins/PrimitiveTools` as a complete worked example

Fair warning: plugins must be built with the same compiler family and Qt version as the editor, and the plugin API is versioned with the engine, so a plugin may need rebuilding when Hephaestus updates. Content only plugins (just scripts and assets, no code) aren't supported yet; every plugin needs a native library.
