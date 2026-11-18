#pragma once

/*
 * C ABI for native game modules.
 *
 * A game module is gameplay code compiled into a shared library: the C++
 * half of a game, where a script per entity is not the right shape. It is
 * loaded from a plugin manifest's "runtimeEntry", like a script language
 * module, so it runs in Editor Play and in the packaged game. One module may
 * export script languages, game modules, or both.
 *
 * The module exports HP_GetGameModules. Each game module starts with every
 * play session, is updated once per frame before scripts run, and stops when
 * the session ends. Engine services come through HPScriptHostAPI, the same
 * table script languages receive, valid until endPlay returns.
 *
 * Every call happens on the gameplay thread.
 */

#include "ScriptLanguageAPI.h"

#define HP_GAME_MODULE_API_VERSION 1u
#define HP_GAME_MODULES_EXPORT_NAME "HP_GetGameModules"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HPGameModule {
    uint32_t structSize;
    uint32_t apiVersion;
    const char* id;          /* Unique, e.g. "com.example.mygame". */
    const char* displayName;

    /* Starts a play session. Returns the module's session state, or NULL when
       it cannot start; it then gets no update or endPlay this session. */
    void* (*beginPlay)(const HPScriptHostAPI* host);
    void (*update)(void* session, float deltaTime);
    void (*endPlay)(void* session);
} HPGameModule;

typedef const HPGameModule* const* (*HPGetGameModulesFn)(uint32_t hostApiVersion, uint32_t* count);

#ifdef __cplusplus
}
#endif
