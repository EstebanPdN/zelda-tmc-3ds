#ifndef PORT_BUGREPORT_H
#define PORT_BUGREPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Captures a bug-report bundle next to the binary:
 *   ./bugreport_<timestamp>/screenshot.png  (240x160 GBA framebuffer)
 *   ./bugreport_<timestamp>/save.bin        (current save EEPROM dump)
 *   ./bugreport_<timestamp>/state.txt       (version/area/coords/reason)
 *   ./bugreport_<timestamp>/backtrace.txt   (only when called from crash handler)
 *
 * `reason` is a free-form short tag written into state.txt to distinguish
 * user-triggered captures (e.g. "user") from automatic ones (e.g.
 * "crash:SIGSEGV@0x..."). NULL is treated as "user".
 *
 * Returns the directory name on success (caller must free), NULL on failure.
 * Triggered by F9 in port_bios.c and by the crash handler. */
char* Port_BugReport_Capture(const char* reason);

/* Install signal / SEH handlers that auto-capture a bug report on crash.
 * Called once from main(). No-op if invoked twice. */
void Port_BugReport_InstallCrashHandlers(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BUGREPORT_H */
