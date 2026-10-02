#include "port_second_screen_state.h"

#include <string.h>

#if defined(__ANDROID__) || defined(TMC_3DS)

#ifdef __ANDROID__
#include <pthread.h>
#define SNAPSHOT_MUTEX_TYPE pthread_mutex_t
#define SNAPSHOT_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define SNAPSHOT_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define SNAPSHOT_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#else
#include "port_second_screen_sync_3ds.h"
#define SNAPSHOT_MUTEX_TYPE unsigned char
#define SNAPSHOT_MUTEX_INITIALIZER 0
#define SNAPSHOT_MUTEX_LOCK(m) Port_SecondScreen_3DS_LockSnapshot()
#define SNAPSHOT_MUTEX_UNLOCK(m) Port_SecondScreen_3DS_UnlockSnapshot()
#endif

#include "area.h"
#include "game.h"
#include "item.h"
#include "itemMetaData.h"
#include "main.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "subtask.h" /* sub_080A6F40 — the map screens' hint-visibility word */
#include "ui.h"

/* The HUD's per-language button-label frame offsets (data/const/ui.s),
 * declared exactly as src/ui.c declares them. */
extern const u8 gUnk_080C9044[];

/* gItemMetaData menu slot the quest screen treats as "a carried quest
 * item" rather than a well of its own: sub_080A5594 spreads every item
 * marked with it across three slots instead of placing it directly. */
#define QUEST_CARRIED_ITEM_SLOT 1

static SecondScreenSnapshot sSnapshot;
static SNAPSHOT_MUTEX_TYPE sSnapshotMutex = SNAPSHOT_MUTEX_INITIALIZER;

/* Pending tap-to-equip request from the UI thread, consumed by Publish().
 * itemId 0 = nothing pending. Guarded by sSnapshotMutex — same one-memcpy
 * discipline, never held across engine calls. */
static uint8_t sPendingEquipItem = 0;
static uint8_t sPendingEquipSlot = 0;

/* Port-side automap: which rooms of each area have been entered this
 * session. TMC's own per-room "visited" state is scattered across
 * area-specific local flags with no uniform room->flag mapping, so the
 * port tracks it directly — same approach as zelda3-android's visited-room
 * dungeon map. Game-thread only. */
static uint64_t sVisitedByArea[256];

void Port_SecondScreenState_Publish(void) {
    /* Assembled outside the lock: this runs on the game thread itself, the
     * same thread that owns gRoomControls/gPlayerEntity/gSave during normal
     * gameplay, so reading them here is exactly as safe as any other engine
     * code doing so — no cross-thread race on this side. The lock only
     * needs to guard the swap into sSnapshot, which the second-screen
     * thread does read cross-thread. */
    SecondScreenSnapshot next;
    memset(&next, 0, sizeof(next));

    uint8_t equipItem;
    uint8_t equipSlot;
    SNAPSHOT_MUTEX_LOCK(&sSnapshotMutex);
    equipItem = sPendingEquipItem;
    equipSlot = sPendingEquipSlot;
    sPendingEquipItem = 0;
    SNAPSHOT_MUTEX_UNLOCK(&sSnapshotMutex);

    next.inGame = gMain.task == TASK_GAME;
    if (next.inGame) {
        /* Tap-to-equip goes through the engine's own path (swap handling,
         * HUD refresh) and only for items actually in the inventory — a
         * stale tap from a previous save can't equip something Link
         * doesn't own. */
        if (equipItem != 0 && GetInventoryValue(equipItem) == 1) {
            ForceEquipItem(equipItem, equipSlot ? EQUIP_SLOT_B : EQUIP_SLOT_A);
        }

        next.area = gRoomControls.area;
        next.room = gRoomControls.room;
        next.playerX = gPlayerEntity.base.x.HALF.HI;
        next.playerY = gPlayerEntity.base.y.HALF.HI;
        next.equippedA = gSave.stats.equipped[SLOT_A];
        next.equippedB = gSave.stats.equipped[SLOT_B];
        next.equippedSlotA = next.equippedA ? gItemMetaData[next.equippedA].menuSlot : 0xFF;
        next.equippedSlotB = next.equippedB ? gItemMetaData[next.equippedB].menuSlot : 0xFF;
        next.health = gSave.stats.health;
        next.maxHealth = gSave.stats.maxHealth;
        next.rupees = gSave.stats.rupees;

        /* Area identity + per-dungeon save state, gated exactly like the
         * engine gates it: dungeonKeys/dungeonItems are only meaningful
         * where AreaHasKeys() holds (src/gameUtils.c reads them through
         * gArea.dungeon_idx under that same check). */
        next.areaFlags = gArea.areaMetadata;
        next.dungeonIdx = gArea.dungeon_idx;
        if ((next.areaFlags & SECOND_SCREEN_AR_HAS_KEYS) && next.dungeonIdx < 0x10) {
            next.dungeonKeys = gSave.dungeonKeys[next.dungeonIdx];
            next.dungeonItemBits = gSave.dungeonItems[next.dungeonIdx];
        }

        /* Quest state for the status strip — plain gSave field reads. */
        for (u32 i = 0; i < 4; i++) {
            if (GetInventoryValue(ITEM_EARTH_ELEMENT + i) == 1) {
                next.elements |= (uint8_t)(1u << i);
            }
        }
        next.walletType = gSave.stats.walletType & 3;
        next.walletMax = gWalletSizes[next.walletType].size;
        next.bombCount = gSave.stats.bombCount;
        next.bombMax = gBombBagSizes[gSave.stats.bombBagType & 3];
        next.arrowCount = gSave.stats.arrowCount;
        next.arrowMax = gQuiverSizes[gSave.stats.quiverType & 3];
        next.kinstoneFused = gSave.kinstones.fusedCount;
        for (u32 i = 0; i < 19; i++) {
            next.kinstoneBag += gSave.kinstones.amounts[i];
        }

        /* QUEST STATUS screen values, read exactly where sub_080A5594
         * (src/menu/pauseMenu.c) reads them when it fills that screen's
         * sixteen slots. Plain gSave and GetInventoryValue reads, no
         * different in cost or safety from the ones above. */
        next.heartPieces = gSave.stats.heartPieces;
        for (u32 i = ITEM_SKILL_SPIN_ATTACK; i <= ITEM_SKILL_PERIL_BEAM; i++) {
            if (GetInventoryValue(i) != 0) {
                next.swordSkills++;
                next.swordSkillBits |= (uint8_t)(1u << (i - ITEM_SKILL_SPIN_ATTACK));
            }
        }
        /* The bag rows verbatim, for the pieces list behind the bag's well. */
        for (u32 i = 0; i < 19; i++) {
            next.kinstoneTypes[i] = gSave.kinstones.types[i];
            next.kinstoneAmounts[i] = gSave.kinstones.amounts[i];
        }
        next.shells = gSave.stats.shells;
        next.shellsOwned = (uint8_t)GetInventoryValue(ITEM_SHELLS);
        next.carlovMedal = (uint8_t)GetInventoryValue(ITEM_QST_CARLOV_MEDAL);
        next.tingleTrophy = (uint8_t)GetInventoryValue(ITEM_QST_TINGLE_TROPHY);
        next.kinstoneBagOwned = GetInventoryValue(ITEM_KINSTONE_BAG) != 0;
        for (u32 i = 0; i < 3; i++) {
            if (GetInventoryValue(ITEM_GRIP_RING + i) == 1) {
                next.passives |= (uint8_t)(1u << i);
            }
        }
        /* The carried-item tray, filled the way sub_080A5594's rolling
         * counter fills slots 6..8: every quest item whose metadata puts
         * it in the shared carried-item slot queues up in item-id order,
         * and once the tray is full the last position keeps whichever is
         * last owned. Items handed in (inventory value 2) drop out of the
         * tray, which is why the test is == 1 and not != 0. */
        {
            u32 tray = 0;
            for (u32 item = ITEM_QST_SWORD; item <= ITEM_FLIPPERS; item++) {
                if (gItemMetaData[item].menuSlot != QUEST_CARRIED_ITEM_SLOT ||
                    GetInventoryValue(item) != 1) {
                    continue;
                }
                next.questItems[tray] = (uint8_t)item;
                if (tray < 2) {
                    tray++;
                }
            }
        }
        {
            /* Owned figurines = set bits in the save's figurine bitset. */
            u32 n = 0;
            for (u32 i = 0; i < sizeof(gSave.figurines); i++) {
                u8 b = gSave.figurines[i];
                while (b) {
                    n += b & 1;
                    b >>= 1;
                }
            }
            next.figurineCount = n > 255 ? 255 : (uint8_t)n;
        }
        next.windcrests = gSave.windcrests;
        memcpy(next.fusedKinstones, gSave.kinstones.fusedKinstones, sizeof(next.fusedKinstones));
        memcpy(next.fusionUnmarked, gSave.kinstones.fusionUnmarked, sizeof(next.fusionUnmarked));

        /* Map hints, resolved the way both map screens resolve them:
         * `gSave.map_hints & sub_080A6F40()` (src/menu/pauseMenu.c
         * sub_080A6438, src/menu/pauseMenuScreen6.c sub_080A68D4). The
         * second operand walks gUnk_08128F38's (type, arg) pairs and clears
         * a hint's bit once sub_0807CB24 says its errand is done — local
         * flags and inventory, i.e. exactly the live save state the
         * second-screen render thread must not touch. The engine's own
         * function is called rather than re-derived: it is a pure predicate
         * (reads gSave, writes nothing), and a copy here would be one more
         * place to keep in step with the flag-bank remaps sub_0807CB24
         * applies on EU/JP. The real menu caches the same value on screen
         * entry (sub_080A6290); publishing per tick is only fresher. */
        next.mapHints = (uint16_t)(gSave.map_hints & sub_080A6F40());

        /* Contextual R prompt, resolved exactly like TextUIElement's
         * type2 == 9 branch (src/ui.c): the player-state action wins, else
         * the area's portal mode names the shrink/grow prompt, else the
         * interactable under Link. The frame id the HUD would actually
         * stamp is that value plus the language's label-block offset —
         * publish both, since the raw action is what names the prompt when
         * the label art isn't available. */
        {
            u32 rAction = gHUD.rActionPlayerState;
            if (rAction == R_ACTION_NONE) {
                switch (gArea.portal_mode) {
                    case 2:
                        rAction = R_ACTION_SHRINK;
                        break;
                    case 3:
                        rAction = R_ACTION_GROW;
                        break;
                    default:
                        rAction = gHUD.rActionInteractObject;
                        break;
                }
            }
            next.rActionId = (uint8_t)rAction;
            if (rAction != 0) {
                rAction += gUnk_080C9044[gSaveHeader->language];
            }
            next.rActionFrame = (uint8_t)rAction;
        }

        /* Mirror of the pause menu's item-screen fill loop
         * (src/menu/pauseMenu.c: PauseMenu_ItemMenu_Init): every owned
         * activatable item lands in its ItemMetaData menu slot; later item
         * ids overwrite earlier ones in the same slot, exactly like the
         * real menu. */
        for (u32 item = ITEM_SMITH_SWORD; item < ITEM_BOTTLE_EMPTY; item++) {
            if (GetInventoryValue(item) == 1) {
                u32 slot = gItemMetaData[item].menuSlot;
                if (slot < SECOND_SCREEN_ITEM_SLOTS) {
                    next.menuItems[slot] = (uint8_t)item;
                }
            }
        }
        for (u32 i = 0; i < 4; i++) {
            next.bottleContents[i] = gSave.stats.bottles[i];
        }

        for (u32 i = 0; i < SECOND_SCREEN_MAX_ROOMS && i < MAX_ROOMS; i++) {
            const RoomResInfo* info = &gArea.roomResInfos[i];
            next.rooms[i].x = info->map_x;
            next.rooms[i].y = info->map_y;
            next.rooms[i].w = info->pixel_width;
            next.rooms[i].h = info->pixel_height;
        }

        sVisitedByArea[next.area] |= 1ull << (next.room & 63);
        next.visitedMask = sVisitedByArea[next.area];
    }

    SNAPSHOT_MUTEX_LOCK(&sSnapshotMutex);
    sSnapshot = next;
    SNAPSHOT_MUTEX_UNLOCK(&sSnapshotMutex);
}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    SNAPSHOT_MUTEX_LOCK(&sSnapshotMutex);
    *out = sSnapshot;
    SNAPSHOT_MUTEX_UNLOCK(&sSnapshotMutex);
}

void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) {
    SNAPSHOT_MUTEX_LOCK(&sSnapshotMutex);
    sPendingEquipItem = itemId;
    sPendingEquipSlot = slot;
    SNAPSHOT_MUTEX_UNLOCK(&sSnapshotMutex);
}

#else /* Platforms without a live second-screen state consumer. */

void Port_SecondScreenState_Publish(void) {}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    memset(out, 0, sizeof(*out));
}

void Port_SecondScreenState_RequestEquip(uint8_t itemId, uint8_t slot) {
    (void)itemId;
    (void)slot;
}

#endif
