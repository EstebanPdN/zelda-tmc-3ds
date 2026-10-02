#ifndef ROLLINGBARRELMANAGER_H
#define ROLLINGBARRELMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    s32 unk_20;
    union SplitWord unk_24;
    u32 unk_28;
    u32 unk_2c;
    u8 unk_30[0x10];
} RollingBarrelManager;

PORT_STATIC_ASSERT_SIZE(RollingBarrelManager, 0x40, 0x58, "RollingBarrelManager size incorrect");
PORT_STATIC_ASSERT_OFFSET(RollingBarrelManager, unk_20, 0x20, 0x38, "RollingBarrelManager unk_20 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(RollingBarrelManager, unk_24, 0x24, 0x3c, "RollingBarrelManager unk_24 offset incorrect");

typedef struct {
    u16 unk_0;
    u16 unk_2;
    u16 unk_4;
    u16 unk_6;
} struct_08108228;

#endif // ROLLINGBARRELMANAGER_H
