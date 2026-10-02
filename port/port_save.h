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
void Port_Save_GetStats(PortSaveStats* stats);
const char* Port_Save_StageName(PortSaveStage stage);

#ifdef __cplusplus
}
#endif

#endif
