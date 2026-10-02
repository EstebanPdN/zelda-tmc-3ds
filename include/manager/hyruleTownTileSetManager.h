#ifndef HYRULETOWNTILESETMANAGER_H
#define HYRULETOWNTILESETMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 gfxGroup0;
    u8 gfxGroup1;
    u8 gfxGroup2;
} HyruleTownTileSetManager;

extern void TryLoadPrologueHyruleTown(void);

#endif // HYRULETOWNTILESETMANAGER_H
