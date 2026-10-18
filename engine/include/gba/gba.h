#ifndef GBA_H
#define GBA_H

#include "defines.h"
#include "io_reg.h"
#include "isagbprint.h"
#include "macro.h"
#include "multi_boot.h"
#ifdef PC_PORT
#include "port/port_gba_mem.h"
#if defined(JP)
#include "port/port_offset_JP.h"
#elif defined(EU)
#include "port/port_offset_EU.h"
#else
#include "port/port_offset_USA.h"
#endif
#endif
#include "syscall.h"
#include "types.h"
#include <string.h>

#endif // GBA_H
