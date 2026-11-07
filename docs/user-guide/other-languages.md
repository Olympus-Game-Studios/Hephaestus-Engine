---
title: Other languages through plugins
description: How Hephaestus supports C#, Python, JavaScript and other languages through scripting language plugins.
order: 16
---

# Other languages through plugins

Lua is built in, but it doesn't have to be the only language. Hephaestus lets a **plugin** add a whole scripting language: C#, Python, JavaScript, C++ or anything else that can talk to a simple C interface. That's a big part of the "the engine shouldn't box you in" idea. If your team thinks in C#, you shouldn't have to switch brains to make a game.

## How it works for you

Once a language plugin is installed and enabled:

- You attach scripts with the same **Lua Script** component. The file extension picks the language: a `.py` file runs in the Python plugin, a `.lua` file runs in Lua.
- Scripts get the same lifecycle (`onAwake`, `onUpdate` and so on, in that language's style), the same physics events and access to transforms, input and entities.
- Their variables can show in the Inspector, just like Lua's `Expose`.
- They reload when you save, while the game runs.
- Scripts in different languages can call each other. A Lua script's `entity:Call("onDamage", 25)` reaches a C# script on that entity and the other way round.
- Packaged games bring the language along automatically.
- If the plugin provides them, the built in code editor gives that language highlighting, autocomplete and hover docs.

## Where things stand

Hephaestus ships one language plugin so far: **C++ Scripting**, covered in [Scripting with C++](scripting-cpp.md). Out of the box you've got Lua, and C++ once you enable it. Any other language plugin is native code, so someone has to write one; the plan is for more first party ones (C# is the obvious next) and for the community to add more.

If you're a programmer who wants to write one, the interface is documented in the engine repository at `docs/plugins/script-languages.md`, alongside the `ScriptLanguageAPI.h` header. See [Plugins](plugins.md) for how plugins are put together.
