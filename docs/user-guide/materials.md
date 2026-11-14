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

The Mesh Renderer card also has **Visible**, **Hide In Game** (handy for helper meshes you only want in the editor), **First-Person View Model**, **Draw Over World**, **Cast Shadows** (turn it off for something that should not throw a shadow, such as the player's own legs) and **Backface Culling**. A view model mesh is drawn in play attached to the active camera, so its position under the camera reads as forward (X), up (Y) and right (Z). It is drawn in front of everything else, so a weapon never pokes into a wall, and it casts no shadows. Put it on the arms and weapon meshes under the player's camera. **Draw Over World** gives a mesh the same in-front-of-everything drawing but leaves it where it is in the world and at the camera's field of view, for the player's own body (such as legs seen while sliding) so it never clips into a wall; it still sorts normally against the weapon and arms. The camera's **View Model FOV** draws view model meshes with their own field of view, so zooming the camera does not resize the weapon; leave it at 0 to use the camera's field of view.

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
| **Normal Map**, **Roughness Map**, **AO Map** | Texture maps for fine surface detail, roughness variation and ambient occlusion. With a Roughness Map, **Roughness** multiplies it: below 1 makes the surface shinier, above 1 rougher (up to chalk). |
| **Specular** | Strength of reflections on non metals. Leave it at 0.5 unless you know you need to change it. |
| **Opacity** | How see-through a Translucent material is, or (with the base color texture's alpha) how much of a Masked material is kept. |
| **Opacity Mask Clip** | For Masked materials: where Opacity times the base color texture's alpha falls below this, the surface is cut away. 0.5 suits most foliage. |
| **Refraction** | Reserved for later. It doesn't change anything yet. |

A tip for packed **ORM** textures (one image holding AO, Roughness and Metallic in its red, green and blue channels): add one **Texture Sample** with the image, connect its **RGBA** to a **Break Vector**, then wire **R** to Ambient Occlusion, **G** to Roughness and **B** to Metallic. Each map reads its own channel of the same texture (a **Component Mask** with one channel ticked works too). The single map nodes (Roughness Texture and friends) read the red channel.

### Node categories

The library groups nodes into **Inputs** (constants), **Parameters** (named values), **Textures** (sampling, tiling and offset, triplanar, normal unpacking), **Math**, **Utility** (masking, splitting and joining vectors, UVs), **Logic** (if, step, compare) and **Blend** (lerp, overlay, screen, height blend). Plugins add more, in these groups or their own (see Plugin nodes below).

### Blend modes

Pick one in **Blend Mode** under Details:

- **Opaque**: a solid surface. The default.
- **Masked**: solid, but cut away wherever **Opacity** times the base color texture's alpha is below **Opacity Mask Clip**. Use it for leaves, grass, fences, grates and chain link. Masked surfaces are drawn with the solid ones, sort perfectly and cast shadows with the same holes. Imported glTF materials marked MASK and USD materials with an opacity threshold come in as Masked. Two exceptions still cast solid shadows for now: skinned characters, and lights with ray-traced shadows turned on.
- **Translucent**: blended by **Opacity** times the base color texture's alpha, for glass, water and decals. Drawn after solid surfaces, furthest first, and casts no shadows.
- **Additive**: adds the base color (times the texture's alpha) plus **Emissive** to the image without lighting, for muzzle flashes, sparks and glows. Drawn after solid surfaces and casts no shadows.

Translucent and Additive only work on meshes that aren't skinned characters. In files, the mode is `"blendMode"`: `"masked"`, `"alphaBlend"` or `"additive"` (none means Opaque).

## How a graph becomes a shader

When a graph only uses constants, colours and the material's texture maps (possibly tinted), it draws with the engine's standard surface shader, like every material always has. When it does anything per pixel, such as tiling or scrolling UVs, noise, Fresnel, or maths that mixes textures, saving it compiles it into its own shader. That shader is saved beside the material as `<name>.hemshader`, so the packaged game uses it without needing the editor. The status bar says **Compiled to its own shader** when that happens, the preview shows the compiled result live, and any shader errors appear in **Validation**. If a graph fails to compile, the material is still saved and draws without its per-pixel parts until you fix it.

The Content Browser hides `.hemshader` files and moves, renames and trashes them with their material.

### Plugin nodes

The default library holds the basic nodes. More nodes come from plugins: the **Material Nodes** plugin that ships with Hephaestus adds Noise, Gradient, Fresnel, UV Distortion, Panner, Time, Camera Vector, View Direction, Reflection Vector, World Position, Object Position, Distance To Camera and Screen Position. Turn it off in **Edit > Plugins** and materials that use its nodes keep them and keep their last compiled shader, so they still draw the same in the game; those nodes can't be edited or recompiled until it's back on.

### Texture data

**Texture Sample** and similar nodes have a **Linear data** box. Tick it for normal maps, roughness, metallic, AO and masks so they are read without colour conversion; leave it off for colour textures. The typed nodes (Normal Texture, Roughness Texture and so on) are always right.

### What still isn't there

- **Transform** passes its input through unchanged for now.
- In a Masked material with its own shader, the cut-out *shadow* follows the base colour texture's alpha, not anything the graph does to it per pixel.
- Skinned characters and particles draw with the standard surface, not a material's compiled shader.
- **Scripts can't change materials while the game runs.** There's no material API in Lua yet.

## Older materials

A material from an earlier version of Hephaestus that has no graph opens as one: the editor builds a graph from its values and textures (a Multiply node where a texture is tinted or scaled) that renders exactly the same. The file only changes when you save. If a material uses something a graph can't express yet (a world-aligned material reading a packed map's green, blue or alpha channel), or its saved graph is broken, it opens in **Legacy PBR** mode instead, where you edit the values directly, and **Convert to graph** is there when you want node editing anyway.

## Particle effects

Smoke, dust, sparks and other effects come from a **Particle Emitter** component (add it like any other component, under Rendering). Each particle is a small square that always faces the camera, drawn with the emitter's **Material**: give it an alpha-blended material for smoke and dust or an additive one for sparks and fire. The texture's alpha shapes each particle.

Particles leave the entity along its local **up (+Y)** axis, inside a cone of **Spread** degrees, and then move on their own in the world, so a moving emitter leaves a trail. Its settings:

- **Play On Start**, **Loop**, **Duration**: emit for Duration seconds, then stop or start again.
- **Rate** (particles a second while emitting) and **Burst** (particles all at once at the start of each loop). A grenade blast is a big Burst with Rate 0; a smoke grenade is a steady Rate over a long Duration.
- **Max Particles**: the most alive at once.
- **Lifetime**, **Speed**, **Spin**: each particle picks a value between the Min and Max.
- **Gravity**: 1 falls like a dropped object, negative values rise like smoke. **Drag** slows particles down over time.
- **Size Start** and **Size End** in metres, with **Size Jitter** for variety. **Color Start** and **Color End** tint the material and fade it out through alpha.
- **Destroy When Done**: remove the entity once it has stopped and its last particle has gone, for one-shot effect prefabs.

Particles only move while the game is playing. To play an effect from a script, spawn a prefab with an emitter (`Scene.instantiate`) or control the script's own emitter with the `Particles` functions in the [Lua API reference](lua-api.md#particles).
