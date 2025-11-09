# Asset Browser - Create C++ Script Feature

## Overview

A new context menu option has been added to the asset browser that allows developers to quickly create new C++ script files directly from the project browser. Simply right-click on any project and select "New C++ Script..." to generate template script files.

## Features

✅ **Right-click context menu** - Access from any project  
✅ **Automatic file generation** - Creates .h and .cpp files  
✅ **Template code** - Pre-populated with ScriptBase structure  
✅ **Organized storage** - Scripts created in `Scripts/` directory  
✅ **Ready to use** - Generated scripts inherit from ScriptBase  
✅ **User feedback** - Success/error messages displayed  

## How to Use

### Step 1: Right-Click on a Project
In the project browser, right-click on any project:

```
┌─────────────────────────────┐
│  MyProject                  │
│  [Right-click here]         │
└─────────────────────────────┘
```

### Step 2: Select "New C++ Script..."
A context menu appears with several options:

```
┌─────────────────────────────┐
│ Open                        │
├─────────────────────────────┤
│ New C++ Script...  ← Click  │
├─────────────────────────────┤
│ Add to Favorites            │
│ Open Folder                 │
│ Edit Details...             │
│ Delete Project...           │
└─────────────────────────────┘
```

### Step 3: Enter Script Name
A dialog opens asking for the script name:

```
┌─────────────────────────────────────┐
│ Create New C++ Script               │
├─────────────────────────────────────┤
│ Project: MyProject                  │
│                                     │
│ Script Name:                        │
│ [PlayerController           ]       │
│                                     │
│ (Will be created in project root    │
│  with .h and .cpp files)            │
│                                     │
│ [Create] [Cancel]                   │
└─────────────────────────────────────┘
```

### Step 4: Click Create
The script files are generated and saved to the project's `Scripts/` directory.

## Generated Files

When you create a script named "PlayerController", two files are generated:

### `Scripts/PlayerController.h`
```cpp
#pragma once
#include "engine/Scripting/ScriptBase.h"

class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    virtual ~PlayerController() = default;

    void OnAwake() override {
        // Initialize the script
    }

    void OnUpdate(float deltaTime) override {
        // Main game logic - called every frame
        // deltaTime is time elapsed since last frame in seconds
    }

    void OnLateUpdate(float deltaTime) override {
        // Called after OnUpdate - useful for camera or post-processing logic
    }

    void OnEnable() override {
        // Called when the script is enabled
    }

    void OnDisable() override {
        // Called when the script is disabled
    }

    void OnDestroy() override {
        // Called when the script is about to be destroyed - cleanup here
    }

private:
    // Add your script members here
};
```

### `Scripts/PlayerController.cpp`
```cpp
#include "PlayerController.h"

// Add any implementation details here if needed
// You can use this file for out-of-line method definitions if your header gets complex
```

## File Location

Scripts are automatically created in:
```
MyProject/
└── Scripts/
    ├── PlayerController.h
    └── PlayerController.cpp
```

## Next Steps After Creation

1. **Edit the generated files** in your favorite text editor
2. **Add your game logic** to the OnUpdate() method
3. **Register the script** in main.cpp using ScriptRegistry
4. **Attach to entities** using scriptEngine.AttachScript()

### Example: Using Your New Script

```cpp
// In main.cpp after creating the script

#include "engine/Game/Scripts/PlayerController.h"

// Register the script
ScriptRegistry::Register<PlayerController>("PlayerController",
    [](Entity entity, Scene* scene) {
        return std::make_shared<PlayerController>(entity, scene);
    }
);

// Later, attach to an entity
Entity player = scene.CreateEntity("Player");
auto script = ScriptRegistry::CreateScript("PlayerController", player, &scene);
scriptEngine.AttachScript(player, script, "PlayerController");
```

## Script Template Structure

All generated scripts include the complete lifecycle:

| Method | When Called | Purpose |
|--------|------------|---------|
| `OnAwake()` | Once at startup | Initialize |
| `OnUpdate(dt)` | Every frame | Main logic |
| `OnLateUpdate(dt)` | After update | Post-processing |
| `OnEnable()` | When enabled | Resume |
| `OnDisable()` | When disabled | Pause |
| `OnDestroy()` | Before cleanup | Cleanup |

## Error Handling

The system provides helpful feedback:

- **"Please enter a script name"** - You left the script name field empty
- **"Failed to create script..."** - Filesystem error (check permissions)
- **"Script created successfully!"** - Success message shown for 3 seconds

## Implementation Details

### New Methods in ProjectManager

```cpp
// Display the new script dialog
void DrawNewScriptDialog();

// Create script files in the project
bool CreateCppScript(const std::filesystem::path& projectPath, 
                     const std::string& scriptName);
```

### New UI State Variables

```cpp
bool m_ShowNewScriptDialog = false;           // Dialog visibility
char m_NewScriptNameBuffer[128] = {};         // User input buffer
ProjectInfo* m_ScriptDialogProject = nullptr; // Selected project
```

### Modified Methods

**ProjectManager::DrawProjectContextMenu()**
- Added "New C++ Script..." menu item
- Opens dialog when clicked

**ProjectManager::Draw()**
- Calls DrawNewScriptDialog() if dialog is active

## Tips & Best Practices

### 1. Use Descriptive Names
✓ `PlayerController` - Clear and descriptive  
✓ `EnemyAI` - Specific purpose  
✗ `Script1` - Too generic  

### 2. Follow Naming Conventions
Use PascalCase for class names:
- ✓ PlayerController
- ✓ CameraFollow
- ✓ SpawnManager

### 3. Organize in Subdirectories
Consider organizing complex projects:
```
Scripts/
├── Gameplay/
│   ├── PlayerController.h
│   └── EnemyAI.h
├── UI/
│   └── MenuManager.h
└── Utils/
    └── AudioManager.h
```

### 4. Keep Scripts Focused
One responsibility per script:
- ✓ PlayerController - handles player input and movement
- ✓ HealthManager - manages health and damage
- ✗ MegaScript - tries to do everything

## Troubleshooting

### Script file doesn't appear in project folder
- Make sure you clicked "Create" in the dialog
- Check that the `Scripts/` subdirectory was created
- Verify file permissions in the project directory

### Can't open the script file
- The file is created but may not be visible until you refresh
- Try opening the project folder directly with your file browser

### Permission denied error
- Check that you have write permissions to the project directory
- Make sure no other application has the project folder open

## Related Features

- **Script Registration**: `Documentation/Features/SCRIPTING_QUICK_START.md`
- **Script System**: `Documentation/Features/SCRIPTING_LAYER_GUIDE.md`
- **API Reference**: `Documentation/Features/SCRIPTING_API_REFERENCE.md`

## Summary

The "New C++ Script" feature streamlines script creation by:

1. ✅ Providing easy right-click access
2. ✅ Generating complete template files
3. ✅ Creating scripts in organized directories
4. ✅ Pre-populating with ScriptBase inheritance
5. ✅ Offering user feedback on success/failure

This makes it quick and easy to start creating game logic for your Hephaestus Engine projects!

---

**Feature Added**: November 10, 2025  
**Status**: Ready to use
