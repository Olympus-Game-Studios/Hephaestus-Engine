---
title: Scenes, entities and components
description: How a level is built from entities and components, placing and arranging things, and reusing setups with prefabs.
order: 5
---

# Scenes, entities and components

A **scene** is one level of your game, saved as a `.hpscene` file. A scene is full of **entities**, and an entity is just a named thing with a position. What it actually *does* comes from the **components** you add to it: a Mesh Renderer makes it visible, a Light makes it glow, a Rigid Body makes it fall over, a Lua Script makes it think.

That's the whole model. Once it clicks, everything else in the editor is just different ways of adding and tweaking components.

## Working with scenes

The File menu calls scenes **levels**:

- **New Level** (`Ctrl+N`), **Open Level...** (`Ctrl+O`), **Save Level** (`Ctrl+S`) and **Save Level As...**
- Keep scenes in `assets/scenes/` so they're easy to find.
- The scene your game starts in is the **Startup Scene** under **Edit > Settings > Project Settings > Project Info**. New projects set it to `default.hpscene` for you.

### Your first five minutes

A new project's default scene already has a sun, a sky, a floor and a camera. A scene you make with **File > New Level** only gets the project's sky, so here's a quick way to fill one in:

1. Right click in the viewport and choose **Create > Directional Light**. That's your sun.
2. If the Hierarchy has no **Environment** yet, select any entity, click **Add Component** in the Inspector and add **Environment** (under Lighting). Set its background to **HDRI** and pick the starter sky from `.engine/hdris`. Only one Environment is allowed per scene.
3. **Create > Plane** for a floor and **Create > Cube** for something to look at.
4. **Create > Camera**, move it somewhere sensible and tick **Active** on its Camera card. That's what the player sees when you press Play.
5. Press **Play**. Press `Esc` to stop.

## Adding things

Right click in the viewport and use **Create**. New things land wherever your mouse was pointing.

- **Cube**, **Sphere** and **Plane**
- **Directional Light**, **Point Light** and **Spot Light**
- **Camera**
- **Empty Entity**, for organising or for scripts that don't need a mesh
- **Audio Source**

The **Create** button in the tool row and the **Add Actors** panel (View menu) do the same thing, and you can drag meshes and prefabs in from the Content Browser.

## Components

Select an entity and its components show up as cards in the Inspector. **Add Component** opens a searchable list:

| Category | Components |
| --- | --- |
| Rendering | **Mesh Renderer** (also gives the entity a Material) |
| Lighting | **Light**, **Environment**, **Probe Volume** |
| Camera | **Camera** |
| Physics | **Rigid Body** (also adds a Physics Shape), **Physics Shape**, **Mesh Collider**, **Character Controller** |
| Audio | **Audio Source** |
| Animation | **Character** |
| Scripting | **Lua Script**, **Script Component** |

Each card's header has a menu with **Copy**, **Paste Values**, **Reset to Default** and **Remove**. Select several entities and the Inspector edits them all at once; fields where they disagree show as mixed.

Every component has its own page in this guide: [Materials](materials.md), [Lighting](lighting.md), [Physics](physics.md), [Characters and animation](characters-and-animation.md), [Audio](audio.md) and [Scripting with Lua](scripting-lua.md). **Script Component** is a leftover from an older C++ scripting system; use Lua Script instead.

## Moving things around

- `W`, `E` and `R` switch the gizmo between move, rotate and scale. `X` toggles World and Local space.
- Drag an arrow to move along one axis, a small square to move along a plane, or the dot in the middle to move parallel to the screen. Squares always sit on the side facing you.
- In rotate mode, drag a coloured ring to turn around that axis, or the grey outer ring to turn around the direction you're looking. A wedge shows how far you've turned.
- In scale mode, drag an end cube to stretch along one axis, or the cube in the middle to scale evenly.
- **Snapping** lives in the tool row, with separate steps for move, rotate and scale. Hold `Ctrl` while dragging to ignore snapping for a moment.
- Hold `Shift` while moving to slide the object along whatever surface is under it.
- Hold `Alt` while dragging the gizmo to drag out a copy.
- Transform values in the Inspector are **local**, meaning relative to the parent.

The **Scene** menu has the tidying tools: **Align to Active**, **Distribute Evenly**, **Drop Selected to Ground** (`End`), **Create Empty Parent** and **Frame All**.

### Parenting and organising

Drag one entity onto another in the Hierarchy to make it a child. Children move, rotate and scale with their parent. **Create Empty Parent** groups the selection under a new empty entity in one step, and you can also create folders in the Hierarchy to keep things tidy.

### Hide, isolate and lock

`H` hides the selection, `Shift+H` shows only the selection and `Ctrl+H` brings everything back. You can also click the eye at the right of a Hierarchy row (it shows when you point at the row, and stays on hidden ones) to hide or show that entity, or Alt+click it to show only that entity (Alt+click again to come out of it). Hiding or isolating something takes its children with it; their rows show a faded eye. **Lock / Unlock Selected** stops something being clicked by accident, which is perfect for floors and walls. Hidden things can't be clicked in the viewport. All of this only affects the editor; your game still sees everything.

## Prefabs

A **prefab** is a saved, reusable entity setup: say an enemy with its mesh, collider, character controller and script all configured. Place it in as many scenes as you like and edit it in one place.

- **Make one:** right click an entity in the Hierarchy and choose **Create Prefab From Selection**, or use **New Prefab** in the Content Browser.
- **Place one:** drag it from the Content Browser into the viewport, or right click it and choose **Instantiate in Scene**.
- **Edit one:** double click the prefab to open the **Prefab Editor**, which uses the same Inspector cards as the main editor. Save with `Ctrl+S`. Its Hierarchy has the same eyes, plus **Hide**, **Isolate** and **Show All** when you right click a node, so you can show just the part you're positioning (a weapon and its arms, say). That's only for the editor window and is never saved into the prefab.

Changes you make to a placed copy are tracked as **overrides** and marked in the Inspector. For each override you can **Revert** it back to the prefab's value or **Apply** it to the prefab so every copy gets it. **Unpack** turns a copy back into ordinary entities if you want to break the link.

Scripts can spawn prefabs while the game runs with `Scene.instantiate`, which is how you'd do bullets, pickups and enemy waves. See the [Lua API reference](lua-api.md#scene).

**Not supported yet:** prefabs inside prefabs, and prefab variants. Don't parent one prefab copy inside another and expect it to behave like a nested prefab.

## Blocking out levels

You can sketch a playable level straight in the viewport with construction shapes, no modelling program needed. They're described by their sizes rather than stored as meshes, so you can keep reshaping them and the walls, doors and collision follow.

- **Draw Block** (`B`) draws a solid block, good for cover, platforms and floors. Drag its footprint on the ground or on top of anything else, let go, move the mouse up to set the height and click. A plain click drops a 1 m block.
- **Draw Room** (`Shift+B`) draws a room: four walls and a floor, open at the top so you can see in. A plain click drops a 6 by 4 m room, 3 m high.
- **Draw Wall** (`L`) draws a wall corner by corner, for rooms that aren't rectangles. Click to place each corner; hold `Shift` to keep a side straight, or type a length and press `Enter` to place the next corner that far along. Click the first corner again to close the shape into a room with a floor, or click the last corner again (or press `Enter`) to leave it as an open wall. `Backspace` takes back the last corner. Doors, windows and subtract blocks cut walls just like rooms. Select a wall to drag its corners around, and set its height, thickness, floor and whether it's closed in the Inspector.
- **Draw Stairs** (`T`) draws a flight of stairs. Drag the footprint and they climb along its longer side, away from where you started dragging, then move up to set the height. Steps are just under the player's step height, so the player and bots can always walk up them; the Inspector shows how many steps you got and lets you pick a number instead.
- **Draw Opening** (`O`) cuts a doorway or window. Drag a rectangle on a wall, or just click: low on the wall gives a 1.2 by 2.1 m door standing on the floor, higher up gives a window. Drag an existing opening to slide it along the wall.
- **Resize** a block or room by selecting it and dragging the handles in the middle of its faces. The opposite side stays put; hold `Shift` to grow both sides at once.

While you draw, sizes snap to a 25 cm grid (or the gizmo's snap value when snapping is on). Hold `Ctrl` to turn snapping off, type sizes such as `4,3` then `Enter` for the footprint and `2.5` then `Enter` for the height, and press `Esc` to cancel. The status bar tells you what each tool is waiting for. `Q`, `W`, `E` or `R` takes you back to selecting.

The Inspector's **Construction** card has the exact numbers: width, height and depth, whether it's a hollow room, wall thickness, floor and ceiling, and for openings which wall they're on, how far from its middle, their sill height and size. New shapes get a 1 m grid texture so you can judge distances at a glance; give them any material you like.

Collision is built for you, so players and bots can walk through doorways straight away. To try a space, use **Play > Play From Here** (`Ctrl+Shift+P`): the player starts where the viewport camera is, facing the same way, and goes back to where you placed them when you stop.

To cut a hole of any size anywhere (a window through two rooms at once, a gap in a floor), use a **subtract** block: press the Cube button in the tool row, or select any block, and set **Mode** to **Subtract** in its Construction settings. A Cube dropped onto a wall or floor is centred on the surface, so it cuts into it straight away. A subtract block shows as a see-through red box, cuts every block and room it overlaps, moves the hole wherever you move it, has no collision and is hidden in the game. Set Mode back to Add to make it a solid block again.

The Plane, Cylinder and Sphere buttons make construction shapes too. Their size, segments and collision are in the same Construction settings, and their face handles resize them. They can't subtract yet.

Construction shapes work in packaged games too: they're built from their sizes when the level loads.

**Not there yet:** ramps, walls drawn as lines, rooms with other footprints and materials per face.

## Undo

Almost everything in a scene can be undone with `Ctrl+Z` and redone with `Ctrl+Y`, including gizmo drags, component edits, renames and creating or deleting things.
