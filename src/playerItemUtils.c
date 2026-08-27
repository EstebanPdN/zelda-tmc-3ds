#include "common.h"
#include "entity.h"
#include "flags.h"
#include "item.h"
#include "message.h"
#include "object.h"
#include "room.h"
#include "player.h"
#include "save.h"
#include "sound.h"
#ifdef PC_PORT
#include "port_bottle_compat.h"
#endif

static Entity* GiveItemWithCutscene(u32, u32, u32);
static bool32 CreateItemEntityInternal(u32, u32, u32, u16);
static void InitTileMessage(u32, u32);

void SetPlayerItemGetState(Entity*, u8, u8);

void CreateItemEntity(u32 type, u32 type2, u32 delay) {
    (void)CreateItemEntityInternal(type, type2, delay, 0);
}

bool32 CreateItemEntityWithFlag(u32 type, u32 type2, u32 delay, u16 completionFlag) {
    return CreateItemEntityInternal(type, type2, delay, completionFlag);
}

static bool32 CreateItemEntityInternal(u32 type, u32 type2, u32 delay, u16 completionFlag) {
    Entity* e = GiveItemWithCutscene(type, type2, delay);
    if (e == NULL) {
        return FALSE;
    }

    e->parent = CreateLinkAnimation(e, e->type, 0);
    if (e->parent == NULL) {
        DeleteEntity(e);
        return FALSE;
    }

    ((GenericEntity*)e)->field_0x6a.HWORD = completionFlag;
    return TRUE;
}

void InitItemGetSequence(u32 type, u32 type2, u32 delay) {
    Entity* e = GiveItemWithCutscene(type, type2, delay);
    if (e != NULL) {
        e->parent = &gPlayerEntity.base;
        SetPlayerItemGetState(e, e->type, 0);
    }
}

#ifdef PC_PORT
#include <stdbool.h>
/* Generic randomizer hook — called at the centralized point where any
 * item entity gets spawned (chests, NPC gifts, drops, cutscenes...).
 * Location-specific hooks run at source call sites when an external
 * .logic file supplies address keys. */
extern bool Rando_OverrideItem(u8* type, u8* subtype);
extern bool Rando_OverrideLocationKey(u32 location_key, u8* type, u8* subtype);
#endif

static Entity* GiveItemWithCutscene(u32 item, u32 type2, u32 delay) {
    Entity* e;
    if (item == ITEM_SHELLS && gSave.stats.hasAllFigurines) {
        item = ITEM_RUPEE50;
        type2 = 0;
    }
#ifdef PC_PORT
    {
        u8 t = (u8)item, s = (u8)type2;
        if (Rando_OverrideItem(&t, &s)) {
            item = t;
            type2 = s;
        }
    }
#endif
    e = CreateAuxPlayerEntity();
    if (e != NULL) {
        e->type = item;
        e->type2 = type2;
        e->timer = delay;
        e->id = LINK_HOLDING_ITEM;
        e->kind = OBJECT;
        AppendEntityToList(e, 6);
    }
    return e;
}

void ClearSmallChests(void) {
    MemClear(gSmallChests, sizeof(gSmallChests));
}

void OpenSmallChest(u32 pos, u32 layer) {
    TileEntity* t = gSmallChests;
    u32 found = 0;
    u32 i;
    for (i = 0; i < 8; ++i, ++t) {
        if (*(u16*)&t->tilePos == pos) {
            found = 1;
            break;
        }
    }
    if ((layer >> 1) == ((u32)(t->_6 << 31) >> 31)) {
        if (found) {
            {
                u8 item = t->_2;
                u8 subtype = t->_3;
#ifdef PC_PORT
                int ci = Rando_RoomChestIndex(gRoomControls.area, gRoomControls.room, t->localFlag);
                if (ci >= 0) {
                    u32 key = ((u32)gRoomControls.area << 16) | ((u32)gRoomControls.room << 8) | (u32)ci;
                    (void)Rando_OverrideLocationKey(key, &item, &subtype);
                }
#endif
#ifdef PC_PORT
                if (!Port_BottleRewardCanBeCollected(&gSave, item)) {
                    SoundReq(SFX_MENU_ERROR);
                    return;
                }
#endif
                SetLocalFlag(t->localFlag);
                CreateItemEntity(item, subtype, 0);
            }
        } else {
            CreateItemEntity(ITEM_FAIRY, 0, 0);
        }
        sub_0807B7D8(0x74, pos, layer);
        RequestPriorityDuration(NULL, 120);
        SoundReq(SFX_CHEST_OPEN);
    }
}

u32 sub_080A7CFC(u32 a1) {
    u32 msg = TEXT_INDEX(TEXT_LOCATIONS, 0x0);
    bool32 hint = FALSE;

    TileEntity* t = GetCurrentRoomProperty(3);
    if (t != 0) {
        do {
            if (t->tilePos == a1) {
                switch (t->type) {
                    case SIGN:
                        hint = FALSE;
                        msg = *(u16*)&t->_6;
                        break;
                    case TILE_EZLO_HINT:
                        hint = TRUE;
                        msg = *(u16*)&t->_6;
                        break;
                }
                break;
            }
            t++;
        } while (t->tilePos != 0);
    }
    InitTileMessage(msg, hint);
}

static void InitTileMessage(u32 msg, u32 hint) {
    if (hint) {
        CreateEzloHint(msg, 0);
    } else {
        // Read sign text
        MessageFromTarget(msg);
    }
}
