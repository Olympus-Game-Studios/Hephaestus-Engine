---
title: A tour of the editor
description: The header, the panels, the three workspace modes and the shortcuts worth learning first.
order: 3
---

# A tour of the editor

Open a project and you're in the editor. It looks like a lot at first, but it's really just a viewport surrounded by a handful of panels, and every panel has one job.

## The header

Along the top, from left to right:

- **The project menu and the main menus**: File, Edit, Scene, Build, View and Help.
- **Command search** (`Ctrl+K`). Type part of any menu command and run it from there. If you can't remember where something lives, this is the fastest way to find it.
- **Undo and redo** (`Ctrl+Z`, `Ctrl+Y`).
- **Play, Pause, Stop and Step**. Play runs your game right inside the viewport. Pause freezes it and Step advances it one frame at a time, which is brilliant for checking physics or animation. `Esc` stops playing and gives you your mouse back.
- **The mode switch**: Default, Artist and Programmer (more on those [below](#modes)).

Under the header there's a tool row with the transform tools, gizmo space (World or Local), snapping, **Create** (for adding things to the scene), **Bake lighting** and **Build & Run**. In Artist mode these tools move to a rail down the side so the viewport gets more room.

Next to Play there's a small arrow with a **Real-time** option. Turn it off and the game only moves forward when you press Step, which is handy when you want to inspect a single frame in detail.

## The panels

| Panel | What it's for |
| --- | --- |
| **Hierarchy** | Every entity in the open scene, as a tree. Shows icons for each type, plus markers for prefabs, hidden and locked entities. |
| **Inspector** | The components on whatever you've selected, as collapsible cards. This is where you change things. |
| **Content Browser** | Your project's files: models, textures, materials, scripts, prefabs, UI and so on. See [Content Browser and assets](content-browser.md). |
| **Console** | The engine log, live. Script errors show up here with a file and line you can double click. While the game is playing there's a `lua>` prompt for running Lua directly. |
| **Output** | Results from validating and packaging your project. |
| **Code** | The built in code editor. Lives in Programmer mode; open it from View in the other modes. |
| **Search** | Project wide text search (`Ctrl+Shift+F`) across your scripts and text files. |
| **Debugger** | Call stack, locals and watches when a Lua breakpoint is hit. See [Scripting with Lua](scripting-lua.md#debugging). |
| **Add Actors** | A palette of things you can drag into the scene. Open it from View. |

Every panel can be dragged, docked, tabbed together or floated. The View menu brings back anything you've closed.

The status bar along the bottom tells you whether the scene is saved, which scene is open, what's selected and the frame time. It also shows the editor's RAM, CPU and GPU use (GPU on Windows only). Right click the status bar to hide any of those.

## Modes

The mode switch in the header changes the whole layout to suit what you're doing.

- **Default** keeps everything in the classic arrangement: Hierarchy left, Inspector right, Content Browser and friends along the bottom.
- **Artist** puts the viewport first, moves the tools to a rail and makes Content Browser thumbnails big. The Hierarchy and Inspector stack on the right.
- **Programmer** opens the Content Browser as a file list beside the **Code** panel, with the Console, Output, Search and Debugger along the bottom. Opening a script opens it in the editor's own code editor instead of an external app.

Each mode remembers its own panel layout, so rearranging Artist mode doesn't mess up Programmer mode. Switch as often as you like.

## Moving around the viewport

| Do this | To |
| --- | --- |
| Hold right mouse and use `W` `A` `S` `D` | Fly around. Scroll while holding right mouse to change speed. |
| `F` | Frame whatever's selected. |
| Click | Select. Drag on empty space to box select. |
| `` ` `` | Cycle through objects stacked under the cursor. |
| `Alt+1` to `Alt+9` | Jump to a saved camera bookmark. `Ctrl+Alt+1` to `9` saves one. |

The viewport toolbar has the shading mode (**Lit**, **Unlit** or **Wireframe**) and the view (**Perspective**, or orthographic **Front**, **Back**, **Left**, **Right**, **Top** and **Bottom**). **View > Overlays** toggles the grid, gizmo, selection outline, sky, light icons, shadows and a statistics overlay.

Things you can't otherwise see get an icon in the viewport so you can find and click them: lights, particle emitters, audio sources, trigger volumes, the Environment, probe volumes, and objects that only run a script (such as a wave director). Each camera shows a small camera model pointing the way it looks, with a faint outline of its view, so you can aim it. Icons and camera models only appear in the editor, never in Play or in your game, and **View > Overlays > Light Billboards** hides them all. A camera's **Show Editor Mesh** setting hides just its model.

## Shortcuts worth learning first

| Shortcut | Does |
| --- | --- |
| `W` / `E` / `R` | Move, rotate or scale gizmo |
| `Q` | Hide the gizmo |
| `X` | Switch between World and Local gizmo space |
| `Ctrl+D` | Duplicate |
| `Alt` + drag a gizmo | Duplicate and move in one go |
| `Delete` | Delete the selection |
| `F2` | Rename (in the Hierarchy) |
| `H` / `Shift+H` / `Ctrl+H` | Hide selected, isolate selected, show everything again |
| `End` | Drop the selection onto the ground |
| `Ctrl+S` | Save the scene |
| `Ctrl+K` | Command search |
| `Ctrl+,` | Settings |
| `Ctrl+Shift+B` | Package your game |

In the Inspector, `Ctrl+Shift+C` and `Ctrl+Shift+V` copy and paste a whole component's values, which is a real time saver when you're setting up lots of similar things.

## Settings

**Edit > Settings** (`Ctrl+,`) has two tabs. **Engine Settings** apply to the editor on your machine, and **Project Settings** travel with the project: its name, startup scene, input, physics layers and build options.

Every setting in that window does something. A few worth knowing about:

- **Gizmo Size** (Engine Settings > Editor) scales the move, rotate and scale handles in the viewport.
- **Default Skybox** (Project Settings > Rendering) is the HDRI that **File > New Level** starts with. New projects point it at the starter sky.
- **Physics Enabled** (Project Settings > Rendering) switches physics off for the whole project, both when you press Play and in packaged games. Handy for a menu only project or a puzzle game that doesn't need it.
