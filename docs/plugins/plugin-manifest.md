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

`version`, `id`, `name`, `pluginVersion`, `engineVersion`, `type`, and `entry` are required. `apiVersion` defaults to 1. `description`, `author`, `content`, and `icon` are optional. Unknown properties are ignored so later manifest versions can add optional data without breaking a 1.0 host.

IDs are lowercase reverse-domain identifiers. `entry`, `content`, and `icon` must be relative to the manifest directory and may not contain `..`. Manifest version 1 accepts `editor`, `runtime`, and `hybrid` as type values; the 1.0 editor host loads native editor functionality. Runtime bootstrap integration is reserved for a later capability group.

The engine constraint supports an exact `major.minor.patch`, `=version`, `>=version`, `^version`, or `*`. A native library is loaded only when both the manifest API version and the exported API version equal `HP_PLUGIN_API_VERSION`.

Plugins are discovered recursively under `<Engine>/Plugins` and `<Project>/Plugins`. Enabled state is stored per project in `.hephaestus/plugins.json`; the manifest is never edited. Engine plugins use the same project override when a project is open. Enable changes take effect after restart.
