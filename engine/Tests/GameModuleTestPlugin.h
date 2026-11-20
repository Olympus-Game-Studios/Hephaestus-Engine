#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Counters the C test game modules keep so the test can observe their lifecycle. */
typedef struct GameModuleTestStats {
    int begins;
    int updates;
    int ends;
    int mounted;
    int files;
    int scriptBytes;
    float lastDelta;
    int refusals;
    int refusedCalls;
    unsigned long long meshEntity;
    int badMeshRejected;
    int hullsAdded;
    int badHullRejected;
    int meshUpdated;
    int meshUpdateCountRejected;
    int viewModelSet;
    int viewModelCleared;
    int viewModelOfNonMeshRejected;
    unsigned long long gunEntity;
    unsigned long long clearedEntity;
} GameModuleTestStats;

GameModuleTestStats* TestGameModule_Stats(void);

#ifdef __cplusplus
}
#endif
