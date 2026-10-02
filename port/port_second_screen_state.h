#ifndef PORT_SECOND_SCREEN_STATE_H
#define PORT_SECOND_SCREEN_STATE_H

/*
 * Thread-safe game-state snapshot for the second-screen render thread.
 *
 * Modeled on port_m4a_backend.cpp's sStateMutex pattern: the game thread
 * publishes a small copy of the fields the second screen needs once per
 * tick; the second-screen render thread only ever reads that published
 * copy, never gSave/gRoomControls/gPlayerEntity directly. Lock is held only
 * for a memcpy on both sides — never around anything that renders or
 * blocks, so the second-screen thread can never stall the game thread.
 *
 * Input flows the other way through the same file: the UI thread requests
 * an equip (tap on the item grid) via Port_SecondScreenState_RequestEquip;
 * Publish() applies it on the game thread through the engine's own
 * ForceEquipItem, so the save file is only ever touched from the thread
 * that owns it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches MAX_ROOMS (include/area.h). */
#define SECOND_SCREEN_MAX_ROOMS 64

/* MENU_SLOT_SWORD..MENU_SLOT_BOTTLE3 (include/itemMetaData.h) — the 16
 * equipable slots of the pause menu's item screen, excluding the save
 * button pseudo-slots. */
#define SECOND_SCREEN_ITEM_SLOTS 16

typedef struct {
    uint16_t x, y; /* room origin within the area, pixels (RoomResInfo.map_x/y) */
    uint16_t w, h; /* room size, pixels; w==0 or h==0 -> slot unused */
} SecondScreenRoom;

/* gArea.areaMetadata bits (include/area.h AreaStateFlags) — republished
 * verbatim so the render side applies the same gating the engine does
 * (AreaIsDungeon / AreaHasKeys / AreaHasMap in src/gameUtils.c). */
#define SECOND_SCREEN_AR_IS_OVERWORLD 0x01
#define SECOND_SCREEN_AR_HAS_KEYS 0x02
#define SECOND_SCREEN_AR_IS_DUNGEON 0x04
#define SECOND_SCREEN_AR_HAS_MAP 0x08

typedef struct {
    uint8_t inGame; /* gMain.task == TASK_GAME; nothing else below is valid otherwise */
    uint8_t area;
    uint8_t room;
    uint8_t equippedA;
    uint8_t equippedB;
    /* Menu slot (grid index) each equipped item lives in, 0xFF if none.
     * Published alongside the raw ids because variants of one item (e.g.
     * lantern lit/unlit) share a slot but differ in id — highlighting by
     * slot matches what the real pause menu marks. */
    uint8_t equippedSlotA;
    uint8_t equippedSlotB;
    uint8_t health;    /* 8 units per heart */
    uint8_t maxHealth;
    uint16_t rupees;
    int32_t playerX; /* area-space pixels */
    int32_t playerY;
    /* Area identity: SECOND_SCREEN_AR_* bits plus the dungeon slot the
     * engine's own key/compass logic indexes with. */
    uint8_t areaFlags;  /* gArea.areaMetadata */
    uint8_t dungeonIdx; /* gArea.dungeon_idx */
    /* Current dungeon's save state (zeroed outside key-bearing areas):
     * key count plus the raw gSave.dungeonItems byte — bit 0 map, bit 1
     * compass, bit 2 big key, exactly what HasDungeonMap/Compass/BigKey
     * (src/gameUtils.c) test. */
    uint8_t dungeonKeys;
    uint8_t dungeonItemBits;
    /* Quest state for the status strip. */
    uint8_t elements;   /* bit n set = ITEM_EARTH_ELEMENT + n owned */
    uint8_t walletType; /* picks the HUD rupee icon tier */
    uint16_t walletMax; /* gWalletSizes[walletType].size; maxed => yellow digits */
    uint8_t bombCount;
    uint8_t bombMax;
    uint8_t arrowCount;
    uint8_t arrowMax;
    uint8_t kinstoneFused; /* gSave.kinstones.fusedCount */
    uint8_t figurineCount; /* set bits in gSave.figurines[] (owned figurines) */
    uint16_t kinstoneBag; /* pieces currently in the bag (sum of amounts[]) */
    /* QUEST STATUS screen (pause screen 2): the save values sub_080A5594
     * reads while filling its sixteen slots, published as the quantities
     * themselves. Which well each one lands in is slot-table and
     * gItemMetaData geometry the render side already has from ROM — only
     * the values have to cross the thread. */
    uint8_t heartPieces;  /* gSave.stats.heartPieces; the well shows heartPieces/4 */
    uint8_t swordSkills;  /* techniques owned, ITEM_SKILL_SPIN_ATTACK..ITEM_SKILL_PERIL_BEAM */
    uint8_t shellsOwned;  /* GetInventoryValue(ITEM_SHELLS): whether the shell well exists */
    uint8_t carlovMedal;  /* GetInventoryValue(ITEM_QST_CARLOV_MEDAL); 1 = won, and the medal
                           * takes the shell well. Published as the raw two-bit inventory
                           * value, not a flag, because sub_080A5594 tests both == 1 (place
                           * the medal) and == 0 (let the shell counter have the well), and
                           * a script can leave a quest item in the spent state 2. */
    uint8_t tingleTrophy; /* GetInventoryValue(ITEM_QST_TINGLE_TROPHY), same sense: 1 takes
                           * the kinstone bag's well, 0 leaves it to the bag */
    uint8_t kinstoneBagOwned; /* GetInventoryValue(ITEM_KINSTONE_BAG); owning the bag is what
                               * puts anything in that well, kinstoneBag only picks the tier */
    uint16_t shells;      /* gSave.stats.shells — the three digits under the shell */
    /* The carried quest items (broken sword, dog food, books, ...), in the
     * order sub_080A5594's rolling counter drops them into its three-slot
     * tray: ascending item id, the third slot holding the last one owned.
     * 0 = empty. Item ids, since the tray draws each one's own icon. */
    uint8_t questItems[3];
    uint8_t passives; /* bit n set = ITEM_GRIP_RING + n owned (ring, bracelets, flippers) */
    /* The two lists the quest tab opens from its own wells, published as the
     * save holds them so the render side can lay them out the way pause
     * screens 7 and 8 do.
     *
     * The bag is a parallel pair of arrays and NOT a fixed table: types[i] is
     * the kinstone id in bag row i and amounts[i] how many, packed from the
     * front and terminated by a zero type, which is exactly how
     * sub_080A6044 walks it. */
    uint8_t kinstoneTypes[19];
    uint8_t kinstoneAmounts[19];
    /* bit n set = the nth sword technique (ITEM_SKILL_SPIN_ATTACK + n) is
     * learned. swordSkills above is just this word's population count, but
     * the techniques list has to know WHICH eight. */
    uint8_t swordSkillBits;
    /* gSave.windcrests verbatim; bits 24..31 are the WindcrestID unlock
     * flags (include/windcrest.h) the fast-travel screen checks. */
    uint32_t windcrests;
    /* gSave.kinstones fusion-marker state, verbatim: which fusions have
     * happened and which map markers the game has retired — the world-map
     * module turns these into the enlarged region map's fusion markers
     * exactly like pause screen 6 does (@see CheckKinstoneFused /
     * CheckFusionMapMarkerDisabled). */
    uint8_t fusedKinstones[13];
    uint8_t fusionUnmarked[13];
    /* Map hints currently showing, bit n = row n of gUnk_08128F58: the very
     * word the pause map's hint pass tests, `gSave.map_hints &
     * sub_080A6F40()`. The second half is a live predicate over local flags
     * and inventory (sub_0807CB24), which only the game thread may read —
     * publishing the resolved mask is what lets the ROM-only world-map
     * module place hint markers at all. */
    uint16_t mapHints;
    /* Contextual R-button prompt: the sprite frame id the game's own HUD
     * would draw beside R this frame (SPEAK / READ / LIFT / ...), resolved
     * exactly like TextUIElement does — gHUD.rActionPlayerState, else the
     * area's portal mode, else gHUD.rActionInteractObject — with the
     * language offset already applied. 0 = no prompt. Published so hiding
     * the top HUD never costs the player that cue. */
    uint8_t rActionFrame;
    /* The same prompt BEFORE the language offset, i.e. the raw
     * ActionRButton value (include/player.h): R_ACTION_SPEAK, _LIFT, ...
     * 0 = none, always 0 exactly when rActionFrame is. The frame id above
     * indexes the game's own label art; this one is what a caller needs to
     * name the action itself (the compositor letters the word with the
     * message font while the art path isn't decoded). */
    uint8_t rActionId;
    /* Pause-menu item screen contents: menuItems[menuSlot] = item id, 0 if
     * that slot is empty. Bottles report the ITEM_BOTTLE1..4 container id;
     * bottleContents[] carries what's inside for icon display. */
    uint8_t menuItems[SECOND_SCREEN_ITEM_SLOTS];
    uint8_t bottleContents[4];
    /* Rooms of the current area, indexed by room id. */
    SecondScreenRoom rooms[SECOND_SCREEN_MAX_ROOMS];
    /* Bit n set = room n of the current area has been entered this session
     * (port-side automap tracking, zelda3-android "visited rooms" style). */
    uint64_t visitedMask;
} SecondScreenSnapshot;

/* Called once per game tick from the main loop (src/main.c). Builds a fresh
 * snapshot from gRoomControls/gPlayerEntity/gSave/gArea, applies any
 * pending equip request, and swaps the snapshot in under a short-held
 * lock. No-op off Android (there is no second-screen thread to publish
 * to). */
void Port_SecondScreenState_Publish(void);

/* Called from the second-screen render thread. Copies the most recently
 * published snapshot into `out`. Safe to call even before the first
 * Publish() — returns a zeroed snapshot in that case. */
void Port_SecondScreenState_Read(SecondScreenSnapshot* out);

/* Called from the UI/JNI thread when the player taps an item on the second
 * screen. slot: 0 = A button, 1 = B button. Applied (and validated against
 * the inventory) on the game thread during the next Publish(); redundant
 * or invalid requests are dropped there. */
void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_STATE_H */
