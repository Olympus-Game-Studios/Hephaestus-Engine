# Plugin manifest (`.hpplugin`)

Plugin System 1.0 uses one JSON file with the `.hpplugin` extension. There is no secondary `manifest.json`.

```json
{
  "version": 1,
  "apiVersion": 1,
  "id": "com.example.terrain",
  "name": "Example Terrain",
  "description": "Terrain authoring tools.",
  "author": "Example Studio",
  "pluginVersion": "1.0.0",
  "engineVersion": ">=2026.9.0",
  "type": "editor",
  "entry": "bin/ExampleTerrain.dll",
  "content": "Content",
  "icon": "Resources/terrain.svg"
}
```

`version`, `id`, `name`, `pluginVersion`, `engineVersion`, and `type` are required, plus at least one of `entry` and `runtimeEntry`. `apiVersion` defaults to 1. `description`, `author`, `content`, and `icon` are optional. Unknown properties are ignored so later manifest versions can add optional data without breaking a 1.0 host.

IDs are lowercase reverse-domain identifiers. `entry`, `runtimeEntry`, `content`, and `icon` must be relative to the manifest directory and may not contain `..`. Manifest version 1 accepts `editor`, `runtime`, and `hybrid` as type values.

`entry` is the editor module (it may use Qt). `runtimeEntry` is an optional Qt-free module that both the editor and packaged games load; today it provides scripting languages (see [script-languages.md](script-languages.md)). When a project is packaged, the folder holding `runtimeEntry` is copied into the game whole, so a language runtime can ship its own libraries beside its module.

The engine constraint supports an exact `major.minor.patch`, `=version`, `>=version`, `^version`, or `*`. A native library is loaded only when both the manifest API version and the exported API version equal `HP_PLUGIN_API_VERSION`.

Plugins are discovered recursively under `<Engine>/Plugins` and `<Project>/Plugins`. Enabled state is stored per project in `.hephaestus/plugins.json`; the manifest is never edited. Engine plugins use the same project override when a project is open. Enable changes take effect after restart.
