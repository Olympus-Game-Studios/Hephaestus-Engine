---
title: Materials
description: PBR materials, the Material Editor and its node graph, and what the graph can and can't do today.
order: 6
---

# Materials

A material decides what a surface looks like: its colour, how shiny or rough it is, whether it's metal, whether it glows. Hephaestus uses **PBR** (physically based rendering) with a metallic and roughness setup, so a material that looks right under one light looks right under all of them. Materials are saved as `.hematerial` files.

## Giving an object a material

Every Mesh Renderer has one or more **material slots**, one per part of the mesh. In the Inspector:

- Drag a material from the Content Browser onto a slot, or click **Assign** and pick one.
- **Open** jumps straight into the Material Editor for that material.
- **Clear** empties the slot, and the mesh falls back to a plain default material.

The Mesh Renderer card also has **Visible**, **Hide In Game** (handy for helper meshes you only want in the editor), **First-Person View Model** and **Backface Culling**. A view model mesh is drawn in play attached to the active camera, so its position under the camera reads as forward (X), up (Y) and right (Z). It is drawn in front of everything else, so a weapon never pokes into a wall, and it casts no shadows. Put it on the arms and weapon meshes under the player's camera. The camera's **View Model FOV** draws view model meshes with their own field of view, so zooming the camera does not resize the weapon; leave it at 0 to use the camera's field of view.

## The Material Editor

Make a material with **New Material** in the Content Browser, then double click it. The Material Editor opens in its own window:

- **Node library** on the left. Search it, or press `Tab` over the graph to search and add a node right where you are.
- **The graph** in the middle. Drag from one pin to another to connect nodes.
- **Validation** under the graph lists any problems. Click an issue to jump to its node.
- **Preview** on the right shows the material on a **Sphere**, **Cube** or **Plane**, lit by a sky you can choose.
- **Details** shows the material's settings, or the selected node's.

**Compile** (and apply to the scene) pushes your changes to everything using the material. `Ctrl+S` saves. `Ctrl+Z` and `Ctrl+Y` undo and redo, `Ctrl+D` duplicates nodes, `Delete` removes them and `F` frames the selection.

### The Material Output node

Every graph ends in exactly one **Material Output** node. Its inputs are the material:

| Input | What it does |
| --- | --- |
| **Base Color** | The surface colour (albedo). |
| **Metallic** | 0 for things like wood, plastic and skin; 1 for bare metal. Rarely anything in between. |
| **Roughness** | 0 is a mirror, 1 is chalk. Most of a material's character lives here. |
| **Emissive** | Light the surface gives off by itself, for screens, lava, glowing runes. |
| **Normal Map**, **Roughness Map**, **AO Map** | Texture maps for fine surface detail, roughness variation and ambient occlusion. |
| **Specular** | Strength of reflections on non metals. Leave it at 0.5 unless you know you need to change it. |
| **Opacity**, **Opacity Mask** | Cut holes in a surface, for leaves, fences and grates. |
| **Refraction** | Reserved for later. It doesn't change anything yet. |

A tip for packed **ORM** textures (one image holding AO, Roughness and Metallic in its red, green and blue channels): the texture nodes pick the right channel for the input you connect them to.

### Node categories

The library groups nodes into **Inputs** (constants, camera and position values), **Parameters** (named values), **Textures** (sampling, tiling and offset, triplanar, normal unpacking), **Math**, **Utility** (masking, splitting and joining vectors, UVs), **Procedural** (noise, gradients, Fresnel, UV distortion, time), **Logic** (if, step, compare) and **Blend** (lerp, overlay, screen, height blend).

## What the graph can't do yet

This is the important bit, so I'll be straight with you. Right now the graph is **worked out once, into a single set of PBR values and textures**, and that's what gets rendered. It isn't turned into a shader that runs for every pixel.

That means:

- **Textures, constants, colours and maths on those work great.** Tinting a texture, mixing two colours, scaling roughness: all fine.
- **Nodes that need to change per pixel or over time don't affect the rendered result yet.** That's TexCoord, Screen Position, Transform, Texture Tiling Offset, Noise, Gradient, Fresnel, UV Distortion, Time, Camera Vector, View Direction, Reflection Vector, World Position, Object Position, Distance To Camera and Static Switch Parameter. The library and the Add Node search label them **(no effect yet)**, and the Validation panel warns you when a graph uses one. You can still place and wire them, and they'll be ready when per pixel materials land, but today they won't animate or vary across a surface.
- **Blend modes aren't in the Material Editor yet.** A material can be see-through or glowing by adding `"blendMode"` to its `.hematerial` file (the editor keeps it when you save): `"alphaBlend"` blends the surface by **Opacity** times its albedo texture's alpha, for decals and glass; `"additive"` adds the albedo (times the texture's alpha) plus **Emissive** to the image without lighting, for muzzle flashes, sparks and glows. Both are drawn after solid surfaces, furthest first, cast no shadows and only work on meshes that aren't skinned characters.
- **Scripts can't change materials while the game runs.** There's no material API in Lua yet.

Per pixel materials are on the way. Until then, if a material looks different in the graph than you expected, check whether it relies on one of the nodes above.

## Older materials

If a material from an earlier version of Hephaestus has no graph, it opens in **Legacy PBR** mode, where you edit the values directly. Click **Convert to graph** when you want node editing. Nothing is lost either way.
