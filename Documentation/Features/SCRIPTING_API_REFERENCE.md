# Scripting Layer API Reference

## Core Classes

### ScriptBase

Base class for all game scripts. Inherit from this to create custom scripts.

#### Constructor
```cpp
// Your derived class constructor
MyScript(Entity entity, Scene* scene) {
    m_Entity = entity;
    m_Scene = scene;
}
```

#### Lifecycle Methods

```cpp
virtual void OnAwake()                      // Called once when script first runs
virtual void OnEnable()                     // Called when script is enabled
virtual void OnDisable()                    // Called when script is disabled
virtual void OnUpdate(float deltaTime)      // Called every frame
virtual void OnLateUpdate(float deltaTime)  // Called after OnUpdate
virtual void OnDestroy()                    // Called before destruction
```

#### Physics Callbacks

```cpp
virtual void OnTriggerEnter(Entity other)   // Collision begins
virtual void OnTriggerExit(Entity other)    // Collision ends
virtual void OnTriggerStay(Entity other)    // During collision
```

#### Member Access

```cpp
// Getters
Entity GetEntity() const;                   // Get attached entity
Scene* GetScene() const;                    // Get scene
bool IsAwoken() const;                      // Is script awakened?
bool IsEnabled() const;                     // Is script enabled?

// Setters
void SetEnabled(bool enabled);              // Enable/disable script
```

#### Protected Members

```cpp
protected:
    Entity m_Entity;                        // The entity this script is attached to
    Scene* m_Scene;                         // The scene containing this entity
    bool m_IsAwoken;                        // Awakening state
    bool m_IsEnabled;                       // Enabled state
```

---

### ScriptComponent

ECS component that holds script instances on an entity.

#### Methods

```cpp
void AddScript(ScriptPtr script, 
               const std::string& scriptName);     // Add a script

ScriptPtr GetScript(size_t index);                 // Get by index

ScriptPtr GetScriptByName(
    const std::string& name);                     // Get by name

void RemoveScript(size_t index);                  // Remove by index

void RemoveAllScripts();                          // Remove all scripts

size_t GetScriptCount() const;                    // Get count

bool HasScripts() const;                          // Check if has any
```

#### Members

```cpp
std::vector<ScriptPtr> scripts;              // List of script instances
std::vector<std::string> scriptNames;        // Script names for debugging
```

---

### ScriptRegistry

Singleton registry for managing script types. All scripts must be registered before use.

#### Static Methods

```cpp
template<typename T>
static void Register(
    const std::string& scriptName,
    ScriptFactory factory);                  // Register a script type

static ScriptPtr CreateScript(
    const std::string& scriptName,
    Entity entity,
    Scene* scene);                           // Create script instance

static bool IsRegistered(
    const std::string& scriptName);          // Check if registered

static std::vector<std::string> 
GetRegisteredScripts();                      // Get all registered names

static void Unregister(
    const std::string& scriptName);          // Unregister a script

static void Clear();                         // Clear all registrations
```

#### Example Usage

```cpp
// Register
ScriptRegistry::Register<PlayerController>("PlayerController",
    [](Entity entity, Scene* scene) {
        return std::make_shared<PlayerController>(entity, scene);
    }
);

// Check if registered
if (ScriptRegistry::IsRegistered("PlayerController")) {
    // Create instance
    auto script = ScriptRegistry::CreateScript(
        "PlayerController", entity, scene);
}

// Get all registered
auto scripts = ScriptRegistry::GetRegisteredScripts();
```

---

### ScriptEngine

Main engine managing script lifecycle and updates.

#### Methods

```cpp
void Initialize(Scene* scene);              // Initialize with scene

void Shutdown();                             // Cleanup and destroy

void AwakeScripts();                         // Awaken all scripts

void UpdateScripts(float deltaTime);         // Update all scripts

void LateUpdateScripts(float deltaTime);     // Late update

void DestroyScripts();                       // Destroy all scripts

void AttachScript(Entity entity,
                  ScriptPtr script,
                  const std::string& scriptName);  // Attach script

void DetachScript(Entity entity,
                  const std::string& scriptName);  // Detach script

void DetachAllScripts(Entity entity);        // Detach all from entity

ScriptPtr GetScript(Entity entity,
                    const std::string& scriptName);  // Get script

std::vector<ScriptPtr> GetAllScripts(
    Entity entity);                          // Get all on entity

size_t GetScriptCount(Entity entity);        // Count scripts

void OnEntityDestroyed(Entity entity);       // Handle destruction

Scene* GetScene() const;                     // Get scene
```

#### Usage Example

```cpp
// Setup
ScriptEngine scriptEngine;
scriptEngine.Initialize(&scene);

// Register and attach scripts...

// Main loop
while (running) {
    float dt = clock.Tick();
    
    // First frame
    if (isFirstFrame) {
        scriptEngine.AwakeScripts();
        isFirstFrame = false;
    }
    
    scriptEngine.UpdateScripts(dt);
    RenderFrame();
    scriptEngine.LateUpdateScripts(dt);
}

// Cleanup
scriptEngine.Shutdown();
```

---

## Type Aliases

```cpp
using ScriptPtr = std::shared_ptr<ScriptBase>;     // Smart pointer type
using ScriptFactory = std::function<
    ScriptPtr(Entity entity, Scene* scene)>;       // Factory function type
```

---

## Common Patterns

### Basic Script

```cpp
class MyScript : public ScriptBase {
public:
    MyScript(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        // Initialize
    }

    void OnUpdate(float deltaTime) override {
        // Update logic
    }

    void OnDestroy() override {
        // Cleanup
    }
};
```

### Script with Components

```cpp
void OnUpdate(float deltaTime) override {
    Entity entity = GetEntity();
    
    // Check for component
    if (entity.HasComponent<TransformComponent>()) {
        // Get component
        auto& transform = entity.GetComponent<TransformComponent>();
        
        // Modify
        transform.position.x += 5.0f * deltaTime;
    }
}
```

### Multiple Scripts on Entity

```cpp
// Attach multiple
scriptEngine.AttachScript(entity, controller, "Controller");
scriptEngine.AttachScript(entity, physics, "Physics");
scriptEngine.AttachScript(entity, ui, "UI");

// Get specific
auto controller = scriptEngine.GetScript(entity, "Controller");

// Get all
auto all = scriptEngine.GetAllScripts(entity);
```

### Script Communication

```cpp
// In one script
Entity other = GetScene()->FindEntityByName("OtherEntity");
auto otherScript = GetScene()->GetScriptEngine()
    ->GetScript(other, "OtherScriptName");
```

### Enable/Disable

```cpp
// Disable temporarily
SetEnabled(false);

// Re-enable
SetEnabled(true);

// Check state
if (IsEnabled()) {
    // Is running
}
```

---

## Error Handling

### Common Issues

```cpp
// Check before using
if (!entity) {
    std::cerr << "Invalid entity\n";
    return;
}

if (!entity.HasComponent<TransformComponent>()) {
    std::cerr << "No transform\n";
    return;
}

if (!ScriptRegistry::IsRegistered("MyScript")) {
    std::cerr << "Script not registered\n";
    return;
}

auto script = scriptEngine.GetScript(entity, "MyScript");
if (!script) {
    std::cerr << "Script not found on entity\n";
    return;
}
```

---

## Performance Tips

1. **Cache references** in `OnAwake()`:
```cpp
void OnAwake() override {
    m_Transform = &GetEntity().GetComponent<TransformComponent>();
}
```

2. **Use early returns** to skip unnecessary work:
```cpp
void OnUpdate(float deltaTime) override {
    if (!IsEnabled()) return;
    if (condition) return;
    // Main logic
}
```

3. **Only enable when needed**:
```cpp
if (!NeedsUpdate()) {
    SetEnabled(false);
}
```

---

## Complete Example

```cpp
#pragma once
#include "engine/Scripting/ScriptBase.h"
#include "engine/Scene/Components.h"

class EnemyAI : public ScriptBase {
public:
    EnemyAI(Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    void OnAwake() override {
        m_Health = 100.0f;
        m_Speed = 3.0f;
    }

    void OnUpdate(float deltaTime) override {
        if (!IsEnabled()) return;

        // Get transform
        if (!m_Entity.HasComponent<TransformComponent>()) return;
        auto& transform = m_Entity.GetComponent<TransformComponent>();

        // Move towards player
        Entity player = GetScene()->FindEntityByName("Player");
        if (player) {
            auto& playerTransform = 
                player.GetComponent<TransformComponent>();
            
            Vec3 direction = 
                playerTransform.position - transform.position;
            
            // Normalize and move
            float dist = std::sqrt(direction.x * direction.x + 
                                   direction.y * direction.y + 
                                   direction.z * direction.z);
            if (dist > 0.01f) {
                direction.x /= dist;
                direction.y /= dist;
                direction.z /= dist;
            }
            
            transform.position.x += direction.x * m_Speed * deltaTime;
            transform.position.z += direction.z * m_Speed * deltaTime;
        }
    }

    void TakeDamage(float damage) {
        m_Health -= damage;
        if (m_Health <= 0) {
            OnDestroy();
        }
    }

private:
    float m_Health;
    float m_Speed;
};
```

---

See `SCRIPTING_LAYER_GUIDE.md` for more detailed examples and patterns.
