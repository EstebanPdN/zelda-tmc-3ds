#ifndef PORT_DUMP_STATE_H
#define PORT_DUMP_STATE_H

#include <stdbool.h>

#include "save.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_DUMP_LOAD_STATE_FILENAME "load-state.bin"
#define PORT_DUMP_LEGACY_STATE_FILENAME "save-state.bin"

typedef enum PortDumpStateResult {
    PORT_DUMP_STATE_OK = 0,
    PORT_DUMP_STATE_OK_LEGACY,
    PORT_DUMP_STATE_NO_DUMP,
    PORT_DUMP_STATE_NO_STATE,
    PORT_DUMP_STATE_INVALID,
    PORT_DUMP_STATE_WRONG_REGION,
    PORT_DUMP_STATE_IO_ERROR,
} PortDumpStateResult;

typedef struct PortDumpStateData {
    SaveFile save;
    bool exactPosition;
} PortDumpStateData;

bool Port_DumpState_WriteFile(const char* path, int region, const SaveFile* save);
PortDumpStateResult Port_DumpState_ReadLatest(const char* dumpsDirectory, int activeRegion,
                                              PortDumpStateData* out);
const char* Port_DumpState_ResultLabel(PortDumpStateResult result);

#ifdef __cplusplus
}
#endif

#endif // PORT_DUMP_STATE_H
