#include "../PluginAPI/GameModuleAPI.h"
#include "GameModuleTestPlugin.h"

#include <stdio.h>
#include <string.h>

static GameModuleTestStats g_Stats;

GameModuleTestStats* TestGameModule_Stats(void) {
    return &g_Stats;
}

typedef struct Session {
    const HPScriptHostAPI* host;
} Session;

static Session g_Session;

static void CountBytes(void* user, const void* data, size_t size) {
    (void)data;
    *(size_t*)user = size;
}

static void* BeginPlay(const HPScriptHostAPI* host) {
    char root[1024];
    char path[1200];
    char error[256] = "";
    size_t size = 0;
    g_Stats.begins++;
    g_Session.host = host;
    if (host->structSize < sizeof(HPScriptHostAPI) || !host->getProjectRoot ||
        host->getProjectRoot(host->host, root, sizeof root) > sizeof root) {
        return &g_Session;
    }
    snprintf(path, sizeof path, "%s/base.pk3", root);
    g_Stats.mounted = host->archiveMount(host->host, path, error, sizeof error);
    g_Stats.files = (int)host->archiveFileCount(host->host);
    if (host->archiveReadFile(host->host, "SCRIPTS/test.txt", CountBytes, &size, error, sizeof error)) {
        g_Stats.scriptBytes = (int)size;
    }
    return &g_Session;
}

static void Update(void* session, float deltaTime) {
    (void)session;
    g_Stats.updates++;
    g_Stats.lastDelta = deltaTime;
}

static void EndPlay(void* session) {
    (void)session;
    g_Stats.ends++;
}

static void* RefuseToStart(const HPScriptHostAPI* host) {
    (void)host;
    g_Stats.refusals++;
    return NULL;
}

static void RefusedUpdate(void* session, float deltaTime) {
    (void)session;
    (void)deltaTime;
    g_Stats.refusedCalls++;
}

static void RefusedEnd(void* session) {
    (void)session;
    g_Stats.refusedCalls++;
}

static const HPGameModule g_Module = {
    sizeof(HPGameModule), HP_GAME_MODULE_API_VERSION, "com.test.gamemodule", "Test Game Module",
    BeginPlay, Update, EndPlay};

static const HPGameModule g_Refuser = {
    sizeof(HPGameModule), HP_GAME_MODULE_API_VERSION, "com.test.refuser", "Refusing Game Module",
    RefuseToStart, RefusedUpdate, RefusedEnd};

static const HPGameModule* const g_Modules[] = {&g_Module, &g_Refuser};

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const HPGameModule* const* HP_GetGameModules(uint32_t hostApiVersion, uint32_t* count) {
    if (hostApiVersion != HP_GAME_MODULE_API_VERSION) {
        *count = 0;
        return NULL;
    }
    *count = 2;
    return g_Modules;
}
