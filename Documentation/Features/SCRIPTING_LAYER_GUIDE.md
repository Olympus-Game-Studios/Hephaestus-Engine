# Hephaestus Engine - C++ Scripting Layer

## Overview

The C++ Scripting Layer allows developers to write game logic scripts that attach to entities and participate in the game loop. Scripts have a full lifecycle with hooks for initialization, updates, and cleanup.

## Architecture

### Core Components

#### 1. **ScriptBase** (`ScriptBase.h`)
The base class for all scripts. Developers inherit from this class to create custom game logic.

**Lifecycle Methods:**
- `OnAwake()` - Called when the script is first initialized
- `OnEnable()` - Called when the script is enabled
- `OnDisable()` - Called when the script is disabled
- `OnUpdate(float deltaTime)` - Called every frame for game logic
- `OnLateUpdate(float deltaTime)` - Called after all updates
- `OnDestroy()` - Called when the script is being destroyed
- `OnTriggerEnter(Entity)`, `OnTriggerExit(Entity)`, `OnTriggerStay(Entity)` - Physics callbacks

**Getters:**
- `GetEntity()` - Get the entity this script is attached to
- `GetScene()` - Get the scene containing this entity
- `IsAwoken()` - Check if the script has been awakened
- `IsEnabled()` - Check if the script is currently enabled

#### 2. **ScriptComponent** (`ScriptComponent.h`)
An ECS component that stores multiple script instances attached to an entity.

**Methods:**
- `AddScript(script, scriptName)` - Add a script
- `GetScript(index)` - Get script by index
- `GetScriptByName(name)` - Get script by name
- `RemoveScript(index)` - Remove a script
- `RemoveAllScripts()` - Remove all scripts
- `GetScriptCount()` - Get number of scripts
- `HasScripts()` - Check if any scripts are attached

#### 3. **ScriptRegistry** (`ScriptRegistry.h`)
A singleton registry that manages script types and factories. Scripts must be registered before use.

**Methods:**
- `Register<T>(name, factory)` - Register a script type
- `CreateScript(name, entity, scene)` - Create a script instance
- `IsRegistered(name)` - Check if a script is registered
- `GetRegisteredScripts()` - Get list of all registered script names
- `Unregister(name)` - Unregister a script
- `Clear()` - Clear all scripts

#### 4. **ScriptEngine** (`ScriptEngine.h`)
The main engine that manages script lifecycle and updates.

**Methods:**
- `Initialize(scene)` - Initialize the engine with a scene
- `Shutdown()` - Cleanup all scripts
- `AwakeScripts()` - Awaken all scripts
- `UpdateScripts(deltaTime)` - Update all scripts
- `LateUpdateScripts(deltaTime)` - Late update all scripts
- `AttachScript(entity, script, name)` - Attach script to entity
- `DetachScript(entity, name)` - Detach script from entity
- `GetScript(entity, name)` - Get script from entity
- `GetAllScripts(entity)` - Get all scripts on entity

## Usage Guide

### Step 1: Create a Script

```cpp
#include "engine/Scripting/ScriptBase.h"

class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "Player controller initialized\n";
    }

    void OnUpdate(float deltaTime) override {
        // Handle player input and movement
        Entity entity = GetEntity();
        // Modify entity components...
    }

    void OnDestroy() override {
        std::cout << "Player controller destroyed\n";
    }

private:
    float m_MoveSpeed = 5.0f;
};
```

### Step 2: Register the Script

In your main engine initialization code (typically in `main.cpp`):

```cpp
#include "engine/Scripting/ScriptRegistry.h"

// Register the script
ScriptRegistry::Register<PlayerController>("PlayerController",
    [](Entity entity, Scene* scene) {
        return std::make_shared<PlayerController>(entity, scene);
    }
);
```

### Step 3: Attach Script to Entity

```cpp
#include "engine/Scripting/ScriptEngine.h"

// Create or get your entity
Entity player = scene->CreateEntity("Player");

// Create script instance
auto playerScript = ScriptRegistry::CreateScript("PlayerController", player, scene);

// Attach to entity
scriptEngine->AttachScript(player, playerScript, "PlayerController");
```

### Step 4: Integrate into Main Loop

In your main game loop (in `main.cpp`):

```cpp
// Scene and script engine setup
Scene scene;
ScriptEngine scriptEngine;
scriptEngine.Initialize(&scene);

// Main game loop
while (running) {
    float deltaTime = clock.Tick();

    // Awaken scripts on first frame
    if (isFirstFrame) {
        scriptEngine.AwakeScripts();
        isFirstFrame = false;
    }

    // Update scripts
    scriptEngine.UpdateScripts(deltaTime);
    
    // Render
    RenderFrame();

    // Late update scripts
    scriptEngine.LateUpdateScripts(deltaTime);
}

// Cleanup
scriptEngine.Shutdown();
```

## Integration with Components

Scripts can interact with entity components:

```cpp
class CameraFollow : public ScriptBase {
    void OnUpdate(float deltaTime) override {
        Entity entity = GetEntity();
        
        // Get and modify components
        if (entity.HasComponent<TransformComponent>()) {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position.x += 1.0f;
        }
    }
};
```

## Best Practices

1. **Initialization in OnAwake**: Use `OnAwake()` for one-time setup, not constructors
2. **Cleanup in OnDestroy**: Always clean up resources you allocate
3. **Cache References**: Cache component references in `OnAwake()` for performance
4. **Use Scene Getters**: Use `GetScene()` to query the scene and find other entities
5. **Disable When Not Needed**: Use `SetEnabled(false)` to pause scripts
6. **Keep Logic Simple**: Complex systems should be broken into multiple scripts

## Advanced Usage

### Multiple Scripts Per Entity

```cpp
Entity entity = scene->CreateEntity("GameManager");

auto controller = ScriptRegistry::CreateScript("GameController", entity, scene);
scriptEngine->AttachScript(entity, controller, "GameController");

auto uiHandler = ScriptRegistry::CreateScript("UIHandler", entity, scene);
scriptEngine->AttachScript(entity, uiHandler, "UIHandler");

// Both scripts will run on the same entity
```

### Dynamic Script Loading

```cpp
// Load scripts based on configuration
std::string scriptName = config["player_script"]; // e.g., "PlayerController"

if (ScriptRegistry::IsRegistered(scriptName)) {
    auto script = ScriptRegistry::CreateScript(scriptName, playerEntity, scene);
    scriptEngine->AttachScript(playerEntity, script, scriptName);
}
```

### Script Communication

Scripts can communicate through the scene or by getting references to other entities:

```cpp
class PlayerAttack : public ScriptBase {
    void OnUpdate(float deltaTime) override {
        // Find enemy entity
        Scene* scene = GetScene();
        Entity enemy = scene->FindEntityByName("Enemy");
        
        if (enemy) {
            // Get enemy script
            auto enemyScript = scene->GetScriptEngine()->GetScript(enemy, "EnemyAI");
            // Communicate through public methods or components
        }
    }
};
```

## File Structure

```
engine/
├── Scripting/
│   ├── ScriptBase.h           # Base class for all scripts
│   ├── ScriptBase.cpp         # Implementation
│   ├── ScriptComponent.h      # ECS component
│   ├── ScriptRegistry.h       # Script registry and factory
│   ├── ScriptEngine.h         # Main script engine
│   ├── ScriptEngine.cpp       # Implementation
│   └── ExampleScript.h        # Example template
```

## Next Steps

1. **Add Script Editor**: Create UI to manage scripts
2. **Serialization**: Save/load scripts with scenes
3. **Script Hot-Reload**: Reload scripts during development
4. **Coroutines**: Add coroutine support for time-based actions
5. **Events System**: Add event-based script communication
