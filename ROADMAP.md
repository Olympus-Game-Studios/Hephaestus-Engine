# Hephaestus Engine Roadmap

This is where Hephaestus is heading. It's a direction, not a promise of dates. For what has already shipped, see [`CHANGELOG.md`](CHANGELOG.md); for what's missing today, see [What's not there yet](docs/user-guide/whats-not-there-yet.md).

## Principles

- **Creativity before complexity.** Simplify how people make games, never what they can make.
- **Artist first, programmer friendly.** Default, Artist and Programmer modes over one shared editor.
- **Extensible.** Editor tools, asset types and scripting languages can come from plugins.
- **Readable files.** Projects stay plain files that diff and merge well.
- **Honest docs.** Unfinished features are labelled as unfinished.

## Where things are (November 2026)

Shipped and in daily use: the Editor 2.0 redesign with workspace modes and the new launcher; Vulkan PBR rendering with soft shadows for every light type, HDRI ambient light, probe volume GI and ray traced shadows; prefabs with overrides; Jolt physics with layers, triggers and a character controller; skinned characters with a Character Editor; Lua with Inspector variables, a code editor and a debugger; scripting language plugins; HephaestusUI with a visual editor; input actions and axes; packaging for Windows and macOS; and the Steam release pipeline.

## Next

### Rendering

The long term goal is fully dynamic lighting that needs no manual setup: global illumination and reflections without placed probe volumes, many shadowed lights and virtualized geometry. The order:

1. **Visual quality tiers** (Low, Medium, High, Ultra) that choose the lighting path. High and Ultra get ray traced GI cascades that follow the camera and ray traced reflections on GPUs that support them; Medium and Low use baked probe volumes. Projects can cap the tier. *In progress on `feature/probe-gi`.*
2. **Temporal accumulation, screen space reflections** for GPUs without ray tracing, and animated meshes in the ray traced scene. *In progress on the same branch.*
3. **Many shadowed lights**, lifting today's limit of 4 point and 8 spot shadows.
4. **Virtualized geometry**, which needs a visibility buffer and GPU culling first.

Alongside that, materials need to compile to real per pixel shaders so nodes like Noise, Fresnel and Time affect the result, and transparency needs proper blending.

### Editor

- UI Editor **Design mode**: Illustrator and Photoshop style drawing tools where any shape can be given a role (a rectangle becomes a Button) and its states drawn in a state editor.
- Alternative UI authoring (for example HTML) provided entirely by plugins.

### Gameplay systems

Gaps the user guide lists that games commonly need: 3D audio and a mixer, save games, more UI widgets (text input, sliders, scrolling lists, keyboard and gamepad navigation), gamepad input in editor Play, animation graphs, particles, terrain, navigation and networking.

### Scripting

First party scripting language plugins, starting with C#.

## How this file is kept

Update it when priorities change, move finished items into `CHANGELOG.md` and keep it short enough to read in a couple of minutes.
