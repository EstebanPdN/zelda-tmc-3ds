#include <stdio.h>
#include <string.h>

#include "flags.h"
#include "item_ids.h"
#include "kinstone.h"
#include "port_bottle_compat.h"
#include "region.h"

int gActiveRegion = TMC_REGION_EU;
static int sFailures;

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                             \
        }                                            \
    } while (0)

static void SetBit(u8* bits, u32 bit) {
    bits[bit >> 3] |= (u8)(1u << (bit & 7));
}

static bool32 HasBit(const u8* bits, u32 bit) {
    return (bits[bit >> 3] >> (bit & 7)) & 1u;
}

static void SetSavedInventoryValue(SaveFile* save, u32 item, u32 value) {
    u32 shift = (item & 3u) << 1;
    save->inventory[item >> 2] =
        (u8)((save->inventory[item >> 2] & ~(3u << shift)) | ((value & 3u) << shift));
}

static void BuildAffectedSave(SaveFile* save) {
    memset(save, 0, sizeof(*save));
    save->initialized = 1;
    SetBit(save->kinstones.fusedKinstones, KINSTONE_16);
    SetBit(save->flags, FLAG_BANK_1 + 0xB4u);
    SetSavedInventoryValue(save, ITEM_BOTTLE1, 1);
    save->stats.bottles[0] = ITEM_BOTTLE_EMPTY;
}

int main(void) {
    SaveFile save;
    SaveFile before;

    BuildAffectedSave(&save);
    before = save;
    CHECK(Port_SmithBottleFlagsNeedRepair(&save, FALSE), "the exact pre-v1.2-E5 EU save is selected");
    CHECK(Port_RepairSmithBottleFlags(&save, FALSE), "the exact affected save is repaired");
    CHECK(HasBit(save.flags, FLAG_BANK_1 + 0xB2u), "the native EU Smith bottle flag is restored");
    CHECK(!HasBit(save.flags, FLAG_BANK_1 + 0xB4u), "the stale USA flag is released for its EU event");
    CHECK(memcmp(save.stats.bottles, before.stats.bottles, sizeof(save.stats.bottles)) == 0,
          "repair never removes or edits bottle contents");
    CHECK(memcmp(save.inventory, before.inventory, sizeof(save.inventory)) == 0,
          "repair never removes bottle ownership");
    CHECK(!Port_RepairSmithBottleFlags(&save, FALSE), "repair is one-shot");

    memset(&save, 0, sizeof(save));
    CHECK(Port_BottleRewardCanBeCollected(&save, ITEM_BOTTLE3), "a bottle reward accepts any free bottle slot");
    SetSavedInventoryValue(&save, ITEM_BOTTLE1, 1);
    SetSavedInventoryValue(&save, ITEM_BOTTLE2, 1);
    SetSavedInventoryValue(&save, ITEM_BOTTLE3, 1);
    SetSavedInventoryValue(&save, ITEM_BOTTLE4, 1);
    CHECK(!Port_BottleRewardCanBeCollected(&save, ITEM_BOTTLE3),
          "a fifth bottle cannot consume its chest when all slots are owned");
    CHECK(Port_BottleRewardCanBeCollected(&save, ITEM_BOMBBAG), "non-bottle chest rewards remain unchanged");

    BuildAffectedSave(&save);
    SetBit(save.flags, FLAG_BANK_1 + 0xB2u);
    CHECK(Port_RepairSmithBottleFlags(&save, FALSE), "a corrected duplicate save releases only the stale bit");
    CHECK(HasBit(save.flags, FLAG_BANK_1 + 0xB2u) && !HasBit(save.flags, FLAG_BANK_1 + 0xB4u),
          "the correct bit remains set while the stale bit is cleared");

    BuildAffectedSave(&save);
    SetBit(save.kinstones.fusedKinstones, KINSTONE_A);
    before = save;
    CHECK(!Port_RepairSmithBottleFlags(&save, FALSE), "EU saves where 0xB4 has a legitimate owner fail closed");
    CHECK(memcmp(&save, &before, sizeof(save)) == 0, "ambiguous progress is untouched");

    BuildAffectedSave(&save);
    memset(save.inventory, 0, sizeof(save.inventory));
    memset(save.stats.bottles, 0, sizeof(save.stats.bottles));
    CHECK(!Port_RepairSmithBottleFlags(&save, FALSE), "a fusion without evidence of collecting the bottle is untouched");

    BuildAffectedSave(&save);
    CHECK(!Port_RepairSmithBottleFlags(&save, TRUE), "randomizer saves are never migrated");

    BuildAffectedSave(&save);
    gActiveRegion = TMC_REGION_USA;
    CHECK(!Port_RepairSmithBottleFlags(&save, FALSE), "USA saves are never migrated");
    gActiveRegion = TMC_REGION_JP;
    CHECK(!Port_RepairSmithBottleFlags(&save, FALSE), "unverified JP saves are never migrated");

    if (sFailures != 0) {
        return 1;
    }
    puts("port_bottle_compat_test: ALL PASS");
    return 0;
}
