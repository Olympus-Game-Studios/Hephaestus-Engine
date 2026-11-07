#pragma once

/*
 * C ABI for scripting-language plugins.
 *
 * Plain C on purpose: a language plugin can be written in C, C++, Rust or Zig,
 * or wrap an embedded runtime (Python, JavaScript, .NET, ...). Nothing here
 * depends on Qt, the STL, or the editor, so the same module runs in the editor
 * and in the packaged game.
 *
 * A language module is a shared library named by the plugin manifest's
 * "runtimeEntry" field. It exports HP_GetScriptLanguages, which returns the
 * languages it provides. Lua is built into the engine and does not use this API.
 *
 * Every call happens on the gameplay thread. Strings passed to the plugin are
 * borrowed for the duration of the call only.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define HP_SCRIPT_LANGUAGE_EXPORT __declspec(dllexport)
#else
#  define HP_SCRIPT_LANGUAGE_EXPORT __attribute__((visibility("default")))
#endif

#define HP_SCRIPT_LANGUAGE_API_VERSION 1u
#define HP_SCRIPT_LANGUAGES_EXPORT_NAME "HP_GetScriptLanguages"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene entity handle. 0 means "no entity". */
typedef uint64_t HPEntityId;

typedef struct HPVec3 {
    float x;
    float y;
    float z;
} HPVec3;

typedef enum HPValueType {
    HP_VALUE_NIL = 0,
    HP_VALUE_BOOL = 1,
    HP_VALUE_NUMBER = 2,
    HP_VALUE_STRING = 3,
    HP_VALUE_VEC2 = 4,
    HP_VALUE_VEC3 = 5,
    HP_VALUE_VEC4 = 6
} HPValueType;

/* A script value crossing the language boundary. Only the member matching
   `type` is meaningful. VEC2/VEC3/VEC4 use the first 2/3/4 entries of `vector`. */
typedef struct HPValue {
    uint32_t type;
    int32_t boolean;
    double number;
    const char* string;
    float vector[4];
} HPValue;

typedef struct HPNamedValue {
    const char* name;
    HPValue value;
} HPNamedValue;

typedef enum HPLogLevel {
    HP_LOG_INFO = 0,
    HP_LOG_WARNING = 1,
    HP_LOG_ERROR = 2
} HPLogLevel;

/*
 * Engine services a language runtime can call. Functions returning int32_t
 * return 1 on success and 0 when the entity or value does not exist.
 * Rotations are Euler angles in radians, matching TransformComponent.
 *
 * New members are only ever appended. Check structSize before using a member
 * added after the version your plugin was built against.
 */
typedef struct HPScriptHostAPI {
    uint32_t structSize;
    uint32_t apiVersion;
    void* host;

    void (*log)(void* host, uint32_t level, const char* message);

    int32_t (*isEntityValid)(void* host, HPEntityId entity);
    HPEntityId (*findEntity)(void* host, const char* name);
    /* Copies the name into buffer when it fits and returns its length
       including the terminator, so a call with bufferSize 0 sizes the buffer. */
    size_t (*getEntityName)(void* host, HPEntityId entity, char* buffer, size_t bufferSize);

    int32_t (*getPosition)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setPosition)(void* host, HPEntityId entity, HPVec3 value);
    int32_t (*getRotation)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setRotation)(void* host, HPEntityId entity, HPVec3 value);
    int32_t (*getScale)(void* host, HPEntityId entity, HPVec3* out);
    int32_t (*setScale)(void* host, HPEntityId entity, HPVec3 value);

    /* Named actions and axes from the project's .hpinput profile. */
    int32_t (*isActionDown)(void* host, const char* action);
    int32_t (*isActionPressed)(void* host, const char* action);
    int32_t (*isActionReleased)(void* host, const char* action);
    float (*getAxis)(void* host, const char* axis);

    /* Calls a function on the script attached to an entity, whatever language
       it is written in (Lua included). Returns 1 when a script handled it. */
    int32_t (*callEntityFunction)(void* host, HPEntityId entity, const char* function,
                                  const HPValue* args, uint32_t argCount);

    /* Added after the first release: check structSize before use.
       The project folder (the game's content folder when packaged), copied
       with the getEntityName buffer convention. */
    size_t (*getProjectRoot)(void* host, char* buffer, size_t bufferSize);
} HPScriptHostAPI;

/* Describes the script a runtime is asked to load for one entity. */
typedef struct HPScriptInstanceInfo {
    HPEntityId entity;
    const char* scriptPath;   /* As authored, e.g. "assets/scripts/player.py". */
    const char* resolvedPath; /* Absolute path of the file on disk. */
    const HPNamedValue* variables; /* Per-entity values set in the inspector. */
    uint32_t variableCount;
} HPScriptInstanceInfo;

typedef enum HPContactKind {
    HP_CONTACT_TRIGGER_ENTER = 0,
    HP_CONTACT_TRIGGER_STAY = 1,
    HP_CONTACT_TRIGGER_EXIT = 2,
    HP_CONTACT_COLLISION_ENTER = 3,
    HP_CONTACT_COLLISION_STAY = 4,
    HP_CONTACT_COLLISION_EXIT = 5
} HPContactKind;

typedef struct HPContactEvent {
    uint32_t kind;     /* HPContactKind */
    HPEntityId other;  /* 0 when the other entity no longer exists. */
    int32_t hasContact; /* When 0 the contact members below are unset. */
    HPVec3 point;
    HPVec3 normal;
    HPVec3 relativeVelocity;
    float penetration;
} HPContactEvent;

typedef void (*HPFieldCallback)(void* user, const char* name, HPValue defaultValue);

/* Colour roles for editor syntax highlighting; the editor theme picks the colours. */
typedef enum HPSyntaxStyle {
    HP_SYNTAX_KEYWORD = 0,
    HP_SYNTAX_TYPE = 1,
    HP_SYNTAX_FUNCTION = 2,
    HP_SYNTAX_STRING = 3,
    HP_SYNTAX_NUMBER = 4,
    HP_SYNTAX_COMMENT = 5
} HPSyntaxStyle;

/* Patterns are Perl-compatible regular expressions (PCRE2) matched within one
   line. The editor scans each line left to right: the earliest match wins, a
   tie goes to the rule listed first, and scanning resumes after the match. */
typedef struct HPSyntaxRule {
    uint32_t style; /* HPSyntaxStyle */
    const char* pattern;
} HPSyntaxRule;

/* A span that can cross lines, such as a block comment or long string. */
typedef struct HPSyntaxBlock {
    uint32_t style; /* HPSyntaxStyle */
    const char* startPattern;
    const char* endPattern;
} HPSyntaxBlock;

typedef enum HPCompletionKind {
    HP_COMPLETION_KEYWORD = 0,
    HP_COMPLETION_FUNCTION = 1,
    HP_COMPLETION_VARIABLE = 2,
    HP_COMPLETION_FIELD = 3,
    HP_COMPLETION_TYPE = 4
} HPCompletionKind;

/* One suggestion. `detail` (may be NULL) is shown beside the label. */
typedef void (*HPCompletionCallback)(void* user, const char* label, uint32_t kind, const char* detail);

typedef struct HPSyntax {
    const HPSyntaxRule* rules;
    uint32_t ruleCount;
    const HPSyntaxBlock* blocks;
    uint32_t blockCount;
    /* Inserted by the editor's Toggle Comment (Ctrl+/), e.g. "#". NULL disables it. */
    const char* lineComment;
} HPSyntax;

/*
 * One scripting language. Required members: id, displayName, extensions,
 * createRuntime, destroyRuntime, createInstance and destroyInstance. Every
 * other callback may be NULL.
 */
typedef struct HPScriptLanguage {
    uint32_t structSize;
    uint32_t apiVersion;
    const char* id;          /* Stable lowercase id, e.g. "python". */
    const char* displayName; /* e.g. "Python". */
    const char* extensions;  /* Semicolon-separated, e.g. ".py;.pyw". */

    /* Called when play starts and the first script in this language needs to
       run. `host` stays valid until destroyRuntime. */
    void* (*createRuntime)(const HPScriptHostAPI* host);
    void (*destroyRuntime)(void* runtime);

    /* Loads a script for one entity. Return NULL and write a message into
       `error` on failure. Called again after the file changes on disk. */
    void* (*createInstance)(void* runtime, const HPScriptInstanceInfo* info,
                            char* error, size_t errorSize);
    /* Run the script's own teardown (for example onDestroy) here. */
    void (*destroyInstance)(void* runtime, void* instance);

    void (*awake)(void* runtime, void* instance);
    void (*update)(void* runtime, void* instance, float deltaSeconds);
    void (*lateUpdate)(void* runtime, void* instance, float deltaSeconds);
    void (*contact)(void* runtime, void* instance, const HPContactEvent* event);
    /* Returns 1 when the script defines `function` and it ran. */
    int32_t (*call)(void* runtime, void* instance, const char* function,
                    const HPValue* args, uint32_t argCount);

    /* Lists the inspector-editable fields of a script file and their default
       values, without a runtime. Return 0 when the file cannot be read. */
    int32_t (*describeFields)(const char* resolvedPath, HPFieldCallback callback, void* user);

    /* Added after the first release: read only when structSize covers it.
       Highlighting for the editor's code editor; NULL shows plain text. */
    const HPSyntax* syntax;

    /* Editor language services, read only when structSize covers them. Both
       get the whole unsaved file in `source`; `line` and `column` are 1-based,
       column counted in bytes of that line's UTF-8 text. */

    /* Suggestions for the cursor position. The editor filters them by the
       word being typed, so return every candidate. Return 0 for none. */
    int32_t (*complete)(const char* path, const char* source, uint32_t line, uint32_t column,
                        HPCompletionCallback callback, void* user);
    /* Plain-text documentation for the symbol under the position, written into
       `buffer`. Returns the length including the terminator (0 for nothing),
       so a call with bufferSize 0 sizes the buffer. */
    size_t (*hover)(const char* path, const char* source, uint32_t line, uint32_t column,
                    char* buffer, size_t bufferSize);

    /* Packaging, read only when structSize covers it. Called once for every
       file of this language a package contains, with absolute paths, before
       the package is committed. Write whatever the script needs at runtime
       (for example compiled code) under packageRoot, which becomes the
       game's project root. Return 0 and write a message into `error` to fail
       the build. */
    int32_t (*packageScript)(const char* projectRoot, const char* scriptPath, const char* packageRoot,
                             char* error, size_t errorSize);

    /* Contents of a new script made from the Content Browser, read only when
       structSize covers it; NULL leaves the language out of the menu. Every
       "$NAME" becomes the name the user typed, which is a valid identifier. */
    const char* newScriptTemplate;
} HPScriptLanguage;

/* Signature of the HP_GetScriptLanguages export. Return NULL when
   hostApiVersion is not supported. */
typedef const HPScriptLanguage* const* (*HPGetScriptLanguagesFn)(uint32_t hostApiVersion,
                                                                 uint32_t* count);

#ifdef __cplusplus
}
#endif
