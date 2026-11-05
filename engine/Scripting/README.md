# engine/Scripting

Gameplay scripting for the editor's Play mode and the packaged game player. Lua 5.4 is built in; other languages come from plugins.

Documentation lives elsewhere:

- Using Lua: [user guide, Scripting with Lua](../../docs/user-guide/scripting-lua.md) and the [Lua API reference](../../docs/user-guide/lua-api.md)
- Exact binding and callback semantics: [LUA_SCRIPTING_GUIDE.md](../../Documentation/Scripting/LUA_SCRIPTING_GUIDE.md)
- Writing a language plugin: [script-languages.md](../../docs/plugins/script-languages.md)

## Files

| File | Role |
| --- | --- |
| `LuaScriptEngine.*` | The Lua state, script instances and lifecycle, and every Lua binding (`Scene`, `Transform`, `Physics`, `Character`, `Input`, `UI`, `Audio` and the rest) |
| `ScriptPhysicsBindings.*` | The shared `Physics.*` and `Character.*` implementation used by Editor Play and the game player |
| `LuaScriptComponent.h` | The component that attaches scripts to an entity (saved as `LuaScript`). Plugin languages use it too, chosen by file extension |
| `LuaExposeScanner.*`, `LuaExposeMetadata.h` | Static scan of `Expose.*` declarations for the Inspector |
| `LuaScriptVariableReconciler.*` | Keeps saved Inspector values in step when a script's `Expose` list changes |
| `LuaDebugger.*` | Breakpoints, stepping, call stack, locals and watches for the editor's Debugger panel |
| `LuaLanguageService.*` | Completion and hover data for the Code panel |
| `ScriptLanguageRegistry.*`, `PluginScriptHost.*` | Loading scripting language plugins and running their scripts beside Lua |
| `ScriptEngine.*`, `ScriptBase.*`, `ScriptComponent.h`, `ScriptRegistry.h`, `ExampleScript.h` | The older C++ script path (`Script Component` in the editor). Kept for engine code; not a gameplay scripting option |

## Dependencies

Lua comes from `extern/lua` on Windows (headers, `lua54.lib` and `lua54.dll`), or from `find_package(Lua)`, vcpkg or Scoop. Lua support follows `LUA_FOUND`; `HEPHAESTUS_GAME_SCRIPTING` controls whether the game player starts scripts.
