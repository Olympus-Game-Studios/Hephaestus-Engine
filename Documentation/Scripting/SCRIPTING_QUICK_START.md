# Quick Start: Scripting in Hephaestus Engine

Welcome to the Hephaestus Engine scripting system! This guide gets you writing game code in minutes.

## The Choice: C++ vs Lua

| Aspect | C++ Scripts | Lua Scripts |
|--------|-----------|-----------|
| **Speed** | Compiled (fast) | Interpreted (good) |
| **Iteration** | Requires rebuild | Instant feedback |
| **Use Case** | Performance-critical | Game logic, prototyping |
| **Learning** | Steeper curve | Easy syntax |
| **Integration** | Built-in | Requires Lua installation |

**Recommendation for Beginners**: Start with C++, move to Lua when you want faster iteration.

---

## Method 1: C++ Scripting (Works Out of the Box)

### Step 1: Create Your Script

Create a new file in `engine/Game/ExampleScripts.h`:

```cpp
class MyAwesomeScript : public ScriptBase {
public:
    MyAwesomeScript(Entity entity, Scene* scene) 
        : ScriptBase(entity, scene) {}

    void OnAwake() override {
        std::cout << "Script started!\n";
    }

    void OnUpdate(float deltaTime) override {
        // Called every frame during play mode
        std::cout << "Running...\n";
    }

    void OnLateUpdate(float deltaTime) override {
        // Called after all OnUpdate calls
    }

    void OnDestroy() override {
        std::cout << "Script destroyed!\n";
    }
};
```

### Step 2: Register Your Script

In `engine/Core/main.cpp` around line 3230, add:

```cpp
ScriptRegistry::Register<MyAwesomeScript>("MyAwesomeScript",
    [](Entity entity, Scene* scene) {
        return std::make_shared<MyAwesomeScript>(entity, scene);
    }
);
```

### Step 3: Rebuild

```powershell
cd build
cmake --build . --config Release
```

### Step 4: Attach to Entity

1. Select entity in scene
2. Scroll to "Scripts" section in Details Panel
3. Click "Attach Script"
4. Select "MyAwesomeScript" from list
5. Press Play button to test

---

## Method 2: Lua Scripting (Fast Iteration)

### Prerequisites

Install Lua first (see LUA_INSTALLATION_GUIDE.md):

**Windows**:
```powershell
.\vcpkg install lua:x64-windows
cd build && cmake .. && cmake --build . --config Release
```

**Linux**:
```bash
sudo apt-get install liblua5.3-dev
cd build && cmake .. && cmake --build . --config Release
```

### Step 1: Create Your Lua Script

Create `engine/Game/scripts/my_script.lua`:

```lua
function onAwake()
    print("[Lua] Script awakened!")
    
    -- OOP-style: Use 'this' to access entity components
    local transform = this:GetTransform()
    local pos = transform:GetPosition()
    print("[Lua] Starting position: " .. pos.x .. ", " .. pos.y .. ", " .. pos.z)
end

function onUpdate(deltaTime)
    print("[Lua] Running... delta time: " .. deltaTime)
    
    -- Check input
    if Input.isKeyPressed("W") then
        print("[Lua] W key pressed!")
    end
    
    -- OOP-style: Access transform through 'this'
    local transform = this:GetTransform()
    local pos = transform:GetPosition()
    print("[Lua] Position: " .. pos.x .. ", " .. pos.y .. ", " .. pos.z)
    
    -- Functional style also works:
    -- local pos = Transform.getPosition()
end

function onDestroy()
    print("[Lua] Script destroyed")
end
```

### Step 2: Attach to Entity

1. Select entity in scene
2. Scroll to "Lua Scripts" section in Details Panel
3. Click "Attach Script"
4. Enter path: `engine/Game/scripts/my_script.lua`
5. Press Play button
6. **No rebuild needed!** Changes take effect immediately (restart play mode)

### Step 3: Iterate Rapidly

Edit your `my_script.lua`, stop play mode, restart play mode. That's it!

---

## Common Tasks

### Move an Entity in Script

**C++**:
```cpp
void OnUpdate(float deltaTime) override {
    auto& transform = m_Entity.GetComponent<TransformComponent>();
    transform.position.x += 5.0f * deltaTime; // Move 5 units/sec
}
```

**Lua (OOP-style)**:
```lua
function onUpdate(deltaTime)
    local transform = this:GetTransform()
    transform:Translate(5.0 * deltaTime, 0, 0)
end
```

**Lua (Functional-style)**:
```lua
function onUpdate(deltaTime)
    Transform.translate(5.0 * deltaTime, 0, 0)
end
```

### Detect Input

**C++**:
```cpp
void OnUpdate(float deltaTime) override {
    // See engine/Core/Input.h for input system
}
```

**Lua**:
```lua
function onUpdate(deltaTime)
    if Input.isKeyPressed("W") then
        -- W pressed
    end
    if Input.isMouseButtonPressed(0) then
        -- Left mouse clicked
    end
end
```

### Rotate an Entity

**C++**:
```cpp
void OnUpdate(float deltaTime) override {
    auto& transform = m_Entity.GetComponent<TransformComponent>();
    transform.rotation.z += 1.0f * deltaTime; // Rotate 1 rad/sec
}
```

**Lua (OOP-style)**:
```lua
function onUpdate(deltaTime)
    local transform = this:GetTransform()
    transform:Rotate(0, 0, 1.0 * deltaTime)
end
```

**Lua (Functional-style)**:
```lua
function onUpdate(deltaTime)
    Transform.rotate(0, 0, 1.0 * deltaTime)
end
```

---

## Script Lifecycle

Both C++ and Lua scripts follow the same lifecycle:

```
┌──────────────────┐
│  Entity Created  │
└────────┬─────────┘
         │
    ┌────▼────┐
    │ onAwake  │  Called once when entity/script first created
    └────┬────┘
         │ (Repeat each frame during play mode)
    ┌────▼──────────┐
    │ onUpdate(dt)  │  Called each frame, first
    └────┬──────────┘
         │
    ┌────▼──────────────┐
    │ onLateUpdate(dt)  │  Called after all onUpdates (physics happens here)
    └────┬──────────────┘
         │
    ┌────▼───────────┐
    │ onDestroy       │  Called when entity destroyed or script detached
    └─────────────────┘
```

---

## Debugging

### C++ Scripts

- Attach debugger (Visual Studio: Debug → Attach to Process)
- Add `std::cout << "Debug info\n"` in your code
- Check Output panel for messages

### Lua Scripts

- Use `engine_log("message")` to print to console
- Check Output panel for `[Lua]` prefixed messages
- See LUA_SCRIPTING_GUIDE.md for tips

### Common Issues

**Script doesn't run:**
- Is entity in scene?
- Is play mode enabled?
- For Lua: Is Lua installed? Check startup message.

**Script attaches but no callback:**
- For C++: Did you register the script in main.cpp?
- For Lua: Is the script path correct?
- Check console for error messages

**Crash or freeze:**
- C++: Check for null pointer access
- Lua: Check for infinite loops or missing Lua installation

---

## Tips & Tricks

### Use Multiple Scripts
- Attach multiple scripts to same entity
- Each runs independently
- Great for organizing behavior

### Mix C++ and Lua
- Use C++ for performance-critical code
- Use Lua for rapidly changing game logic
- Both work on same entity!

### Performance Tips
- C++ is ~100x faster than Lua for tight loops
- Use Lua for UI, input handling, high-level logic
- Move compute-heavy code to C++

### Project Organization
```
engine/Game/
├── ExampleScripts.h          # Your C++ scripts here
└── scripts/
    ├── my_script.lua
    ├── player_logic.lua
    └── enemy_ai.lua
```

---

## Next Steps

1. **Try Method 1**: Write a simple C++ script first
2. **Experience Iteration**: Notice recompile time
3. **Try Method 2**: Install Lua and write a Lua script
4. **Compare**: Feel the difference in iteration speed
5. **Combine**: Use both in your project strategically

---

## Complete Documentation

- **C++ Scripting**: See `SCRIPT_DEVELOPMENT_GUIDE.md`
- **Lua Scripting**: See `LUA_SCRIPTING_GUIDE.md`
- **Lua Installation**: See `LUA_INSTALLATION_GUIDE.md`
- **API Reference**: Generated from headers in `engine/Scripting/`

---

## Feedback & Support

- Check documentation files first
- Look at example scripts in `engine/Game/ExampleScripts.h`
- Try example Lua script: `engine/Game/scripts/example.lua`

Happy scripting! 🚀
