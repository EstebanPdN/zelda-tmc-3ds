#ifndef HOLEMANAGER_H
#define HOLEMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    s16 x;
    s16 y;
    u16 width;
    u16 height;
    u8 unk_28[8];
    u16 persistance_x;
    u16 persistance_y;
    u16 persistance_offset_x;
    u16 persistance_offset_y;
    u8 unk_38[7];
    u8 unk_3f;
} HoleManager;

#endif // HOLEMANAGER_H
