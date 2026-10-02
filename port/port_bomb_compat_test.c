#undef NDEBUG /* Assertions must execute in release test builds. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "port_bomb_compat.h"
#include "item.h"
#include "kinstone.h"
#include "region.h"
#include "ui.h"
#include "area.h"
SaveFile gSave;
HUD gHUD;
Area gArea;
int gActiveRegion = TMC_REGION_EU;
static bool sNonstandardProfile;
static int sBackup = 1;
int Port_Save_IsStandardProfile(void) { return !sNonstandardProfile; }
int Port_Save_PreserveBeforeBombInventoryRepair(void) { return sBackup; }
bool Port_Reborn_IsEnabled(int feature) { return false; }
u32 GetInventoryValue(u32 item) { return (gSave.inventory[item >> 2] >> ((item & 3) * 2)) & 3; }
u32 SetInventoryValue(u32 item, u32 value) {
    u32 shift = (item & 3) * 2;
    gSave.inventory[item >> 2] = (gSave.inventory[item >> 2] & ~(3u << shift)) | (value << shift);
    return value;
}
void SoundReq(u32 sound) {}
void LoadItemGfx(void) {}
s32 ModHealth(s32 amount) { return 0; }
void ModRupees(s32 amount) {}
void ModDungeonKeys(s32 amount) {}
void AddKinstoneToBag(u32 item) {}
void SetDungeonItem(u32 item) {}

int main(int argc, char** argv) {
    SaveFile before, expected;
    memset(&gSave, 0, sizeof(gSave));
    gSave.initialized = 1;
    gSave.stats.bombBagType = 1;
    gSave.stats.bombCount = 30;
    SetInventoryValue(ITEM_BOMBBAG, 1);
    if (argc == 2) {
        FILE* f = fopen(argv[1], "rb"); assert(f);
        assert(fread(&gSave, 1, sizeof(gSave), f) == sizeof(gSave)); fclose(f);
    }
    before = gSave;
    assert(Port_BombInventoryNeedsRepair(&gSave, false));
    SetInventoryValue(ITEM_BOMBS, 1); expected = gSave; gSave = before;
    assert(Port_RepairBombInventory(&gSave, false));
    assert(!memcmp(&gSave, &expected, sizeof(gSave)));
    assert(!Port_RepairBombInventory(&gSave, false));
    for (int i = 0; i < 9; ++i) {
        gSave = before; gActiveRegion = TMC_REGION_EU;
        switch (i) {
        case 0: gSave.invalid = 1; break;
        case 1: gSave.initialized = 0; break;
        case 2: SetInventoryValue(ITEM_BOMBBAG, 0); break;
        case 3: SetInventoryValue(ITEM_BOMBS, 1); break;
        case 4: SetInventoryValue(ITEM_REMOTE_BOMBS, 1); break;
        case 5: gSave.stats.bombBagType = 4; break;
        case 6: gSave.stats.bombCount = 100; break;
        case 7: gSave.kinstones.fusedKinstones[KINSTONE_1C >> 3] |= 1 << (KINSTONE_1C & 7); break;
        case 8: gActiveRegion = TMC_REGION_USA; break;
        }
        expected = gSave;
        assert(!Port_RepairBombInventory(&gSave, false));
        assert(!memcmp(&gSave, &expected, sizeof(gSave)));
    }
    gActiveRegion = TMC_REGION_EU; gSave = before;
    assert(!Port_RepairBombInventory(&gSave, true));
    assert(!Port_BombInventoryNeedsRepair(NULL, false));
    GiveItem(ITEM_BOMBBAG, 0);
    assert(GetInventoryValue(ITEM_BOMBS) == 1);
    assert(gSave.stats.bombBagType == before.stats.bombBagType + 1);
    gSave = before; sBackup = 0;
    GiveItem(ITEM_BOMBBAG, 0);
    assert(GetInventoryValue(ITEM_BOMBS) == 0);
    gSave = before; sBackup = 1; sNonstandardProfile = true;
    GiveItem(ITEM_BOMBBAG, 0);
    assert(GetInventoryValue(ITEM_BOMBS) == 0);
    puts("bomb_compat_test: ALL PASS (recovery, exclusions, production GiveItem)");
    return 0;
}
