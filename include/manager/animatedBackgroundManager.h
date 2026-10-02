#ifndef ANIMATEDBACKGROUNDMANAGER_H
#define ANIMATEDBACKGROUNDMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
} AnimatedBackgroundManager;

void AnimatedBackgroundManager_RestoreBgGfx(AnimatedBackgroundManager*);

#endif // ANIMATEDBACKGROUNDMANAGER_H
