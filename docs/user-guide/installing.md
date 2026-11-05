---
title: Installing Hephaestus
description: What you need, what Steam installs and where Hephaestus keeps logs and settings.
order: 1
---

# Installing Hephaestus

Install Hephaestus from your Steam library like any other app. Steam also takes care of the Visual C++ runtime, so there's nothing else to download.

## What you need

- **Windows 10 or 11, 64 bit.** Windows is where Hephaestus gets the most use and testing.
- **A graphics card with up to date Vulkan drivers.** Hephaestus renders with Vulkan, so update your GPU driver before you start. Most cards from the last several years are fine.
- **Optional: a GPU with hardware ray tracing.** Some lighting features (ray traced shadows and ray traced probe lighting) switch on when your card supports Vulkan ray queries. Everything still works without one; those features quietly fall back to their regular versions.

There's also a macOS build (Apple Silicon, through MoltenVK). It's much newer than the Windows one and has had far less use, so expect rough edges. The biggest one right now: **sound doesn't play on macOS yet**, because the audio backend is Windows only.

## What's in the install folder

You don't need to poke around in here, but it helps to know what's what:

| File or folder | What it is |
| --- | --- |
| `HephaestusLauncher.exe` | The launcher, where you create and open projects. |
| `HephaestusEngine.exe` | The editor itself. |
| `HephaestusGame.exe` | The game player. Packaging copies this into your game and renames it after your game. |
| `tools\HephaestusBuild.exe` | A command line packaging tool, for when you want to package without opening the editor. |
| `Plugins\` | Engine plugins that come with Hephaestus. |
| `.engine\` and `assets\` | Built in meshes, skies and other engine content. |
| `LICENSE.md` | The Hephaestus licence. Worth a read before you sell a game (there's a summary in [Packaging and shipping](packaging.md#the-licence-in-plain-english)). |
| `THIRD_PARTY_NOTICES.md` | Licences for the libraries Hephaestus uses. |

Treat the install folder as read only. Your projects live somewhere else (by default `Documents\HephaestusProjects`), and Steam may replace anything in the install folder when it updates.

## Logs and crash reports

If something goes wrong, the log is the first thing to look at, and the first thing to attach to a bug report.

| | Windows |
| --- | --- |
| Editor log | `%LOCALAPPDATA%\Hephaestus\Logs\hephaestus.log` |
| Packaged game log | `%LOCALAPPDATA%\<Your Game>\Logs\runtime.log` |
| Packaged game crash dumps | `%LOCALAPPDATA%\<Your Game>\Crashes\` |

Paste `%LOCALAPPDATA%` into the Explorer address bar to jump straight there. Inside the editor, the **Console** panel shows the same log as it happens, so most of the time you won't need the file at all.

On macOS the editor and games log to `~/Library/Logs/`.

## Updating and project versions

Steam keeps Hephaestus up to date for you. Version numbers are dates: `2026.11.3` is the build from 3 November 2026.

Each project remembers the newest Hephaestus version that has opened it. If you open a project in an **older** Hephaestus than the one it was last saved with, the editor warns you before opening it. It'll still open, but the project may use features the older version doesn't understand, and saving could lose that work. If you see that warning, the safe move is to update first.
