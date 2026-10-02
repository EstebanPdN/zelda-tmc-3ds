#include "port_bomb_compat.h"
#include "item_ids.h"
#include "kinstone.h"
#include "region.h"

static u32 Inventory(const SaveFile* save, u32 item) {
    return (save->inventory[item >> 2] >> ((item & 3) * 2)) & 3;
}

bool32 Port_BombInventoryNeedsRepair(const SaveFile* save, bool32 nonstandardProfile) {
    static const u8 capacities[] = { 10, 30, 50, 99 };
    if (!save || nonstandardProfile || !REGION_IS_EU || save->invalid || !save->initialized) return FALSE;
    /* Before the remote-bomb fusion, an owned vanilla bag unambiguously
     * implies normal bombs. Do not guess a variant after that exchange unlocks. */
    if ((save->kinstones.fusedKinstones[KINSTONE_1C >> 3] >> (KINSTONE_1C & 7)) & 1) return FALSE;
    return Inventory(save, ITEM_BOMBBAG) == 1 && Inventory(save, ITEM_BOMBS) == 0 &&
           Inventory(save, ITEM_REMOTE_BOMBS) == 0 && save->stats.bombBagType < 4 &&
           save->stats.bombCount <= capacities[save->stats.bombBagType];
}

bool32 Port_RepairBombInventory(SaveFile* save, bool32 nonstandardProfile) {
    if (!Port_BombInventoryNeedsRepair(save, nonstandardProfile)) return FALSE;
    save->inventory[ITEM_BOMBS >> 2] |= 1u << ((ITEM_BOMBS & 3) * 2);
    return TRUE;
}
