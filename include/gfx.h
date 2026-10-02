#ifndef GFX_H
#define GFX_H

#include "global.h"

#ifdef PC_PORT
extern const u8* gGlobalGfxAndPalettes;
#else
extern const u8 gGlobalGfxAndPalettes[];
#endif

#endif // GFX_H
