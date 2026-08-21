#include "area.h"
#include "entity.h"
#include "manager/delayedEntityLoadManager.h"
#include "npc.h"
#include "object.h"
#include "room.h"
#include "save.h"
#include "script.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    Manager base;
    u8 bankOffset;
    u8 spawnedCount;
} TestDelayedEntityLoadManager;

Area gArea;
SaveFile gSave;
RoomControls gRoomControls;
NPCStruct gNPCData[NPC_DATA_CAPACITY];
u8 gUnk_020342F8[0x100];
u8 gEntCount;
u8* gRomData;
u32 gRomSize;

static ScriptExecutionContext sContext;
static Entity sSpawnedEntity;
static bool32 sContextAvailable;
static bool32 sObjectAvailable;
static int sContextRequests;
static int sContextRollbacks;
static int sObjectCreates;
static int sScriptInitializations;
static int sFailures;

static u16 sDummyScript[] = { 0xffff };
static u8 sDummyProperty;

void DelayedEntityLoadManager_Main(TestDelayedEntityLoadManager*);

void* GetCurrentRoomProperty(u32 property) {
    (void)property;
    return &sDummyProperty;
}

u32 CheckRectOnScreen(s32 x, s32 y, u32 marginX, u32 marginY) {
    (void)x;
    (void)y;
    (void)marginX;
    (void)marginY;
    return TRUE;
}

u32 ReadBit(void* src, u32 bit) {
    return (((u8*)src)[bit / 8] >> (bit & 7)) & 1;
}

u32 WriteBit(void* src, u32 bit) {
    u8* byte = &((u8*)src)[bit / 8];
    u8 mask = (u8)(1u << (bit & 7));
    u32 old = *byte & mask;
    *byte |= mask;
    return old;
}

u32 ClearBit(void* src, u32 bit) {
    u8* byte = &((u8*)src)[bit / 8];
    u8 mask = (u8)(1u << (bit & 7));
    u32 old = *byte & mask;
    *byte &= (u8)~mask;
    return old;
}

ScriptExecutionContext* CreateScriptExecutionContext(void) {
    sContextRequests++;
    return sContextAvailable ? &sContext : NULL;
}

void DestroyScriptExecutionContext(ScriptExecutionContext* context) {
    sContextRollbacks++;
    memset(context, 0, sizeof(*context));
}

Entity* CreateObject(Object id, u32 type, u32 type2) {
    (void)id;
    (void)type;
    (void)type2;
    sObjectCreates++;
    if (!sObjectAvailable) return NULL;
    memset(&sSpawnedEntity, 0, sizeof(sSpawnedEntity));
    return &sSpawnedEntity;
}

Entity* CreateNPC(u32 id, u32 type, u32 type2) {
    (void)id;
    (void)type;
    (void)type2;
    return NULL;
}

void InitScriptForEntity(Entity* entity, ScriptExecutionContext* context, Script* script) {
    sScriptInitializations++;
    entity->flags |= ENT_SCRIPTED;
    context->scriptInstructionPointer = script;
}

void SetEntityPriority(Entity* entity, u32 priority) {
    (void)entity;
    (void)priority;
}

void DeleteThisEntity(void) {
}

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                             \
        }                                            \
    } while (0)

static void ResetScenario(TestDelayedEntityLoadManager* manager) {
    memset(manager, 0, sizeof(*manager));
    memset(&gArea, 0, sizeof(gArea));
    memset(&gSave, 0, sizeof(gSave));
    memset(&gNPCData, 0, sizeof(gNPCData));
    memset(&gUnk_020342F8, 0, sizeof(gUnk_020342F8));
    memset(&sContext, 0, sizeof(sContext));
    memset(&sSpawnedEntity, 0, sizeof(sSpawnedEntity));

    manager->base.action = 1; /* Property list was already copied. */
    manager->base.type2 = 0;
    manager->base.timer = 1; /* Spawn objects, including WHIRLWIND. */
    manager->bankOffset = 0;

    gSave.global_progress = 0;
    gEntCount = 0;
    gNPCData[0].id = WHIRLWIND;
    gNPCData[0].collisionLayer = 1;
    gNPCData[0].x = 0x248;
    gNPCData[0].y = 0x48;
    gNPCData[0].script = sDummyScript;
    gNPCData[0].progressBitfield = 1;
    gNPCData[1].id = 0xff;

    sContextAvailable = FALSE;
    sObjectAvailable = TRUE;
    sContextRequests = 0;
    sContextRollbacks = 0;
    sObjectCreates = 0;
    sScriptInitializations = 0;
}

int main(void) {
    TestDelayedEntityLoadManager manager;

    ResetScenario(&manager);

    /* Reproduce OLD-6a: all script contexts are occupied on the first frame
     * that the hidden Cloud Tops whirlwind enters the loader's viewport. */
    DelayedEntityLoadManager_Main(&manager);
    CHECK(sContextRequests == 1, "script-context exhaustion is exercised");
    CHECK(sObjectCreates == 0, "no object is spawned without a script context");
    CHECK(ReadBit(gUnk_020342F8, 0) == 0,
          "failed context reservation leaves the delayed slot retryable");

    /* Once a context becomes free, the exact same on-screen slot must retry
     * and publish the object plus its script atomically. */
    sContextAvailable = TRUE;
    DelayedEntityLoadManager_Main(&manager);
    CHECK(sContextRequests == 2, "the on-screen scripted object is retried");
    CHECK(sObjectCreates == 1, "retry creates the hidden whirlwind");
    CHECK(sScriptInitializations == 1, "retry attaches its hidden-whirlwind script");
    CHECK(ReadBit(gUnk_020342F8, 0) != 0, "successful spawn commits the delayed slot bit");
    CHECK(sSpawnedEntity.health == 1, "spawned entity keeps the delayed-slot identity");
    CHECK(sSpawnedEntity.x.HALF.HI == 0x248 && sSpawnedEntity.y.HALF.HI == 0x48,
          "spawned entity keeps the Cloud Tops coordinates");

    /* A free script context followed by a full entity pool is a second
     * partial-allocation boundary.  Neither the context nor the delayed bit
     * may leak, and the next frame must still be able to retry. */
    ResetScenario(&manager);
    sContextAvailable = TRUE;
    sObjectAvailable = FALSE;
    DelayedEntityLoadManager_Main(&manager);
    CHECK(sObjectCreates == 1, "full entity pool is exercised after reserving a context");
    CHECK(sContextRollbacks == 1, "failed entity allocation rolls back the reserved context");
    CHECK(ReadBit(gUnk_020342F8, 0) == 0, "failed entity allocation leaves the slot retryable");

    sObjectAvailable = TRUE;
    DelayedEntityLoadManager_Main(&manager);
    CHECK(sObjectCreates == 2, "entity-pool recovery retries the hidden whirlwind");
    CHECK(sScriptInitializations == 1, "entity-pool recovery attaches the script once");
    CHECK(ReadBit(gUnk_020342F8, 0) != 0, "entity-pool recovery commits the slot once");

    /* Room transitions alternate the delayed loader between two 0x80-slot
     * banks.  Preserve the high health bit used by Whirlwind's bitmap check. */
    ResetScenario(&manager);
    gNPCData[0x80] = gNPCData[0];
    gNPCData[0x81].id = 0xff;
    manager.bankOffset = 0x80;
    sContextAvailable = TRUE;
    DelayedEntityLoadManager_Main(&manager);
    CHECK(ReadBit(gUnk_020342F8, 0x80) != 0, "alternate delayed bank commits its own bit");
    CHECK(sSpawnedEntity.health == 0x81, "alternate bank preserves the Whirlwind health identity");

    if (sFailures != 0) return 1;
    puts("port_delayed_entity_script_slot_test: ALL PASS");
    return 0;
}
