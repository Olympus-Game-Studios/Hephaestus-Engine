---
title: Plugins
description: What plugins can do, the ones that come with Hephaestus, turning them on and off and making your own.
order: 16
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
- **Sketchfab**: open it from the **...** menu in the Sketchfab Store window (below) to search downloadable Sketchfab models, see who made each one and its licence, and import it with one click. Importing needs your Sketchfab API token (copy it from the Password & API page of your Sketchfab settings); Hephaestus keeps it in Windows Credential Manager, never in your project. Each model comes in as a prefab with its materials made and applied, under `assets/Imported/Sketchfab`. NonCommercial models get a warning, since you can't use them in a game you sell.
- **Fab**: the quickest way in is straight from the Epic Games Launcher. In the launcher's Fab settings, set the export target to **Custom (socket port)**; then clicking **Export** on any listing in your library imports it into the project open in Hephaestus as prefabs with materials and a credit (the launcher shows whether it worked). Many sellers keep their textures in additional content, so for the best result click **Download only** first, then the **...** menu > **Download additional content**, then **Export**: when an export comes without textures, Hephaestus looks through the listing's additional content too. Hephaestus listens on the same port as the launcher, only on your own machine, and you can switch it off, change the port or pick the licence it credits from the **...** menu > **Import settings** in the Fab Store window. You can also pick a format like FBX, glTF or OBJ on Fab (not the Unreal Engine format), then drop the .zip, folder or model onto that same Import settings window and fill in where it came from. It understands Megascans and Unreal style texture names (`_BaseColor`, `_Normal`, `_ORM` and friends), only takes LOD0 from packs that ship every LOD, and turns texture sets with no model into materials. It also adds **Import with Plugin > Asset pack with materials** to the Content Browser's right click menu for any pack you have on disk.

Imported models remember their materials (in a small `.hpmesh` file beside the model that the Content Browser keeps out of sight), so you can drop the mesh itself or its prefab into a scene and both come in textured. Both write a `credits.json` beside each asset they import (who made it, where it's from and its licence) and rebuild `assets/scripts/ImportedCredits.lua` from all of them, ready for your credits screen. To change a credit, edit its `credits.json` (the Lua file is rewritten on every import).
- **Asset Store Browser**: Fab and Sketchfab inside the editor, under **Window > Asset Stores > Fab Store** and **Sketchfab Store**. Downloading a model on Sketchfab (FBX, glTF and the rest come as .zip files) imports it the same way, and its author and licence are read from Sketchfab for the credit. It opens in its own window (it remembers its size and place); sign in with your Epic account there (you stay signed in), pick a format like FBX, glTF or OBJ on a listing and download it: the pack lands in the open project as prefabs with materials and a credit, just like the Fab plugin's imports. It brings its own web browser (Qt WebEngine, about 270 MB), which is why it's a separate plugin you can leave out.
- **Hephaestus MCP**: lets AI assistants that speak the Model Context Protocol drive the editor (inspect the scene, play, step frames, take screenshots, package builds). It only listens on your own machine (`127.0.0.1`) and you can change its port in its settings. It's enabled by default, so switch it off in the Plugin Manager if you don't use AI tools. AI assistants can bring in assets too: `asset.import_pack` imports a .zip, folder or model you've downloaded (from Fab or anywhere) the same way the Fab plugin does, and with the Sketchfab plugin on, `sketchfab.search`, `sketchfab.import` and `sketchfab.import_status` let them find and import Sketchfab models using the token you saved. They won't import NonCommercial or NoDerivs models unless you tell them to.

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
