# Hephaestus Engine - C++ Scripting Layer

## Overview

The scripting layer provides a robust framework for game developers to write C++ scripts that control entity behavior in the Hephaestus Engine. Scripts have a full lifecycle and integrate seamlessly with the ECS system.

## 📚 Documentation

For comprehensive guides on creating and using scripts, see:
- **[SCRIPT_DEVELOPMENT_GUIDE.md](../../SCRIPT_DEVELOPMENT_GUIDE.md)** - Complete guide with examples
- Includes PlayerController example implementation
- Best practices and debugging tips

## Quick Start

Create a script:
```cpp
class MyScript : public ScriptBase {
    void OnUpdate(float deltaTime) override {
        // Your game logic
    }
};
```

Register it:
```cpp
ScriptRegistry::Register<MyScript>("MyScript",
    [](Entity e, Scene* s) { return std::make_shared<MyScript>(e, s); }
);
```

Attach it:
```cpp
auto script = ScriptRegistry::CreateScript("MyScript", entity, scene);
scriptEngine.AttachScript(entity, script, "MyScript");
```

## Core Components

### Files

| File | Purpose |
|------|---------|
| `ScriptBase.h/cpp` | Base class for all scripts |
| `ScriptComponent.h` | ECS component for storing scripts |
| `ScriptRegistry.h` | Script registration and factory |
| `ScriptEngine.h/cpp` | Lifecycle and update management |
| `ExampleScript.h` | Simple template example |

### Classes

- **`ScriptBase`** - Inherit from this for custom scripts
- **`ScriptComponent`** - ECS component (internal)
- **`ScriptRegistry`** - Static registry for script types
- **`ScriptEngine`** - Manages script lifecycle

## Key Features

✅ Multiple scripts per entity  
✅ Full lifecycle hooks (OnAwake, OnUpdate, OnDestroy, etc.)  
✅ Component access within scripts  
✅ Scene queries from scripts  
✅ Enable/disable control  
✅ Type-safe registration  
✅ Easy integration with main loop  

## Documentation

| Document | Content |
|----------|---------|
| `SCRIPTING_QUICK_START.md` | 5-minute setup guide |
| `SCRIPTING_LAYER_GUIDE.md` | Complete user guide |
| `SCRIPTING_API_REFERENCE.md` | Full API documentation |
| `SCRIPTING_INTEGRATION_GUIDE.md` | Integration checklist |
| `SCRIPTING_IMPLEMENTATION_SUMMARY.md` | Architecture overview |

## Lifecycle

Scripts go through this lifecycle:

1. **Creation** - Script instance created via registry
2. **Attachment** - Script attached to entity
3. **Awake** - `OnAwake()` called once at startup
4. **Updates** - `OnUpdate()` called each frame
5. **Disable/Enable** - Can be paused/resumed
6. **Destruction** - `OnDestroy()` called before cleanup

## Example Scripts

See `engine/Game/ExampleScripts.h` for working examples:

- `RotatingCube` - Transform rotation
- `SimpleMovement` - Entity movement
- `Lifetime` - Auto-destruction after time
- `BillboardEffect` - Camera-facing objects
- `SimpleFSM` - State machine pattern

## Integration

To integrate into the engine:

1. Update `engine/Scene/Components.h` to include `ScriptComponent`
2. Update `engine/Scene/Scene.h` to include `ScriptEngine`
3. Update `engine/Core/main.cpp` to call script engine methods
4. Create scripts in `engine/Game/`

See `SCRIPTING_INTEGRATION_GUIDE.md` for step-by-step instructions.

## Usage Example

```cpp
// Register
ScriptRegistry::Register<PlayerController>("PlayerController",
    [](Entity e, Scene* s) { 
        return std::make_shared<PlayerController>(e, s); 
    }
);

// Create entity
Entity player = scene.CreateEntity("Player");

// Create and attach script
auto script = ScriptRegistry::CreateScript("PlayerController", player, &scene);
scriptEngine.AttachScript(player, script, "PlayerController");

// Main loop
while (running) {
    scriptEngine.UpdateScripts(deltaTime);
    Render();
    scriptEngine.LateUpdateScripts(deltaTime);
}
```

## API Quick Reference

### ScriptBase Methods

```cpp
void OnAwake()                      // Initialize
void OnUpdate(float dt)             // Main update
void OnLateUpdate(float dt)         // Post-update
void OnDestroy()                    // Cleanup
void OnEnable()                     // Resume
void OnDisable()                    // Pause
```

### ScriptEngine Methods

```cpp
void Initialize(Scene* scene)       // Setup
void AwakeScripts()                 // Start scripts
void UpdateScripts(float dt)        // Update all
void LateUpdateScripts(float dt)    // Post-update
void AttachScript(...)              // Add script to entity
void DetachScript(...)              // Remove script
```

### ScriptRegistry Methods

```cpp
ScriptRegistry::Register<T>(name, factory)      // Register type
ScriptRegistry::CreateScript(name, entity, scene) // Create instance
ScriptRegistry::IsRegistered(name)              // Check if exists
```

## Common Patterns

### Access Transform

```cpp
if (GetEntity().HasComponent<TransformComponent>()) {
    auto& transform = GetEntity().GetComponent<TransformComponent>();
    transform.position.x += 5.0f;
}
```

### Query Scene

```cpp
Entity other = GetScene()->FindEntityByName("Player");
// Use other entity...
```

### Multiple Scripts

```cpp
scriptEngine.AttachScript(entity, script1, "Logic");
scriptEngine.AttachScript(entity, script2, "Physics");
// Both run on same entity
```

## Performance

- Minimal overhead per script
- Only enabled scripts update
- Linear iteration in main loop
- No reflection or dynamic dispatch
- Shared pointer memory management

## Debugging

Enable debug output to see lifecycle events:

```cpp
// In script OnAwake
std::cout << "MyScript initialized\n";

// Check registry
auto registered = ScriptRegistry::GetRegisteredScripts();
for (const auto& name : registered) {
    std::cout << "Registered: " << name << "\n";
}
```

## Building

Scripts are included in the build via CMakeLists.txt:

```cmake
engine/Scripting/ScriptBase.cpp
engine/Scripting/ScriptEngine.cpp
```

No additional configuration needed.

## Next Steps

1. ✅ Read `SCRIPTING_QUICK_START.md` for immediate setup
2. ✅ Create your first script
3. ✅ Read `SCRIPTING_LAYER_GUIDE.md` for advanced features
4. ✅ See `SCRIPTING_API_REFERENCE.md` for complete API
5. ✅ Look at `engine/Game/ExampleScripts.h` for patterns

## Support

- Full documentation in `Documentation/Features/`
- Examples in `engine/Game/ExampleScripts.h`
- Template in `engine/Scripting/ExampleScript.h`
- API reference with examples in `SCRIPTING_API_REFERENCE.md`

---

**Ready to create your first script?** Start with `SCRIPTING_QUICK_START.md`!
