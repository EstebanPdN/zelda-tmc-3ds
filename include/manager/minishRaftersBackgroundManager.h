#ifndef MINISHRAFTERSBACKGROUNDMANAGER_H
#define MINISHRAFTERSBACKGROUNDMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 unk_00[0x1C];
    u32 unk_3c;
} MinishRaftersBackgroundManager;

extern void sub_08058324(u32);

#endif // MINISHRAFTERSBACKGROUNDMANAGER_H
