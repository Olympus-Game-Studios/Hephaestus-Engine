/*
 * A tiny scripting language written in plain C against ScriptLanguageAPI.h.
 * ScriptLanguagePluginTests links it directly and also loads it as a shared
 * module, which proves the ABI works without any C++ on the plugin side.
 *
 * A ".tscript" file holds no code: an entity moves along +X at its "speed"
 * variable, pings the entity named "LuaTarget" once, and answers "bump(n)"
 * by moving up n. A file starting with "fail" refuses to load.
 */

#include "../PluginAPI/ScriptLanguageAPI.h"
#include "ScriptLanguageTestPlugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TestStats g_stats;

TestStats* TestLanguage_Stats(void) { return &g_stats; }

typedef struct Runtime {
    const HPScriptHostAPI* host;
} Runtime;

typedef struct Instance {
    HPEntityId entity;
    double speed;
    int pinged;
} Instance;

static void* CreateRuntime(const HPScriptHostAPI* host) {
    Runtime* runtime = (Runtime*)calloc(1, sizeof(Runtime));
    runtime->host = host;
    g_stats.runtimesCreated++;
    return runtime;
}

static void DestroyRuntime(void* runtime) {
    g_stats.runtimesDestroyed++;
    free(runtime);
}

static void* CreateInstance(void* runtime, const HPScriptInstanceInfo* info, char* error, size_t errorSize) {
    char head[5] = {0};
    FILE* file = fopen(info->resolvedPath, "rb");
    uint32_t i;
    Instance* instance;
    (void)runtime;
    g_stats.createAttempts++;
    if (!file) {
        snprintf(error, errorSize, "cannot open %s", info->resolvedPath);
        return NULL;
    }
    fread(head, 1, 4, file);
    fclose(file);
    if (strcmp(head, "fail") == 0) {
        snprintf(error, errorSize, "script asked to fail");
        return NULL;
    }
    instance = (Instance*)calloc(1, sizeof(Instance));
    instance->entity = info->entity;
    instance->speed = 1.0;
    for (i = 0; i < info->variableCount; ++i) {
        if (strcmp(info->variables[i].name, "speed") == 0 &&
            info->variables[i].value.type == HP_VALUE_NUMBER) {
            instance->speed = info->variables[i].value.number;
        }
    }
    g_stats.instancesCreated++;
    return instance;
}

static void DestroyInstance(void* runtime, void* instance) {
    (void)runtime;
    g_stats.instancesDestroyed++;
    free(instance);
}

static void Awake(void* runtime, void* instance) {
    (void)runtime;
    (void)instance;
    g_stats.awakes++;
}

static void Update(void* runtimePtr, void* instancePtr, float deltaSeconds) {
    Runtime* runtime = (Runtime*)runtimePtr;
    Instance* instance = (Instance*)instancePtr;
    const HPScriptHostAPI* host = runtime->host;
    HPVec3 position;
    g_stats.updates++;
    if (host->getPosition(host->host, instance->entity, &position)) {
        position.x += (float)(instance->speed * deltaSeconds);
        host->setPosition(host->host, instance->entity, position);
    }
    if (!instance->pinged) {
        HPValue arg;
        memset(&arg, 0, sizeof(arg));
        arg.type = HP_VALUE_NUMBER;
        arg.number = 7.0;
        instance->pinged = 1;
        host->callEntityFunction(host->host, host->findEntity(host->host, "LuaTarget"), "onPing", &arg, 1);
    }
}

static void LateUpdate(void* runtime, void* instance, float deltaSeconds) {
    (void)runtime;
    (void)instance;
    (void)deltaSeconds;
    g_stats.lateUpdates++;
}

static void Contact(void* runtime, void* instance, const HPContactEvent* event) {
    (void)runtime;
    (void)instance;
    g_stats.contacts++;
    g_stats.lastContactKind = (int)event->kind;
}

static int32_t Call(void* runtimePtr, void* instancePtr, const char* function,
                    const HPValue* args, uint32_t argCount) {
    Runtime* runtime = (Runtime*)runtimePtr;
    Instance* instance = (Instance*)instancePtr;
    const HPScriptHostAPI* host = runtime->host;
    HPVec3 position;
    if (strcmp(function, "bump") != 0 || argCount < 1 || args[0].type != HP_VALUE_NUMBER) {
        return 0;
    }
    if (!host->getPosition(host->host, instance->entity, &position)) {
        return 0;
    }
    position.y += (float)args[0].number;
    host->setPosition(host->host, instance->entity, position);
    return 1;
}

static int32_t DescribeFields(const char* resolvedPath, HPFieldCallback callback, void* user) {
    HPValue value;
    (void)resolvedPath;
    memset(&value, 0, sizeof(value));
    value.type = HP_VALUE_NUMBER;
    value.number = 1.0;
    callback(user, "speed", value);
    value.type = HP_VALUE_STRING;
    value.string = "hi";
    callback(user, "label", value);
    return 1;
}

static const HPSyntaxRule g_syntaxRules[] = {
    { HP_SYNTAX_KEYWORD, "\\b(?:when|then)\\b" },
    { HP_SYNTAX_COMMENT, "#.*" }
};

static const HPSyntax g_syntax = {
    g_syntaxRules, 2, NULL, 0, "#"
};

static const HPScriptLanguage g_language = {
    sizeof(HPScriptLanguage),
    HP_SCRIPT_LANGUAGE_API_VERSION,
    "testlang",
    "Test Language",
    ".tscript;.TSX",
    CreateRuntime,
    DestroyRuntime,
    CreateInstance,
    DestroyInstance,
    Awake,
    Update,
    LateUpdate,
    Contact,
    Call,
    DescribeFields,
    &g_syntax
};

static const HPScriptLanguage* const g_languages[] = { &g_language };

#ifdef __cplusplus
extern "C"
#endif
HP_SCRIPT_LANGUAGE_EXPORT const HPScriptLanguage* const* HP_GetScriptLanguages(uint32_t hostApiVersion,
                                                                              uint32_t* count) {
    if (hostApiVersion != HP_SCRIPT_LANGUAGE_API_VERSION) {
        return NULL;
    }
    *count = 1;
    return g_languages;
}
