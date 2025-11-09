# Scripting Layer - Visual Guide & Reference

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      Main Game Loop                             │
│                                                                 │
│  while (running) {                                              │
│    scriptEngine.AwakeScripts() ──┐                              │
│    scriptEngine.UpdateScripts()  ├─→ Script Lifecycle          │
│    Render()                      │   Execution                 │
│    scriptEngine.LateUpdateScripts()┘                            │
│  }                                                              │
└──────────────────────┬──────────────────────────────────────────┘
                       │
          ┌────────────▼──────────────┐
          │    ScriptEngine           │
          │ (Lifecycle Manager)       │
          └────────────┬──────────────┘
                       │
        ┌──────────────┼──────────────┬─────────────────┐
        │              │              │                 │
        ▼              ▼              ▼                 ▼
   ┌────────┐  ┌─────────────┐  ┌─────────┐    ┌───────────────┐
   │Registry│  │  Scene      │  │ Entities│    │ ScriptComponent
   │        │  │             │  │         │    │ (Multiple      │
   │Maps    │  │Contains--→  │--│ has --→ │───│ scripts)       │
   │script  │  │   Entities  │  │         │    │                │
   │names   │  └─────────────┘  └─────────┘    └───────────────┘
   │to      │                                        │
   │classes │                                        │
   └────────┘                                    ┌───▼──────────────┐
                                                 │  ScriptBase      │
                                                 │                  │
                                                 │  OnAwake()       │
                                                 │  OnUpdate(dt)    │
                                                 │  OnDestroy()     │
                                                 │  GetEntity()     │
                                                 │  GetScene()      │
                                                 └──────────────────┘
```

## Script Lifecycle Timeline

```
Time ────────────────────────────────────────────────────────────→

┌─ Script Created
│   ├─ Constructor called
│   └─ m_Entity and m_Scene set
│
├─ AttachScript() called
│   ├─ Script added to entity
│   └─ Added to awaken queue
│
├─ AwakeScripts() called
│   ├─ OnAwake() called ONCE
│   └─ m_IsAwoken = true
│
├─ Frame Loop (every frame)
│   ├─ OnUpdate(deltaTime) called
│   ├─ Render (your game)
│   └─ OnLateUpdate(deltaTime) called
│
├─ When SetEnabled(false)
│   ├─ OnDisable() called
│   └─ OnUpdate skipped
│
├─ When SetEnabled(true)
│   ├─ OnEnable() called
│   └─ OnUpdate resumes
│
├─ When Detached/Destroyed
│   ├─ OnDestroy() called
│   └─ Script cleaned up
│
└─ END
```

## Class Hierarchy

```
      ScriptBase (abstract base)
      /         |         \
     /          |          \
PlayerControl  EnemyAI    CameraFollow
    |            |              |
    └────────┬───┴──────────┬───┘
             │              │
          Your Scripts    Your Scripts
```

## Data Flow: Creating & Running Scripts

```
Step 1: Register Script Type
─────────────────────────────
ScriptRegistry::Register<PlayerController>(
    "PlayerController",
    [](Entity e, Scene* s) { 
        return std::make_shared<PlayerController>(e, s); 
    }
);
        │
        ▼
   Registry Map:
   ┌──────────────────────────────────┐
   │ "PlayerController" → factory fn  │
   └──────────────────────────────────┘


Step 2: Create Entity
──────────────────────
Entity player = scene.CreateEntity("Player");
        │
        ▼
   Scene Entities:
   ┌──────────────────────────────────┐
   │ [player] → no components yet     │
   └──────────────────────────────────┘


Step 3: Create Script Instance
───────────────────────────────
auto script = ScriptRegistry::CreateScript(
    "PlayerController", player, &scene);
        │
        ▼
   Registry looks up factory → calls it
   factory(player, &scene) called
        │
        ▼
   PlayerController instance created
   m_Entity = player
   m_Scene = &scene


Step 4: Attach Script
──────────────────────
scriptEngine.AttachScript(player, script, "PlayerController");
        │
        ▼
   ScriptComponent added to player entity
   ScriptComponent.scripts = [script]
   ScriptComponent.scriptNames = ["PlayerController"]
        │
        ▼
   Script added to engine's tracking lists
   Added to m_AllScripts
   Added to m_ScriptsToAwake


Step 5: Main Loop - Awake Phase
────────────────────────────────
scriptEngine.AwakeScripts();
        │
        ▼
   For each script in m_ScriptsToAwake:
   ├─ script->m_IsAwoken = true
   └─ script->OnAwake() called
        │
        ▼
   m_ScriptsToAwake cleared


Step 6: Main Loop - Update Phase (Every Frame)
───────────────────────────────────────────────
scriptEngine.UpdateScripts(deltaTime);
        │
        ▼
   For each script in m_AllScripts:
   ├─ if (script->m_IsEnabled && script->m_IsAwoken)
   └─ script->OnUpdate(deltaTime) called
        │
        ▼
   Player's OnUpdate(0.016f) executes
   └─ Contains player movement logic


Step 7: Rendering
──────────────────
RenderFrame();
   └─ Renders scene with updated positions


Step 8: Late Update Phase
──────────────────────────
scriptEngine.LateUpdateScripts(deltaTime);
   └─ For each script: OnLateUpdate(deltaTime)


Step 9: Cleanup (On Scene Unload)
──────────────────────────────────
scriptEngine.Shutdown();
        │
        ▼
   For each script in m_AllScripts:
   └─ script->OnDestroy() called
        │
        ▼
   m_AllScripts.clear()
   m_ScriptsToAwake.clear()
```

## Method Call Order

```
Initialization Phase:
━━━━━━━━━━━━━━━━━━━━
1. ScriptEngine::Initialize(scene)
2. Register scripts
3. Create entities and attach scripts
4. ScriptEngine::AwakeScripts()
   └─→ Script::OnAwake() called once

Update Phase (every frame):
━━━━━━━━━━━━━━━━━━━━━━━━━
1. ScriptEngine::UpdateScripts(deltaTime)
   └─→ Script::OnUpdate(deltaTime) called

2. RenderFrame()

3. ScriptEngine::LateUpdateScripts(deltaTime)
   └─→ Script::OnLateUpdate(deltaTime) called

4. Handle input/events
   └─→ SetEnabled(false) might call:
       └─→ Script::OnDisable()

5. SetEnabled(true) calls:
   └─→ Script::OnEnable()

Cleanup Phase:
━━━━━━━━━━━━━
1. DetachScript() calls Script::OnDestroy()
2. ScriptEngine::Shutdown() calls OnDestroy() on all
3. Scripts cleaned up
```

## Entity-Script Relationship

```
┌─────────────────────────────────────┐
│  Entity "Player"                    │
│                                     │
│  Components:                        │
│  ├─ TransformComponent              │
│  ├─ MeshComponent                   │
│  ├─ PhysicsComponent                │
│  └─ ScriptComponent                 │
│      ├─ scripts[0] → PlayerController│
│      ├─ scripts[1] → InputHandler   │
│      └─ scripts[2] → HealthManager  │
│                                     │
│  Scripts can access:                │
│  - GetEntity() → Entity "Player"    │
│  - GetScene() → Scene               │
│  - Components via GetEntity()       │
│  - Other entities via GetScene()    │
└─────────────────────────────────────┘
```

## Common Script Patterns

### Pattern 1: Simple Update
```cpp
class RotatingEntity : public ScriptBase {
    void OnUpdate(float dt) override {
        auto& transform = GetEntity()
            .GetComponent<TransformComponent>();
        transform.rotation.y += 1.0f * dt;
    }
};
```

### Pattern 2: State Machine
```cpp
class StatefulScript : public ScriptBase {
    enum State { IDLE, MOVING, ATTACKING };
    State state = IDLE;
    
    void OnUpdate(float dt) override {
        switch (state) {
            case IDLE: UpdateIdle(dt); break;
            case MOVING: UpdateMoving(dt); break;
            case ATTACKING: UpdateAttacking(dt); break;
        }
    }
};
```

### Pattern 3: Scene Interaction
```cpp
class PlayerController : public ScriptBase {
    void OnUpdate(float dt) override {
        Entity enemy = GetScene()
            ->FindEntityByName("Enemy");
        if (enemy) {
            // Interact with enemy
        }
    }
};
```

### Pattern 4: Caching Components
```cpp
class CachedAccess : public ScriptBase {
    TransformComponent* m_Transform = nullptr;
    
    void OnAwake() override {
        m_Transform = &GetEntity()
            .GetComponent<TransformComponent>();
    }
    
    void OnUpdate(float dt) override {
        // Fast access, no HasComponent check needed
        m_Transform->position.x += 1.0f;
    }
};
```

## Error Handling Guide

```
❌ Error: "Script not registered"
   ✓ Solution: ScriptRegistry::Register() before CreateScript()

❌ Error: "Entity is invalid"
   ✓ Solution: Check entity exists before attaching scripts

❌ Error: "Component not found"
   ✓ Solution: Use entity.HasComponent<T>() before GetComponent<T>()

❌ Error: "OnUpdate not called"
   ✓ Solution: Call scriptEngine.AwakeScripts() at start

❌ Error: "Memory leak / segfault"
   ✓ Solution: Scripts use shared_ptr - check Scene cleanup
```

## Performance Tips

```
✓ DO:
  - Cache component references in OnAwake()
  - Use entity.HasComponent() before GetComponent()
  - SetEnabled(false) to pause expensive scripts
  - Keep OnUpdate() logic fast
  - Use simple types (int, float, bool)

✗ DON'T:
  - Query components every frame without caching
  - Create new objects in OnUpdate()
  - Do complex math in OnUpdate()
  - Keep all scripts enabled if not needed
  - Call Scene::FindEntity() every frame
```

## Integration Checklist

```
□ 1. Review documentation
   └─ Start with SCRIPTING_QUICK_START.md

□ 2. Update engine files
   ├─ engine/Scene/Components.h - include ScriptComponent
   ├─ engine/Scene/Scene.h - add ScriptEngine member
   └─ engine/Core/main.cpp - integrate lifecycle calls

□ 3. Register your scripts
   └─ In main() before game loop

□ 4. Create test entity with script
   └─ Verify OnAwake, OnUpdate, OnDestroy print

□ 5. Build and run
   └─ cmake --build build --config Release

□ 6. Extend with game logic
   └─ Create more scripts, attach to entities
```

## File Reference Quick Links

```
Implementation:
├─ engine/Scripting/ScriptBase.h → Base class
├─ engine/Scripting/ScriptEngine.h → Lifecycle
├─ engine/Scripting/ScriptRegistry.h → Factory
├─ engine/Scripting/ScriptComponent.h → ECS component
└─ engine/Scripting/ExampleScript.h → Template

Examples:
└─ engine/Game/ExampleScripts.h → Working examples

Documentation:
├─ SCRIPTING_QUICK_START.md → First 5 minutes
├─ SCRIPTING_LAYER_GUIDE.md → Complete guide
├─ SCRIPTING_API_REFERENCE.md → All methods
├─ SCRIPTING_INTEGRATION_GUIDE.md → How to integrate
└─ SCRIPTING_IMPLEMENTATION_SUMMARY.md → Architecture
```

---

**This visual guide provides a quick reference for the scripting system architecture and usage patterns.**
