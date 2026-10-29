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
} HPScriptLanguage;

/* Signature of the HP_GetScriptLanguages export. Return NULL when
   hostApiVersion is not supported. */
typedef const HPScriptLanguage* const* (*HPGetScriptLanguagesFn)(uint32_t hostApiVersion,
                                                                 uint32_t* count);

#ifdef __cplusplus
}
#endif
