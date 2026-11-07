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

## Syntax highlighting

The editor's Code panel (Programmer mode) highlights a file with the syntax table of the language that claims its extension. Set `syntax` on your `HPScriptLanguage` to an `HPSyntax`, or leave it `NULL` for plain text. Built-in Lua supplies its table the same way.

```c
static const HPSyntaxRule g_rules[] = {
    { HP_SYNTAX_KEYWORD, "\\b(?:def|class|return|if|else|for|while|import)\\b" },
    { HP_SYNTAX_FUNCTION, "\\b[A-Za-z_]\\w*(?=\\s*\\()" },
    { HP_SYNTAX_NUMBER, "\\b\\d+(?:\\.\\d*)?\\b" },
    { HP_SYNTAX_STRING, "\"(?:[^\"\\\\]|\\\\.)*\"" },
    { HP_SYNTAX_COMMENT, "#.*" }
};
static const HPSyntaxBlock g_blocks[] = {
    { HP_SYNTAX_STRING, "\"\"\"", "\"\"\"" }
};
static const HPSyntax g_syntax = { g_rules, 5, g_blocks, 1, "#" };
```

- Patterns are Perl-compatible regular expressions (PCRE2), matched within one line.
- Each line is scanned left to right. The earliest match wins, a tie goes to the rule listed first, and scanning resumes after the match, so `#` inside a string stays part of the string.
- A block (`HPSyntaxBlock`) starts at `startPattern` and runs, across lines if needed, to the next `endPattern`.
- Styles are roles (`HP_SYNTAX_KEYWORD`, `TYPE`, `FUNCTION`, `STRING`, `NUMBER`, `COMMENT`); the editor theme picks the colours.
- An invalid pattern is skipped and reported in the Output log.
- `lineComment` is what Toggle Comment (Ctrl+/) adds and removes; `NULL` turns it off.
- Script errors that name a file as `path:line:` in the Console underline that line in the editor, and double-clicking the Console entry opens it. Write your runtime's errors in that form to get the same.

### Completion and hover

Two optional callbacks give your language autocomplete and hover documentation in the Code panel:

```c
static int32_t Complete(const char* path, const char* source, uint32_t line, uint32_t column,
                        HPCompletionCallback callback, void* user) {
    callback(user, "spawn", HP_COMPLETION_FUNCTION, "Scene");
    return 1;
}
static size_t Hover(const char* path, const char* source, uint32_t line, uint32_t column,
                    char* buffer, size_t bufferSize) {
    const char* text = "spawn(prefab) creates an entity";
    const size_t size = strlen(text) + 1;
    if (bufferSize >= size) memcpy(buffer, text, size);
    return size;
}
```

- Both receive the whole file as the editor has it, including unsaved edits. `line` and `column` are 1-based; the column counts bytes of that line's UTF-8 text.
- `complete` lists every candidate for the position. The editor filters them by the word being typed. Typing two letters, `.` or `:` opens the list, and so does Ctrl+Space.
- `hover` follows the same buffer convention as `getEntityName`: return the length including the terminator, and the editor calls again with a buffer that size.
- Without `complete`, the editor offers the words already in the file. Lua's completion lists keywords, the engine API (read from the functions the engine registers, so it stays current), and the file's own locals and functions.

The debugger (breakpoints, stepping, call stack, locals and watches) works with Lua only.

`syntax`, `complete` and `hover` were added after the first release. Modules built before them still load, with their files shown as plain text and no language services.

### New script template

Set `newScriptTemplate` to the contents of a new file and the Content Browser's create menu gets **New <displayName> Script**, which writes a file with your first extension. Every `$NAME` in the template becomes the name the user typed; the editor only accepts names that are identifiers (letters, digits and `_`, not starting with a digit), so the name can be used as a class name.

## Packaging

When a game is packaged, the packager loads each enabled plugin's `runtimeEntry` module (the plugin's own copy, not the staged one) and calls `packageScript` once for every file under the package's `assets` and `scripts` folders whose extension the language claims:

```c
static int32_t PackageScript(const char* projectRoot, const char* scriptPath, const char* packageRoot,
                             char* error, size_t errorSize);
```

`scriptPath` is the source file in the project and `packageRoot` is the staged game's root, which the packaged game sees as its project root (`getProjectRoot`). Write anything the script needs at runtime under `packageRoot`, such as compiled code. Return 0 with a message in `error` to fail the package; the message appears in the build's problems. Leave `packageScript` `NULL` when the source file is all a script needs. The packager runs in the editor and in `HephaestusBuild`, so the callback must not rely on a play session.

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

`createRuntime` receives an `HPScriptHostAPI` table. The first release had logging, finding entities, entity names, transform get/set (radians for rotation), named input actions and axes and `callEntityFunction`. The table now also has `getProjectRoot` (the project folder in the editor, the game's root when packaged) and a member for every Lua engine function: time and quit, entity names and components, prefab spawning (`instantiatePrefab`, with Inspector overrides), deferred `destroyEntity`, `reloadScene`, mesh visibility, raw keys, mouse, gamepad and cursor mode, the script view camera, camera components and lenses, physics bodies and `raycast`, character controllers, animation and sockets, and audio. Each is commented in `ScriptLanguageAPI.h` with how it differs from Lua, if at all. The Lua and plugin paths share one implementation for anything with logic in it (spawning, deferred destroy, active cameras, sockets, cursor mode), so they behave the same.

Members take the entity they act on, where Lua's global tables act on the calling script's entity. Functions that return a list (`findEntitiesWithPrefix`, `findCameras`) write up to `capacity` ids and return how many exist; functions that return a string use the `getEntityName` buffer convention. Destroys requested during `update` happen after every plugin script's update, as Lua's do after `onUpdate`. `callEntityFunction` reaches the script on any entity, Lua included, and Lua's `entity:Call(name, ...)` reaches plugin scripts, so languages can be mixed in one game. Values crossing languages are nil, bool, number, string, and 2–4 component vectors.

New services are only appended to the table. Check `structSize` before using a member newer than the version you built against.

## Not covered yet

- Character controller scripts (`CharacterComponent`) are still Lua only, and so is `CallController`.
- `onAnimationFinished`, `onEnable` and `onDisable` have no callbacks yet; disabling a component destroys a plugin script.
- Game UI (`display.ui`, `destroy.ui` and `UI.*`) is not in the host table yet.
- Languages stay registered until the editor restarts, even if their plugin is disabled.

`engine/Tests/ScriptLanguageTestPlugin.c` is a complete minimal language written in C, used by `ScriptLanguagePluginTests` and `PackageLayoutTests`. `Plugins/CppScripting` is a full first-party language: it compiles each `.cpp` script into a DLL with the user's Visual Studio and implements every optional callback.
