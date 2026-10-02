#ifndef ITEM_H
#define ITEM_H

#include "global.h"
#include "entity.h"
#include "player.h"

extern u32 GiveItem(u32, u32);
extern u32 CreateRandomItemDrop(Entity*, u32);
extern void DisableRandomDrops();
extern void EnableRandomDrops(void);
extern u32 IsMinishItem(u32);

void CreateItemEntity(u32, u32, u32);
extern void ExecuteItemFunction(ItemBehavior* this, u32 index);

extern void ItemDebug(ItemBehavior*, u32);
extern void ItemSword(ItemBehavior*, u32);
extern void ItemBomb(ItemBehavior*, u32);
extern void ItemBow(ItemBehavior*, u32);
extern void ItemBoomerang(ItemBehavior*, u32);
extern void ItemShield(ItemBehavior*, u32);
extern void ItemLantern(ItemBehavior*, u32);
extern void ItemGustJar(ItemBehavior*, u32);
extern void ItemPacciCane(ItemBehavior*, u32);
extern void ItemMoleMitts(ItemBehavior*, u32);
extern void ItemRocsCape(ItemBehavior*, u32);
extern void ItemPegasusBoots(ItemBehavior*, u32);
extern void ItemOcarina(ItemBehavior*, u32);
extern void ItemTryPickupObject(ItemBehavior*, u32);
extern void ItemJarEmpty(ItemBehavior*, u32);

#include "item_ids.h"

/** Slot that the item is equipped in. */
typedef enum {
    EQUIP_SLOT_A,
    EQUIP_SLOT_B,
    EQUIP_SLOT_NONE,
} EquipSlot;

/** Function used to create the item. */
typedef enum {
    CREATE_ITEM_0,
    CREATE_ITEM_1,
    CREATE_ITEM_2,
    CREATE_ITEM_3,
    CREATE_ITEM_4,
    CREATE_ITEM_5,
} CreateItemFunc;

#endif // ITEM_H
