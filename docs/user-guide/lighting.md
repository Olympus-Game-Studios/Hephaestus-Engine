---
title: Lighting
description: Lights, shadows, skies and bounce lighting with probe volumes, plus what changes on GPUs with ray tracing.
order: 7
---

# Lighting

Good lighting does more for how a game looks than almost anything else, so it's worth getting to know. In Hephaestus you light a scene with three things: **lights** for direct light and shadows, an **Environment** for the sky and the ambient light it gives off, and **Probe Volumes** for light that bounces off surfaces.

## Lights

Add a light from **Create** (right click in the viewport) or add a **Light** component to any entity. Pick its **Type**:

- **Directional**: the sun. Its light comes from one direction everywhere, so only rotation matters, not position. One is usually all you need.
- **Point**: a bare bulb, shining in every direction up to its **Range**.
- **Spot**: a torch or stage light, a cone set by **Inner Cone** and **Outer Cone**.

Every light has **Color**, **Intensity** and **Enabled**.

In the viewport each light shows an icon for its type (a sun, a bulb or a spotlight) drawn in the light's colour, so you can tell lights apart at a glance. Icons stay the same size on screen however far away the light is; click one to select its light. **View > Overlays > Light Billboards** hides them.

## Shadows

The **Shadows** group on a light controls how it casts them. **Cast Shadows** is on by default.

- **Strength** sets how dark shadows get. 1 is fully dark; lower values let some light through, which can look softer and more natural outdoors.
- **Sun Size** (directional lights only) is how big the sun looks, in degrees. Shadows stay crisp where an object touches the ground and soften the further they stretch from it, like real ones. A bigger sun gives softer shadows.
- **Distance**, **Max Distance** and **Auto-Fit** control how far from the camera the sun's shadows reach. Shorter distances give sharper shadows close up.
- **Bias** and **Normal Bias** (in metres) fix two classic shadow problems. Raise them a touch if you see speckly "shadow acne" on lit surfaces; lower them if shadows look detached from the objects casting them.

There's a limit on how many point and spot lights cast shadows in one frame: **4 point lights and 8 spot lights**. Any beyond that still light the scene, just without shadows, so keep shadow casting for the lights that matter and switch it off on small fill lights.

### Ray traced shadows

Tick **Ray-Traced Shadows** on a light and, on a GPU with hardware ray tracing, its shadow is traced against the actual scene instead of read from a shadow map. They're exact at any distance, and the sun's shadows get soft edges based on Sun Size. Point and spot lights trace hard shadows.

On GPUs without ray tracing the setting is simply ignored and the light uses regular shadows, so it's safe to leave on. Animated characters don't cast ray traced shadows yet.

## The sky: Environment

Add one **Environment** component per scene (it lives under Lighting in Add Component). Set **Mode** to:

- **Solid Color** for a flat background.
- **Sky HDRI** to use an `.hdr` sky image. Every new project comes with one in `.engine/hdris`. **Rotation** spins it to put the bright bit where you want it and **Intensity** brightens or dims it.

With an HDRI, the sky also lights your scene: surfaces facing up get the sky's colour and surfaces facing sideways get the horizon's. The sun in the HDRI image is left out on purpose, because your directional light provides it. **Reflection Intensity** controls how strongly shiny materials reflect the sky.

**Render Distance** lets you skip drawing objects beyond a certain distance from the camera, which helps in big open scenes.

### Post Processing

The Environment's **Post Processing** settings change the look of the whole picture, in the editor and in your game:

- **Exposure** brightens or darkens everything, in stops.
- **Contrast** and **Saturation** start at 1, which changes nothing. Lower saturation towards 0 for black and white.
- **Color Filter** tints everything. White leaves colours alone; a pale orange gives a warm, filmic look.
- **Vignette** darkens the edges of the screen.
- **Film Grain** adds fine moving noise.
- **Scanlines** darken every third row of pixels for a monitor or broadcast feel.

Your game's menus and HUD are drawn on top afterwards, so they stay clean.

## Bounce light: Probe Volumes

In real life, light bounces. A red wall tints the floor next to it, and a sunlit courtyard lights up the inside of the room beside it. That's **global illumination** (GI), and in Hephaestus you get it with **Probe Volumes**.

A Probe Volume is a box filled with a grid of light probes. Each probe works out the light arriving at its spot, including light that's bounced off nearby surfaces, and anything inside the box picks up that bounce light.

1. Add a **Probe Volume** component (Add Component > Lighting) to an entity and size the box to cover the area you care about. The viewport shows the box and every probe in it.
2. Set how many probes run along each axis. More probes catch finer detail but cost more. Up to 64 on each axis.
3. Click **Bake** (or **Build > Bake Lighting**, or the Bake lighting button in the tool row, which bakes every volume in the scene).

Baking records what each probe can see in your static meshes and saves it next to the scene as a `.hpprobes` file. The lighting itself is then updated live every frame from your current lights, sky and glowing materials, so you can move the sun around and the bounce light follows without baking again. You only need to re-bake when the **geometry** changes. The inspector tells you when a bake is missing or out of date.

Outside any Probe Volume, surfaces fall back to the sky's ambient light. Animated characters pick up probe lighting too.

### With hardware ray tracing

Tick **Hardware ray tracing** on a Probe Volume and, on a GPU that supports it, the probes trace the scene every frame instead of using the bake. Moving objects then block, bounce and shadow light too, and you don't need to bake at all. On other GPUs the volume quietly uses its bake, so bake anyway if you want the scene to look right for everyone.

## Coming soon: quality tiers and GI without volumes

Work is well under way on **visual quality tiers** (Low, Medium, High and Ultra) that pick the lighting path for you. On High and Ultra with a ray tracing GPU you'd get fully dynamic GI that follows the camera, with no Probe Volumes to place, plus ray traced reflections. Medium and Low would use baked Probe Volumes as they work today. This isn't in the Steam build yet, so for now place Probe Volumes where you want bounce light.
