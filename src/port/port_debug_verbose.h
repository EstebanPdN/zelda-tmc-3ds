/*
 * port/port_debug_verbose.h — global per-frame log gate.
 *
 * Default OFF. Set TMC_VERBOSE=1 at launch to enable. Use to skip
 * fprintf + fflush calls in hot per-frame paths so a Pi 4 / Steam
 * Deck / old laptop doesn't spend CPU on stderr formatting nobody
 * reads.
 */

#ifndef PORT_DEBUG_VERBOSE_H
#define PORT_DEBUG_VERBOSE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read once at startup from env. Inline-checkable. */
extern bool Port_DebugVerbose;

/* Initialise from $TMC_VERBOSE. Idempotent; called from port_main. */
void Port_DebugVerbose_Init(void);

#ifdef __cplusplus
}
#endif

#endif
