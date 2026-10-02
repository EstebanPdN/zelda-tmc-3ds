/**
 * @file tileChangeObserveManager.c
 * @ingroup Managers
 *
 * @brief Loads entity lists when a tile changes
 *
 * e.g. Puffstools in DWS if you suck away the dirt
 * spawns the red portal in FoW if you move the stone
 */
#include "manager/tileChangeObserveManager.h"
#include "flags.h"
#include "room.h"
#include "map.h"
#ifdef PC_PORT
#include <stdint.h>
#endif

#ifdef PC_PORT
#define TCOM_TILEPOS(this) (*(u16*)((u8*)(this) + sizeof(Manager) + 0x18))
#define TCOM_LAYER(this) (*(u16*)((u8*)(this) + sizeof(Manager) + 0x1A))
#define TCOM_FLAG(this) (*(u16*)((u8*)(this) + sizeof(Manager) + 0x1E))
#else
#define TCOM_TILEPOS(this) ((this)->tilePos)
#define TCOM_LAYER(this) ((this)->field_0x3a)
#define TCOM_FLAG(this) ((this)->flag)
#endif

void TileChangeObserveManager_Init(TileChangeObserveManager*);
void TileChangeObserveManager_Action1(TileChangeObserveManager*);
void TileChangeObserveManager_Action2(TileChangeObserveManager*);

void TileChangeObserveManager_Main(TileChangeObserveManager* this) {
    static void (*const TileChangeObserveManager_Actions[])(TileChangeObserveManager*) = {
        TileChangeObserveManager_Init,
        TileChangeObserveManager_Action1,
        TileChangeObserveManager_Action2,
    };
    TileChangeObserveManager_Actions[super->action](this);
}

void TileChangeObserveManager_Init(TileChangeObserveManager* this) {
    u16* tileIndex;
    if (CheckFlags(TCOM_FLAG(this)) != 0) {
        DeleteThisEntity();
    } else {
        super->action = 1;
        tileIndex = &GetLayerByIndex(TCOM_LAYER(this))->mapData[TCOM_TILEPOS(this)];
        this->observedTile = tileIndex;
        this->initialTile = tileIndex[0];
    }
}

void TileChangeObserveManager_Action1(TileChangeObserveManager* this) {
    if (this->initialTile != this->observedTile[0]) {
        super->action++;
        super->timer = 15;
    }
}

void TileChangeObserveManager_Action2(TileChangeObserveManager* this) {
    if (--super->timer == 0) {
        SetFlag(TCOM_FLAG(this));
        if (super->type != 0) {
            LoadRoomEntityList((EntityData*)GetCurrentRoomProperty(super->type));
        }
        DeleteManager(super);
    }
}
