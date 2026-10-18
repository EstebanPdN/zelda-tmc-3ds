#ifndef MOVEABLEOBJECTMANAGER_H
#define MOVEABLEOBJECTMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 unk_20[0x12];
    u16 unk_32;
    u8 unk_34[2];
    u8 unk_36;
    u8 unk_37;
    u16 unk_38;
    u16 unk_3a;
    u16 unk_3c;
    u16 flags;
} MoveableObjectManager;

PORT_STATIC_ASSERT_SIZE(MoveableObjectManager, 0x40, 0x58, "MoveableObjectManager size incorrect");
PORT_STATIC_ASSERT_OFFSET(MoveableObjectManager, unk_36, 0x36, 0x4e, "MoveableObjectManager unk_36 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(MoveableObjectManager, unk_38, 0x38, 0x50, "MoveableObjectManager unk_38 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(MoveableObjectManager, unk_3c, 0x3c, 0x54, "MoveableObjectManager unk_3c offset incorrect");
PORT_STATIC_ASSERT_OFFSET(MoveableObjectManager, flags, 0x3e, 0x56, "MoveableObjectManager flags offset incorrect");

#endif // MOVEABLEOBJECTMANAGER_H
