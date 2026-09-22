#include "area.h"
#include "color.h"
#include "common.h"
#include "functions.h"
#include "manager/diggingCaveEntranceManager.h"
#include "message.h"
#include "npc.h"
#include "physics.h"
#include "room.h"
#include "script.h"

typedef struct Temp {
    void* prev;
    void* next;
#if PORT_POINTERS_ARE_64_BIT
    /*
     * 64-bit: Must be large enough for the biggest manager struct.
     * FightManagerHelper / EnemyInteractionManager have Entity* enemies[8]
     * which makes them 120 bytes total (56 base + 64 pointer array).
     * 120 − 16 (prev+next) = 104 = 0x68, rounded up to 0x70 for safety.
     */
    u8 _0[0x70];
#else
    u8 _0[0x38];
#endif
} Temp;

PORT_STATIC_ASSERT_SIZE(Temp, 0x40, 0x80, "Manager pool slot size incorrect");

#ifdef PC_PORT
Temp gUnk_02033290[32] __attribute__((aligned(8)));
#endif
#ifdef PC_PORT
#include "port_entity_ctx.h"
#ifdef PC_PORT
#include <stdio.h> /* fprintf for the entity-list cycle warning in GetEmptyEntity */

/* Bounded-iteration safety shared by every gEntityLists walker in this
 * file. The Lake Hylia warp repro produced a `next`-pointer cycle that
 * didn't include the list head sentinel, so the unguarded `i != list`
 * checks spun forever. Cap at 4× MAX_ENTITIES so any real list (< 80
 * entries) is well within bounds; once-per-list-per-session diagnostic
 * lets us find the corruption source from stderr without gdb. Same
 * defensive style as the #93 NULL-guard above. */
#define ENT_WALK_CAP 256
#define ENT_WALK_TRIPPED(_n) ((_n) >= ENT_WALK_CAP)
static inline void Port_LogEntityListCycle_(const char* fn, int listIdx, void* lastEnt, unsigned kind, unsigned id,
                                            unsigned flags) {
    /* One bit per (function, list-index) pair so a misbehaving cycle
     * doesn't spam stderr every frame, but we still see new sites. */
    static unsigned long sWarned = 0;
    const unsigned bit = ((unsigned)((uintptr_t)fn >> 4) ^ (unsigned)listIdx) & 63u;
    const unsigned long mask = 1UL << bit;
    if (sWarned & mask)
        return;
    sWarned |= mask;
    fprintf(stderr,
            "[entity] %s: cycle detected on gEntityLists[%d] after %d steps; "
            "last_ent=%p kind=%u id=0x%x flags=0x%x. Bailing.\n",
            fn, listIdx, ENT_WALK_CAP, lastEnt, kind, id, flags);
}
#define ENT_WALK_WARN(_lst, _ent)                                                                      \
    Port_LogEntityListCycle_(__func__, (int)((LinkedList*)(_lst) - gEntityLists), (void*)(_ent),       \
                             (_ent) ? (unsigned)(_ent)->kind : 0u, (_ent) ? (unsigned)(_ent)->id : 0u, \
                             (_ent) ? (unsigned)(_ent)->flags : 0u)

/* Pointer-range sanity check. Lake Hylia warp repro showed an entity
 * list with a `next` pointing at 0x1100 (a stray GBA EWRAM-relative
 * value that escaped widening) — dereferencing that crashes. Any
 * Entity* we walk must live in one of the four legitimate pools:
 *   - gPlayerEntity              (single GenericEntity)
 *   - gAuxPlayerEntities[0..6]   (7 GenericEntities)
 *   - gEntities[0..MAX_ENTITIES-1]
 *   - &gEntityLists[0..8]        (sentinels — the doubly-linked-list
 *                                  walkers compare against them to stop)
 * If a `next` falls outside all four, the list is corrupt; treat as
 * end-of-list and let the cycle-cap log + repair kick in. */
int Port_IsValidEntityAddr(const void* p) {
    if (p == NULL)
        return 0;
    const uintptr_t a = (uintptr_t)p;
    if (a == (uintptr_t)(void*)&gPlayerEntity)
        return 1;
    if (a >= (uintptr_t)(void*)&gAuxPlayerEntities[0] &&
        a <= (uintptr_t)(void*)&gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES - 1])
        return 1;
    if (a >= (uintptr_t)(void*)&gEntities[0] && a <= (uintptr_t)(void*)&gEntities[MAX_ENTITIES - 1])
        return 1;
    if (a >= (uintptr_t)(void*)&gEntityLists[0] && a < (uintptr_t)(void*)&gEntityLists[9])
        return 1;
    /* Manager pool — use the actual Temp array extent. A 64-bit host uses
     * 128-byte slots; 32-bit native ports retain the 64-byte GBA slots.
     * Without this entry the entity-list
     * walker mis-classifies live managers (TempleOfDropletsManager etc.)
     * as out-of-pool garbage and resets the lists, orphaning them out
     * of the pool. Manifested as Temple of Droplets sunbeam never
     * appearing in room 0x21 (#75). */
    {
        const uintptr_t mgrBase = (uintptr_t)(void*)&gUnk_02033290;
        const uintptr_t mgrEnd = mgrBase + sizeof(gUnk_02033290);
        if (a >= mgrBase && a < mgrEnd)
            return 1;
    }
    return 0;
}
#endif
#endif

#include "tiles.h"

extern u8 gUpdateVisibleTiles;
#ifndef PC_PORT
extern Manager gUnk_02033290;
#endif
void UpdatePlayerInput(void);
void ClearHitboxList(void);
void sub_0805EE88(void);
void ClearAllDeletedEntities(void);
void DeleteAllEntities(void);
void sub_0805E98C(void);
extern void UnloadOBJPalette2(Entity*);
extern void sub_08017744(Entity*);
extern void EnemyDetachFX(Entity*);

void ClearDeletedEntity(Entity*);
extern void ram_ClearAndUpdateEntities(void);
extern void ram_UpdateEntities(u32);

static void UpdatePriorityTimer(void);
static void ReleaseTransitionManager(void*);
static void UnlinkEntity(Entity*);

extern UpdateContext gUpdateContext;

// List by entity kind.
const u8 gEntityListLUT[] = {
    8, 1, 8, 4, 5, 8, 6, 7, 2, 6,
};

// TODO: wrong file, maybe an "enter.c" according to lexicographical order
void sub_0805E248(void) {
    s32 tilePos;

    tilePos = gDiggingCaveEntranceTransition.entrance->targetTilePos;
    if (gRoomControls.area == AREA_VEIL_FALLS || gRoomControls.area == AREA_VEIL_FALLS_DIG_CAVE) {
        SetTileType(TILE_TYPE_636, tilePos + TILE_POS(-1, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_643, tilePos + TILE_POS(-1, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_637, tilePos + TILE_POS(0, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_644, tilePos + TILE_POS(0, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_638, tilePos + TILE_POS(1, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_645, tilePos + TILE_POS(1, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_639, tilePos + TILE_POS(-1, 0), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_640, tilePos + TILE_POS(0, 0), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_642, tilePos + TILE_POS(1, 0), LAYER_BOTTOM);
    } else {
        SetTileType(TILE_TYPE_620, tilePos + TILE_POS(-1, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_627, tilePos + TILE_POS(-1, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_621, tilePos + TILE_POS(0, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_628, tilePos + TILE_POS(0, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_622, tilePos + TILE_POS(1, -1), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_629, tilePos + TILE_POS(1, -1), LAYER_TOP);
        SetTileType(TILE_TYPE_623, tilePos + TILE_POS(-1, 0), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_624, tilePos + TILE_POS(0, 0), LAYER_BOTTOM);
        SetTileType(TILE_TYPE_626, tilePos + TILE_POS(1, 0), LAYER_BOTTOM);
    }
    gUpdateVisibleTiles = 0;
}

const u8 gPrioritiesDefault[] = {
    [0] = 0, [PLAYER] = 1, [2] = 0,   [ENEMY] = 0,       [PROJECTILE] = 0,
    [5] = 0, [OBJECT] = 1, [NPC] = 1, [PLAYER_ITEM] = 1, [MANAGER] = 1,
};
const u8 gPrioritiesKinstoneMenu[] = {
    [0] = 0, [PLAYER] = 3, [2] = 0,   [ENEMY] = 3,       [PROJECTILE] = 3,
    [5] = 0, [OBJECT] = 3, [NPC] = 3, [PLAYER_ITEM] = 3, [MANAGER] = 3,
};

void InitDefaultPriority(Entity* e) {
    u8 r3 = gRoomTransition.entity_update_type;
    const u8* array = gPrioritiesKinstoneMenu;

    if (r3 != 2) {
        array = gPrioritiesDefault;
    }
    SetEntityPriority(e, array[e->kind]);
}

void SetEntityPriority(Entity* ent, u32 prio) {
    ent->updatePriorityPrev = prio;
    ent->updatePriority = prio;
}

bool32 EntityDisabled(Entity* this) {
    u32 value;

    if (this->flags & ENT_DELETED)
        return TRUE;
    if (this->action == 0)
        return FALSE;

    // pick highest
    if (gPriorityHandler.event_priority > gPriorityHandler.ent_priority)
        value = gPriorityHandler.event_priority;
    else
        value = gPriorityHandler.ent_priority;

    if (gMessage.state & MESSAGE_ACTIVE)
        value = max(value, PRIO_MESSAGE);
    return value > this->updatePriority;
}

bool32 AnyPrioritySet(void) {
    u32 prio = gPriorityHandler.event_priority;
    if (gPriorityHandler.event_priority <= gPriorityHandler.ent_priority)
        prio = gPriorityHandler.ent_priority;
    return prio != PRIO_MIN;
}

static void UpdatePriority(void) {
    UpdatePriorityTimer();
    if (gPriorityHandler.queued_priority) {
        gPriorityHandler.ent_priority = gPriorityHandler.queued_priority;
        gPriorityHandler.queued_priority = PRIO_MIN;
    } else if (gPriorityHandler.queued_priority_reset) {
        gPriorityHandler.ent_priority = gPriorityHandler.queued_priority_reset = PRIO_MIN;
    }
}

s32 SetMinPriority(u32 prio) {
    if (prio < gPriorityHandler.queued_priority || prio < gPriorityHandler.ent_priority)
        return 0;
    gPriorityHandler.queued_priority = prio;
    return 1;
}

void ResetEntityPriority(void) {
    gPriorityHandler.queued_priority_reset = 1;
}

void RequestPriority(Entity* this) {
    this->updatePriorityPrev = this->updatePriority;
    this->updatePriority = PRIO_MESSAGE;
    if (SetMinPriority(PRIO_MESSAGE))
        gPriorityHandler.requester = this;
}

void RequestPriorityOverPlayer(Entity* this) {
    SetPlayerControl(1);
    this->updatePriorityPrev = this->updatePriority;
    this->updatePriority = PRIO_NO_BLOCK;
    if (SetMinPriority(PRIO_PLAYER))
        gPriorityHandler.requester = this;
}

void RevokePriorityOverPlayer(Entity* this) {
    SetPlayerControl(0);
    RevokePriority(this);
}

void RequestPriorityDuration(Entity* this, u32 time) {
    if (this != NULL) {
        this->updatePriorityPrev = this->updatePriority;
        this->updatePriority = PRIO_NO_BLOCK;
    }
    if (SetMinPriority(PRIO_PLAYER))
        gPriorityHandler.requester = this;
    SetPriorityTimer(time);
}

void SetPriorityTimer(u32 time) {
    if (gPriorityHandler.priority_timer < time)
        gPriorityHandler.priority_timer = time;
}

static void UpdatePriorityTimer(void) {
    // if timer initialized to zero, priority is indeterminate
    if (gPriorityHandler.priority_timer != 0) {
        if (--gPriorityHandler.priority_timer == 0)
            ResetEntityPriority();
    }
}

void SetPlayerEventPriority(void) {
    gPriorityHandler.event_priority = PRIO_PLAYER_EVENT;
    gPlayerEntity.base.updatePriority = PRIO_PLAYER_EVENT;
}

void ResetPlayerEventPriority(void) {
    gPriorityHandler.event_priority = PRIO_MIN;
    gPlayerEntity.base.updatePriority = PRIO_PLAYER;
}

void RevokePriority(Entity* e) {
    e->updatePriority = e->updatePriorityPrev;
    ResetEntityPriority();
}

void SetRoomReloadPriority(void) {
    gPriorityHandler.event_priority = PRIO_PLAYER_EVENT;
}

void SetInitializationPriority(void) {
    gPriorityHandler.event_priority = PRIO_HIGHEST;
}

void ClearEventPriority(void) {
    gPriorityHandler.event_priority = PRIO_MIN;
}

void UpdateEntities(void) {
    void (*f)(u32);

    // TODO: why is this needed? Does this avoid a bug?
    gRoomVars.numKinstoneDropsPrevFrame = gRoomVars.numKinstoneDrops;
    gRoomVars.numKinstoneDrops = 0;

    UpdatePlayerInput();
    UpdatePriority();
    ClearHitboxList();
    CollideFollowers();
    f = ram_UpdateEntities;
    f(0);
    ClearAllDeletedEntities();
    sub_0805EE88();
}

void UpdateManagers(void) {
    void (*f)(u32);
    f = ram_UpdateEntities;
    f(1);
    ClearAllDeletedEntities();
}

void EraseAllEntities(void) {
    DeleteAllEntities();
    MemClear(&gPriorityHandler, sizeof(PriorityHandler));
#ifdef PC_PORT
    /* On PC these are separate globals, not contiguous like GBA EWRAM. */
    MemClear(&gPlayerEntity, sizeof(gPlayerEntity));
    MemClear(gAuxPlayerEntities, sizeof(gAuxPlayerEntities));
    MemClear(gEntities, sizeof(gEntities));
    /* Clear the 64-bit script context side table to prevent stale pointers */
    MemClear(gEntityScriptCtxTable, sizeof(gEntityScriptCtxTable));
#else
    MemClear(&gPlayerEntity.base, 10880);
#endif
#ifdef PC_PORT
    MemClear(&gUnk_02033290, sizeof(gUnk_02033290));
#else
    MemClear(&gUnk_02033290, 2048);
#endif
    sub_0805E98C();
    gEntCount = 0;
    gManagerCount = 0;
    gOAMControls.unk[0].unk7 = 1;
    gOAMControls.unk[0].unk6 = 1;
    gOAMControls.unk[1].unk6 = 1;
}

Entity* GetEmptyEntity() {
    u8 flags_ip;
    GenericEntity* end;
    GenericEntity* rv;
    GenericEntity* currentEnt;
    LinkedList* nextList;

    LinkedList* listPtr;
    LinkedList* endListPtr;

    if (gEntCount < MAX_ENTITIES - 1) {
        currentEnt = gEntities;
        end = currentEnt + ARRAY_COUNT(gEntities);

        do {
            if (currentEnt->base.prev == 0) {
                return &currentEnt->base;
            }
        } while (++currentEnt < end);
    }

#ifdef PC_PORT
    /* On PC, scan each array separately (not contiguous). */
    {
        int i;
        Entity* ent;
        ent = &gPlayerEntity.base;
        if ((intptr_t)ent->prev < 0 && (ent->flags & (ENT_UNUSED1 | ENT_UNUSED2)) &&
            ent != gUpdateContext.current_entity) {
            ClearDeletedEntity(ent);
            return ent;
        }
        for (i = 0; i < MAX_AUX_PLAYER_ENTITIES; i++) {
            ent = &gAuxPlayerEntities[i].base;
            if ((intptr_t)ent->prev < 0 && (ent->flags & (ENT_UNUSED1 | ENT_UNUSED2)) &&
                ent != gUpdateContext.current_entity) {
                ClearDeletedEntity(ent);
                return ent;
            }
        }
        for (i = 0; i < MAX_ENTITIES; i++) {
            ent = &gEntities[i].base;
            if ((intptr_t)ent->prev < 0 && (ent->flags & (ENT_UNUSED1 | ENT_UNUSED2)) &&
                ent != gUpdateContext.current_entity) {
                ClearDeletedEntity(ent);
                return ent;
            }
        }
    }
#else
    currentEnt = (GenericEntity*)&gPlayerEntity.base;

    do {
        if ((intptr_t)currentEnt->base.prev < 0 && (currentEnt->base.flags & (ENT_UNUSED1 | ENT_UNUSED2)) &&
            (Entity*)currentEnt != gUpdateContext.current_entity) {
            ClearDeletedEntity(&currentEnt->base);
            return &currentEnt->base;
        }
    } while (++currentEnt < (GenericEntity*)&gCarriedEntity);
#endif

    flags_ip = 0;
    rv = NULL;
    listPtr = gEntityLists;
    endListPtr = listPtr + ARRAY_COUNT(gEntityLists);

    do {
        currentEnt = (GenericEntity*)listPtr->first;
        nextList = listPtr + 1;
#ifdef PC_PORT
        /* #93 follow-up: post-cutscene corruption — same pattern as
         * RecycleEntities/etc. NULL-guard the iteration. */
        if (currentEnt == NULL) {
            listPtr = nextList;
            continue;
        }
#endif
#ifdef PC_PORT
        /* Bounded iteration safety: gEntityLists is a doubly-linked list
         * with listPtr as the sentinel. We've seen Lake Hylia post-warp
         * produce a cycle that doesn't include the sentinel (entity
         * free-list corruption after a specific room-init sequence) —
         * walker spins forever, main thread hangs, OS prompts force-quit.
         * Cap at 256 (4× MAX_ENTITIES, way above any real list length);
         * log once-per-list-per-session with the suspect entity so the
         * underlying corruption can be tracked down. */
        int safety = 0;
        const int kMaxSteps = 256;
#endif
        while ((intptr_t)currentEnt != (intptr_t)listPtr
#ifdef PC_PORT
               && currentEnt != NULL && safety < kMaxSteps && Port_IsValidEntityAddr(currentEnt)
#endif
        ) {
            if (currentEnt->base.kind != MANAGER &&
                flags_ip < (currentEnt->base.flags & (ENT_UNUSED1 | ENT_UNUSED2 | ENT_DELETED)) &&
                gUpdateContext.current_entity != &currentEnt->base) {
                flags_ip = currentEnt->base.flags & (ENT_UNUSED1 | ENT_UNUSED2 | ENT_DELETED);
                rv = currentEnt;
            }
            currentEnt = (GenericEntity*)currentEnt->base.next;
#ifdef PC_PORT
            ++safety;
#endif
        }
#ifdef PC_PORT
        /* Cycle / out-of-pool detection. Without this the walker hangs
         * or SEGVs on lists corrupted by the Wind Ruins → Lake Hylia
         * warp. Destructive repair is necessary to prevent the hang:
         * splice currentEnt's next to the sentinel so subsequent walks
         * terminate. Caveat: when this runs DURING the room-init spawn
         * loop, it can orphan some of the freshly-spawned entities for
         * that room — Lake Hylia's tree portal is one such casualty.
         * The proper fix is to eliminate the corruption source upstream
         * (src/room.c's SetCurrentRoomPropertyList cache-refresh patch
         * eliminated the largest source — the post-warp lists being
         * iterated by stale `properties[1]` — but at least one more
         * remains). */
        if (safety >= kMaxSteps ||
            (currentEnt != NULL && (intptr_t)currentEnt != (intptr_t)listPtr && !Port_IsValidEntityAddr(currentEnt))) {
            static unsigned char sWarned = 0;
            const int listIdx = (int)(listPtr - gEntityLists);
            const unsigned char bit = (unsigned char)(1u << (listIdx & 7));
            if (!(sWarned & bit)) {
                sWarned |= bit;
                fprintf(stderr,
                        "[entity] GetEmptyEntity: %s on gEntityLists[%d] "
                        "after %d steps; last_ent=%p. Repairing list.\n",
                        (safety >= kMaxSteps) ? "cycle detected" : "out-of-pool next", listIdx, safety,
                        (void*)currentEnt);
            }
            /* Splice the cycle/garbage point back to the sentinel.
             * Only do this if currentEnt is a writable pool address —
             * writing through an out-of-pool pointer corrupts unrelated
             * memory. If it's out-of-pool we can't safely splice; reset
             * the list head instead. */
            if (currentEnt != NULL && (intptr_t)currentEnt != (intptr_t)listPtr && Port_IsValidEntityAddr(currentEnt)) {
                currentEnt->base.next = (Entity*)listPtr;
                listPtr->last = (Entity*)currentEnt;
            } else {
                listPtr->first = (Entity*)listPtr;
                listPtr->last = (Entity*)listPtr;
            }
        }
#endif

        listPtr = nextList;
    } while (listPtr < endListPtr);

    if (rv) {
        DeleteEntity(&rv->base);
        ClearDeletedEntity(&rv->base);
    }

#ifdef PC_PORT
    /* Pool-orphan recovery. If we reached here with rv==NULL (every list
     * walk failed AND no recyclable slot in the pool-scans), check
     * whether the pool is full of *orphaned* entities — ones with live
     * flags but whose prev/next no longer point at any valid pool
     * address. Those are leftovers from our containment splices (when
     * we reset corrupted lists to empty); they bumped gEntCount but
     * never got formally deleted, so the gEntCount<MAX gate above
     * skipped the never-used-slot scan, and the deleted-entity scan
     * never found them because prev isn't < 0.
     *
     * Recovery: mark each orphan as deleted (prev=-1, flags|=UNUSED1)
     * and decrement gEntCount. Then re-run the deleted-entity scan and
     * return one. Without this, Lake Hylia post-warp can never spawn a
     * tree portal / windcrest / objects because the pool is locked. */
    if (rv == NULL) {
        int recovered = 0;
        int i;
        for (i = 0; i < MAX_ENTITIES; i++) {
            Entity* ent = &gEntities[i].base;
            /* Live (kind!=0 or flags set) but orphaned (prev/next outside
             * all valid entity pool ranges) → recover. */
            if (ent == gUpdateContext.current_entity)
                continue;
            const int liveFlag = (ent->flags & 0xFF) != 0 || ent->kind != 0;
            if (!liveFlag)
                continue;
            const int prevOk = Port_IsValidEntityAddr(ent->prev) || ent->prev == NULL;
            const int nextOk = Port_IsValidEntityAddr(ent->next) || ent->next == NULL;
            if (prevOk && nextOk)
                continue;
            /* Orphan. Reclaim it. */
            ent->prev = (Entity*)(intptr_t)-1;
            ent->next = NULL;
            ent->flags |= ENT_UNUSED1;
            if (ent->kind != MANAGER && gEntCount > 0) {
                gEntCount--;
            }
            ++recovered;
        }
        if (recovered > 0) {
            static int sLoggedRecovery = 0;
            if (!sLoggedRecovery) {
                sLoggedRecovery = 1;
                fprintf(stderr,
                        "[entity] GetEmptyEntity: recovered %d orphaned pool slots "
                        "(gEntCount now=%u). One-time warning.\n",
                        recovered, (unsigned)gEntCount);
            }
            /* Try the first available recovered slot. */
            for (i = 0; i < MAX_ENTITIES; i++) {
                Entity* ent = &gEntities[i].base;
                if ((intptr_t)ent->prev < 0 && (ent->flags & ENT_UNUSED1) && ent != gUpdateContext.current_entity) {
                    ClearDeletedEntity(ent);
                    return ent;
                }
            }
        }
    }
#endif

    return rv != NULL ? &rv->base : NULL;
}

Entity* CreateAuxPlayerEntity(void) {
    GenericEntity* ent = gAuxPlayerEntities;

    do {
        if (ent->base.prev == NULL) {
#ifdef PC_PORT
            memset(ent, 0, sizeof(GenericEntity));
#endif
            return &ent->base;
        }
    } while (++ent < &gAuxPlayerEntities[7]);

    return NULL;
}

typedef void* (*Getter)(void);

void* GetEmptyEntityByKind(u32 kind) {
    Getter getter = NULL;
    if (kind == MANAGER) {
        getter = (Getter)GetEmptyManager;
    } else {
        getter = (Getter)GetEmptyEntity;
    }
    return getter();
}

void DeleteThisEntity(void) {
    void (*f)(void);
#ifdef PC_PORT
    {
        Entity* ent = gUpdateContext.current_entity;
        if (ent && ent->kind == 6 && ent->id == 105) {
            extern void Port_LogOrchEvent(const char* op, void* ent);
            Port_LogOrchEvent("delete-this", (void*)ent);
        }
    }
#endif
    DeleteEntityAny(gUpdateContext.current_entity);
    f = ram_ClearAndUpdateEntities;
    f();
}

typedef void (*Deleter)(void*);

void DeleteEntityAny(Entity* ent) {
    Deleter deleter = NULL;
    if (ent->kind == MANAGER) {
        deleter = (Deleter)DeleteManager;
    } else {
        deleter = (Deleter)DeleteEntity;
    }
    deleter(ent);
}

void DeleteEntity(Entity* ent) {
#ifdef PC_PORT
    {
        extern void Port_LogEntityEvent(const char* op, void* ent, unsigned kind, unsigned id, void* prev, void* next);
        Port_LogEntityEvent("delete", (void*)ent, ent->kind, ent->id, ent->prev, ent->next);
        if (ent->kind == 6 && ent->id == 105) {
            extern void Port_DumpOrchStack(const char* tag, void* ent);
            Port_DumpOrchStack("DeleteEntity", (void*)ent);
        }
    }
#endif
    if (ent->next != NULL) {
        UnloadGFXSlots(ent);
        UnloadOBJPalette(ent);
        UnloadOBJPalette2(ent);
        RemoveInteractableObject(ent);
        FreeCarryEntity(ent);
        sub_0805EC60(ent);
        sub_08017744(ent);
        ReleaseTransitionManager(ent);
        UnloadCutsceneData(ent);
        UnloadHitbox(ent);
        zFree(ent->myHeap);
        ent->myHeap = NULL;
        if (ent->kind == ENEMY) {
            EnemyDetachFX(ent);
        }
        COLLISION_OFF(ent);
        ent->spriteSettings.draw = 0;
        ent->collisionFlags = 0;
        ent->contactFlags = 0;
        ent->knockbackDuration = 0;
        ent->health = 0;
        UnlinkEntity(ent);
        ent->next = NULL;
        ent->prev = (Entity*)(intptr_t)-1;
    }
}

void ClearAllDeletedEntities(void) {
#ifdef PC_PORT
    /* On PC, gPlayerEntity/gAuxPlayerEntities/gEntities are separate globals. */
    int i;
    if ((intptr_t)gPlayerEntity.base.prev < 0)
        ClearDeletedEntity(&gPlayerEntity.base);
    for (i = 0; i < MAX_AUX_PLAYER_ENTITIES; i++) {
        if ((intptr_t)gAuxPlayerEntities[i].base.prev < 0)
            ClearDeletedEntity(&gAuxPlayerEntities[i].base);
    }
    for (i = 0; i < MAX_ENTITIES; i++) {
        if ((intptr_t)gEntities[i].base.prev < 0)
            ClearDeletedEntity(&gEntities[i].base);
    }
#else
    GenericEntity* ent = (GenericEntity*)&gPlayerEntity.base;
    do {
        //! @bug if prev pointed to a VALID location higher than a signed int, would still be deleted
        if ((intptr_t)ent->base.prev < 0) {
            ClearDeletedEntity(&ent->base);
        }
    } while (ent++, ent < (((GenericEntity*)&gPlayerEntity.base) + 80));
#endif
}

void ClearDeletedEntity(Entity* ent) {
#ifdef PC_PORT
    {
        extern void Port_LogEntityEvent(const char* op, void* ent, unsigned kind, unsigned id, void* prev, void* next);
        Port_LogEntityEvent("clear-del", (void*)ent, ent->kind, ent->id, ent->prev, ent->next);
    }
    /* Use memset directly — DmaClear32 uses port_DmaTransfer which may not work for native pointers */
    memset(ent, 0, sizeof(GenericEntity));
#else
    DmaClear32(3, ent, sizeof(GenericEntity));
#endif
    gEntCount--;
}

void DeleteAllEntities(void) {
    Entity* ent;
    Entity* next;
    LinkedList* it;
#ifdef PC_PORT
    {
        extern void Port_LogOrchEvent(const char* op, void* ent);
        Port_LogOrchEvent("DELETEALL", NULL);
    }
#endif

    it = &gEntityLists[0];
    if (it->first) {
        do {
            for (ent = it->first; ent != NULL && (intptr_t)ent != (intptr_t)it; ent = next) {
                next = ent->next;
                DeleteEntityAny(ent);
            }
        } while (++it < &gEntityLists[9]);
        ClearAllDeletedEntities();
    }
}

// fix this
Manager* GetEmptyManager(void) {
    Temp* it;
    for (it = (Temp*)&gUnk_02033290; it < ((Temp*)&gUnk_02033290 + 32); it++) {
        if (it->prev == 0) {
            return (Manager*)it;
        }
    }
    return NULL;
}

void DeleteManager(void* ent) {
    Manager* manager = (Manager*)ent;
#ifdef PC_PORT
    if (manager->id == 0x2e) {
        fprintf(stderr, "[ToD-mgr] DeleteManager id=0x2e mgr=%p kind=0x%x type=0x%x action=0x%x area=%u room=%u\n",
                (void*)manager, (unsigned)manager->kind, (unsigned)manager->type, (unsigned)manager->action,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
    }
#endif
    if (manager->next == NULL)
        return;

    ReleaseTransitionManager(manager);
    UnlinkEntity((Entity*)manager);
    MemClear(manager, sizeof(Temp));
    gManagerCount--;
}

// Removes the entity from the transition manager duty, if applicable
void ReleaseTransitionManager(void* mgr) {
    if (mgr == gArea.transitionManager) {
        gArea.transitionManager = NULL;
        gArea.onEnter = NULL;
        gArea.onExit = NULL;
    }
}

extern LinkedList gEntityListsBackup[9];

void sub_0805E958(void) {
    MemCopy(gEntityLists, gEntityListsBackup, sizeof(gEntityLists));
    sub_0805E98C();
}

void sub_0805E974(void) {
    MemCopy(gEntityListsBackup, gEntityLists, sizeof(gEntityLists));
}

void sub_0805E98C(void) {
    LinkedList* list;

    for (list = gEntityLists; list < &gEntityLists[9]; list++) {
        list->last = (Entity*)list;
        list->first = (Entity*)list;
    }
}

void RecycleEntities(void) {
    Entity* i;
    LinkedList* list;

    list = &gEntityLists[0];
    do {
#ifdef PC_PORT
        /* #93 follow-up: post-takeover-cutscene, some lists are left
         * with first=NULL instead of the usual (Entity*)list sentinel
         * — the cutscene's entity churn ends with a list head that
         * never got re-initialised. The original loop unconditionally
         * dereferences i->flags on the first iteration, so first=NULL
         * → NULL+0x18 SIGSEGV (Entity.flags is at offset 0x18 on the
         * 64-bit port). Skip the list when it's in that uninitialised
         * state; the entities it would have touched are already gone.
         * The same guard pattern is applied to all entity-list
         * iterations in this file by adding `i != NULL &&` to the
         * loop condition (covers the "first ent's next pointer is
         * NULL mid-iteration" variant of the same bug). */
        if (list->first == NULL || list->last == NULL)
            continue;
#endif
#ifdef PC_PORT
        int _w = 0;
        for (i = list->first;
             i != NULL && (intptr_t)i != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(i);
             i = i->next, ++_w) {
#else
        for (i = list->first; i != NULL && (intptr_t)i != (intptr_t)list; i = i->next) {
#endif
            i->flags &= ~ENT_SCRIPTED;
            if ((i->flags & ENT_PERSIST) == 0) {
                i->flags |= ENT_DELETED;
            }
        }
#ifdef PC_PORT
        if (ENT_WALK_TRIPPED(_w))
            ENT_WALK_WARN(list, i);
#endif
    } while (++list < &gEntityLists[9]);
}

void DeleteSleepingEntities(void) {
    Entity* ent;
    Entity* next;
    LinkedList* list;

    list = &gEntityLists[0];
    do {
#ifdef PC_PORT
        if (list->first == NULL || list->last == NULL)
            continue;
#endif
#ifdef PC_PORT
        int _w = 0;
        for (ent = list->first;
             ent != NULL && (intptr_t)ent != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(ent);
             ent = next, ++_w) {
#else
        for (ent = list->first; ent != NULL && (intptr_t)ent != (intptr_t)list; ent = next) {
#endif
            next = ent->next;
            if (ent->flags & ENT_DELETED)
                DeleteEntityAny(ent);
        }
#ifdef PC_PORT
        if (ENT_WALK_TRIPPED(_w))
            ENT_WALK_WARN(list, ent);
#endif
    } while (++list < &gEntityLists[9]);
}

void AppendEntityToList(Entity* entity, u32 listIndex) {
    LinkedList* list;

    list = &gEntityLists[listIndex];
#ifdef PC_PORT
    /* #93 follow-up: if a prior cutscene left the list head with
     * NULL first/last (uninitialised state), re-init it as empty
     * before appending. Otherwise `list->last->next = entity`
     * dereferences NULL.last and crashes. */
    if (list->first == NULL || list->last == NULL) {
        list->first = (Entity*)list;
        list->last = (Entity*)list;
    }
#endif
    entity->next = (Entity*)list;
    entity->prev = list->last;
    list->last->next = entity;
    list->last = entity;
    if (entity->kind != MANAGER) {
        entity->spritePriority.b0 = 4;
        gEntCount++;
    } else {
        gManagerCount++;
    }
    InitDefaultPriority(entity);
#ifdef PC_PORT
    /* #93 chase: log every kind=6 (OBJECT) list-assignment so we can
     * see which list orchestrators (id=0x69) land in, and catch the
     * moment one gets re-listed mid-cutscene. */
    if (entity->kind == 6) {
        extern void Port_LogListOp(const char* op, void* ent, unsigned kind, unsigned id, unsigned listIdx,
                                   void* listAddr);
        Port_LogListOp("append", (void*)entity, entity->kind, entity->id, listIndex, (void*)list);
    }
#endif
}

void PrependEntityToList(Entity* entity, u32 listIndex) {
    LinkedList* list;

    UnlinkEntity(entity);
    list = &gEntityLists[listIndex];
#ifdef PC_PORT
    if (list->first == NULL || list->last == NULL) {
        list->first = (Entity*)list;
        list->last = (Entity*)list;
    }
#endif
    entity->prev = (Entity*)list;
    entity->next = list->first;
    list->first->prev = entity;
    list->first = entity;
#ifdef PC_PORT
    if (entity->kind == 6) {
        extern void Port_LogListOp(const char* op, void* ent, unsigned kind, unsigned id, unsigned listIdx,
                                   void* listAddr);
        Port_LogListOp("prepend", (void*)entity, entity->kind, entity->id, listIndex, (void*)list);
    }
#endif
}

static void UnlinkEntity(Entity* ent) {
#ifdef PC_PORT
    {
        extern void Port_LogEntityEvent(const char* op, void* ent, unsigned kind, unsigned id, void* prev, void* next);
        Port_LogEntityEvent("unlink", (void*)ent, ent->kind, ent->id, ent->prev, ent->next);
    }
#endif
    if (ent == gUpdateContext.current_entity) {
        gUpdateContext.current_entity = ent->prev;
    }
#ifdef PC_PORT
    /* Defensive guard against #131-class crashes where ent->prev gets
     * partially overwritten by a GBA-era 32-bit write to what's now an
     * 8-byte pointer on PC (the "half-pointer write" hazard).
     * Symptom: prev like 0x0000_5555_11113333 — high
     * bytes from the original pointer survive, low 4 bytes hold an
     * unrelated u32. The deref ent->prev->next then either segfaults
     * (low bytes alone point to unmapped memory) or trashes random
     * memory elsewhere.
     *
     * If prev/next aren't in the entity-pool range (or NULL/-1), we
     * have undefined data. Skip the link update — the entity may not
     * be in any list, OR the link state is too corrupted to fix.
     * Log the corruption so we can chase the writer separately. */
    {
        extern int Port_IsValidEntityAddr(const void*);
        const bool prevOk = ent->prev == NULL || (intptr_t)ent->prev == -1 || Port_IsValidEntityAddr(ent->prev);
        const bool nextOk = ent->next == NULL || Port_IsValidEntityAddr(ent->next);
        if (!prevOk || !nextOk) {
            fprintf(stderr,
                    "[ENT-GUARD] UnlinkEntity skipped: ent=%p kind=%u id=0x%X "
                    "prev=%p next=%p (one or both invalid)\n",
                    (void*)ent, ent->kind, ent->id, ent->prev, ent->next);
            return;
        }
    }
#endif
    ent->prev->next = ent->next;
    ent->next->prev = ent->prev;
}

bool32 EntityHasDuplicateID(Entity* ent) {
    Entity* i;
    LinkedList* list;

    list = &gEntityLists[0];
    do {
#ifdef PC_PORT
        int _w = 0;
        for (i = list->first;
             i != NULL && (intptr_t)i != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(i);
             i = i->next, ++_w) {
#else
        for (i = list->first; i != NULL && (intptr_t)i != (intptr_t)list; i = i->next) {
#endif
            if (i != ent && i->kind == ent->kind && i->id == ent->id) {
                return TRUE;
            }
        }
#ifdef PC_PORT
        if (ENT_WALK_TRIPPED(_w))
            ENT_WALK_WARN(list, i);
#endif
    } while (++list < &gEntityLists[9]);

    return FALSE;
}

Entity* FindEntityByID(u32 kind, u32 id, u32 listIndex) {
    Entity* it;
    LinkedList* list;

    list = &gEntityLists[listIndex];
#ifdef PC_PORT
    int _w = 0;
    for (it = list->first;
         it != NULL && (intptr_t)it != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(it);
         it = it->next, ++_w) {
#else
    for (it = list->first; it != NULL && (intptr_t)it != (intptr_t)list; it = it->next) {
#endif
        if (kind == it->kind && id == it->id)
            return it;
    }
#ifdef PC_PORT
    if (ENT_WALK_TRIPPED(_w))
        ENT_WALK_WARN(list, it);
#endif
    return NULL;
}

Entity* FindEntity(u32 kind, u32 id, u32 listIndex, u32 type, u32 type2) {
    Entity* i;
    LinkedList* list;

    list = &gEntityLists[listIndex];
#ifdef PC_PORT
    int _w = 0;
    for (i = list->first;
         i != NULL && (intptr_t)i != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(i);
         i = i->next, ++_w) {
#else
    for (i = list->first; i != NULL && (intptr_t)i != (intptr_t)list; i = i->next) {
#endif
        if (kind == i->kind && id == i->id && type == i->type && type2 == i->type2)
            return i;
    }
#ifdef PC_PORT
    if (ENT_WALK_TRIPPED(_w))
        ENT_WALK_WARN(list, i);
#endif
    return NULL;
}

Entity* FindNextDuplicateID(Entity* ent, int listIndex) {
    Entity* i;
    LinkedList* list;

    list = &gEntityLists[listIndex];
#ifdef PC_PORT
    int _w = 0;
    for (i = ent->next; i != NULL && (intptr_t)i != (intptr_t)list && !ENT_WALK_TRIPPED(_w); i = i->next, ++_w) {
#else
    for (i = ent->next; i != NULL && (intptr_t)i != (intptr_t)list; i = i->next) {
#endif
        if (i->kind == ent->kind && i->id == ent->id)
            return i;
    }
#ifdef PC_PORT
    if (ENT_WALK_TRIPPED(_w))
        ENT_WALK_WARN(list, i);
#endif
    return NULL;
}

Entity* DeepFindEntityByID(u32 kind, u32 id) {
    Entity* i;
    LinkedList* list;

    list = &gEntityLists[0];
    do {
#ifdef PC_PORT
        int _w = 0;
        for (i = (Entity*)list->first;
             i != NULL && (intptr_t)i != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(i);
             i = i->next, ++_w) {
#else
        for (i = (Entity*)list->first; i != NULL && (intptr_t)i != (intptr_t)list; i = i->next) {
#endif
            if (kind == i->kind && (id == i->id))
                return i;
        }
#ifdef PC_PORT
        if (ENT_WALK_TRIPPED(_w))
            ENT_WALK_WARN(list, i);
#endif
    } while (++list < &gEntityLists[9]);

    return NULL;
}

void DeleteAllEnemies(void) {
    Entity* ent;
    Entity* next;
    LinkedList* list;

    list = &gEntityLists[0];
    do {
#ifdef PC_PORT
        int _w = 0;
        for (ent = list->first;
             ent != NULL && (intptr_t)ent != (intptr_t)list && !ENT_WALK_TRIPPED(_w) && Port_IsValidEntityAddr(ent);
             ent = next, ++_w) {
#else
        for (ent = list->first; ent != NULL && (intptr_t)ent != (intptr_t)list; ent = next) {
#endif
            next = ent->next;
            if (ent->kind == ENEMY)
                DeleteEntity(ent);
        }
#ifdef PC_PORT
        if (ENT_WALK_TRIPPED(_w))
            ENT_WALK_WARN(list, ent);
#endif
    } while (++list < &gEntityLists[9]);
    ClearAllDeletedEntities();
}
