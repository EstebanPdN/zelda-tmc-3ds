#ifndef STATICBACKGROUNDMANAGER_H
#define STATICBACKGROUNDMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u32 field_0x20;
} StaticBackgroundManager;

extern void LoadStaticBackground(u32);

#endif // STATICBACKGROUNDMANAGER_H
