# Hephaestus Project Format (.heproject)

## Overview

`project.heproject` is a JSON file that marks a folder as a Hephaestus project and holds its metadata. The launcher only lists folders that contain one, and **Open project...** opens one directly.

New projects also get a copy at `.hephaestus/project.heproject`. The editor writes both when it updates the engine version (see below).

## Schema

A project created by the launcher looks like this:

```json
{
  "formatVersion": 1,
  "name": "My Game",
  "description": "New Hephaestus project",
  "engineVersion": "2026.11.3",
  "created": 1762300800,
  "lastModified": 1762300800,
  "defaultScene": "assets/scenes/default.hpscene",
  "editorMode": "default"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `formatVersion` | Integer | Yes | Format version. Currently `1`; other values are refused. |
| `name` | String | Yes | Project name shown in the launcher and editor. |
| `description` | String | No | Free text description. |
| `engineVersion` | String | Yes | Newest Hephaestus version that has opened the project in the editor (see [Engine versions](#engine-versions)). |
| `created` | Integer | Yes | Unix timestamp of project creation. |
| `lastModified` | Integer | Yes | Unix timestamp of the last change. |
| `defaultScene` | String | No | The Startup Scene: the scene the editor and packaged games open first. Set from **Project Settings > Project Info**. Missing when the project was created without a default scene. |
| `editorMode` | String | No | Workspace mode the editor opens the project in: `default`, `artist` or `programmer`. Written by the launcher and editor. |
| `isFavorite` | Boolean | No | Kept for older launchers. The current launcher doesn't show or change it. |
| `screenshot` | String | No | Project relative image path. Read by the launcher but not shown by the current design. |
| `tags` | Array[String] | No | Free form tags. Read by the launcher but not shown. |

Unknown fields are left alone.

## Engine versions

New projects record the engine version that created them. Each time the editor opens a project it compares `engineVersion` with its own version (`Major.Minor.Patch`, compared number by number):

- **Older, equal or missing:** the editor writes its own version to `engineVersion` in `project.heproject` and `.hephaestus/project.heproject`.
- **Newer:** the editor leaves `engineVersion` alone and warns that the project has been opened with a newer Hephaestus, that it may use features this version doesn't support, and that saving it could lose that work. The project still opens.

A project whose `formatVersion` this editor doesn't support (anything but `1`) is refused with an error instead.

## Broken projects

If `project.heproject` can't be read or parsed, the launcher still lists the project but marks it broken ("Invalid or unreadable project metadata") and won't open it until the file is fixed.

Projects from very early versions that only had `.hephaestus/project.meta` are no longer detected or migrated. Create a `project.heproject` by hand (the fields above) to bring one back.

## Project layout

`ProjectCreationService::CreateBlankProject` (`engine/ToolsCommon/ProjectCreationService.cpp`) creates:

```
MyGame/
├── project.heproject
├── assets/
│   ├── scenes/default.hpscene        (when "Create a default scene" is ticked)
│   ├── textures/
│   ├── scripts/
│   ├── materials/
│   ├── prefabs/
│   ├── ui/
│   └── input/Default.hpinput         default actions and axes
├── .engine/hdris/studio_garden_1k.hdr   starter sky (skipped if not found)
└── .hephaestus/
    ├── project.heproject             copy of the project file
    └── physics_layers.json           Default, World, Character and Trigger layers
```

Other files the editor keeps in `.hephaestus/`:

| Path | Contents | Commit to version control? |
| --- | --- | --- |
| `physics_layers.json` | Collision layers and matrix | Yes |
| `plugins.json` | Which plugins this project enables | Yes |
| `project_settings.json` | Project Settings values | Yes |
| `thumbnails/` | Content Browser preview cache | No |
| `recovery/` | Autosave snapshots | No |
| `trash/` | Assets deleted from the Content Browser | No |
| `transactions/` | Rollback data for asset operations | No |

Packaging copies `physics_layers.json` into the game and leaves the rest of `.hephaestus/` behind.

## Detecting a project from code

A folder is a project when it contains `project.heproject`:

```cpp
bool isValidProject = std::filesystem::exists(projectPath / "project.heproject");
```
