---
title: Packaging and shipping
description: Turning your project into a game other people can play, what goes in the package and the licence basics.
order: 19
---

# Packaging and shipping

At some point you'll want someone else to play your game, which means turning your project into a folder with an `.exe` they can just run. Hephaestus calls that **packaging**. The people playing don't need Hephaestus, Lua or anything else installed.

## Before you package

1. **Set your Startup Scene** in **Edit > Settings > Project Settings > Project Info**. That's the scene the game opens in.
2. **Validate**. **Build > Validate Project** checks every scene, prefab, material, script reference and setting for problems (a missing texture, a broken path) and lists them in the **Output** panel. Fix anything it flags before you go further.

## Packaging

Open **Build > Package Project** (`Ctrl+Shift+B`). The Build Project window has:

- **Configuration**: **Development** or **Release**. Development is for testing: it keeps extra logging and the testing hooks the editor and AI tools use. **Release** is what you give to players.
- **Platform**: Windows x64.
- **Title**, **Resolution**, **Fullscreen** and **VSync** for the game window. These are saved with the project.
- **Output**: where the package goes. This is remembered on your machine only, so teammates can each use their own folder.
- **Create ZIP** to get a ready to upload archive as well as the folder.
- **Validate project first**, which is worth keeping ticked.

Then click **Package**, or **Package & Run** to launch the game as soon as it's done. The Output panel shows progress file by file. You can cancel at any time, and your previous successful package isn't touched until the new one has finished, so a failed package never leaves you with nothing.

**Build > Run Last Package** launches the most recent package again, and **Build > Clean Package** deletes it (after asking). The arrow on the **Build & Run** button in the tool row has the same options.

For a quick test, **Play > Build and Run** (`Ctrl+B`, or a click on the **Build & Run** button) skips this window: it packages the scene you have open with your project's settings into `Saved/BuildAndRun/Development` (or `Release`, whichever is ticked in the Play menu) and starts the game. It doesn't change the settings above or your usual output folder. If the scene has unsaved changes, you choose between saving first and building the last saved version.

**Report possibly unused assets** lists files nothing seems to reference. It's only advice: Hephaestus packages everything in your `assets` folder anyway, because scripts can load files by name and there's no way to know for sure what they'll ask for.

## What's in the package

```
MyGame/
├── MyGame.exe               The game player, renamed after your game
├── lua54.dll                Lua
├── game.ini                 Window and startup settings
├── assets/                  Your scenes, scripts, materials, textures and so on
├── shaders/                 Compiled shaders
├── Plugins/                 Content from your enabled plugins
├── build_manifest.json      What went into this build
└── ThirdPartyNotices.txt    Licences for the libraries the game uses
```

Ship the whole folder. Players run `MyGame.exe`. You can set a custom icon with **Application Icon** under **Edit > Settings > Project Settings > Build**.

Your game writes its log and crash dumps to `%LOCALAPPDATA%\MyGame\` rather than its own folder, so it works fine from read only places like a Steam install. If players report a crash, that's where to ask them to look.

Prefer the command line? `tools\HephaestusBuild.exe` in the Hephaestus install folder does the same packaging without opening the editor, which is useful for automated builds. Run it with `--help` to see its options.

## Platforms

Packaging from the Windows editor makes **Windows 64 bit** games. The macOS editor packages a Mac `.app`, but as mentioned in [Installing Hephaestus](installing.md), macOS is newer, less tested and has no sound yet. Linux, consoles and mobile aren't supported.

## The licence in plain English

Hephaestus is **source available**, not open source. This is just a friendly summary. `LICENSE.md` in the install folder is what actually counts, so please read it before you sell anything.

- **You can make and sell games with it**, including commercial ones, and you own what you make.
- **It's royalty free until a game earns US$1,000,000** in lifetime gross revenue. Past that it's 5% of gross revenue above the first million. You need to let us know within 30 days of the end of the quarter you cross that line.
- **Your game can include the engine runtime** that packaging puts in it. It can't include the engine's source code, or the editor and tools.
- **Mod tools are allowed.** You can give players the editor, for free, so they can make mods for *your* game. The licence lists the conditions.
- **You can't use Hephaestus to make a competing engine or editor**, or redistribute or sell the engine itself without permission.

Packaging copies `ThirdPartyNotices.txt` into your game automatically, which covers the open source libraries Hephaestus uses. Keep it with your game.
