#ifndef PORT_DUMP_STATE_3DS_H
#define PORT_DUMP_STATE_3DS_H

#include <stdbool.h>

#include "port_dump_state.h"

bool Port_DumpState3DS_Write(const char* dumpDirectory);
PortDumpStateResult Port_DumpState3DS_LoadLatest(void);

#endif // PORT_DUMP_STATE_3DS_H
