#include "asm.h"
#include "collision.h"
#include "effects.h"
#include "entity.h"
#include "fade.h"
#include "map.h"
#include "menu.h"
#include "object.h"
#include "physics.h"
#include "player.h"
#include "gba/defines.h"
#include "gba/syscall.h"
#include "port_entity_ctx.h"
#include "port_audio_mute.h"
#include "port_gba_mem.h"
#include "room.h"
#include "screen.h"
#include "script.h"
#include "sound.h"
#include "port_debug_verbose.h" /* Port_DebugVerbose — gates per-frame diag logs */

/* execinfo.h ships with glibc and macOS libSystem; MinGW does not have
 * it, so the diagnostic Port_DumpOrchStack() backtrace dump is a no-op
 * on Windows. The cutscene-watchdog fix doesn't depend on it — it's
 * only used to identify the call path that deletes orchestrator
 * entities during #93 debug. */
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__) && !defined(TMC_N64) && \
    !defined(__ANDROID__) && !defined(TMC_3DS)
#include <execinfo.h>
#define PORT_HAVE_EXECINFO 1
#else
#define PORT_HAVE_EXECINFO 0
#endif
#include <stdint.h>
#include <stdlib.h>

extern const u8 gUnk_08007DF4[];
extern const KeyValuePair gUnk_080046A4[];
extern const u16 gUnk_080047F6[];
extern const KeyValuePair gMapActTileToSurfaceType[];
extern u32 CalcDistance(s32 x, s32 y);
extern u32 sub_0806F58C(Entity* a, Entity* b);
extern Entity* gCollidableList[];
extern u8 gCollidableCount;

u8* DoTileInteractionOffset(Entity* entity, u32 interaction, s32 xOffset, s32 yOffset);

extern u8 gUnk_02024048;
u16 gUnk_02021F20[8];

typedef struct {
    u16 flags;
    u8 objectId;
    u8 objectType;
    u16 _pad;
    u16 tileChange;
} TileInteractionDef;

static bool32 FindEntryForKeyInternal(u32 key, const KeyValuePair* list, u16* outValue) {
    if (list == NULL) {
        if (outValue != NULL) {
            *outValue = 0;
        }
        return 0;
    }

    for (; list->key != 0; list++) {
        if (list->key == key) {
            if (outValue != NULL) {
                *outValue = list->value;
            }
            return 1;
        }
    }

    if (outValue != NULL) {
        *outValue = 0;
    }
    return 0;
}

void sub_08000E92(const void* src, void* dest, u32 size) {
    (void)size;
    if (src != NULL && dest != NULL) {
        LZ77UnCompVram(src, dest);
    }
}

u32 sub_08000E44(s32 value) {
    if (value == 0) {
        return 0;
    }
    return (value < 0) ? (u32)-1 : 1;
}

u32 sub_08000E62(u32 value) {
    value = (value & 0x55555555U) + ((value >> 1) & 0x55555555U);
    value = (value & 0x33333333U) + ((value >> 2) & 0x33333333U);
    value = (value & 0x0F0F0F0FU) + ((value >> 4) & 0x0F0F0F0FU);
    value = (value & 0x00FF00FFU) + ((value >> 8) & 0x00FF00FFU);
    value = value + (value << 16);
    return value >> 16;
}

u32 FindValueForKey(u32 key, const KeyValuePair* keyValuePairList) {
    u16 value;
    return FindEntryForKeyInternal(key, keyValuePairList, &value) ? value : 0;
}

void sub_08007DCE(PlayerEntity* player) {
    DoPlayerAction(player);
}

void EnqueueSFX(u32 sfx) {
    /* SoundReq has the same gate, but EnqueueSFX bypasses SoundReq
     * (the queue is flushed each frame by port_draw.c which then calls
     * SoundReq for real), so we need the filter here too. Without it
     * the per-1.5s low-health beep would still register on the queue
     * even with the mute toggle on, and we'd be wasting a queue slot
     * for nothing. */
    if (Port_AudioMute_ShouldSuppress((unsigned)sfx))
        return;

    u8 count = gUnk_02024048;
    if (count < (u8)ARRAY_COUNT(gUnk_02021F20)) {
        gUnk_02024048 = count + 1;
        gUnk_02021F20[count] = (u16)sfx;
    }
}

void SoundReqClipped(Entity* entity, u32 sfx) {
    if (CheckOnScreen(entity)) {
        SoundReq(sfx);
    }
}

u32 sub_080040A2(Entity* entity) {
    u8 rawSpriteSettings = *(u8*)&entity->spriteSettings;
    if (rawSpriteSettings & 0x2) {
        return 1;
    }
    return CheckOnScreen(entity);
}

static u32 ReadCollisionBitAtPosition(Entity* entity, const u16* slopeTable, u32 worldX, u32 worldY) {
    u32 roomX = (u16)worldX - gRoomControls.origin_x;
    u32 roomY = (u16)worldY - gRoomControls.origin_y;
    u32 tilePos = ((roomX & 0x3F0) >> 4) + ((roomY & 0x3F0) << 2);
    u32 collision = GetCollisionDataAtTilePos(tilePos, entity->collisionLayer);

    if (collision < 0x10) {
        if ((roomY & 0x8) == 0) {
            collision >>= 2;
        }
        if ((roomX & 0x8) == 0) {
            collision >>= 1;
        }
        return collision & 1;
    }

    if (collision == 0xFF) {
        return 1;
    }

    if (slopeTable == NULL) {
        return 0;
    }

    u16 bits = slopeTable[collision - 0x10];

    u32 xf = roomX & 0xF;
    if (xf >= 4) {
        bits >>= 1;
        if (xf >= 8) {
            bits >>= 1;
            if (xf >= 12) {
                bits >>= 1;
            }
        }
    }

    u32 yf = roomY & 0xF;
    if (yf >= 4) {
        bits >>= 4;
        if (yf >= 8) {
            bits >>= 4;
            if (yf >= 12) {
                bits >>= 4;
            }
        }
    }

    return bits & 1;
}

u32 sub_080040D8(Entity* entity, const u8* slopeTable, s32 worldX, s32 worldY) {
    return ReadCollisionBitAtPosition(entity, (const u16*)slopeTable, (u32)worldX, (u32)worldY);
}

u32 sub_080040E2(Entity* entity, const u8* slopeTable) {
    return ReadCollisionBitAtPosition(entity, (const u16*)slopeTable, (u16)entity->x.HALF.HI, (u16)entity->y.HALF.HI);
}

void SnapToTile(Entity* entity) {
    entity->x.WORD = (entity->x.WORD & ~0x000FFFFF) + 0x00080000;
    entity->y.WORD = (entity->y.WORD & ~0x000FFFFF) + 0x00080000;
}

void sub_0800417E(Entity* entity, u32 collisions) {
    u32 direction = entity->direction;
    if (collisions & 0xEE00) {
        direction = 0x20 - direction;
    }
    if (collisions & 0x00EE) {
        direction = 0x10 - direction;
    }
    entity->direction = direction & 0x1F;
}

u32 sub_0800419C(Entity* a, Entity* b, u32 xRange, u32 yRange) {
    if (xRange != 0) {
        s32 tmp = (s32)a->x.HALF.HI - (s32)b->x.HALF.HI + (s32)xRange;
        if ((xRange << 1) < (u32)tmp) {
            return 0;
        }
    }

    if (yRange != 0) {
        s32 tmp = (s32)a->y.HALF.HI - (s32)b->y.HALF.HI + (s32)yRange;
        if ((yRange << 1) < (u32)tmp) {
            return 0;
        }
    }

    return 1;
}

u32 sub_080041DC(Entity* entity, u32 x, u32 y) {
    return CalcDistance((s32)entity->x.HALF.HI - (s32)x, (s32)entity->y.HALF.HI - (s32)y);
}

u32 sub_080041E8(s32 x1, s32 y1, s32 x2, s32 y2) {
    return CalcDistance(x1 - x2, y1 - y2);
}

static u32 CheckEntityPickup(Entity* source, Entity* target, u32 xRange, u32 yRange) {
    Hitbox* hb = (Hitbox*)port_resolve_addr((uintptr_t)target->hitbox);
    if (hb == NULL) {
        return 0;
    }

    u32 rangeX = xRange + (u8)hb->width;
    u32 rangeY = yRange + (u8)hb->height;

    s32 targetX = (s32)(u16)target->x.HALF.HI + hb->offset_x;
    if ((*(u8*)&target->spriteSettings & 0x4) != 0) {
        targetX = (s32)(u16)target->x.HALF.HI - hb->offset_x;
    }
    s32 targetY = (s32)(u16)target->y.HALF.HI + hb->offset_y;

    if ((source->collisionLayer & target->collisionLayer) != 3) {
        if (rangeX != 0) {
            u32 xDelta = (u32)((s32)(u16)source->x.HALF.HI - targetX + (s32)rangeX);
            if ((rangeX << 1) < xDelta) {
                return 0;
            }
        }
        if (rangeY != 0) {
            u32 yDelta = (u32)((s32)(u16)source->y.HALF.HI - targetY + (s32)rangeY);
            if ((rangeY << 1) < yDelta) {
                return 0;
            }
        }
    }

    return 1;
}

u32 sub_08003FDE(Entity* source, Entity* target, u32 xRange, u32 yRange) {
    u32 picked = CheckEntityPickup(source, target, xRange, yRange);
    if (picked) {
        sub_0806F58C(source, target);
    }
    return picked;
}

// Advance tilePos by one tile in the direction given by animationState,
// then look up the tile type at that position.
// tilePos is a BYTE offset into the mapData u16 array (i.e. element_index * 2).
// On GBA, sub_08004212 leaves the modified tilePos in r2 and the tile in r1.
// sub_08004202 returns the modified tilePos (r2) and writes the tile to *outTileType.
static u32 AdvanceTilePos(u32 animationState, u32 tilePos) {
    if ((animationState & 3) != 0) {
        tilePos += (animationState & 4) ? (u32)-2 : 2;
    }

    if ((animationState & 3) != 2) {
        tilePos += (((animationState + 1) & 4) != 0) ? 0x80 : (u32)-0x80;
    }

    return tilePos & 0x1FFF;
}

static u16 LookupTileType(Entity* entity, u32 tilePos) {
    MapLayer* layer = GetLayerByIndex(entity->collisionLayer);
    u16 tile = layer->mapData[tilePos >> 1]; // tilePos is byte offset, convert to element index
    if ((tile & 0x4000) == 0) {
        tile = layer->tileTypes[tile];
    }
    return tile;
}

u32 sub_08004212(Entity* entity, u32 animationState, u32 tilePos) {
    tilePos = AdvanceTilePos(animationState, tilePos);
    return LookupTileType(entity, tilePos);
}

u32 sub_08004202(Entity* entity, u8* outTileType, u32 tilePos) {
    tilePos = AdvanceTilePos(entity->animationState, tilePos);
    u32 tileType = LookupTileType(entity, tilePos);
    *(u32*)outTileType = tileType;
    return tilePos; // Return modified tilePos (matches GBA r2 behavior)
}

static void CreateHazardFX(Entity* entity, u32 fxType) {
    Entity* fx = CreateObject(SPECIAL_FX, fxType, 0);
    if (fx != NULL) {
        fx->x.HALF.HI = entity->x.HALF.HI;
        fx->y.HALF.HI = entity->y.HALF.HI;
        fx->z.HALF.HI = entity->z.HALF.HI;
        if (entity->kind == ENEMY) {
            fx->type2 = 1;
        }
    }
    DeleteEntity(entity);
}

void CreateDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_WATER_SPLASH);
}

void CreateLavaDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_LAVA_SPLASH);
}

void CreateSwampDrownFX(Entity* entity) {
    CreateHazardFX(entity, FX_GREEN_SPLASH);
}

void CreatePitFallFX(Entity* entity) {
    CreateHazardFX(entity, FX_FALL_DOWN);
}

void UpdateCollision(Entity* entity) {
    if ((entity->flags & ENT_COLLIDE) == 0) {
        return;
    }
    /* Bound = GBA capacity (see MAX_COLLIDABLE_ENTITIES in entity.h). The old
     * MAX_ENTITIES (72) cap silently dropped the last registrants in full
     * rooms, making random entities non-collidable. */
    if (gCollidableCount < MAX_COLLIDABLE_ENTITIES) {
        gCollidableList[gCollidableCount] = entity;
        gCollidableCount++;
    }
}

s32 DoItemTileInteraction(Entity* entity, u32 interaction, ItemBehavior* behavior) {
    const s8* offs = (const s8*)&gUnk_08007DF4[entity->animationState & 6];
    u8* entry = (u8*)DoTileInteractionOffset(entity, interaction, offs[0], offs[1]);

    if (entry != NULL) {
        u8* rawBehavior = (u8*)behavior;
        rawBehavior[3] = entry[2];
        rawBehavior[7] = entry[3];
        rawBehavior[8] = entry[5];
        return 1;
    }

    return 0;
}

u8* DoTileInteractionOffset(Entity* entity, u32 interaction, s32 xOffset, s32 yOffset) {
    u32 x = (u16)entity->x.HALF.HI + xOffset;
    u32 y = (u16)entity->y.HALF.HI + yOffset;
    return (u8*)DoTileInteraction(entity, interaction, x, y);
}

u32* DoTileInteractionHere(Entity* entity, u32 interaction) {
    return (u32*)DoTileInteraction(entity, interaction, (u16)entity->x.HALF.HI, (u16)entity->y.HALF.HI);
}

u16* DoTileInteraction(Entity* entity, u32 interaction, u32 worldX, u32 worldY) {
    if (gRoomControls.reload_flags == 1) {
        return NULL;
    }

    u32 tileType = GetTileTypeAtWorldCoords((s32)worldX, (s32)worldY, entity->collisionLayer);
    u16 entryIndex;
    if (!FindEntryForKeyInternal(tileType, gUnk_080046A4, &entryIndex)) {
        return NULL;
    }

    TileInteractionDef* entry = (TileInteractionDef*)((u8*)gUnk_080047F6 + ((u32)entryIndex << 3));
    if ((((u32)entry->flags >> interaction) & 1U) == 0) {
        return NULL;
    }

    u8 objectId = entry->objectId;
    u8 objectType = entry->objectType;

    if (objectId != 0xFF && interaction != 6 && interaction != 0xE && interaction != 0xA && interaction != 0xB &&
        !(interaction == 0xD && objectId == SPECIAL_FX && objectType == FX_GRASS_CUT)) {
        u32 objectType2 = (objectId == SPECIAL_FX) ? 0x80 : 0;
        Entity* created = CreateObject((Object)objectId, objectType, objectType2);
        if (created != NULL) {
            if (objectId != 0) {
                created->x.HALF.HI = ((u16)worldX & ~0xF) + 8;
                created->y.HALF.HI = ((u16)worldY & ~0xF) + 8;
            } else {
                created->x.HALF.HI = entity->x.HALF.HI;
                created->y.HALF.HI = entity->y.HALF.HI;
                created->z.HALF.HI = entity->z.HALF.HI;
            }

            created->parent = entity;
            created->collisionLayer = entity->collisionLayer;
            UpdateSpriteForCollisionLayer(created);
        }
    }

    u32 tileX = ((u16)worldX - gRoomControls.origin_x) >> 4;
    u32 tileY = ((u16)worldY - gRoomControls.origin_y) >> 4;
    u32 tilePos = tileX + (tileY << 6);
    u32 layer = entity->collisionLayer;
    u16 tileChange = entry->tileChange;

    if (tileChange & 0x4000) {
        if (tileChange == 0xFFFF) {
            RestorePrevTileEntity(tilePos, layer);
        } else {
            GetLayerByIndex(layer)->mapData[tilePos] = tileChange;
        }
    } else {
        sub_0807B7D8(tileChange, tilePos, layer);
    }

    return (u16*)entry;
}

u32 CheckNEastTile(Entity* entity) {
    u32 actTile = GetActTileRelativeToEntity(entity, 0, 0);
    if (actTile & 0x4000) {
        return 0;
    }

    u16 surfaceType;
    if (!FindEntryForKeyInternal(actTile, gMapActTileToSurfaceType, &surfaceType)) {
        return 0;
    }

    return (surfaceType == 1) ? 1 : 0;
}

u32 PlayerCheckNEastTile(void) {
    return CheckNEastTile(&gPlayerEntity.base);
}

static const s8 sVelocities1[] = { 0, -3, 3, -3, 3, 0, 3, 3, 0, 3, -3, 3, -3, 0, -3, -3 };
static const s8 sIceVelocities[] = { 0, -10, 10, -10, 10, 0, 10, 10, 0, 10, -10, 10, -10, 0, -10, -10 };
/* Full 16-byte table from ASM `velocities3` (player.s). It is indexed by
 * (animationState & ~1), 0..14, reading two consecutive bytes — so it must be
 * 16 bytes. The earlier port truncated it to 8, making any animationState >= 8
 * on the jump_status!=0 / diff>4 path read past the array (garbage velocity).
 * Second half (0x10..0x13 rows) restored to match GBA. */
static const s8 sVelocities3[] = { 0, 6, -6, 0, 0, -6, 6, 0, 19, 18, 18, 16, 16, 17, 17, 19 };

static void ClampPlayerVelocityAxis(s16* axis) {
    if (*axis > 0x180) {
        *axis = 0x180;
    } else if (*axis < -0x180) {
        *axis = -0x180;
    }
}

static void AddPlayerVelocity(s32 x, s32 y) {
    s16 vx = (s16)gPlayerState.vel_x;
    s16 vy = (s16)gPlayerState.vel_y;
    vx = (s16)(vx + x);
    vy = (s16)(vy + y);
    ClampPlayerVelocityAxis(&vx);
    ClampPlayerVelocityAxis(&vy);
    gPlayerState.vel_x = (u16)vx;
    gPlayerState.vel_y = (u16)vy;
}

static void DampPlayerVelocityAxis(s16* axis) {
    if (*axis >= 0) {
        *axis -= 3;
        if (*axis < 0) {
            *axis = 0;
        }
    } else {
        *axis += 3;
        if (*axis > 0) {
            *axis = 0;
        }
    }
}

static void ApplyIceVelocityCore(Entity* entity, u32 direction, bool32 setDirectionFromState) {
    if (setDirectionFromState) {
        if (gPlayerState.field_0x7 != 0 || gPlayerState.field_0xa != 0) {
            return;
        }
        entity->direction = (u8)direction;
        if (direction & 0x80) {
            goto apply_movement;
        }
    }

    if (gPlayerState.heldObject == 1 || gPlayerState.heldObject == 2) {
        ResetPlayerVelocity();
        return;
    }

    if (gPlayerState.jump_status != 0) {
        u32 animStateEven = ((u32)entity->animationState >> 1) << 1;
        u32 diff = (((direction >> 2) - animStateEven) + 2) & 7;
        if (diff > 4) {
            const s8* vel = &sVelocities3[animStateEven];
            AddPlayerVelocity(vel[0], vel[1]);
        } else {
            const s8* vel = &sVelocities1[(direction >> 2) << 1];
            AddPlayerVelocity(vel[0], vel[1]);
        }
    } else {
        const s8* vel = &sIceVelocities[(direction >> 2) << 1];
        AddPlayerVelocity(vel[0], vel[1]);
    }

apply_movement:;
    s16 velX = (s16)gPlayerState.vel_x;
    if (velX != 0) {
        u32 moveDir = 8;
        s32 moveSpeed = velX;
        if (moveSpeed < 0) {
            moveDir = 0x18;
            moveSpeed = -moveSpeed;
        }
        LinearMoveDirectionOLD(entity, (u32)moveSpeed, moveDir);
        sub_0807A5B8(moveDir);
    }

    s16 velY = (s16)gPlayerState.vel_y;
    if (velY != 0) {
        u32 moveDir = 0x10;
        s32 moveSpeed = velY;
        if (moveSpeed < 0) {
            moveDir = 0;
            moveSpeed = -moveSpeed;
        }
        LinearMoveDirectionOLD(entity, (u32)moveSpeed, moveDir);
        sub_0807A5B8(moveDir);
    }

    if (gPlayerState.jump_status == 0) {
        s16 dampX = (s16)gPlayerState.vel_x;
        s16 dampY = (s16)gPlayerState.vel_y;
        DampPlayerVelocityAxis(&dampX);
        DampPlayerVelocityAxis(&dampY);
        gPlayerState.vel_x = (u16)dampX;
        gPlayerState.vel_y = (u16)dampY;
    }
}

void sub_08008926(Entity* entity) {
    ApplyIceVelocityCore(entity, gPlayerState.direction, 1);
}

void UpdateIcePlayerVelocity(Entity* entity) {
    u32 direction = ((u32)entity->animationState >> 1) << 3;
    ApplyIceVelocityCore(entity, direction, 0);
}

/* Sync-flag tripwires for #93 cutscene-softlock chase. */
void Port_DiagSyncFlag(const char* op, unsigned flag, unsigned cur, unsigned k, unsigned id, unsigned t) {
    if (!Port_DebugVerbose)
        return;
    static int sCount = 0;
    if (sCount < 256) {
        sCount++;
        fprintf(stderr, "[sync] %s flag=0x%08X cur=0x%08X (k=%u id=%u type=%u)\n", op, flag, cur, k, id, t);
    }
}
/* #93 chase: track orchestrator entities + detect state changes that
 * bypass DeleteEntity/UnlinkEntity/ClearDeletedEntity. */
typedef struct {
    Entity* ent;
    unsigned char kind;
    unsigned char id;
    unsigned char action;
    unsigned char flags;
    void* prev;
    void* next;
    unsigned alive_frames;
    int active;
} OrchTrack;
static OrchTrack sOrchs[4];
static unsigned sOrchTickCount = 0;

/* Watched entity addresses — any modification (delete/unlink/memset) logs. */
static void* sWatchedAddrs[4] = { NULL, NULL, NULL, NULL };

static int Port_IsWatched(void* ent) {
    int i;
    for (i = 0; i < 4; i++) {
        if (sWatchedAddrs[i] == ent)
            return 1;
    }
    return 0;
}

void Port_LogEntityEvent(const char* op, void* ent, unsigned kind, unsigned id, void* prev, void* next) {
    if (!Port_IsWatched(ent))
        return;
    fprintf(stderr, "[%s] ent=%p kind=%u id=0x%X prev=%p next=%p\n", op, ent, kind, id, prev, next);
}
void Port_LogPostAction(unsigned action_idx, unsigned kind, unsigned id) {
    if (!Port_DebugVerbose)
        return;
    static unsigned sCount = 0;
    if (sCount < 32) {
        sCount++;
        fprintf(stderr, "[post-act] act=%u kind=%u id=0x%X\n", action_idx, kind, id);
    }
}
void Port_LogOrchEvent(const char* op, void* ent) {
    if (!Port_DebugVerbose)
        return;
    fprintf(stderr, "[orch-%s] ent=%p\n", op, ent);
}

/* #93 chase: per-frame snapshot of an orchestrator entity's script state
 * (PC, wait counter, postScriptActions, global syncFlags). Dedup'd —
 * only emits when one of those values changes. Lets us see exactly when
 * the orchestrator's script PC stops advancing and which step it died on.
 *
 * Up to 4 tracked entity pointers, indexed in entry-order. */
typedef struct {
    Entity* ent;
    void* lastSip;
    u32 lastPostActions;
    u32 lastSync;
    u16 lastWait;
    u8 used;
} OrchPcSlot;
static OrchPcSlot sOrchPcSlots[4];

void Port_LogOrchScriptPc(Entity* ent) {
    if (!Port_DebugVerbose)
        return;
    ScriptExecutionContext* ctx = Port_GetEntityScriptCtx(ent);
    if (!ctx)
        return;

    int i, slot = -1;
    for (i = 0; i < 4; i++) {
        if (sOrchPcSlots[i].used && sOrchPcSlots[i].ent == ent) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < 4; i++) {
            if (!sOrchPcSlots[i].used) {
                sOrchPcSlots[i].used = 1;
                sOrchPcSlots[i].ent = ent;
                sOrchPcSlots[i].lastSip = (void*)0xDEADBEEF;
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return;

    OrchPcSlot* s = &sOrchPcSlots[slot];
    void* sip = (void*)ctx->scriptInstructionPointer;
    u16 wait = ctx->wait;
    u32 postActions = ctx->postScriptActions;
    u32 sync = gActiveScriptInfo.syncFlags;

    if (sip != s->lastSip || wait != s->lastWait || postActions != s->lastPostActions || sync != s->lastSync) {
        fprintf(stderr, "[orch-pc] ent=%p kind=%u id=0x%X sip=%p wait=%u postAct=0x%X sync=0x%X\n", (void*)ent,
                ent->kind, ent->id, sip, (unsigned)wait, postActions, sync);
        s->lastSip = sip;
        s->lastWait = wait;
        s->lastPostActions = postActions;
        s->lastSync = sync;
    }
}

/* #93 chase: per-frame player state log. Dedup'd by (action,
 * controlMode, flags, eventPrio, area, room) so position drift
 * doesn't spam, but key transitions ARE captured. After the takeover
 * cutscene the player softlocks; this answers:
 *  - is the player entity still alive and being updated (line firing
 *    at all)?
 *  - what action is it stuck in (PLAYER_NORMAL=movable, anything else
 *    typically locks input)?
 *  - is gPlayerState.controlMode CONTROL_DISABLED (input gated off)?
 *  - is gPriorityHandler.event_priority elevated (PRIO_PLAYER_EVENT
 *    blocks normal input)?
 *  - is the player in the new area (Hyrule Field 3/8) or stuck in
 *    the pre-cutscene room?
 */
/* #93 chase: log UpdatePlayerInput's outputs. The user's input is
 * confirmed reaching gInput.heldKeys (per [player] log) but
 * gPlayerState.direction ends up 0xFF (DIR_NONE), gating all
 * movement. This shows whether ctlMode/macro/keys-gating is the
 * culprit or whether ConvInputToState / the gUnk_08109202 lookup
 * is producing the wrong value. */
void Port_LogPlayerInput(unsigned ctlMode, unsigned heldKeys, unsigned keys, unsigned state, unsigned direction,
                         int hasMacro) {
    if (!Port_DebugVerbose)
        return;
    static unsigned lCtl = 0xFFFFu, lHeld = 0xFFFFu, lKeys = 0xFFFFu;
    static unsigned lState = 0xFFFFu, lDir = 0xFFFFu;
    static int lMacro = -1;
    if (ctlMode != lCtl || heldKeys != lHeld || keys != lKeys || state != lState || direction != lDir ||
        hasMacro != lMacro) {
        fprintf(stderr, "[upi] ctl=%u held=0x%X keys=0x%X state=0x%X dir=0x%X macro=%d\n", ctlMode, heldKeys, keys,
                state, direction, hasMacro);
        lCtl = ctlMode;
        lHeld = heldKeys;
        lKeys = keys;
        lState = state;
        lDir = direction;
        lMacro = hasMacro;
    }
}

/* #93 chase: log PlayerNormal entry. Dedup by queued_action so we
 * see when this transitions. Combined with [pn-ret] this tells us
 * whether PlayerNormal is even being entered (vs DoPlayerAction
 * routing somewhere else). */
void Port_LogPlayerNormalEnter(unsigned queuedAction) {
    if (!Port_DebugVerbose)
        return;
    static unsigned lastQ = 0xFFFFu;
    static unsigned heartbeat = 0;
    heartbeat++;
    if (queuedAction != lastQ || (heartbeat % 60) == 0) {
        fprintf(stderr, "[pn-enter] queuedAction=%u%s\n", queuedAction, queuedAction == lastQ ? " (heartbeat)" : "");
        lastQ = queuedAction;
    }
}

/* #93 chase: log the v13 movement-gate decision in PlayerNormal.
 * v13&2 is what calls UpdatePlayerMovement(). If v13 is 0 or 1
 * post-cutscene, that's why position is frozen. Dedup'd by all inputs
 * so we only see actual transitions. */
void Port_LogPlayerMovementGate(unsigned v13, unsigned dir, unsigned psDir, unsigned f7, unsigned fa, unsigned dash,
                                unsigned floor, unsigned swordState) {
    if (!Port_DebugVerbose)
        return;
    static unsigned lV13 = 0xFFFFu, lDir = 0xFFFFu, lPsDir = 0xFFFFu;
    static unsigned lF7 = 0xFFFFu, lFa = 0xFFFFu, lDash = 0xFFFFu;
    static unsigned lFloor = 0xFFFFu, lSword = 0xFFFFu;
    static unsigned heartbeat = 0;
    heartbeat++;
    int changed = v13 != lV13 || dir != lDir || psDir != lPsDir || f7 != lF7 || fa != lFa || dash != lDash ||
                  floor != lFloor || swordState != lSword;
    int beat = (heartbeat % 60) == 0;
    if (changed || beat) {
        fprintf(stderr,
                "[mv-gate] v13=%u superDir=0x%X psDir=0x%X f7=0x%X fa=0x%X "
                "dash=0x%X floor=%u sword=0x%X%s\n",
                v13, dir, psDir, f7, fa, dash, floor, swordState, beat && !changed ? " (heartbeat)" : "");
        lV13 = v13;
        lDir = dir;
        lPsDir = psDir;
        lF7 = f7;
        lFa = fa;
        lDash = dash;
        lFloor = floor;
        lSword = swordState;
    }
}

/* #93 chase: log which early-return inside PlayerNormal fires.
 * Dedup'd by tag — if movement is gated, we'll see the same tag every
 * frame; if movement reaches the end, no [pn-ret] log. */
void Port_LogPlayerEarlyReturn(const char* tag) {
    static const char* sLastTag = "";
    extern bool Port_DebugVerbose;
    if (!Port_DebugVerbose)
        return;
    if (tag != sLastTag) {
        fprintf(stderr, "[pn-ret] %s\n", tag);
        sLastTag = tag;
    }
}

void Port_LogPlayerState(unsigned action, unsigned controlMode, unsigned stateFlags, unsigned eventPrio, unsigned area,
                         unsigned room, int x, int y, unsigned heldKeys, unsigned knockback, unsigned jumpStatus,
                         unsigned swimState, unsigned framestate, unsigned attackStatus, unsigned spd) {
    if (!Port_DebugVerbose)
        return;
    static unsigned lastAction = 0xFFFFu;
    static unsigned lastCtl = 0xFFFFu;
    static unsigned lastFlags = 0xFFFFu;
    static unsigned lastPrio = 0xFFFFu;
    static unsigned lastArea = 0xFFFFu;
    static unsigned lastRoom = 0xFFFFu;
    static int lastX = -99999;
    static int lastY = -99999;
    static unsigned lastKeys = 0xFFFFu;
    static unsigned lastKnock = 0xFFFFu;
    static unsigned lastJump = 0xFFFFu;
    static unsigned lastSwim = 0xFFFFu;
    static unsigned lastFs = 0xFFFFu;
    static unsigned lastAtk = 0xFFFFu;
    static unsigned lastSpd = 0xFFFFu;
    static unsigned heartbeat = 0;
    int stateChanged = action != lastAction || controlMode != lastCtl || stateFlags != lastFlags ||
                       eventPrio != lastPrio || area != lastArea || room != lastRoom || x != lastX || y != lastY ||
                       heldKeys != lastKeys || knockback != lastKnock || jumpStatus != lastJump ||
                       swimState != lastSwim || framestate != lastFs || attackStatus != lastAtk || spd != lastSpd;
    heartbeat++;
    int beat = (heartbeat % 60) == 0;
    if (stateChanged || beat) {
        fprintf(stderr,
                "[player] action=%u ctl=%u flags=0x%X prio=%u area=%u room=%u "
                "pos=(%d,%d) keys=0x%X knock=%u jump=0x%X swim=0x%X fs=%u "
                "atk=%u spd=%u%s\n",
                action, controlMode, stateFlags, eventPrio, area, room, x, y, heldKeys, knockback, jumpStatus,
                swimState, framestate, attackStatus, spd, beat && !stateChanged ? " (heartbeat)" : "");
        lastAction = action;
        lastCtl = controlMode;
        lastFlags = stateFlags;
        lastPrio = eventPrio;
        lastArea = area;
        lastRoom = room;
        lastX = x;
        lastY = y;
        lastKeys = heldKeys;
        lastKnock = knockback;
        lastJump = jumpStatus;
        lastSwim = swimState;
        lastFs = framestate;
        lastAtk = attackStatus;
        lastSpd = spd;
    }
}

/* #93 chase: log every SetFade / SetFadeInverted call so we can trace
 * the exact post-cutscene fade chain and find why type ends at 5
 * (fade-OUT to black) instead of 4 (fade-IN to color). NO dedup —
 * every call gets logged. */
void Port_LogFadeCall(const char* fn, u32 arg1, u32 arg2, u32 priorType, u32 priorActive, u32 priorProg) {
    if (!Port_DebugVerbose)
        return;
    fprintf(stderr, "[fade-call] %s arg1=0x%X arg2=0x%X prior(type=0x%X active=%u prog=%u)\n", fn, arg1, arg2,
            priorType, priorActive, priorProg);
}

/* #93 chase: log subtask entry. Dedup'd by (name, active, nextToLoad)
 * so we only see actual state changes. Tells us whether
 * Subtask_FadeOut/FadeIn/Init/Die actually run post-cutscene. */
void Port_LogSubtaskEntry(const char* name, unsigned active, unsigned nextToLoad) {
    if (!Port_DebugVerbose)
        return;
    static const char* sLastName = "";
    static unsigned sLastActive = 0xFFFFu;
    static unsigned sLastNTL = 0xFFFFu;
    if (name != sLastName || active != sLastActive || nextToLoad != sLastNTL) {
        fprintf(stderr, "[subtask-%s] active=%u nextToLoad=%u\n", name, active, nextToLoad);
        sLastName = name;
        sLastActive = active;
        sLastNTL = nextToLoad;
    }
}

/* #93 chase: log entity-list-assignment for kind=6 (OBJECT) entities.
 * Dual orchestrators (id=0x69) hit AppendEntityToList(_, 6) at spawn,
 * but our log shows the parent and child end up in DIFFERENT lists.
 * This logger lets us see exactly which listIndex each one lands in. */
void Port_LogListOp(const char* op, void* ent, unsigned kind, unsigned id, unsigned listIdx, void* listAddr) {
    if (!Port_DebugVerbose)
        return;
    fprintf(stderr, "[list-%s] ent=%p kind=%u id=0x%X listIdx=%u listAddr=%p\n", op, ent, kind, id, listIdx, listAddr);
}

/* #93 chase: per-frame snapshot of display state (DISPCNT) + palette
 * buffer hash. Dedup'd. The post-takeover black-screen issue: by the
 * time fade.before logs active=0 progress=0, gFadeControl says screen
 * should be visible. If it's still black, the cause is one of:
 *   - displayControl bits disable BG/OAM
 *   - gPaletteBuffer is all-zero (palette never re-loaded for new area)
 * Hashing first 64 bytes of palette is enough to tell "all zero" vs
 * "has color data". */
static unsigned Port_HashBytes(const void* p, unsigned n) {
    const unsigned char* b = (const unsigned char*)p;
    unsigned h = 0x811C9DC5u;
    for (unsigned i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x01000193u;
    }
    return h;
}

extern u8 gPaletteBuffer[];

void Port_LogDisplayFrame(void) {
    /* Per-frame palette/display state hash + log. On low-end CPUs this
     * 1.5 KiB hash burns measurable cycles every frame. Skip entirely
     * when TMC_VERBOSE is off. */
    extern bool Port_DebugVerbose;
    if (!Port_DebugVerbose)
        return;

    static u16 lastDispCtl = 0xFFFFu;
    static unsigned lastPalHash = 0u;
    static unsigned lastPalRamHash = 0u;

    u16 dispCtl = gScreen.lcd.displayControl;
    /* Hash the FULL 1024-byte palette buffer + the GPU palette (gBgPltt
     * + gObjPltt — backing storage for PAL_RAM on the PC port; PAL_RAM
     * itself isn't a real address-space reservation here so we read the
     * underlying buffers). If palBuf changes but palRam doesn't, the
     * buffer was loaded but never pushed to the GPU. If both stay the
     * same across an area transition, the new area's palette never
     * loaded. */
    unsigned palHash = Port_HashBytes(gPaletteBuffer, 0x400);
    unsigned palRamHash = Port_HashBytes(gBgPltt, 0x200) ^ Port_HashBytes(gObjPltt, 0x200);

    if (dispCtl != lastDispCtl || palHash != lastPalHash || palRamHash != lastPalRamHash) {
        fprintf(stderr, "[disp] dispCtl=0x%04X palBuf=0x%08X palRam=0x%08X\n", dispCtl, palHash, palRamHash);
        lastDispCtl = dispCtl;
        lastPalHash = palHash;
        lastPalRamHash = palRamHash;
    }
}

/* #93 chase: per-frame snapshot of gFadeControl state. Dedup'd — only
 * emits when active/type/progress/sustain/mask change. Targets the
 * post-takeover black-screen issue: lets us see whether the AuxCutscene
 * exit-fade actually flips active=1 with the right type, and whether
 * progress drains back to 0 (visible) or sustains at 0x100 (black). */
void Port_LogFadeFrame(void) {
    if (!Port_DebugVerbose)
        return;
    static u8 lastActive = 0xFF;
    static u16 lastType = 0xFFFF;
    static u16 lastProgress = 0xFFFF;
    static u16 lastSustain = 0xFFFF;
    static u8 lastColor = 0xFF;
    static u32 lastMask = 0xFFFFFFFFu;

    u8 active = (u8)gFadeControl.active;
    u16 type = gFadeControl.type;
    u16 progress = gFadeControl.progress;
    u16 sustain = gFadeControl.sustain;
    u8 color = gFadeControl.color;
    u32 mask = gFadeControl.mask;

    if (active != lastActive || type != lastType || progress != lastProgress || sustain != lastSustain ||
        color != lastColor || mask != lastMask) {
        fprintf(stderr, "[fade] active=%u type=0x%X progress=%u sustain=%u color=0x%X mask=0x%X\n", active, type,
                progress, sustain, color, mask);
        lastActive = active;
        lastType = type;
        lastProgress = progress;
        lastSustain = sustain;
        lastColor = color;
        lastMask = mask;
    }
}

void Port_DumpOrchStack(const char* tag, void* ent) {
    if (!Port_DebugVerbose)
        return;
#if PORT_HAVE_EXECINFO
    void* bt[24];
    int n = backtrace(bt, 24);
    char** sym = backtrace_symbols(bt, n);
    fprintf(stderr, "[orch-stack-%s] ent=%p depth=%d\n", tag, ent, n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  #%d %s\n", i, sym ? sym[i] : "?");
    }
    if (sym)
        free(sym);
#else
    fprintf(stderr, "[orch-stack-%s] ent=%p (no execinfo on this platform)\n", tag, ent);
#endif
    fflush(stderr);
}

void Port_TrackOrch(Entity* ent) {
    if (!Port_DebugVerbose)
        return;
    int i;
    /* Find existing slot or first empty */
    for (i = 0; i < 4; i++) {
        if (sOrchs[i].active && sOrchs[i].ent == ent) {
            /* Update snapshot — entity is still alive and being called */
            sOrchs[i].kind = ent->kind;
            sOrchs[i].id = ent->id;
            sOrchs[i].action = ent->action;
            sOrchs[i].flags = (unsigned char)ent->flags;
            sOrchs[i].prev = ent->prev;
            sOrchs[i].next = ent->next;
            sOrchs[i].alive_frames = sOrchTickCount;
            return;
        }
    }
    for (i = 0; i < 4; i++) {
        if (!sOrchs[i].active) {
            sOrchs[i].active = 1;
            sOrchs[i].ent = ent;
            sOrchs[i].kind = ent->kind;
            sOrchs[i].id = ent->id;
            sOrchs[i].action = ent->action;
            sOrchs[i].flags = (unsigned char)ent->flags;
            sOrchs[i].prev = ent->prev;
            sOrchs[i].next = ent->next;
            sOrchs[i].alive_frames = sOrchTickCount;
            sWatchedAddrs[i] = (void*)ent;
            fprintf(stderr, "[track] slot=%d ent=%p kind=%u id=0x%X (start)\n", i, (void*)ent, ent->kind, ent->id);
            return;
        }
    }
}

/* Issue #93 watchdog: when the takeover orchestrator dies prematurely
 * (which it always does on PC port), helper entities are still parked
 * in their first WAIT&CLR for a start signal that the orchestrator
 * never sent. Walk them through the same sequence the orchestrator
 * would have driven. Triggered the moment ScriptCommand_DoPostScriptAction
 * case 1<<6 broadcasts 0x400 (orchestrator self-delete). */
static int sTakeoverWdActive = 0;
static int sTakeoverWdStep = 0;
static int sTakeoverWdFrame = 0;
static int sTakeoverWdDone = 0; /* one-shot: never re-run after first completion */
void Port_TakeoverWatchdog(void) {
    /* Disabled — defer to the cutscene.c sub_08053BBC watchdog instead,
     * which fires SetFade + DispReset + menuType++ in the right order for
     * the AuxCutscene exit-fade handoff. The port-side watchdog was
     * racing the cutscene-side one and the screen was ending up at full
     * black with the fade-in machinery skipped. Keeping the function so
     * the call from Port_CheckOrchIntegrity is harmless. */
    if (1)
        return;
    if (sTakeoverWdDone) {
        if (gActiveScriptInfo.syncFlags & 0x400u) {
            gActiveScriptInfo.syncFlags &= ~0x400u;
        }
        return;
    }
    static const struct {
        unsigned setFlag;
        int frames;
    } kSeq[] = {
        { 0x010, 30 }, /* wake Vaati 1 */
        { 0x004, 30 }, /* wake King 1 */
        { 0x010, 30 }, /* wake Vaati 2 */
        { 0x010, 30 }, /* wake Vaati 3 */
        { 0x004, 30 }, /* wake King 2 */
        { 0x010, 30 }, /* wake Vaati 4 */
        { 0x010, 30 }, /* extra */
        { 0x040, 30 }, /* wake Guards */
        { 0x001, 30 }, /* wake Minister */
        { 0x004, 30 }, /* wake King 3 */
        { 0x200, 15 }, /* signal penultimate */
        { 0x004, 15 }, /* wake King final */
        { 0x400, 15 }, /* final cutscene-over */
    };
    if (!sTakeoverWdActive) {
        if (gActiveScriptInfo.syncFlags & 0x400u) {
            sTakeoverWdActive = 1;
            sTakeoverWdStep = 0;
            sTakeoverWdFrame = 0;
            fprintf(stderr, "[wd] activated (syncFlags=0x%X)\n", gActiveScriptInfo.syncFlags);
        }
        return;
    }
    if (sTakeoverWdStep < (int)(sizeof(kSeq) / sizeof(kSeq[0]))) {
        gActiveScriptInfo.syncFlags |= kSeq[sTakeoverWdStep].setFlag;
        sTakeoverWdFrame++;
        {
            static int sLogStep = -1;
            if (sLogStep != sTakeoverWdStep) {
                sLogStep = sTakeoverWdStep;
                fprintf(stderr, "[wd] step=%d setFlag=0x%X\n", sTakeoverWdStep, kSeq[sTakeoverWdStep].setFlag);
            }
        }
        if (sTakeoverWdFrame >= kSeq[sTakeoverWdStep].frames) {
            sTakeoverWdStep++;
            sTakeoverWdFrame = 0;
        }
    } else {
        /* Sequence complete. We do NOT call SetRoomFlag(0) here — the
         * cutscene.c version of the watchdog (sub_08053BBC) handles
         * that, and it ALSO falls through to the original gMenu.menuType++
         * + DispReset + SetFade(FADE_INSTANT, 0x100) sequence that's
         * required for the AuxCutscene→FadeOut→FadeIn handoff to work. */
        gActiveScriptInfo.syncFlags &= ~0x400u;
        sTakeoverWdActive = 0;
        sTakeoverWdDone = 1;
        fprintf(stderr, "[wd] sequence complete -- cleared 0x400, latched done\n");
    }
}

void Port_CheckOrchIntegrity(unsigned phase, const char* where) {
    if (!Port_DebugVerbose)
        return;
    int i;
    sOrchTickCount++;
    Port_TakeoverWatchdog();
    /* Per-frame fade snapshot (dedup'd in Port_LogFadeFrame). Cheap when
     * fade state is stable. Only logs on actual transitions. */
    Port_LogFadeFrame();
    Port_LogDisplayFrame();
    for (i = 0; i < 4; i++) {
        if (!sOrchs[i].active)
            continue;
        Entity* ent = sOrchs[i].ent;
        /* Check if state changed since last seen */
        if (ent->kind != sOrchs[i].kind || ent->id != sOrchs[i].id || ent->prev != sOrchs[i].prev ||
            ent->next != sOrchs[i].next || (unsigned char)ent->flags != sOrchs[i].flags) {
            fprintf(stderr,
                    "[track] CHANGE slot=%d ent=%p (%s phase=%u tick=%u age=%u)\n"
                    "        kind: %u -> %u\n"
                    "        id:   0x%X -> 0x%X\n"
                    "        flags: 0x%X -> 0x%X\n"
                    "        prev: %p -> %p\n"
                    "        next: %p -> %p\n",
                    i, (void*)ent, where, phase, sOrchTickCount, sOrchTickCount - sOrchs[i].alive_frames,
                    sOrchs[i].kind, ent->kind, sOrchs[i].id, ent->id, sOrchs[i].flags,
                    (unsigned)(unsigned char)ent->flags, sOrchs[i].prev, ent->prev, sOrchs[i].next, ent->next);
            /* Update snapshot to avoid spam */
            sOrchs[i].kind = ent->kind;
            sOrchs[i].id = ent->id;
            sOrchs[i].action = ent->action;
            sOrchs[i].flags = (unsigned char)ent->flags;
            sOrchs[i].prev = ent->prev;
            sOrchs[i].next = ent->next;
            /* If kind/id no longer orchestrator, deactivate slot */
            if (ent->kind != 6 || ent->id != 105) {
                fprintf(stderr, "[track] slot %d no longer an orchestrator — stopping watch\n", i);
                sOrchs[i].active = 0;
            }
        }
    }
}

void Port_DiagSyncWait(const char* op, unsigned flag, unsigned cur, unsigned k, unsigned id, unsigned t) {
    if (!Port_DebugVerbose)
        return;
    /* Dedupe WAIT/WAIT&CLR events (they fire every frame the wait holds) but
     * NEVER dedupe WAIT-PASS / SET / CLR — those are state transitions we
     * always want to see. */
    static unsigned sLastFlag = 0xFFFFFFFFu;
    static unsigned sLastCur = 0xFFFFFFFFu;
    static unsigned sLastKid = 0xFFFFFFFFu;
    static const char* sLastOp = "";
    int alwaysLog = (op[0] == 'W' && op[1] == 'A' && op[2] == 'I' && op[3] == 'T' && op[4] == '-') /* WAIT-PASS */
                    || (op[0] == 'S')                                                              /* SET */
                    || (op[0] == 'C');                                                             /* CLR */
    unsigned kid = (k << 16) | (id << 8) | t;
    if (alwaysLog || flag != sLastFlag || cur != sLastCur || kid != sLastKid || op != sLastOp) {
        sLastFlag = flag;
        sLastCur = cur;
        sLastKid = kid;
        sLastOp = op;
        fprintf(stderr, "[sync] %s flag=0x%08X cur=0x%08X (k=%u id=%u type=%u)\n", op, flag, cur, k, id, t);
    }
}
