#include "port_bottle_compat.h"

#include "flags.h"
#include "item_ids.h"
#include "kinstone.h"
#include "region.h"

#define PORT_EU_SMITH_BOTTLE_FLAG 0xB2u
#define PORT_EU_STALE_USA_SMITH_BOTTLE_FLAG 0xB4u

static bool32 Port_SaveBitIsSet(const u8* bits, u32 bit) {
    return (bits[bit >> 3] >> (bit & 7)) & 1u;
}

static void Port_SaveSetBit(u8* bits, u32 bit) {
    bits[bit >> 3] |= (u8)(1u << (bit & 7));
}

static void Port_SaveClearBit(u8* bits, u32 bit) {
    bits[bit >> 3] &= (u8)~(1u << (bit & 7));
}

static u32 Port_SaveInventoryValue(const SaveFile* save, u32 item) {
    return (save->inventory[item >> 2] >> ((item & 3u) << 1)) & 3u;
}

static bool32 Port_SaveOwnsAnyBottle(const SaveFile* save) {
    u32 item;

    for (item = ITEM_BOTTLE1; item <= ITEM_BOTTLE4; ++item) {
        if (Port_SaveInventoryValue(save, item) != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

bool32 Port_BottleRewardCanBeCollected(const SaveFile* save, u32 item) {
    u32 bottle;

    if (save == NULL) {
        return FALSE;
    }
    if (item < ITEM_BOTTLE1 || item > ITEM_BOTTLE4) {
        return TRUE;
    }
    for (bottle = ITEM_BOTTLE1; bottle <= ITEM_BOTTLE4; ++bottle) {
        if (Port_SaveInventoryValue(save, bottle) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

bool32 Port_SmithBottleFlagsNeedRepair(const SaveFile* save, bool32 nonstandardProfile) {
    bool32 correctFlagSet;

    if (save == NULL || nonstandardProfile || !REGION_IS_EU || save->invalid || !save->initialized) {
        return FALSE;
    }
    if (!Port_SaveBitIsSet(save->kinstones.fusedKinstones, KINSTONE_16) ||
        Port_SaveBitIsSet(save->kinstones.fusedKinstones, KINSTONE_A)) {
        return FALSE;
    }
    if (!Port_SaveBitIsSet(save->flags, FLAG_BANK_1 + PORT_EU_STALE_USA_SMITH_BOTTLE_FLAG)) {
        return FALSE;
    }

    correctFlagSet = Port_SaveBitIsSet(save->flags, FLAG_BANK_1 + PORT_EU_SMITH_BOTTLE_FLAG);
    return correctFlagSet || Port_SaveOwnsAnyBottle(save);
}

bool32 Port_RepairSmithBottleFlags(SaveFile* save, bool32 nonstandardProfile) {
    if (!Port_SmithBottleFlagsNeedRepair(save, nonstandardProfile)) {
        return FALSE;
    }

    Port_SaveSetBit(save->flags, FLAG_BANK_1 + PORT_EU_SMITH_BOTTLE_FLAG);
    Port_SaveClearBit(save->flags, FLAG_BANK_1 + PORT_EU_STALE_USA_SMITH_BOTTLE_FLAG);
    return TRUE;
}

bool32 Port_GoronBottleNeedsRepair(const SaveFile* save, bool32 nonstandardProfile) {
    if (save == NULL || nonstandardProfile || !REGION_IS_EU || save->invalid || !save->initialized) {
        return FALSE;
    }
    /* EU's dog-food delivery uses quest-item value 2, not USA's BIN_DOGFOOD.
     * The chest ordinal 0x76 is ROM-native in both verified Goron room lists. */
    if (!Port_SaveBitIsSet(save->flags, FLAG_BANK_4 + 0x76) ||
        !Port_SaveBitIsSet(save->kinstones.fusedKinstones, KINSTONE_2F) ||
        !Port_SaveBitIsSet(save->kinstones.fusedKinstones, KINSTONE_16) ||
        !Port_SaveBitIsSet(save->flags, FLAG_BANK_1 + PORT_EU_SMITH_BOTTLE_FLAG) ||
        !Port_SaveBitIsSet(save->flags, FLAG_BANK_0 + AKINDO_BOTTLE_SELL) ||
        Port_SaveInventoryValue(save, ITEM_QST_DOGFOOD) != 2) {
        return FALSE;
    }
    for (u32 i = 0; i < 3; ++i) {
        if (Port_SaveInventoryValue(save, ITEM_BOTTLE1 + i) != 1) {
            return FALSE;
        }
    }
    return Port_SaveInventoryValue(save, ITEM_BOTTLE4) == 0 && save->stats.bottles[3] == 0;
}

bool32 Port_RepairGoronBottle(SaveFile* save, bool32 nonstandardProfile) {
    if (!Port_GoronBottleNeedsRepair(save, nonstandardProfile)) {
        return FALSE;
    }
    Port_SaveClearBit(save->flags, FLAG_BANK_4 + 0x76);
    return TRUE;
}

static_assert(KAKERA_TAKARA_A == 0xB4u, "USA Smith bottle flag changed");
static_assert(KINSTONE_A == 0x0Au, "EU 0xB4 owner fusion changed");
static_assert(KINSTONE_16 == 0x16u, "Smith fusion id changed");
static_assert(ITEM_BOTTLE4 == ITEM_BOTTLE1 + 3, "bottle inventory ids must remain contiguous");
