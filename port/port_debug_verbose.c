#include "port_debug_verbose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Port_DebugVerbose = false;

void Port_DebugVerbose_Init(void) {
    const char* env = getenv("TMC_VERBOSE");
    if (env && *env && strcmp(env, "0") != 0) {
        Port_DebugVerbose = true;
        fprintf(stderr, "[verbose] TMC_VERBOSE=%s — per-frame stderr logs enabled\n", env);
        return;
    }
    /* Android has no way to hand the app an environment variable; a
     * marker file in the app data dir (the CWD — port_main chdir'd
     * there before calling us) does the same job:
     *   adb shell touch /sdcard/Android/data/dev.picori.tmc/files/verbose
     * Checked everywhere for consistency, not just on Android. */
    FILE* marker = fopen("verbose", "rb");
    if (marker) {
        fclose(marker);
        Port_DebugVerbose = true;
        fprintf(stderr, "[verbose] 'verbose' marker file found — per-frame stderr logs enabled\n");
    }
}
