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
- When you move an object near another one, its sides snap flush to the other's: a side within 20 cm of touching a neighbour's side, or of lining up with it, jumps the rest of the way. It only snaps along the directions you're dragging, and `Ctrl` turns it off. This is how a wall meets the side of a block without a sliver between them. Dragging a shape's size handles does the same for the face you're dragging.
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

### Prefabs inside prefabs

Prefabs can be built out of other prefabs. Place the smaller prefabs (crates, a barrier, a lamp), parent them under one entity or under one of them, and use **Create Prefab From Selection** on the top one. The new prefab keeps a link to each smaller prefab instead of copying it, along with any changes you'd made to those copies. Now when you edit the crate prefab, every crate updates: in your levels, inside the new prefab, and inside every copy of the new prefab.

Changes stack up in layers. The bigger prefab can change its crates (paint one red), and a level can change a copy of the bigger prefab (turn its lamp off), without breaking either link. Editing a part of a nested prefab in the Prefab Editor, or applying an override to it, saves the change on the bigger prefab rather than on the crate prefab itself.

**Variants:** Create Prefab From Selection on a single prefab copy makes a variant of it: a new prefab that is the original plus your changes, and that still picks up later fixes to the original.

**Not supported yet:** removing a component or a child from a nested part, or moving a nested part to a different parent, is not kept when you save. Change its values instead, or unpack it.

## Blocking out levels

You can sketch a playable level straight in the viewport with construction shapes, no modelling program needed. They're described by their sizes rather than stored as meshes, so you can keep reshaping them and the walls, doors and collision follow.

- **Draw Block** (`B`) draws a solid block, good for cover, platforms and floors. Drag its footprint on the ground or on top of anything else, let go, move the mouse up to set the height and click. A plain click drops a 1 m block.
- **Draw Room** (`Shift+B`) draws a room: four walls and a floor, open at the top so you can see in. A plain click drops a 6 by 4 m room, 3 m high.
- **Draw Wall** (`L`) draws a wall corner by corner, for rooms that aren't rectangles. Click to place each corner, or type a length and press `Enter` to place the next corner that far along. A side that's nearly straight along X or Z is made exactly straight (hold `Shift` to force it, `Ctrl` to turn snapping off), and corners snap onto the corners of other walls and rooms nearby, so separate pieces meet cleanly. Start on the end of an existing open wall to carry that wall on, so the new corner is mitred like any other. Click the first corner again to close the shape into a room with a floor, or click the last corner again (or press `Enter`) to leave it as an open wall. `Backspace` takes back the last corner. Doors, windows and subtract blocks cut walls just like rooms. Select a wall to drag its corners around, and set its height, thickness, floor and whether it's closed in the Inspector.
- **Draw Stairs** (`T`) draws a flight of stairs. Drag the footprint and they climb along its longer side, away from where you started dragging, then move up to set the height. Steps are just under the player's step height, so the player and bots can always walk up them; the Inspector shows how many steps you got and lets you pick a number instead. To make them turn, pick **Turn** in the Inspector: a quarter turn left or right puts half the steps up one side, a landing in the far corner and the rest across the far side; a U-turn climbs back down the other side instead. **Flight Width** sets how wide each flight and the landing are.
- **Draw Ramp** (`Shift+T`) draws a solid slope the same way: drag the footprint and it rises along the longer side, away from where you started, then move up for the height. A plain click drops a 2 by 4 m ramp 1 m high. The Inspector shows its slope and warns you when it's steeper than characters can walk up (45 degrees by default).
- **Draw Opening** (`O`) cuts a doorway or window. Drag a rectangle on a wall, or just click: low on the wall gives a 1.2 by 2.1 m door standing on the floor, higher up gives a window. Drag an existing opening to slide it along the wall.
- **Resize** a block or room by selecting it and dragging the handles in the middle of its faces. The opposite side stays put; hold `Shift` to grow both sides at once.

While you draw, sizes snap to a 25 cm grid (or the gizmo's snap value when snapping is on). Hold `Ctrl` to turn snapping off, type sizes such as `4,3` then `Enter` for the footprint and `2.5` then `Enter` for the height, and press `Esc` to cancel. The status bar tells you what each tool is waiting for. `Q`, `W`, `E` or `R` takes you back to selecting.

The Inspector's **Construction** card has the exact numbers: width, height and depth, whether it's a hollow room, wall thickness, floor and ceiling, and for openings which wall they're on, how far from its middle, their sill height and size. New shapes get a 1 m grid texture so you can judge distances at a glance; give them any material you like.

Collision is built for you, so players and bots can walk through doorways straight away. To try a space, use **Play > Play From Here** (`Ctrl+Shift+P`): the player starts where the viewport camera is, facing the same way, and goes back to where you placed them when you stop.

To find what would stop the player before you play, use **Build > Check Level**. It checks the level against the Character Controllers in the scene (their height, width, step height, slope limit and jump) and lists doorways too narrow or too low to get through, stairs with steps too high to climb, ramps too steep to walk up, and floor the characters can't walk to from where they start, such as a room with no way in. For floor they can't walk to, it works out whether a running jump at walking speed gets there: a gap too wide or a ledge too high is a warning that says how far short the jump falls, floor reached only by jumping or dropping down is listed as a note, and floor behind a wall counts as unreachable. A doorway that the player fits through but bots can't path through is a warning. Double-click a problem to select the shape at fault and frame it in the viewport, and press **Check Again** after fixing it. With no character in the scene, the default player size is used and reachability isn't checked.

To see how long it takes to get somewhere, use **Measure Walk** (`M`, the last button in the tool row). Click where the walk starts: the route to the cursor is drawn in green, and the readout next to the tool buttons shows its length, how long it takes at the characters' walking speed and the straight-line distance, such as "Walk 18.9 m, 3.8 s (straight line 11.7 m)". Click again to keep the measurement, click once more to start a new one, and press `Esc` to clear it. The route is drawn orange when the end can't be walked to; it then stops at the closest point it reaches. The route follows the same navmesh as the bots, so it goes through doorways and around obstacles, and counts vaults over low cover. Sprinting isn't counted.

To see how a level actually plays, use **Build > Run Bot Playtest**. It plays the open level as a bot match faster than real time (choose how many minutes of match to simulate) and then colours the floor: **Traffic** shows where soldiers walked, **Kills** where shooters stood when they got a kill, and **Deaths** where soldiers fell, from blue (rarely) through yellow to red (most). The window also sums up the kills per team and the busiest fighting spot. Look for lanes nobody uses, one spot where most of the fighting happens, and kills that lean heavily towards one team. The playtest needs a game script that knows how to play a bot match; the Hephaestus FPS plugin's team deathmatch does, and you sit the match out while bots fill both teams. Pick **Off** to hide the colours.

The navmesh is split into chunks about 38 m across. The first measurement or level check on a big level builds all of them, which can take a few seconds; after that, an edit only rebuilds the chunks it touches, and pressing Play reuses the chunks that are already built, so bots are ready straight away. Built chunks are also kept in the project's `.hephaestus/ddc` folder, so the next time you open the editor they load in a moment instead of being built again. That folder is a cache: it is safe to delete, and projects made with the editor already leave it out of version control.

To **paint a face**, drag a material from the Assets panel onto it in the viewport: only that face changes, such as one side of a block, the inside of one room wall, a room's floor, or one side of one wall segment. Hold `Shift` while you drop to paint the whole shape instead. Painted faces keep their material when you resize the shape or cut doors into it, and the **Painted Faces** list in the Construction settings shows them, each with a **Clear** button. Dropping a material on any other mesh puts it in its first material slot.

Each painted face in that list also lines up its texture:

- **Offset** moves the texture across and up the face, in texture repeats.
- **Scale** sets how wide and tall one repeat is, in metres. A texture is normally one metre across, lined up with the world.
- **Turn** rotates the texture on the face, in degrees.
- **Fit** stretches the texture once across the whole face, such as a sign or a door texture. Scale then sets how many times it repeats.

The alignment is saved with the scene and kept in prefabs and copies.

To cut a hole of any size anywhere (a window through two rooms at once, a gap in a floor), use a **subtract** block: press the Cube button in the tool row, or select any block, and set **Mode** to **Subtract** in its Construction settings. A Cube dropped onto a wall or floor is centred on the surface, so it cuts into it straight away. A subtract block shows as a see-through red box, cuts every block and room it overlaps, moves the hole wherever you move it, has no collision and is hidden in the game. Set Mode back to Add to make it a solid block again.

The Plane, Cylinder and Sphere buttons make construction shapes too. Their size, segments and collision are in the same Construction settings, and their face handles resize them. They can't subtract yet.

Construction shapes work in packaged games too: they're built from their sizes when the level loads.

### Buildings with more than one storey

Select a room (a hollow block, or walls drawn with **Draw Wall** and closed into a room of any shape) and press **Add Storey Above** in its Construction settings: a copy of the room is stacked on top with the same footprint, wall thickness, kit and look, its floor resting on the room's walls (the room's own ceiling goes, since the new floor replaces it). The new storey sits under the room in the Hierarchy, so moving the room moves the whole building, and you can select the storey and add another on top of it. Then press **Add Stairs Up** on the lower room: a flight goes along one of its longest straight inside walls, from its floor to the top of the floor above, with a metre of floor in front of the first step, and a stairwell is cut through the floor above it. It keeps clear of doorways. In a room too short for a straight flight it builds one that turns back on itself, with the landing against the wall. Stairs only go against walls that run along X or Z. The steps follow the player's step height, and bots can climb them. Adding stairs to the next storey puts its flight along the other wall, clear of the stairwell below. Doors, windows and kits work on every storey as on any room, and the stairs and stairwell are ordinary shapes you can move, resize or delete.

To finish the building, select the top storey and press **Flat Roof** or **Pitched Roof**. A flat roof is a slab over the walls (30 cm past them on a room made as a block); a pitched roof is two 30 degree slopes meeting in a ridge along the room's longer side, and is for rooms made as blocks. Either replaces the room's ceiling. For a balcony, cut a doorway in an upper storey, select the doorway and press **Add Balcony**: a slab 1.5 m deep and a metre wider than the door on each side is built outside it, level with the floor, with railings along its three open edges. The balcony is one shape: drag its handles to make it wider or deeper and the railings follow, drag the top handle to change the railing height, and set its floor thickness in its Construction settings. Balconies at the same height join up: where one balcony's floor runs into another's, such as two balconies stretched to meet round a corner of the building, the railing between them goes so you can walk from one to the other, and the outer edges keep theirs. Roofs and balconies are ordinary shapes under the room, so you can resize, move or delete them.

For shapes a recipe can't make, such as a wedge with a slanted top, a tapered pillar or a skewed platform, select a block or ramp and press **Make Editable** in its Construction settings. It keeps its shape, but instead of size handles it gets a handle on every corner: drag one to move that corner across at its own height, or hold `Shift` to move it straight up or down. Corners snap to the grid like everything else (`Ctrl` turns snapping off), each drag is one undo step, and `Esc` puts the corner back. Every face of an editable mesh can be painted on its own.

An editable mesh also has a handle in the middle of every edge and every face:

- Drag an edge handle to move both ends of that edge together (`Shift` for up and down).
- Drag a face handle to push or pull the whole face along the way it faces.
- `Shift`-drag a face handle outwards to pull out a new part from that face, with sides joining it to the rest, such as a step on a platform or a buttress on a wall. `Shift`-drag it inwards to push in a recess, such as an alcove in a wall or a pit in a floor.
- `Alt`-click a face handle to split the face in two across its middle. Split the top of a block, then pull out one half, to make an L shape.

Handles snap to the grid, each edit is one undo step and `Esc` cancels a drag. An editable mesh collides exactly as it is drawn, so an L shape collides as an L, a recess can be walked into and a corner pulled inwards collides where it is. Bots use the same shape. Doors and windows work on an editable mesh's upright faces just as on blocks: use **Draw Opening** on a face, and the opening is cut right through the mesh. Subtract blocks cut editable meshes too. To start again, press **Turn Back Into Block** (or **Turn Back Into Ramp**): the mesh becomes a plain block that fits around it, in the same place.

### Dressing walls with a kit

A kit turns a blockout into finished walls made from your own art. It's a `.hpkit` file in your project's `assets` folder that lists prefabs (wall panels, a filler, a corner post, door and window frames) with the width of each. Select a wall or a room and pick the kit in the **Kit** section of its Construction settings: the panels are laid along every wall, corner posts go at the corners and frames go over doors and windows. Each stretch gets the mix of panels that needs the least stretching, and anything the kit can't fill shows as a red box in the editor, so you can tell at a glance where you need another piece. The Kit section says how many pieces were placed, how far they stretched and how many gaps there are.

The blockout is still there underneath: it gives the level its collision and navmesh, so dressing a room never changes how it plays, and its floor and ceiling stay visible. Move a corner, resize the room or cut a new door and the pieces are laid out again; so are they when you save the kit file. Pick **None** to go back to the blockout. You can also drag a kit from the Assets panel straight onto a wall or room in the viewport.

To **swap a single piece**, click it in the viewport: its wall or room is selected and the Kit section names the piece you clicked, with a list of the kit's other pieces that have the same role and width. Pick one to put it in that spot, or pick the one marked "kit's choice" to go back. A swap stays in its spot when you resize the room or move a corner. If the spot disappears, for example because a door now covers it, the Kit section tells you how many swaps no longer match, and **Clear Swaps** puts the kit's own choice back everywhere. Swaps are undone with `Ctrl+Z` like any other change.

Pieces you only want by hand, such as a damaged panel or one with a vent, are **variants**: tick **Variant** for them in the kit editor (`"variant": true` in the file). The kit never places a variant by itself; it only shows up in the swap list of spots holding a piece of the same role and width.

To make a kit, right-click in the Assets panel and pick **New Kit**, then double-click it to open the kit editor. **Add Pieces** picks prefabs from your project and measures each one's width from its meshes; set each piece's role (wall panel, filler, corner, door frame or window frame), and for frames the range of opening widths they fit. **Try a wall** shows which pieces the kit would use for a wall of any length and how far they'd stretch, and anything wrong with the kit is listed underneath. Saving updates every wall and room that uses the kit straight away.

Kits are plain text, and a kit file looks like this:

```json
{
  "schema": "HephaestusKit",
  "name": "Stone",
  "pieces": [
    {"prefab": "assets/Kits/Panel2m.hpprefab", "role": "wall", "width": 2.0, "stretch": 0.08},
    {"prefab": "assets/Kits/Panel1m.hpprefab", "role": "wall", "width": 1.0, "stretch": 0.08},
    {"prefab": "assets/Kits/Filler.hpprefab", "role": "filler", "width": 0.5, "stretch": 0.5},
    {"prefab": "assets/Kits/Corner.hpprefab", "role": "corner", "width": 0.3, "stretch": 0},
    {"prefab": "assets/Kits/DoorFrame.hpprefab", "role": "door", "width": 1.8, "stretch": 0, "openingMin": 0.9, "openingMax": 1.3}
  ]
}
```

Build each piece standing on its origin, with its width along X and centred on it, and the wall's thickness centred on Z; +Z faces into the room. `stretch` is how far a piece may be stretched or squashed to fit, as a fraction of its width (0.08 is 8 percent). A run uses at most one filler. Door and window frames are used for openings whose width is between their `openingMin` and `openingMax`.

**Not there yet:** swapping a piece for one of another width. Pieces don't have their own collision.

## Undo

Almost everything in a scene can be undone with `Ctrl+Z` and redone with `Ctrl+Y`, including gizmo drags, component edits, renames and creating or deleting things.
