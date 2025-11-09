# Scripting Layer Integration Checklist

## Implementation Steps

### 1. Update Components.h
Add the ScriptComponent to the core components list:

```cpp
#include "../Scripting/ScriptComponent.h"
```

### 2. Update Scene.h/Scene.cpp
Add ScriptEngine as a member of the Scene class:

```cpp
class Scene {
private:
    ScriptEngine m_ScriptEngine;
public:
    ScriptEngine* GetScriptEngine() { return &m_ScriptEngine; }
};
```

### 3. Update main.cpp Game Loop
Integrate script lifecycle calls:

```cpp
// After scene initialization
scriptEngine.Initialize(&scene);
scriptEngine.AwakeScripts();

// In game loop
while (running) {
    float deltaTime = clock.Tick();
    
    // Update logic
    scriptEngine.UpdateScripts(deltaTime);
    
    // Render
    RenderFrame();
    
    // Late updates
    scriptEngine.LateUpdateScripts(deltaTime);
}

// Cleanup
scriptEngine.Shutdown();
```

### 4. Register Game Scripts
In your main.cpp, before the game loop starts:

```cpp
#include "engine/Scripting/ScriptRegistry.h"
#include "engine/Scripting/ScriptEngine.h"

// Register your custom scripts
ScriptRegistry::Register<MyGameScript>("MyGameScript",
    [](Entity entity, Scene* scene) {
        return std::make_shared<MyGameScript>(entity, scene);
    }
);

// More scripts...
```

### 5. Create Your First Script
Create a new file like `engine/Game/PlayerController.h`:

```cpp
#pragma once
#include "engine/Scripting/ScriptBase.h"

class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        // Initialize
    }

    void OnUpdate(float deltaTime) override {
        // Game logic
    }

private:
    float m_Speed = 5.0f;
};
```

## File Locations

All scripting files are in: `engine/Scripting/`

- `ScriptBase.h/cpp` - Base class for all scripts
- `ScriptComponent.h` - ECS component for holding scripts
- `ScriptRegistry.h` - Script type registry
- `ScriptEngine.h/cpp` - Main script lifecycle manager
- `ExampleScript.h` - Example template

## Compilation

Run the build task to compile with the new scripting layer:

```
cmake --build build --config Release
```

## Testing

Create a simple test script and verify:

1. Script is registered
2. Script is created without errors
3. OnAwake is called
4. OnUpdate is called each frame
5. OnDestroy is called on cleanup

## Documentation

See `Documentation/Features/SCRIPTING_LAYER_GUIDE.md` for complete usage guide.

## Future Enhancements

- [ ] Script serialization (save/load with scenes)
- [ ] Script hot-reload during development
- [ ] Editor UI for script management
- [ ] Coroutine support
- [ ] Event system for script communication
- [ ] Performance profiling
- [ ] Script debugging
