---
title: The launcher and projects
description: Creating and opening projects, choosing how you want to work and what lives in a project folder.
order: 2
---

# The launcher and projects

The launcher is the first thing you see. It's where you start new projects, reopen old ones and pick how you like to work.

## Your recent projects

The middle of the launcher lists the projects you've opened, newest first, with each one's workspace mode and when it last changed. Double click one to open it in the editor.

- **Search** with the box at the top, or press `Ctrl+F`.
- **Right click** a project for **Show in Explorer** and **Delete from disk...** Be careful with that last one. It really does delete the whole folder, and there's no undo.
- **Open project...** lets you pick a `project.heproject` file anywhere on disk, which is handy for projects someone sent you or ones you've moved.

If a project shows up as broken, its `project.heproject` file is damaged or unreadable. The launcher won't open it until that file is fixed.

## Making a new project

Click **New project**, give it a **Name** and a **Location** (it defaults to `Documents\HephaestusProjects`) and hit **Create**.

Leave **Create a default scene** ticked unless you have a reason not to. It gives you a `default.hpscene` with a sun, the starter sky, a floor and a camera, and sets it as the scene your game starts in, so you can press Play straight away and see something. The floor has a collider, so anything you give a Rigid Body lands on it instead of falling forever.

## Default, Artist or Programmer?

Under "How do you want to work?" there are three cards. They pick the layout the editor opens new projects in:

- **Default**: Hierarchy on the left, viewport in the middle, Inspector on the right, with the Content Browser and Console along the bottom. A good place to start.
- **Artist**: the viewport gets as much room as possible, tools sit on a rail and the Content Browser shows big thumbnails.
- **Programmer**: project files, a built in code editor, the viewport, Inspector, Console and build output. Scripts open inside the editor instead of an external app.

This isn't a commitment. You can switch any time from the mode switch in the editor's header, each mode remembers its own panel layout and the project remembers which mode you last used. More on that in [A tour of the editor](editor-tour.md#modes).

## What's in a project folder

Everything in a Hephaestus project is a plain file you can read, back up and put in version control.

```
MyGame/
├── project.heproject        Project name, version and the startup scene
├── assets/
│   ├── scenes/              Your levels (.hpscene)
│   ├── materials/           Materials (.hematerial)
│   ├── textures/
│   ├── scripts/             Lua scripts
│   ├── prefabs/             Reusable entity setups (.hpprefab)
│   ├── ui/                  Game UI layouts (.hui)
│   └── input/Default.hpinput    Your input actions and axes
├── .engine/hdris/           A starter sky, copied in for you
├── .hephaestus/             Editor state and project settings
└── Plugins/                 Optional plugins just for this project
```

A few things worth knowing about `.hephaestus/`:

- It holds settings you'll want to keep, such as `physics_layers.json` (your collision layers) and `plugins.json` (which plugins this project uses). Commit these.
- It also holds things that are safe to throw away: `thumbnails/` (Content Browser previews), `recovery/` (autosave snapshots) and `trash/` (assets you've deleted from the Content Browser, kept in case you want them back).

## Autosave and recovery

The editor quietly snapshots unsaved scenes while you work. If it crashes, or your PC does, the next time you open the project you'll be asked whether to restore those snapshots or throw them away. You can change how often it snapshots with **Autosave Interval** under **Edit > Settings > Engine Settings > General**.

It's a safety net, not a replacement for saving. `Ctrl+S` early and often.
