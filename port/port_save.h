#ifndef PORT_SAVE_H
#define PORT_SAVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortSaveStage {
    PORT_SAVE_STAGE_IDLE = 0,
    PORT_SAVE_STAGE_RECOVER,
    PORT_SAVE_STAGE_OPEN_TEMP,
    PORT_SAVE_STAGE_WRITE_TEMP,
    PORT_SAVE_STAGE_FLUSH_TEMP,
    PORT_SAVE_STAGE_SYNC_TEMP,
    PORT_SAVE_STAGE_CLOSE_TEMP,
    PORT_SAVE_STAGE_VERIFY_TEMP,
    PORT_SAVE_STAGE_BACKUP_CURRENT,
    PORT_SAVE_STAGE_INSTALL_TEMP,
    PORT_SAVE_STAGE_VERIFY_INSTALLED,
    PORT_SAVE_STAGE_RESTORE_BACKUP,
    PORT_SAVE_STAGE_COMPLETE,
} PortSaveStage;

typedef struct PortSaveStats {
    uint64_t flushAttempts;
    uint64_t flushSuccesses;
    uint64_t flushFailures;
    uint64_t interruptedRecoveries;
    uint64_t rollbackRestores;
    uint64_t rollbackFailures;
    int32_t transactionDepth;
    int32_t lastErrno;
    PortSaveStage lastStage;
    bool initialized;
    bool dirty;
    char activePath[64];
} PortSaveStats;

void Port_Save_BeginTransaction(void);
int Port_Save_EndTransaction(void);
/* Create a durable, uniquely named copy of the active raw 8 KiB file before
 * an automatic in-place migration. Returns 0 without altering the source. */
int Port_Save_PreserveBeforeMigration(void);
/* Create one durable, never-overwriting backup of the active profile before
 * repairing v1.2-E1's region-contaminated fuser cursors. Repeated repairs in
 * the same profile/session reuse that already-verified preservation. */
int Port_Save_PreserveBeforeFuserRepair(void);
/* Preserve the active profile before moving the pre-v1.2-E5 USA Smith bottle
 * chest bit to its native European ordinal. */
int Port_Save_PreserveBeforeSmithBottleFlagRepair(void);
/* Preserve the complete active profile before the one-shot Cloud Tops lost
 * reward repair. Repeated checks during one profile activation reuse it. */
int Port_Save_PreserveBeforeCloudTopsRepair(void);
/* Preserve the complete active profile before the one-shot Vaati progression
 * repair. Repeated checks during one profile activation reuse it. */
int Port_Save_PreserveBeforeVaatiProgressRepair(void);
/* Switch profiles only after pending data for the current profile is durable.
 * Returns 0 and retains the current path/state if that flush fails. */
int Port_Save_SetActivePath(const char* path);
const char* Port_Save_GetActivePath(void);
int Port_Save_SaveAsProfile(const char* path);
int Port_Save_ListProfiles(char out[][64], int max);
int Port_Save_FilenameMax(void);
int Port_Save_DeleteProfile(const char* path);
int Port_Save_RenameProfile(const char* oldPath, const char* newPath);
void Port_Save_GetStats(PortSaveStats* stats);
const char* Port_Save_StageName(PortSaveStage stage);
int Port_Save_ClearActiveProfileData(void);

#ifdef PORT_SAVE_TEST
/* Deterministic fault injection used by integration tests.  These hooks are
 * absent from production builds and each request is consumed once. */
void Port_Save_TestFailNextPreserve(void);
void Port_Save_TestFailNextAtomicWrite(void);
void Port_Save_TestFailNextEepromWriteAtBlock(uint16_t block);
#endif

#ifdef __cplusplus
}
#endif

#endif
