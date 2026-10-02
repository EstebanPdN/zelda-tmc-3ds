#ifndef VERTICALMINISHPATHBACKGROUNDMANAGER_H
#define VERTICALMINISHPATHBACKGROUNDMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u32 field_0x20[0x6];
    void* field_0x38;
    void* field_0x3c;
} VerticalMinishPathBackgroundManager;

extern void sub_080575C8(u32);
extern void sub_08057688(void);

#endif // VERTICALMINISHPATHBACKGROUNDMANAGER_H
