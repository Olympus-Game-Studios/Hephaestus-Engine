---
title: Scripting with C++
description: Writing gameplay in C++ with the C++ Scripting plugin, compiled with Visual Studio and reloaded while the game runs.
order: 15
---

# Scripting with C++

Lua is the quickest way to get something moving, but plenty of people already think in C++, and some code just wants to be native. The **C++ Scripting** plugin lets you write gameplay scripts in C++ and attach them to entities exactly like Lua scripts. They compile in the background of Play, reload when you save and ship inside your packaged game.

C++ scripting is Windows only for now, because it compiles with Microsoft's C++ compiler.

## Setting up

1. Install **Visual Studio** (the free Community edition is fine) with the **Desktop development with C++** workload. You don't need to open it; Hephaestus finds the compiler on its own.
2. In the editor, open **Edit > Plugins...**, tick **Enabled** next to **C++ Scripting** and restart the editor.

That's it. The Content Browser's right click menu now has **New C++ Script**.

## Your first script

1. In the Content Browser, go to `assets/scripts`, right click and choose **New C++ Script**. Call it `Spinner`. The name becomes the class name, so it has to be letters, digits and underscores.
2. Open it and make `OnUpdate` spin the entity:

    ```cpp
    #include "HephaestusScript.h"

    class Spinner : public hp::Script {
    public:
        HP_FIELD(float, speed, 90.0f);

        void OnUpdate(float deltaSeconds) override {
            GetEntity().Rotate({0, speed * deltaSeconds, 0});
        }
    };

    HP_SCRIPT(Spinner)
    ```

3. Select a cube, **Add Component > Lua Script** and set its script to `Spinner.cpp`. (Yes, the component is still called Lua Script; the file extension picks the language.)
4. Press **Play**. The first time, the Console says `Compiling assets/scripts/Spinner.cpp` and the game pauses for a second or two while it builds. Then it spins.
5. While it's playing, change the axis and save. The script recompiles and restarts with your change.

## How a script works

A script is a class that derives from `hp::Script`, and `HP_SCRIPT(ClassName)` at the bottom tells the engine which class the file provides (one per file). Override the callbacks you need:

| Function | When it's called |
| --- | --- |
| `OnAwake()` | Once, when the script first starts. |
| `OnUpdate(float deltaSeconds)` | Every frame. |
| `OnLateUpdate(float deltaSeconds)` | Every frame, after every script's `OnUpdate`. |
| `OnDestroy()` | When the entity is removed, the script reloads or the game stops. Your destructor runs straight after. |
| `OnTriggerEnter(other, contact)` and friends | Physics events, [below](#reacting-to-physics). |
| `OnCall(function, args)` | Another script called a function on this entity. |

`GetEntity()` is the entity the script is attached to, the same as `this` in Lua. Your class is constructed when the script starts, so you can set things up in the constructor too, and every instance has its own member variables.

## Inspector fields with `HP_FIELD`

`HP_FIELD` declares a member variable that also shows up in the Inspector:

```cpp
HP_FIELD(float, moveSpeed, 5.0f);
HP_FIELD(int, ammo, 30);
HP_FIELD(bool, canSprint, true);
HP_FIELD(std::string, doorId, "main_door");
HP_FIELD(hp::Vec2, screenOffset, 0, 0);
HP_FIELD(hp::Vec3, spawnOffset, 0, 1, 0);
HP_FIELD(hp::Color, tint, 1, 0.8f, 0.6f, 1);
```

The type comes first, then the name, then the default. Vectors and colours list their numbers one after another. Like Lua's `Expose`, the editor reads these lines straight from the file, so keep each one on a single line with plain values. Values you set in the Inspector replace the default when the script starts.

## Talking to other entities

```cpp
// Find things by name
hp::Entity door = hp::Scene::Find("MainDoor");

// Move things around (rotations are in degrees, like Lua)
door.SetPosition({0, 2, 0});
door.Rotate({0, 90, 0});

// Call a function in another entity's script, in any language
door.Call("onOpen", 2.5f, "fast");
```

`Call` reaches Lua scripts as well as C++ ones, and Lua's `entity:Call("Bump", 3)` reaches yours. To answer a call, override `OnCall`:

```cpp
bool OnCall(std::string_view function, hp::Args args) override {
    if (function == "Bump" && !args.empty()) {
        GetEntity().Translate({0, args[0].AsFloat(), 0});
        return true;
    }
    return false;
}
```

Arguments can be numbers, booleans, strings and 2 to 4 component vectors. `hp::Log`, `hp::LogWarning` and `hp::LogError` write to the Console.

The rest of the engine is there too, and it matches Lua's API piece by piece: spawning and destroying prefabs, cameras, raycasts and physics bodies, character controllers, animation, sound, input and game UI. A taste:

```cpp
void OnUpdate(float deltaSeconds) override {
    if (hp::Input::IsActionPressed("Fire")) {
        hp::Audio::Play("assets/audio/shot.wav");
        if (auto hit = Raycast(GetEntity().GetPosition(), {0, 0, -1}, 100.0f)) {
            hit->entity.Call("onDamage", 25);
        }
    }
    if (auto controller = GetEntity().GetController()) {
        controller->Move(hp::Input::GetAxis("MoveX"), hp::Input::GetAxis("MoveY"));
    }
}
```

The [C++ API reference](cpp-api.md) lists everything, with the Lua function each one matches.

## Reacting to physics

```cpp
void OnTriggerEnter(hp::Entity other, const hp::Contact* contact) override {
    if (other && other.GetName() == "Player") {
        hp::Log("Player reached the checkpoint");
    }
}
```

The full set is `OnTriggerEnter`, `OnTriggerStay`, `OnTriggerExit`, `OnCollisionEnter`, `OnCollisionStay` and `OnCollisionExit`. `contact` (with `point`, `normal`, `relativeVelocity` and `penetration`) is sometimes null, and the exit callbacks never get one, so check it. `other` is empty if the other entity was destroyed.

## When things go wrong

- **Compile errors** appear in the Console as `file:line: message`, just like Lua errors. Double click one to jump to the line. The script doesn't run until it compiles.
- **Exceptions** thrown out of a callback are caught and reported, and the script keeps running.
- **Crashes** (a null pointer, say) stop that script, report which callback crashed and leave the editor running. Save a fix and it starts again. Because a crash can leave memory in a bad state, it's worth restarting Play after one.

To debug a script with breakpoints, open Visual Studio, use **Debug > Attach to Process** on `HephaestusEngine.exe`, open your `.cpp` file and set breakpoints as normal. The debug symbols sit beside the compiled script, so Visual Studio finds them on its own. Scripts are built with optimisations on, so a few variables may show as optimised away.

## Packaging

When you package the game, every C++ script is compiled (if it isn't already) and its compiled DLL goes into the package. Players don't need Visual Studio. A script that doesn't compile stops the package with its errors, so you find out before your players do.

## Things to know

- Compiled scripts are cached in your project's `.hephaestus/build/cpp-scripts` folder. You can delete it whenever you like; scripts just compile again.
- A script recompiles when its own `.cpp` file changes. If you `#include` a header of your own and only change the header, save the `.cpp` too.
- A `.cpp` file without `HP_SCRIPT` is skipped when packaging, but it isn't compiled into other scripts either. To share code between scripts, put it in a header and `#include` it.
- Scripts are native code with full access to your computer, so only run scripts you trust.
- The C++ API covers everything Lua's does except `onAnimationFinished`, `onEnable`/`onDisable` and character controller scripts, which are still Lua only for now. So is the debugger built into the Code panel (use Visual Studio's, above).
