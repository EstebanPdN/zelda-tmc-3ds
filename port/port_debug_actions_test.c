/*
 * port/port_debug_actions_test.c — regression test for the F8 debug-menu
 * action layer (port_debug_actions.c).
 *
 * Guards the per-item toggle exclusivity logic in particular: a post-commit
 * adversarial review found that keying exclusivity off gItemMetaData[].menuSlot
 * cross-cleared unrelated inventory (e.g. owning the Big Wallet wiped every
 * sword), because that engine byte aliases garbage for non-grid item ids. The
 * fix keys exclusivity off an explicit in-table group; this test pins that
 * behaviour plus the flag / dungeon / stat / bottle helpers so the same class
 * of bug can't silently return.
 *
 * Standalone binary: it compiles port_debug_actions.c and provides minimal
 * definitions/stubs for the engine globals + functions that TU references but
 * does not own. Only the save-state effects are asserted.
 */
#include <stdio.h>
#include <string.h>

#include "save.h"
#include "area.h"
#include "player.h"
#include "main.h"        /* Main / gMain */
#include "room.h"        /* RoomControls / RoomHeader / gRoomControls */
#include "transitions.h" /* Transition / RoomTransition / gRoomTransition */
#include "item_ids.h"
#include "flags.h"        /* FIGURE_ALLCOMP */
#include "port_debug_actions.h"

/* ---- engine state the action layer reads (the bits the test exercises) ---- */
SaveFile gSave;
Area gArea;

/* Capacity tables (4 tiers each, indexed by walletType/bombBagType/quiverType
 * 0..3): realistic caps so the stat-clamp assertions exercise real bounds. */
const Wallet gWalletSizes[] = { { 100, 0 }, { 300, 0 }, { 500, 0 }, { 999, 0 } };
const u8 gBombBagSizes[] = { 10, 30, 50, 99 };  /* mirrors src/itemUtils.c */
const u8 gQuiverSizes[]  = { 30, 50, 70, 99 };  /* mirrors src/itemUtils.c */

/* ---- symbols pulled in only by the warp/teleport/enumeration functions the
 *      test never calls; zero-init definitions + no-op stubs satisfy the link. */
Main gMain;
PlayerState gPlayerState;
PlayerEntity gPlayerEntity;
RoomControls gRoomControls;
RoomTransition gRoomTransition;
RoomHeader* gAreaRoomHeaders[0x90];
Hitbox gPlayerHitbox;

bool32 SetAffineInfo(Entity* entity, u32 x, u32 y, u32 z) { (void)entity; (void)x; (void)y; (void)z; return 0; }
void PlayerSetNormalAndCollide(void) {}
void LoadItemGfx(void) {}
void UpdatePlayerSkills(void) {}
void DoExitTransition(const Transition* data) { (void)data; }
u32  GetCollisionDataAtTilePos(u32 tilePos, u32 layer) { (void)tilePos; (void)layer; return 0; }
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr) { (void)ptr; return 0; }
void Port_RefreshAreaData(unsigned int area) { (void)area; }
bool Port_Config_GetConsoleParity(void) { return 0; }  /* config module not linked into this test */

/* ---- assertion harness ---- */
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (msg)); ++g_fails; } \
} while (0)

static int Owned(int item) { return Port_DebugQuery_ToggleItemOwned(item); }

/* Raw 2-bit inventory read by ITEM_* id (for items not in the toggle table). */
static int InvBit(int item) {
    return (gSave.inventory[item / 4] >> ((item % 4) * 2)) & 3;
}

/* Find a toggle-table index by display name (test reads the table the same way
 * the menu does, so it stays valid if the order changes). */
static int IdxByName(const char* name) {
    int n = Port_DebugQuery_ToggleItemCount();
    for (int i = 0; i < n; ++i) {
        const char* nm = Port_DebugQuery_ToggleItemName(i);
        if (nm && strcmp(nm, name) == 0) return i;
    }
    fprintf(stderr, "FAIL: toggle item '%s' not found\n", name);
    ++g_fails;
    return -1;
}

int main(void) {
    memset(&gSave, 0, sizeof(gSave));
    memset(&gArea, 0, sizeof(gArea));

    /* ---- #1 the regression: non-grid items must NOT clear unrelated items ---- */
    const int smith  = IdxByName("Smith's Sword");
    const int green  = IdxByName("Green Sword");
    const int wallet = IdxByName("Big Wallet");
    const int water  = IdxByName("Water Element");
    const int bow    = IdxByName("Bow");

    Port_DebugAction_SetToggleItem(smith, 1);
    CHECK(Owned(smith), "smith sword should be owned");
    Port_DebugAction_SetToggleItem(wallet, 1);
    CHECK(Owned(wallet), "big wallet should be owned");
    CHECK(Owned(smith), "REGRESSION: big wallet cleared the sword");

    Port_DebugAction_SetToggleItem(bow, 1);
    Port_DebugAction_SetToggleItem(water, 1);
    CHECK(Owned(water), "water element should be owned");
    CHECK(Owned(bow), "REGRESSION: water element cleared the bow");

    /* ---- exclusivity WITHIN a real group still works ---- */
    Port_DebugAction_SetToggleItem(green, 1);
    CHECK(Owned(green), "green sword should be owned");
    CHECK(!Owned(smith), "owning green sword should clear smith sword (same group)");

    const int bombs  = IdxByName("Bombs");
    const int remote = IdxByName("Remote Bombs");
    Port_DebugAction_SetToggleItem(bombs, 1);
    Port_DebugAction_SetToggleItem(remote, 1);
    CHECK(Owned(remote) && !Owned(bombs), "remote bombs should clear plain bombs");

    /* ---- equip cleared when an owned item is un-owned ---- */
    gSave.stats.equipped[0] = ITEM_GREEN_SWORD;
    Port_DebugAction_SetToggleItem(green, 0);
    CHECK(!Owned(green), "green sword un-owned");
    CHECK(gSave.stats.equipped[0] == ITEM_NONE, "un-owning should drop the A-slot equip");

    /* ---- bottle content ---- */
    Port_DebugAction_SetBottleContent(0, ITEM_BOTTLE_RED_POTION);
    CHECK(Port_DebugQuery_BottleOwned(0), "setting content should own the bottle");
    CHECK(Port_DebugQuery_BottleContent(0) == ITEM_BOTTLE_RED_POTION, "bottle content set");
    Port_DebugAction_SetBottleContent(0, 9999); /* out of range -> ignored */
    CHECK(Port_DebugQuery_BottleContent(0) == ITEM_BOTTLE_RED_POTION, "invalid content rejected");

    /* ---- dungeon items (bit 1=map,2=compass,4=bigkey) + keys ---- */
    Port_DebugAction_SetDungeonItem(3, 0, 1); /* map */
    Port_DebugAction_SetDungeonItem(3, 2, 1); /* big key */
    CHECK((gSave.dungeonItems[3] & 1) && (gSave.dungeonItems[3] & 4), "map+bigkey set");
    CHECK(!(gSave.dungeonItems[3] & 2), "compass not set");
    Port_DebugAction_SetDungeonItem(3, 0, 0);
    CHECK(!(gSave.dungeonItems[3] & 1), "map cleared");
    Port_DebugAction_SetDungeonKeys(3, 5);
    CHECK(gSave.dungeonKeys[3] == 5, "dungeon keys set");

    /* ---- flag browser raw bit round-trip ---- */
    Port_DebugAction_SetFlag(1, 10, 1); /* bank 1 (offset 0x100), index 10 -> bit 0x10A */
    CHECK(Port_DebugQuery_Flag(1, 10) == 1, "flag set reads back");
    CHECK((gSave.flags[0x10A >> 3] >> (0x10A & 7)) & 1, "flag bit at correct absolute position");
    Port_DebugAction_SetFlag(1, 10, 0);
    CHECK(Port_DebugQuery_Flag(1, 10) == 0, "flag cleared");

    /* ---- stat clamp to wallet capacity ---- */
    Port_DebugAction_SetStat(1 /* shells */, 5000);
    CHECK(gSave.stats.shells == 999, "shells clamped to 999");
    Port_DebugAction_SetStat(2 /* max hearts */, 99);
    CHECK(gSave.stats.maxHealth == 20 * 8, "max hearts clamped to 20 (=0xA0)");
    gSave.stats.walletType = 2; /* cap 500 */
    Port_DebugAction_SetStat(0 /* rupees */, 5000);
    CHECK(gSave.stats.rupees == 500, "rupees clamped to wallet-tier cap (500)");

    /* ---- expanded give-all: complete file with no grid-corrupting ids ---- */
    memset(&gSave, 0, sizeof(gSave));
    Port_DebugAction_GiveAllItems();
    CHECK(InvBit(ITEM_FOURSWORD) == 1 && InvBit(ITEM_SMITH_SWORD) == 1,
          "give-all owns the full sword progression");
    CHECK(InvBit(ITEM_UNUSED_SWORD) == 0, "give-all skips UNUSED_SWORD (grid corruptor)");
    CHECK(InvBit(ITEM_LIGHT_ARROW) == 1 && InvBit(ITEM_BOW) == 1,
          "give-all owns BOTH bow + light arrow (canonical layered state)");
    CHECK(InvBit(ITEM_MIRROR_SHIELD) == 1 && InvBit(ITEM_SHIELD) == 0,
          "give-all owns the upgraded shield member only");
    CHECK(InvBit(ITEM_REMOTE_BOMBS) == 1 && InvBit(ITEM_BOMBS) == 0,
          "give-all owns remote bombs only");
    CHECK(InvBit(ITEM_MAP) == 1, "give-all grants the overworld map");
    CHECK(InvBit(ITEM_SKILL_LONG_SPIN) == 1 && InvBit(ITEM_SKILL_SPIN_ATTACK) == 1,
          "give-all grants the sword scrolls");
    CHECK(InvBit(ITEM_BOTTLE_EMPTY) == 0, "give-all does not own a bottle-content tag id");
    CHECK(InvBit(ITEM_BOTTLE1) == 1 && gSave.stats.bottles[0] == ITEM_BOTTLE_EMPTY,
          "give-all owns bottles with drawable content");
    CHECK(gSave.stats.walletType == 3 && gSave.stats.rupees == 999,
          "give-all maxes wallet tier + rupees");
    CHECK(gSave.stats.bombBagType == 3 && gSave.stats.bombCount == 99,
          "give-all maxes bomb bag + bombs");
    CHECK(gSave.stats.quiverType == 3 && gSave.stats.arrowCount == 99,
          "give-all maxes quiver + arrows");
    CHECK(gSave.stats.maxHealth == 0xA0 && gSave.stats.heartPieces == 0,
          "give-all maxes hearts with no dangling piece");
    CHECK((gSave.dungeonItems[7] & 0x7) == 0x7 && gSave.dungeonKeys[7] == 9,
          "give-all grants every dungeon's items + keys");
    CHECK(gSave.stats.figurineCount == 0 && gSave.stats.hasAllFigurines == 0,
          "give-all leaves figurines untouched (granted by the separate actions)");

    /* ---- figurine completion: 130 (no game-clear) vs 100% (marks beaten) ---- */
    memset(&gSave, 0, sizeof(gSave));
    Port_DebugAction_AllFigurines130();
    CHECK(gSave.stats.figurineCount == 130, "all-figurines-130 sets count 130");
    CHECK(((gSave.figurines[130 >> 3] >> (130 & 7)) & 1) == 1, "figurine bit 130 set");
    CHECK((gSave.figurines[0] & 1) == 0, "figurine bit 0 stays clear (130)");
    CHECK(gSave.saw_staffroll == 0 && gSave.stats.hasAllFigurines == 0,
          "all-figurines-130 does NOT mark the game beaten");

    memset(&gSave, 0, sizeof(gSave));
    Port_DebugAction_AllFigurines100();
    CHECK(gSave.stats.figurineCount == 136, "all-figurines-100 sets count 136");
    CHECK(((gSave.figurines[136 >> 3] >> (136 & 7)) & 1) == 1, "figurine bit 136 set");
    CHECK((gSave.figurines[0] & 1) == 0, "figurine bit 0 stays clear (100)");
    CHECK(gSave.saw_staffroll == 1 && gSave.stats.hasAllFigurines == 1 &&
          gSave.stats._hasAllFigurines == 0xFF, "all-figurines-100 marks complete + beaten");
    CHECK(((gSave.flags[FIGURE_ALLCOMP >> 3] >> (FIGURE_ALLCOMP & 7)) & 1) == 1,
          "all-figurines-100 sets FIGURE_ALLCOMP");
    CHECK(InvBit(ITEM_QST_CARLOV_MEDAL) == 1, "all-figurines-100 grants the Carlov medal");

    /* ---- noclip toggle (the engine movement hook is interactive-only; this
     *      pins the C action layer + the Console-Parity-gated predicate) ---- */
    Port_DebugAction_SetNoclip(1);
    CHECK(Port_DebugQuery_Noclip() == 1, "noclip query reflects on");
    CHECK(Port_Debug_NoclipEnabled() == 1, "noclip enabled when on + parity off");
    Port_DebugAction_SetNoclip(0);
    CHECK(Port_DebugQuery_Noclip() == 0, "noclip query reflects off");
    CHECK(Port_Debug_NoclipEnabled() == 0, "noclip predicate off when toggle off");

    if (g_fails == 0) {
        fprintf(stderr, "DEBUG-ACTIONS REGRESSION OK\n");
        return 0;
    }
    fprintf(stderr, "DEBUG-ACTIONS REGRESSION: %d failure(s)\n", g_fails);
    return 1;
}
