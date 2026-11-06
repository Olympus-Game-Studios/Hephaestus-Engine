---
title: Content Browser and assets
description: Importing models, textures and sound, creating new assets and moving things around without breaking references.
order: 4
---

# Content Browser and assets

The Content Browser is a window onto your project's `assets` folder. Anything you see there is a real file on disk, so you can also manage things in Explorer if you prefer. The Content Browser is just smarter about it, because it knows which scenes and materials use which files.

## Finding your way around

- **Breadcrumbs** at the top show where you are. Click any part of the path to jump back up.
- **Filter chips** narrow the view to one type: meshes, textures, materials, scripts, prefabs, UI layouts, audio, characters and so on.
- **Search assets...** searches the whole project, not just the folder you're in.
- Switch between **Grid** and **List** views. Artist mode uses big cards, Programmer mode uses a list.

Cards show a preview of the asset. Textures show the image, materials are rendered on a sphere and meshes and prefabs are rendered from a three quarter view with their materials on. Previews are made in the background so the browser never stalls, and they refresh when the asset (or anything it uses) changes. Animated characters, DDS and EXR files keep a plain type icon for now.

## Bringing files in

Click **Import Assets...** (or right click a folder and pick **Import**). The files are copied into the folder you're looking at, so the originals stay where they were.

| Kind | Formats |
| --- | --- |
| Meshes | `.obj`, `.gltf`, `.glb`, `.fbx` |
| Textures | `.png`, `.jpg`, `.tga`, `.bmp` |
| Skies (HDRI) | `.hdr` |
| Sound | `.wav`, `.mp3` |
| Characters | `.glb` and `.fbx` with a skeleton, through **Import Character...** |

A few tips:

- **FBX from Blender, Maya, Mixamo or 3ds Max** is converted to Hephaestus's axes automatically.
- If you import a **rigged FBX** through Import Assets, you'll be asked whether to import it as a character. Say yes if you want it to animate; otherwise it comes in as a static mesh. [Characters and animation](characters-and-animation.md) has the details.
- Meshes bring their **material slots** across, but not the materials themselves. You assign Hephaestus materials in the editor, which takes a minute and gives you proper PBR control.

When you change a source file outside the editor (say you tweak a texture in your paint app), Hephaestus notices. Imported meshes, textures and sounds reimport on their own about half a second after you save, and the Content Browser refreshes when files are added, removed or edited in the folder you're looking at. If something was changed while the editor was closed, right click it and choose **Reimport**, or right click empty space and use **Reimport All Changed** to catch everything at once.

## Making new assets

Right click empty space in a folder:

- **New Folder**
- **New Material** opens in the [Material Editor](materials.md)
- **New Lua Script** gives you a script with `onAwake`, `onUpdate` and `onDestroy` ready to fill in ([Scripting with Lua](scripting-lua.md))
- **New Prefab** ([prefabs](scenes-and-entities.md#prefabs))
- **New UI File** opens in the [UI Editor](game-ui.md)
- **New Character** ([Characters and animation](characters-and-animation.md))

Double click an asset to open it in the right editor. Scripts open in the built in code editor in Programmer mode, and in your system's default app for `.lua` files in the other modes.

## Rename, move and delete without breaking things

This is the bit that saves you real headaches. When you **Rename** or move an asset from the Content Browser, Hephaestus updates every scene, prefab, material, UI layout and setting that points at it. Before anything changes you'll see what's affected.

- **Find References...** shows what an asset depends on and everything that uses it. Check it before deleting anything you're unsure about.
- **Delete** moves the asset into the project's `.hephaestus/trash` folder rather than wiping it. Changed your mind? Right click empty space in the Content Browser, choose **Restore Deleted Asset...** and pick it from the list. It goes back where it was, as long as nothing new has taken its place.
- **Copy Asset Path** gives you the path to paste into a script, like `assets/prefabs/Enemy.hpprefab`.
- **Show in Explorer** opens the folder on disk.

If you rename or move files in Explorer instead, Hephaestus can't follow along, so anything that referenced them will show up as missing. Do your reorganising in the Content Browser.

## Putting assets in the scene

Drag a mesh, prefab or sound from the Content Browser into the viewport. You'll get a preview that sits on whatever surface is under your mouse:

- **Click** to place it.
- **Shift+click** to place another copy and keep going.
- **Esc** to cancel.

To give an object a material, select it and drag the material onto one of the material slots in its Mesh Renderer card in the Inspector. With something selected you can also right click an asset and use **Replace Selected Mesh** or **Replace Selected With Prefab**.
