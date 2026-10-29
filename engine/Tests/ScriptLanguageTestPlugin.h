#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Counters the C test language keeps so the test can observe its lifecycle. */
typedef struct TestStats {
    int runtimesCreated;
    int runtimesDestroyed;
    int createAttempts;
    int instancesCreated;
    int instancesDestroyed;
    int awakes;
    int updates;
    int lateUpdates;
    int contacts;
    int lastContactKind;
} TestStats;

TestStats* TestLanguage_Stats(void);

#ifdef __cplusplus
}
#endif
