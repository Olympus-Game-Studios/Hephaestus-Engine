# Scripting language plugins

Lua is built into the engine and stays the default. Any other language (C, C++, C#, Python, JavaScript, ...) can be added by a plugin that implements the plain C interface in [ScriptLanguageAPI.h](../../engine/PluginAPI/ScriptLanguageAPI.h). Because the interface is C, the plugin can be written in anything that can export a C function, and it can embed whatever runtime the language needs.

## How a script picks its language

Scripts are attached with the same Script component Lua uses (`LuaScriptComponent`, saved as `"LuaScript"` in scenes and prefabs). The file extension decides who runs it:

- a file whose extension a plugin language registered (for example `.py`) runs in that language;
- every other file, and embedded source, stays with Lua.

Per-entity variables set in the inspector are passed to the script when it loads. If the language implements `describeFields`, the inspector lists that script's fields the same way it lists a Lua script's `Expose` fields.

## Shipping a language

A language is a Qt-free shared library named by the manifest's `runtimeEntry` (see [plugin-manifest.md](plugin-manifest.md)). It exports one function:

```c
#include "PluginAPI/ScriptLanguageAPI.h"

static const HPScriptLanguage g_python = {
    sizeof(HPScriptLanguage), HP_SCRIPT_LANGUAGE_API_VERSION,
    "python", "Python", ".py",
    CreateRuntime, DestroyRuntime, CreateInstance, DestroyInstance,
    Awake, Update, LateUpdate, Contact, Call, DescribeFields
};
static const HPScriptLanguage* const g_languages[] = { &g_python };

HP_SCRIPT_LANGUAGE_EXPORT const HPScriptLanguage* const*
HP_GetScriptLanguages(uint32_t hostApiVersion, uint32_t* count) {
    if (hostApiVersion != HP_SCRIPT_LANGUAGE_API_VERSION) return NULL;
    *count = 1;
    return g_languages;
}
```

The editor loads the module when the plugin is enabled. Packaged games copy the module's folder into `Plugins/<id>/` and load it at startup, so `HEPHAESTUS_GAME_SCRIPTING` covers plugin languages as well as Lua. A plugin can have only a `runtimeEntry`, or both an editor `entry` and a `runtimeEntry`.

A language cannot use the id `lua`, claim `.lua`, or claim an extension another language already registered. Registration errors mark the plugin as failed in the Plugin Manager.

## Lifecycle

All calls happen on the gameplay thread, after Lua each frame:

| Callback | When |
| --- | --- |
| `createRuntime` / `destroyRuntime` | First script of the language in a play session / when play stops or the scene reloads |
| `createInstance` / `destroyInstance` | A script is attached, re-enabled, or its file changes on disk / it is removed, disabled, reloaded, or play stops. Run the script's own teardown in `destroyInstance`. |
| `awake` | Once, before the instance's first update |
| `update`, `lateUpdate` | Every frame, around physics, in the same order as Lua |
| `contact` | Trigger and collision events for the entity |
| `call` | Another script called a function on this entity |
| `describeFields` | The editor inspects the script file; no runtime exists |

A script that fails to load is reported once in the Output log and retried when its file changes.

## Engine services

`createRuntime` receives an `HPScriptHostAPI` table: logging, finding entities, entity names, transform get/set (radians for rotation), named input actions and axes, and `callEntityFunction`. `callEntityFunction` reaches the script on any entity, Lua included, and Lua's `entity:Call(name, ...)` reaches plugin scripts, so languages can be mixed in one game. Values crossing languages are nil, bool, number, string, and 2–4 component vectors.

New services are only appended to the table. Check `structSize` before using a member newer than the version you built against.

## Not covered yet

- Character controller scripts (`CharacterComponent`) are still Lua only.
- `onAnimationFinished` and the wider Lua API (physics queries, audio, UI, scene spawning, camera) are not in the host table yet.
- Languages stay registered until the editor restarts, even if their plugin is disabled.

`engine/Tests/ScriptLanguageTestPlugin.c` is a complete minimal language written in C, used by `ScriptLanguagePluginTests`.
