# C++ Scripting Layer - Quick Start

## 5-Minute Setup

### 1. Create Your First Script

Create a file `engine/Game/MyScript.h`:

```cpp
#pragma once
#include "engine/Scripting/ScriptBase.h"
#include "engine/Scene/Components.h"

class MyScript : public ScriptBase {
public:
    MyScript(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "MyScript started!\n";
    }

    void OnUpdate(float deltaTime) override {
        // Your game logic here
    }

    void OnDestroy() override {
        std::cout << "MyScript ended\n";
    }
};
```

### 2. Register the Script

In `engine/Core/main.cpp`, add this in the engine initialization:

```cpp
#include "engine/Scripting/ScriptRegistry.h"
#include "engine/Scripting/ScriptEngine.h"
#include "engine/Game/MyScript.h"

// ... in initialization code ...
ScriptRegistry::Register<MyScript>("MyScript",
    [](Entity entity, Scene* scene) {
        return std::make_shared<MyScript>(entity, scene);
    }
);
```

### 3. Attach to Entity

When you create an entity:

```cpp
Entity myEntity = scene.CreateEntity("MyEntity");

auto script = ScriptRegistry::CreateScript("MyScript", myEntity, &scene);
scriptEngine.AttachScript(myEntity, script, "MyScript");
```

### 4. Call Script Methods in Main Loop

In the main game loop in `main.cpp`:

```cpp
// Before main loop
ScriptEngine scriptEngine;
scriptEngine.Initialize(&scene);

// In main loop
scriptEngine.AwakeScripts();  // Call once at start

while (running) {
    float deltaTime = clock.Tick();
    
    scriptEngine.UpdateScripts(deltaTime);
    
    // Render...
    
    scriptEngine.LateUpdateScripts(deltaTime);
}

// Cleanup
scriptEngine.Shutdown();
```

## Common Tasks

### Get Entity Transform

```cpp
void OnUpdate(float deltaTime) override {
    Entity entity = GetEntity();
    if (entity.HasComponent<TransformComponent>()) {
        auto& transform = entity.GetComponent<TransformComponent>();
        // Modify transform
        transform.position.x += 1.0f;
    }
}
```

### Find Another Entity

```cpp
Entity target = GetScene()->FindEntityByName("TargetName");
if (target) {
    // Do something with target
}
```

### Access Scene

```cpp
Scene* scene = GetScene();
// Query, create, destroy entities...
```

### Enable/Disable Script

```cpp
SetEnabled(false);  // Disables the script
SetEnabled(true);   // Re-enables the script
```

## API Quick Reference

### ScriptBase Methods to Override

| Method | When Called | Usage |
|--------|------------|-------|
| `OnAwake()` | Once when first awakened | Initialize |
| `OnEnable()` | When enabled | Resume |
| `OnDisable()` | When disabled | Pause |
| `OnUpdate(dt)` | Every frame | Main logic |
| `OnLateUpdate(dt)` | After update | Cleanup |
| `OnDestroy()` | Before destroy | Cleanup |

### ScriptBase Getters

```cpp
Entity GetEntity()      // Get attached entity
Scene* GetScene()       // Get scene
bool IsAwoken()         // Is awakened?
bool IsEnabled()        // Is enabled?
void SetEnabled(bool)   // Enable/disable
```

## Example: Complete Working Script

```cpp
#pragma once
#include "engine/Scripting/ScriptBase.h"
#include "engine/Scene/Components.h"

class PlayerController : public ScriptBase {
public:
    PlayerController(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        std::cout << "Player initialized\n";
        m_Speed = 5.0f;
        m_Stamina = 100.0f;
    }

    void OnUpdate(float deltaTime) override {
        // Get transform
        if (!m_Entity.HasComponent<TransformComponent>()) return;
        auto& transform = m_Entity.GetComponent<TransformComponent>();

        // Simple movement
        transform.position.x += m_Speed * deltaTime;
        
        // Stamina decay
        m_Stamina -= deltaTime * 10.0f;
        if (m_Stamina < 0) m_Stamina = 0;
    }

    void OnDestroy() override {
        std::cout << "Player destroyed\n";
    }

private:
    float m_Speed = 0.0f;
    float m_Stamina = 100.0f;
};
```

## File Locations

All scripting code is organized as follows:

```
engine/Scripting/           ← Core scripting system
├── ScriptBase.h           ← Inherit from this
├── ScriptBase.cpp
├── ScriptComponent.h      ← For ECS
├── ScriptRegistry.h       ← Register scripts
├── ScriptEngine.h         ← Manage lifecycle
├── ScriptEngine.cpp
└── ExampleScript.h        ← Simple example

engine/Game/                ← Your game scripts
├── MyScript.h
├── PlayerController.h
└── ...

Documentation/Features/     ← Full documentation
├── SCRIPTING_LAYER_GUIDE.md
├── SCRIPTING_INTEGRATION_GUIDE.md
└── SCRIPTING_IMPLEMENTATION_SUMMARY.md
```

## Troubleshooting

**Script not running?**
- ✓ Is it registered? Call `ScriptRegistry::Register(...)`
- ✓ Is it attached? Call `scriptEngine->AttachScript(...)`
- ✓ Is the entity valid? Check `GetEntity()`
- ✓ Is it enabled? Check `IsEnabled()`

**OnAwake not called?**
- ✓ Call `scriptEngine->AwakeScripts()` in main loop

**OnUpdate not called?**
- ✓ Call `scriptEngine->UpdateScripts(dt)` in main loop
- ✓ Check if script is enabled
- ✓ Check if script is awakened

**Can't access entity components?**
- ✓ Use `entity.HasComponent<T>()` first
- ✓ Use `entity.GetComponent<T>()` to get reference

## Next Steps

1. ✅ Create your first script
2. ✅ Register it
3. ✅ Attach to entity
4. ✅ Run and verify lifecycle events print
5. Read `SCRIPTING_LAYER_GUIDE.md` for advanced features
6. Create more scripts for your game!

---

**Need more details?** Check the full guide at `Documentation/Features/SCRIPTING_LAYER_GUIDE.md`
