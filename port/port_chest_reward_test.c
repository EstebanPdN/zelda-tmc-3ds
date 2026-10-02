#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "area.h"
#include "entity.h"
#include "item.h"
#include "object.h"
#include "room.h"
#include "save.h"
#include "sound.h"
#include "tiles.h"
#include "beanstalkSubtask.h"
#include "region.h"

/* Exercise the production chest and item-pair allocation paths with a bounded
 * auxiliary pool. Rendering and the separate animation object are stubbed. */
void ChestSpawner_Type2Action4(void*);
bool32 sub_08084074(u32);
Area gArea;
RoomControls gRoomControls;
SaveFile gSave;
int gActiveRegion = TMC_REGION_EU;
static GenericEntity sItem, sAnimation;
static TileEntity sTiles[3];
static int sItemAvailable, sAnimationAvailable, sDeletes, sCreated, sFlagWrites, sErrors, sInteractable;
static int sFailures;

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); ++sFailures; } } while (0)

void* GetCurrentRoomProperty(u32 property) { return property == 3 ? sTiles : NULL; }
u32 sub_0800445C(Entity* e) { (void)e; return 0; }
void GetNextFrame(Entity* e) { (void)e; }
void InitializeAnimation(Entity* e, u32 animation) { e->animIndex = animation; }
void SoundReq(u32 sound) { sErrors += sound == SFX_MENU_ERROR; }
void SetLocalFlag(u32 flag) { (void)flag; ++sFlagWrites; }
void AddInteractableChest(void* chest) { (void)chest; ++sInteractable; }
void SetMultipleTiles(const TileData* data, u32 tile, u32 layer) { (void)data; (void)tile; (void)layer; }
Entity* CreateAuxPlayerEntity(void) {
    if (!sItemAvailable) return NULL;
    memset(&sItem, 0, sizeof(sItem));
    ++sCreated;
    return &sItem.base;
}
void AppendEntityToList(Entity* e, u32 list) { (void)list; e->next = e->prev = e; }
Entity* CreateLinkAnimation(Entity* parent, u32 type, u32 type2) {
    (void)parent; (void)type; (void)type2;
    return sAnimationAvailable ? &sAnimation.base : NULL;
}
void DeleteEntity(Entity* e) { ++sDeletes; e->next = e->prev = NULL; }

static void Reset(GenericEntity* chest) {
    memset(chest, 0, sizeof(*chest));
    memset(&gSave, 0, sizeof(gSave));
    memset(sTiles, 0, sizeof(sTiles));
    sTiles[0].type = SMALL_CHEST;
    sTiles[0].localFlag = 0x77;
    sTiles[1].type = BIG_CHEST;
    sTiles[1].localFlag = 0x76;
    sTiles[1]._2 = ITEM_BOTTLE4;
    chest->base.action = 4;
    chest->base.type2 = 0x76;
    chest->base.frame = ANIM_DONE;
    chest->base.subtimer = 30;
    sItemAvailable = sAnimationAvailable = 1;
    sDeletes = sCreated = sFlagWrites = sErrors = sInteractable = 0;
}

int main(void) {
    GenericEntity chest;
    /* Before the fix, the first finished-animation tick consumes the flag,
     * and failed allocation permanently leaves action 5 (an empty chest). */
    Reset(&chest);
    ChestSpawner_Type2Action4(&chest);
    CHECK(sFlagWrites == 0 && sCreated == 0, "opening animation cannot commit an uncreated reward");
    for (unsigned i = 0; i < 29; ++i) ChestSpawner_Type2Action4(&chest);
    CHECK(chest.base.action == 5 && sCreated == 1, "normal chest starts exactly one reward");
    CHECK(sFlagWrites == 0 && sItem.field_0x6a.HWORD == 0x76,
          "completion belongs to the item-get entity, not the chest animation");
    CHECK(sItem.base.type == ITEM_BOTTLE4 && sItem.base.parent == &sAnimation.base,
          "correct bottle reward has both cutscene entities");

    for (unsigned failure = 0; failure < 2; ++failure) {
        Reset(&chest);
        chest.base.subtimer = 1;
        if (failure == 0) sItemAvailable = 0; else sAnimationAvailable = 0;
        ChestSpawner_Type2Action4(&chest);
        CHECK(sFlagWrites == 0 && chest.base.action == 3 && sInteractable == 1 && sErrors == 1,
              "either allocation failure recloses an interactable, unconsumed chest");
        CHECK(sDeletes == (int)failure, "partial item pair is rolled back");
        sItemAvailable = sAnimationAvailable = 1;
        CHECK(sub_08084074(0x76), "reward can be retried after allocation recovers");
    }
    Reset(&chest);
    chest.base.subtimer = 1;
    gSave.inventory[ITEM_BOTTLE1 >> 2] = 0x55;
    ChestSpawner_Type2Action4(&chest);
    CHECK(sCreated == 0 && sFlagWrites == 0 && chest.base.action == 3, "full bottle inventory keeps the chest");
    Reset(&chest);
    CHECK(!sub_08084074(0x75) && sCreated == 0, "missing tile reward cannot complete a chest");
    sTiles[1]._2 = ITEM_RUPEE20;
    CHECK(sub_08084074(0x76) && sItem.base.type == ITEM_RUPEE20, "non-bottle big chest still works");
    Reset(&chest);
    chest.base.timer = 24;
    chest.base.subtimer = 1;
    ChestSpawner_Type2Action4(&chest);
    CHECK(chest.base.action == 6 && sFlagWrites == 1 && sCreated == 0, "rupee fountain keeps its own completion path");
    if (sFailures) return 1;
    puts("port_chest_reward_test: ALL PASS");
    return 0;
}
