# Hephaestus technical reference

This folder is the technical reference for people working on or deeply with the engine: file formats, runtime contracts and exact API behaviour.

**Making a game?** Start with the [user guide](../docs/user-guide/README.md) instead. It covers the editor and every system from a game maker's point of view. Plugin authors want [`docs/plugins/`](../docs/plugins/plugin-system-1.0.md).

## Projects and assets

- [ProjectFormat.md](ProjectFormat.md): `project.heproject`, engine versions and the project folder layout
- [Guides/AssetImporting.md](Guides/AssetImporting.md): mesh, FBX and character import rules
- [Guides/AssetReimporting.md](Guides/AssetReimporting.md): the reimport pipeline and asset database
- [PrefabSystem.md](PrefabSystem.md): prefab files, instances, overrides and the Prefab Editor

## Rendering and materials

- [Guides/MATERIAL_NODE_REFERENCE.md](Guides/MATERIAL_NODE_REFERENCE.md): every Material Editor node, and which ones affect rendering today
- [Guides/MATERIAL_EDITOR_COMPATIBILITY.md](Guides/MATERIAL_EDITOR_COMPATIBILITY.md): `.hematerial` compatibility guarantees and legacy materials

## Physics, characters and animation

- [Guides/Physics.md](Guides/Physics.md): the Jolt runtime contract (stepping, motion ownership, layers, triggers, contacts)
- [Guides/CharacterAssets.md](Guides/CharacterAssets.md): `.hpcharacter` import, placement and reimport
- [Guides/CharacterEditor.md](Guides/CharacterEditor.md): the Character Editor
- [Guides/CharacterControllers.md](Guides/CharacterControllers.md): controller scripts, the animator API and tick order

## Scripting

- [Scripting/LUA_SCRIPTING_GUIDE.md](Scripting/LUA_SCRIPTING_GUIDE.md): Lua lifecycle, bindings and physics callback semantics
- [Scripting/LUA_UI_API_REFERENCE.md](Scripting/LUA_UI_API_REFERENCE.md): the `UI` table
- [Scripting/LUA_DISPLAY_DESTROY_API.md](Scripting/LUA_DISPLAY_DESTROY_API.md): `display.ui` and `destroy.ui`
- [Guides/RuntimeSceneLifecycle.md](Guides/RuntimeSceneLifecycle.md): `Scene.reload()` and scene identity
- Scripting language plugins: [`docs/plugins/script-languages.md`](../docs/plugins/script-languages.md)

## Game UI (HephaestusUI)

- [Features/HUI_FORMAT_REFERENCE.md](Features/HUI_FORMAT_REFERENCE.md): the `.hui` file format and controllers
- [Features/HEPHAESTUS_UI_RUNTIME_GUIDE.md](Features/HEPHAESTUS_UI_RUNTIME_GUIDE.md): the runtime UI system and Vulkan renderer
- [Features/VISUAL_UI_EDITOR.md](Features/VISUAL_UI_EDITOR.md): the UI Editor
- [Features/HUI_VISUAL_EDITOR_DESIGN_TOOLS.md](Features/HUI_VISUAL_EDITOR_DESIGN_TOOLS.md): canvas, snapping, guides and grouping internals

## Editor

- [Features/EDITOR_SHORTCUTS.md](Features/EDITOR_SHORTCUTS.md): every keyboard shortcut

## Building and shipping games

- [Guides/GameExportGuide.md](Guides/GameExportGuide.md): the editor and game player split, package layout, `game.ini` and troubleshooting
- [Guides/HeadlessBuilds.md](Guides/HeadlessBuilds.md): `HephaestusBuild` for command line and CI packaging
- [Guides/AutomationExecution.md](Guides/AutomationExecution.md): manual simulation control for automation

Building the engine itself is covered in the root [README](../README.md).
