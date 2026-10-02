#ifndef TEMPLEOFDROPLETSMANAGER_H
#define TEMPLEOFDROPLETSMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 unk_20;
    u8 unk_21;
    u8 unk_22;
    u8 unk_23; // used
    u16 unk_24;
    u16 unk_26;
    void* unk_28;
    u16 unk_2c;
    u8 unk_2e[0x6];
    s16 unk_34;
    s16 unk_36;
    s16 unk_38;
    s16 unk_3a;
    u16 flag;
    u16 localFlag; // used
} TempleOfDropletsManager;

/* Issue #75: room.c::LoadRoomEntity hand-patches these fields after the
 * generic MemCopy lands them in the wrong slots on PC (the spawn copy
 * dest is sizeof(Manager)+0x10, but TempleOfDropletsManager's void*
 * unk_28 adds an extra 4-byte shift on top of Manager's own 0x18-byte
 * shift). Lock these offsets so the patch and the struct layout stay
 * in sync if either is touched. */
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, unk_34, 0x34, 0x50, "TempleOfDropletsManager unk_34 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, unk_36, 0x36, 0x52, "TempleOfDropletsManager unk_36 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, unk_38, 0x38, 0x54, "TempleOfDropletsManager unk_38 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, unk_3a, 0x3a, 0x56, "TempleOfDropletsManager unk_3a offset incorrect");
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, flag,   0x3c, 0x58, "TempleOfDropletsManager flag offset incorrect");
PORT_STATIC_ASSERT_OFFSET(TempleOfDropletsManager, localFlag, 0x3e, 0x5a, "TempleOfDropletsManager localFlag offset incorrect");

#endif // TEMPLEOFDROPLETSMANAGER_H
