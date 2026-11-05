# Hephaestus Project Format (.heproject)

## Overview

The `.heproject` file is a JSON-formatted metadata file that identifies and describes a Hephaestus Engine project. Every valid Hephaestus project must contain a `project.heproject` file at its root directory.

## File Location

```
MyProject/
├── project.heproject          ← Project metadata file
├── assets/
├── .hephaestus/
└── [other folders]
```

## File Format

The `.heproject` file uses JSON format for maximum extensibility and human readability.

### Schema

```json
{
  "formatVersion": 1,
  "name": "My Game Project",
  "description": "A 3D action game built with Hephaestus Engine",
  "engineVersion": "1.0.0",
  "created": 1704067200,
  "lastModified": 1704153600,
  "isFavorite": false,
  "screenshot": "assets/project_screenshot.png",
  "tags": ["3D", "Action", "Game"]
}
```

### Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `formatVersion` | Integer | Yes | Format version for backwards compatibility. Currently `1` |
| `name` | String | Yes | Human-readable project name |
| `description` | String | No | Project description (empty string if not provided) |
| `engineVersion` | String | Yes | Newest Hephaestus Engine version that has opened the project in the editor (see [Engine versions](#engine-versions)) |
| `created` | Integer | Yes | Unix timestamp of project creation |
| `lastModified` | Integer | Yes | Unix timestamp of last modification |
| `isFavorite` | Boolean | No | Whether project is marked as favorite in editor |
| `screenshot` | String | No | Relative path to project screenshot (for thumbnail display) |
| `tags` | Array[String] | No | Array of category/template tags |

## Engine versions

New projects record the engine version that created them. Each time the editor opens a project it compares `engineVersion` with its own version (`Major.Minor.Patch`, compared number by number):

- **Older, equal or missing:** the editor writes its own version to `engineVersion` in `project.heproject` and `.hephaestus/project.heproject`.
- **Newer:** the editor leaves `engineVersion` alone and warns that the project has been opened with a newer Hephaestus, that it may use features this version doesn't support, and that saving it could lose that work. The project still opens.

A project whose `formatVersion` this editor doesn't support (anything but `1`) is refused with an error instead.

## Project Migration

### Automatic Migration

When you refresh the project list, the engine automatically detects legacy projects (those from previous versions without `.heproject` files) and migrates them silently in the background. This happens during `RefreshProjects()`.

Legacy projects are identified by:
- Presence of `.hephaestus/` folder with metadata
- Standard Hephaestus folder structure (`.hephaestus` + `assets` directories)

### Manual Migration

For projects that were not in the default projects directory, you can manually trigger migration:

1. **From the UI**: In the Project Manager, click **"Migrate Projects..."** in the sidebar under **ACTIONS**
2. **The Migration Dialog** will:
   - Scan the projects root directory for legacy projects
   - Display all found legacy projects
   - Allow you to migrate them all at once with a single click
   - Show migration status (✓ for successful migrations)
   - Display any errors that occurred

### Migration Process

When a project is migrated:

1. **Metadata Extraction**: The engine reads the legacy `.hephaestus/project.meta` file if it exists
2. **File Creation**: A new `project.heproject` file is created in JSON format
3. **Legacy Support**: The old metadata file is retained for backwards compatibility
4. **Timestamps**: If no timestamps exist, they're set based on the project folder's modification time
5. **Refresh**: The project list is automatically refreshed to show migrated projects

### Migration Outcomes

| Scenario | Result |
|----------|--------|
| Legacy project with metadata | Metadata preserved, `.heproject` created |
| Legacy project without metadata | Default metadata generated with folder name |
| Already migrated project | No action taken (already has `.heproject`) |
| Non-Hephaestus folder | Ignored (not recognized as a project) |

## Backwards Compatibility

The engine maintains support for legacy project metadata stored in `.hephaestus/project.meta`. However:

1. **Project Detection**: Only folders containing `project.heproject` are recognized as valid projects
2. **Metadata Merging**: When loading, the engine attempts to read `.heproject` first, then falls back to the legacy format
3. **Automatic Migration**: When a project is modified through the editor, both formats are updated

### Legacy Format

For reference, the old format stored metadata in `.hephaestus/project.meta`:

```
name=My Project
description=A test project
engineVersion=1.0.0
created=1704067200
lastModified=1704153600
isFavorite=0
screenshot=assets/project_screenshot.png
tags=3D,Game
```

## Project Validation

The ProjectManager validates projects by checking for the presence of `project.heproject`. This ensures:

- **Explicit Project Marking**: Folders are only treated as projects if they contain the marker file
- **Accidental Directory Prevention**: Random folders in the projects directory won't be treated as projects
- **Plugin/Mod Distinction**: Future extensions can be clearly distinguished from projects

## Creation

New projects are automatically created with a `.heproject` file when:
- Creating a new project through the Project Manager UI
- Using the project creation template system
- Importing projects

## Editing Metadata

When modifying project metadata through the editor (name, description, favorite status, etc.), the changes are written to both:
1. `project.heproject` (primary)
2. `.hephaestus/project.meta` (legacy support)

This ensures compatibility with any tools that might depend on the legacy format.

## JSON Encoding Details

The JSON implementation in Hephaestus properly handles:
- String escaping for special characters (quotes, newlines, etc.)
- Unicode escape sequences for non-ASCII characters
- Proper quote handling in string values
- Timestamp representation as integers (Unix epoch)

## Future Extensions

The format is designed to be extensible. Future versions may add:
- Build configuration settings
- Plugin dependencies
- Asset cache metadata
- Editor workspace preferences
- Custom metadata fields

Format versions will be incremented to maintain backwards compatibility during evolution.

## Example Complete Project Structure

```
MyGameProject/
├── project.heproject                   ← Project identifier & metadata
├── assets/
│   ├── models/
│   ├── textures/
│   ├── materials/
│   ├── scenes/
│   ├── scripts/
│   ├── audio/
│   └── prefabs/
├── .hephaestus/                        ← Engine data directory
│   ├── project.meta                    ← Legacy metadata (kept for compatibility)
│   └── [other engine cache files]
└── scripts/                            ← Lua/C++ scripts
```

## Command Line Usage

When launching a project from command line or programmatically, the presence of `project.heproject` confirms it's a valid project:

```cpp
bool isValidProject = std::filesystem::exists(projectPath / "project.heproject");
```

This makes project detection simple and reliable across different tools and scripts.
