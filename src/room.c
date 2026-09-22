// TODO: original name is probably floor.c

#include "room.h"
#include "area.h"
#include "common.h"
#include "enemy.h"
#include "effects.h"
#include "flags.h"
#include "script.h"
#include "game.h"
#include "manager/bombableWallManager.h"
#include "manager/templeOfDropletsManager.h"
#include "manager/angryStatueManager.h"
#include "map.h"
#include "object.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port_rom.h"
#include "port/port_generic_entity.h"
#include <stdio.h>
#else
#define GE_FIELD(ent, fname) (&((GenericEntity*)(ent))->fname)
#endif

static void sub_0804B058(EntityData* dat);
extern void sub_0801AC98(void);
extern u32 EnemyEnableRespawn(u32);

extern void** gCurrentRoomProperties;
extern void*** gAreaTable[];
#ifdef PC_PORT
extern void* Port_ReadPackedRomPtr(const void* base, u32 index);
extern void* Port_GetRoomFuncProp(u32 area, u32 room, u32 prop_idx);
#endif
extern u8 gEntityListLUT[];

extern void sub_080186EC(void);
extern void sub_0804B16C(void);
extern void ClearSmallChests(void);
extern Entity* GetEmptyEntityByKind(u32 kind);

void RegisterRoomEntity(Entity*, const EntityData*);
void sub_0804AF0C(Entity*, const EntityData*);
void sub_0804AFB0(void** properties);

void sub_08054524(void);
void sub_0806F704(Entity*, u32);

void sub_0805BB00(u32, u32);

static void LoadRoomVisitTile(TileEntity*);
static void LoadSmallChestTile(TileEntity*);
static void LoadBombableWallTile(TileEntity*);
static void LoadDarknessTile(TileEntity*);
static void LoadDestructibleTile(TileEntity*);
static void LoadGrassDropTile(TileEntity*);
static void LoadLocationTile(TileEntity*);

void LoadRoomEntityList(const EntityData* listPtr) {
    if (listPtr != NULL) {
        while (listPtr->kind != 0xFF) {
            LoadRoomEntity(listPtr++);
        }
    }
}

Entity* LoadRoomEntity(const EntityData* dat) {
    int kind;
    Entity* entity;

// r4/r5 regalloc
#ifndef NON_MATCHING
    asm("" ::: "r5");
#endif

    kind = dat->kind & 0xF;
    if ((dat->flags & 0xF0) == 0x50 && DeepFindEntityByID(kind, dat->id))
        return NULL;
    entity = GetEmptyEntityByKind(kind);
    if (entity != NULL) {
        entity->kind = kind;
        entity->id = dat->id;
        entity->type = dat->type;
        RegisterRoomEntity(entity, dat);
#if PORT_POINTERS_ARE_64_BIT
        /* Issue #75 — TempleOfDropletsManager fields after Manager.base
         * get mis-mapped on 64-bit hosts by the manager-spawn MemCopy in
         * RegisterRoomEntity. Done AFTER RegisterRoomEntity so the
         * MemCopy doesn't clobber our writes.
         *
         * On GBA, the manager spawn copies the 16-byte EntityData onto
         * the pool slot at offset 0x30 (= sizeof(Manager_GBA) + 0x10).
         * That lands type2 in unk_34, xPos in unk_38, yPos in unk_3a,
         * spritePtr halves in flag/localFlag — the layout the manager's
         * update logic expects.
         *
         * On 64-bit hosts, Manager grew from 0x20 to 0x38 (prev/next/parent/child
         * widened 4→8) AND TempleOfDropletsManager's own void* unk_28
         * grew 4→8. The RegisterRoomEntity PC fork copies to
         * sizeof(Manager_PC) + 0x10 = 0x48, but the additional 4-byte
         * shift inside the struct (from unk_28's widening) means
         * EntityData fields land 4 bytes earlier than the manager logic
         * expects:
         *
         *     EntityData → GBA dest field   → PC actual write site
         *     type2      → unk_34/unk_36    → unk_2e[2..5]  (junked)
         *     xPos       → unk_38           → unk_34        (wrong)
         *     yPos       → unk_3a           → unk_36        (wrong)
         *     spritePtr  → flag/localFlag   → unk_38/unk_3a (wrong)
         *
         * unk_34/unk_36 feed bg3.xOffset/yOffset in sub_0805A94C — so
         * when xPos (e.g. 0x88) lands in unk_34 instead of paramB's
         * signed offset (e.g. -8), the bg3 scroll falls hundreds of
         * pixels off and BG3 reads from an empty region of the
         * tilemap. Visible symptom: WIN1 is sized and positioned
         * correctly, but bg_layers[3] is transparent everywhere inside
         * it → no sunbeam.
         *
         * Fix: re-write each of the six fields from the source
         * EntityData at the right 64-bit offset. Gated on id == 0x15 (=
         * TempleOfDropletsManager) so other managers using the same
         * MemCopy aren't disturbed. */
        if (kind == 9 && dat->id == 0x15) {
            TempleOfDropletsManager* mgr = (TempleOfDropletsManager*)entity;
            /* unk_34 / unk_36 — bg3 scroll-offset corrections, from
             * paramB (= type2). Encoded as a packed pair of s16. */
            mgr->unk_34 = (s16)(dat->type2 & 0xFFFFu);
            mgr->unk_36 = (s16)(dat->type2 >> 16);
            /* unk_38 / unk_3a — beam world position from xPos/yPos.
             * sub_0805A4CC uses these to place the spawned LightRay. */
            mgr->unk_38 = (s16)dat->xPos;
            mgr->unk_3a = (s16)dat->yPos;
            /* flag / localFlag — puzzle-state flags from spritePtr
             * halves. The lever-push → flag-set → manager-checks-flag
             * chain depends on these matching the data. */
            mgr->flag = (u16)(dat->spritePtr & 0xFFFFu);
            mgr->localFlag = (u16)(dat->spritePtr >> 16);
        }
        /* #77: same RegisterRoomEntity MemCopy mis-map as #75 above. On GBA the
         * 16-byte EntityData lands at manager 0x30, putting spritePtr at
         * 0x3c-0x3f so the AngryStatueManager completion flag field_0x3e
         * (0x3e = paramC>>16) is populated. On PC the copy lands at 0x48
         * (Manager grew 0x20->0x38 AND field_0x20[4] grew 0x10->0x20), leaving
         * field_0x3e (0x66) zero — so AngryStatueManager_Action2's
         * SetFlag(field_0x3e) becomes SetFlag(0) on completion and destroying
         * all four statues never sets the reward flag (the pillars never drop).
         * field_0x20[]/field_0x36 are re-initialised by AngryStatueManager_Init,
         * so only the flag needs restoring here. */
        if (kind == 9 && dat->id == ANGRY_STATUE_MANAGER) {
            ((AngryStatueManager*)entity)->field_0x3e = (u16)(dat->spritePtr >> 16);
        }
#endif
        if ((dat->flags & 0xF0) != 16) {
            u8 kind2;
            entity->type2 = *(u8*)&dat->type2;
            entity->timer = (dat->type2 & 0xFF00) >> 8;
            if (kind == 9)
                return entity;
            sub_0804AF0C(entity, dat);
            if (!entity->next)
                return entity;
            kind2 = dat->kind & 0xF0;
            if ((kind2 & 0x10) == 0) {
                if ((kind2 & 0x20) != 0) {
                    entity->collisionLayer = 2;
                    return entity;
                }
            }

            if ((kind2 & 0x10) || (gRoomControls.scroll_flags & 2)) {
                entity->collisionLayer = 1;
                return entity;
            }

            ResolveCollisionLayer(entity);
            return entity;
        }
    }
    return entity;
}

void RegisterRoomEntity(Entity* ent, const EntityData* dat) {
    u32 list;
    u32 kind;

    list = dat->flags & 0xF;
    kind = dat->kind & 0xF;
    if (ent->prev == NULL) {
        if (list == 0xF) {
            AppendEntityToList(ent, gEntityListLUT[kind]);
        } else if (list == 8) {
            AppendEntityToList(ent, 8);
        } else {
            AppendEntityToList(ent, list);
        }
    }
    if (kind == MANAGER) {
#if PORT_POINTERS_ARE_64_BIT
        MemCopy(dat, (u8*)ent + sizeof(Manager) + 0x10, sizeof(EntityData));
#else
        MemCopy(dat, &ent->y, sizeof(EntityData));
#endif
    } else {
        /*
         * On 64-bit PC, entity subtypes (Enemy, Player) have pointer fields
         * that shift the extra area layout relative to GenericEntity.
         * Use GE_FIELD accessor to compute correct byte offset per entity kind.
         */
        u32 spritePtr = dat->spritePtr;

        GE_FIELD(ent, field_0x78)->HALF.LO = dat->kind;
        GE_FIELD(ent, field_0x78)->HALF.HI = dat->flags;
        GE_FIELD(ent, field_0x7a)->HALF.LO = dat->id;
        GE_FIELD(ent, field_0x7a)->HALF.HI = dat->type;
        GE_FIELD(ent, field_0x7c)->WORD_U = dat->type2;
        GE_FIELD(ent, field_0x80)->HWORD = dat->xPos;
        GE_FIELD(ent, field_0x82)->HWORD = dat->yPos;
        GE_FIELD(ent, cutsceneBeh)->HWORD = (u16)(spritePtr & 0xFFFF);
        GE_FIELD(ent, field_0x86)->HWORD = (u16)(spritePtr >> 16);
#if PORT_POINTERS_ARE_64_BIT
        /* GenericEntity's cutsceneBeh/field_0x86 union is 8-byte aligned,
         * so it sits at 64-bit offset 0xB0/0xB2 — but most entity subclass
         * structs (WarpPointEntity, HeartContainerEntity, GentariCurtain,
         * lots of others) lay out a `flag` field at GBA 0x86 without
         * the void* trick, landing at PC 0xAE. Mirror the spritePtr
         * halves to those natural-aligned bytes too so subclass reads
         * pick up the right value. The mirrored bytes lie in
         * the 64-bit GenericEntity's pre-union padding (0xAC-0xAF), so cutscene
         * entities are unaffected — StartCutscene's later 8-byte
         * scriptContext write at 0xB0 supersedes it. */
        if (kind != ENEMY) {
            *(u16*)((u8*)ent + 0xAC) = (u16)(spritePtr & 0xFFFF);
            *(u16*)((u8*)ent + 0xAE) = (u16)(spritePtr >> 16);
        }
#endif
    }
}

void sub_0804AF0C(Entity* ent, const EntityData* dat) {
    switch (dat->flags & 0xf0) {
        case 0x0:
            ent->x.HALF.HI = dat->xPos + gRoomControls.origin_x;
            ent->y.HALF.HI = dat->yPos + gRoomControls.origin_y;
            break;
        case 0x20:
            ((Enemy*)ent)->enemyFlags |= EM_FLAG_CAPTAIN;
            ent->x.HALF.HI = dat->xPos + gRoomControls.origin_x;
            ent->y.HALF.HI = dat->yPos + gRoomControls.origin_y;
            break;
        case 0x40:
            ent->x.HALF.HI = dat->xPos + gRoomControls.origin_x;
            ent->y.HALF.HI = dat->yPos + gRoomControls.origin_y;
#ifdef PC_PORT
            {
                /* dat may be a compiled table (baked USA script addr) or a
                 * ROM-resolved entity list (region-native addr) — resolve by
                 * provenance so native EU/JP addrs skip the USA-key lookup. */
                void* resolved = Port_ResolveEntityScript(dat, dat->spritePtr);
                ScriptExecutionContext* ctx = StartCutscene(ent, (u16*)resolved);
                if (!ctx)
                    DeleteEntity(ent);
            }
#else
            if (!StartCutscene(ent, (u16*)dat->spritePtr))
                DeleteEntity(ent);
#endif
            break;
    }
}

void sub_0804AF90(void) {
    sub_0804AFB0(gArea.pCurrentRoomInfo->properties);
    ClearSmallChests();
}

#ifdef PC_PORT
static void** GetAreaRoomPropertyList(u32 area, u32 room) {
    void*** areaTable = gAreaTable[area];
    const u8* ptr = (const u8*)areaTable;
    bool32 inRom = FALSE;
    bool32 readable = FALSE;

    if (areaTable == NULL) {
        Port_RefreshAreaData(area);
        areaTable = gAreaTable[area];
        ptr = (const u8*)areaTable;
        if (areaTable == NULL) {
            return (void**)Port_ResolveAreaPropertiesFromRom(area, room);
        }
    }

    /* Sanity check: architecture-agnostic. */
    {
        inRom = (gRomData != NULL && ptr >= gRomData && ptr < gRomData + gRomSize);
        readable = Port_IsAreaTablePtrReadable(area, areaTable);
        if (!readable) {
            Port_RefreshAreaData(area);
            areaTable = gAreaTable[area];
            ptr = (const u8*)areaTable;
            if (areaTable == NULL) {
                return (void**)Port_ResolveAreaPropertiesFromRom(area, room);
            }
            inRom = (gRomData != NULL && ptr >= gRomData && ptr < gRomData + gRomSize);
            readable = Port_IsAreaTablePtrReadable(area, areaTable);
            if (!readable) {
                return (void**)Port_ResolveAreaPropertiesFromRom(area, room);
            }
        }
        if (inRom) {
            void** result = Port_ReadPackedRomPtr(areaTable, room);
            return result;
        }
    }

    {
        /* Host-side tables (asset-cache slot vectors, port_rom.c shadow
         * arrays) hold at least MAX_ROOMS slots — same bound as the
         * ReadAreaSubTableEntry fix in gameUtils.c; junk indices fall back
         * to the ROM resolver, which bounds-checks against gRomSize. */
        void** result = room < MAX_ROOMS ? areaTable[room] : NULL;
        return result != NULL ? result : (void**)Port_ResolveAreaPropertiesFromRom(area, room);
    }
}

static bool32 IsRoomPropertyListInRom(void** properties) {
    const u8* ptr = (const u8*)properties;

    if (properties == NULL || gRomData == NULL) {
        return FALSE;
    }

    return ptr >= gRomData && ptr < gRomData + gRomSize;
}
#endif

void sub_0804AFB0(void** properties) {
    u32 i;

    gCurrentRoomProperties = properties;
    if (properties == NULL) {
        for (i = 0; i < 8; ++i) {
            gRoomVars.properties[i] = NULL;
        }
        return;
    }

    for (i = 0; i < 8; ++i) {
#ifdef PC_PORT
        void* val = NULL;
        if (i >= 4) {
            /* Properties 4..7 are usually room callback functions (init
             * / enter / update / exit). The port keeps those in a
             * hand-built function table because raw GBA function pointers
             * cannot survive ROM relocation. But some rooms put DATA
             * pointers here too — e.g. Minish Forest lily pads index a rail
             * array via type2 in 4..7. Try the function table first, then
             * fall back to packed ROM reads so non-callback rooms still get
             * data. */
            val = Port_GetRoomFuncProp(gRoomControls.area, gRoomControls.room, i);
        }
        if (val == NULL) {
            val = IsRoomPropertyListInRom(properties) ? Port_ReadPackedRomPtr(properties, i) : properties[i];
        }
        gRoomVars.properties[i] = val;
#else
        gRoomVars.properties[i] = gCurrentRoomProperties[i];
#endif
    }
}

u32 CallRoomProp6(void) {
    u32 result;
    u32 (*func)(void);

    result = 1;
    func = (u32 (*)())GetCurrentRoomProperty(6);
    if (func != NULL)
        result = func();
    return result;
}

void CallRoomProp5And7(void) {
    void (*func)(void);

    sub_080186EC();
    func = (void (*)())GetCurrentRoomProperty(5);
    if (func) {
        func();
    }
    func = (void (*)())GetCurrentRoomProperty(7);
    if (func) {
        func();
    }
    sub_0804B16C();
}

void LoadRoom(void) {
    LoadRoomEntityList(GetCurrentRoomProperty(1));
    LoadRoomEntityList(GetCurrentRoomProperty(0));

    if (CheckGlobalFlag(TABIDACHI)) {
        sub_0804B058(GetCurrentRoomProperty(2));
    }

    LoadRoomTileEntities(GetCurrentRoomProperty(3));
    sub_0801AC98();
}

static void sub_0804B058(EntityData* dat) {
    Entity* ent;
    u32 uVar2;

    if ((dat != NULL) && dat->kind != 0xff) {
        uVar2 = 0;
        do {
            if ((uVar2 < 0x20) && ((dat->kind & 0xF) == 3)) {
                if (EnemyEnableRespawn(uVar2) != 0) {
                    ent = LoadRoomEntity(dat);
                    if ((ent != NULL) && (ent->kind == ENEMY)) {
                        ((Enemy*)ent)->idx = uVar2 | 0x80; // TODO Set the room tracker flag that can be set by the
                                                           // enemy so it does not appear next time the room is visited?
                    }
                }
            } else {
                LoadRoomEntity(dat);
            }
            uVar2++;
            dat++;
        } while (dat->kind != 0xff);
    }
}

void sub_0804B0B0(u32 area, u32 room) {
    LoadRoomEntityList(GetRoomProperty(area, room, 1));
}

void SetCurrentRoomPropertyList(u32 area, u32 room) {
    gCurrentRoomProperties = NULL;
    if (gAreaTable[area] != NULL) {
#ifdef PC_PORT
        gCurrentRoomProperties = GetAreaRoomPropertyList(area, room);
        /* Refresh the gRoomVars.properties[0..7] cache from the new
         * property list. Without this, GetCurrentRoomProperty(idx<=7)
         * keeps returning the PREVIOUS room's cached values — and
         * LoadRoomEntityList iterates that stale pointer as if it
         * were EntityData[], walking off the end into .bss garbage
         * (e.g. sEntityUpdateJmpBuf) and writing garbage `next`
         * pointers into freshly-spawned entities. Repro: ocarina
         * fast-travel from Wind Ruins to Lake Hylia, which routes
         * through subtaskWorldEvent.c → SetCurrentRoomPropertyList
         * without going through the normal area-change path that
         * already calls sub_0804AFB0 in LoadRoom/init.
         *
         * Use the (area, room) ARGUMENTS rather than gRoomControls —
         * the caller (sub_08054974) sets gRoomControls AFTER us, so
         * gRoomControls still names the source area at this point.
         * Routing through sub_0804AFB0 directly would cache the source
         * area's room callbacks (init/enter/update/exit) under the
         * destination's property indices, leaving the destination's
         * tree-portal / ocarina / interaction handlers inactive even
         * after the warp visually completes. */
        if (gCurrentRoomProperties != NULL) {
            u32 i;
            for (i = 0; i < 8; ++i) {
                void* val = NULL;
                if (i >= 4) {
                    val = Port_GetRoomFuncProp(area, room, i);
                }
                if (val == NULL) {
                    val = IsRoomPropertyListInRom(gCurrentRoomProperties)
                              ? Port_ReadPackedRomPtr(gCurrentRoomProperties, i)
                              : gCurrentRoomProperties[i];
                }
                gRoomVars.properties[i] = val;
            }
        }
#else
        gCurrentRoomProperties = gAreaTable[area][room];
#endif
    }
}

void sub_0804B0E8(u32 area, u32 room) {
    void (*func)(void);

    // init function at index 4 of room data
    func = (void (*)())GetRoomProperty(area, room, 4);
    if (func != NULL) {
        func();
    }
}

void* GetRoomProperty(u32 area, u32 room, u32 property) {
    void** temp;
    temp = NULL;
    if (gAreaTable[area] != NULL) {
#ifdef PC_PORT
        temp = GetAreaRoomPropertyList(area, room);
#else
        temp = gAreaTable[area][room];
#endif
        if (temp != NULL) {
#ifdef PC_PORT
            void* val = NULL;
            if (property >= 4 && property <= 7) {
                val = Port_GetRoomFuncProp(area, room, property);
            }
            if (val == NULL) {
                val = IsRoomPropertyListInRom(temp) ? Port_ReadPackedRomPtr(temp, property) : temp[property];
            }
            return val;
#else
            temp = temp[property];
#endif
        }
    }
    return temp;
}

#ifdef PC_PORT
/* Randomizer chest identity: the `.logic` format addresses chests as
 * area-room-index, where index is the 0-based position of the chest among the
 * room's SMALL_CHEST/BIG_CHEST TileEntities (in property-list order). Map a
 * chest's localFlag to that index so the reward hooks build a matching key.
 * Returns -1 if not found. */
int Rando_RoomChestIndex(u32 area, u32 room, u32 localFlag) {
    TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
    int index = 0;
    if (te == NULL)
        return -1;
    for (int i = 0; i < 256 && te[i].type != 0; ++i) {
        if (te[i].type != SMALL_CHEST && te[i].type != BIG_CHEST)
            continue;
        if (te[i].localFlag == localFlag)
            return index;
        index++;
    }
    return -1;
}
#endif

void* GetCurrentRoomProperty(u32 idx) {
    if (gCurrentRoomProperties == NULL)
        return NULL;

    if (idx >= 0x80) { // TODO different kind of room properties?
        return gRoomVars.entityRails[idx & 7];
    } else if (idx <= 7) {
        return gRoomVars.properties[idx];
    } else {
#ifdef PC_PORT
        /* Native (asset-cache) property lists hold max(N,64) slots; idx is an
         * entity-data byte that can reach 0x7F. ROM lists are bounds-checked
         * by Port_ReadPackedRomPtr; bound the host read the same way. */
        if (!IsRoomPropertyListInRom(gCurrentRoomProperties)) {
            return idx < MAX_ROOMS ? gCurrentRoomProperties[idx] : NULL;
        }
        return Port_ReadPackedRomPtr(gCurrentRoomProperties, idx);
#else
        return gCurrentRoomProperties[idx];
#endif
    }
}

void sub_0804B16C(void) {
    TileEntity* tileEntity = gSmallChests;
    do {
        if (tileEntity->tilePos != 0 && CheckLocalFlag(tileEntity->localFlag)) {
            SetTileType(TILE_TYPE_116, tileEntity->tilePos, tileEntity->_6 & 1 ? LAYER_TOP : LAYER_BOTTOM);
        }
    } while (++tileEntity < gSmallChests + 8);
}

void LoadRoomTileEntities(TileEntity* list) {
    TileEntity* t = list;

    if (t == NULL)
        return;

    for (t; t->type != 0; ++t) {
        switch (t->type) {
            case ROOM_VISIT_MARKER:
                LoadRoomVisitTile(t);
                break;
            case SMALL_CHEST:
                LoadSmallChestTile(t);
                break;
            case BOMBABLE_WALL:
                LoadBombableWallTile(t);
                break;
            case MUSIC_SETTER:
                gArea.queued_bgm = t->_3;
                break;
            case DARKNESS:
#ifdef PC_PORT
                fprintf(stderr, "[ROOM] DARKNESS area=%u room=%u light=%u tilePos=0x%03X layer=%u\n",
                        gRoomControls.area, gRoomControls.room, t->_3, t->tilePos, t->_2);
#endif
                LoadDarknessTile(t);
                break;
            case DESTRUCTIBLE_TILE:
                LoadDestructibleTile(t);
                break;
            case GRASS_DROP_CHANGER:
                LoadGrassDropTile(t);
                break;
            case LOCATION_CHANGER:
#ifdef PC_PORT
                fprintf(stderr, "[ROOM] LOCATION_CHANGER area=%u room=%u -> location=%u\n", gRoomControls.area,
                        gRoomControls.room, t->localFlag);
#endif
                LoadLocationTile(t);
                break;
            case TILE_ENTITY_D:
                gRoomVars.fight_bgm = t->_3;
                break;
        }
    }
}

static void LoadGrassDropTile(TileEntity* tileEntity) {
    const Droptable* tbl = gAreaDroptables;
    if (REGION_IS_EU) {
        tbl = gAreaDroptables_eu;
    }
    MemCopy(&tbl[tileEntity->localFlag], &gRoomVars.currentAreaDroptable, 0x20);
}

static void LoadLocationTile(TileEntity* tileEntity) {
    gArea.locationIndex = tileEntity->localFlag;
    sub_08054524();
}

static void LoadRoomVisitTile(TileEntity* tileEntity) {
    SetLocalFlag(tileEntity->localFlag);
}

static void LoadSmallChestTile(TileEntity* tileEntity) {
    TileEntity* t = gSmallChests;
    u32 i = 0;
    for (i = 0; i < 8; ++i, ++t) {
        if (!t->tilePos) {
            MemCopy(tileEntity, t, sizeof(TileEntity));
            if ((t->_6 & 1) && (gRoomControls.scroll_flags & 2) && !CheckLocalFlag(t->localFlag)) {
                Entity* e = CreateObject(SPECIAL_CHEST, t->localFlag, 0);
                if (e != NULL) {
                    sub_0806F704(e, t->tilePos);
                }
            }
            return;
        }
    }
}

static void LoadBombableWallTile(TileEntity* tileEntity) {
    BombableWallManager* mgr = (BombableWallManager*)GetEmptyManager();
    if (mgr != NULL) {
        mgr->base.kind = MANAGER;
        mgr->base.id = BOMBABLE_WALL_MANAGER;
        mgr->x = tileEntity->tilePos;
        mgr->y = *(u16*)&tileEntity->_6;
        mgr->layer = tileEntity->_2;
        mgr->flag = tileEntity->localFlag;
        AppendEntityToList((Entity*)mgr, 6);
    }
}

static void LoadDarknessTile(TileEntity* tileEntity) {
    sub_0805BB00(tileEntity->_3, 1);
}

static void LoadDestructibleTile(TileEntity* tileEntity) {
    if (CheckLocalFlag(*(u16*)&tileEntity->_2)) {
        SetTileType(*(u16*)&tileEntity->_6, tileEntity->tilePos, tileEntity->localFlag);
    } else if (!gRoomVars.destructableManagerLoaded) {
        Manager* mgr;
        gRoomVars.destructableManagerLoaded = TRUE;
        mgr = GetEmptyManager();
        if (mgr != NULL) {
            mgr->kind = MANAGER;
            mgr->id = DESTRUCTIBLE_TILE_OBSERVE_MANAGER;
            AppendEntityToList((Entity*)mgr, 6);
        }
    }
}

void sub_0804B388(u32 a1, u32 a2) {
    Entity* e;
    SetTileType(a2 == 1 ? 38 : 52, a1, a2);
    e = CreateObject(SPECIAL_FX, FX_DEATH, 0);
    if (e != NULL) {
        e->collisionLayer = a2;
        sub_0806F704(e, a1);
    }
    ModDungeonKeys(-1);
}

void LoadSmallChestTile2(TileEntity* tileEntity) {
    LoadSmallChestTile(tileEntity);
}
