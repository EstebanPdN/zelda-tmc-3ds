/*
 * port_debug_actions.c — C-only shim that exposes game-state mutations
 * for the (C++) debug menu. Keeping these in C avoids pulling the game
 * headers (which use `this` as a C parameter name) into C++ TUs where
 * they would not parse.
 */

#include "save.h"
#include "flags.h"
#include "room.h"
#include "area.h"
#include "global.h"
#include "common.h"
#include "main.h"
#include "player.h"
#include "hitbox.h"
#include "item.h"
#include "transitions.h"
#include "asm.h"
#include "port_debug_query.h"
#include "port_debug_actions.h"

void DoExitTransition(const Transition* data);
void LoadItemGfx(void);
void UpdatePlayerSkills(void);
extern bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
extern void Port_RefreshAreaData(unsigned int area);

#define DEBUG_AREA_COUNT 0x90 /* matches kAreaCount in port_asset_loader.cpp */

/* Set a single 2-bit slot in gSave.inventory[] without touching neighbours. */
static void SetItem(unsigned int item, unsigned int value) {
    unsigned int byteIdx = item / 4;
    unsigned int shift = (item % 4) * 2;
    if (byteIdx >= sizeof(gSave.inventory)) {
        return;
    }
    gSave.inventory[byteIdx] = (gSave.inventory[byteIdx] & ~(3u << shift)) | ((value & 3u) << shift);
}

/* Read a single 2-bit inventory slot. Mirrors the engine's GetInventoryValue. */
static unsigned int GetItem(unsigned int item) {
    unsigned int byteIdx = item / 4;
    unsigned int shift = (item % 4) * 2;
    if (byteIdx >= sizeof(gSave.inventory)) {
        return 0;
    }
    return (gSave.inventory[byteIdx] >> shift) & 3u;
}

void Port_DebugAction_GiveAllItems(void) {
    /* Build a COMPLETE file by whitelisting the real items + writing the Stats
     * / bottle / dungeon fields directly. A blanket inventory fill corrupts the
     * pause grid: 0xFF makes each 2-bit field 3 (failing the ==1 draw guard, so
     * items vanish) and 0x55 owns alias ids — bottle-content tags (id>=0x20),
     * ORBs, TRY_PICKUP_OBJECT, UNUSED_SWORD, quest ids — that light up cells the
     * menu can't draw. So whitelist only. (Composition researched against the
     * pause-menu draw + item-use dispatch.) */
    int i;

    /* Swords: own the FULL progression. They share one grid cell and the engine
     * resolves the greatest owned (script.c), so the ladder is the natural end
     * state and the cell renders the Four Sword — no blackout. (Not UNUSED_SWORD.) */
    SetItem(ITEM_SMITH_SWORD, 1);
    SetItem(ITEM_GREEN_SWORD, 1);
    SetItem(ITEM_RED_SWORD, 1);
    SetItem(ITEM_BLUE_SWORD, 1);
    SetItem(ITEM_FOURSWORD, 1);

    /* Upgrade pairs. For bombs and boomerang the engine's own grant clears the
     * basic bit (GiveItem cases 7 / 0x12), so REMOTE_BOMBS / MAGIC_BOOMERANG
     * alone is the canonical state. The BOW is the exception: Light Arrow's
     * grant (itemMetaData unk1=0xb) never clears ITEM_BOW, so a real file holds
     * BOTH bits — and 3 engine sites test ITEM_BOW specifically (Castor Wilds
     * re-presents the bow pickup; Stockwell gates arrow ammo), so own both.
     * Shield/lantern sites OR both bits, so the upgraded member alone is fine. */
    SetItem(ITEM_REMOTE_BOMBS, 1);
    SetItem(ITEM_BOW, 1);
    SetItem(ITEM_LIGHT_ARROW, 1);
    SetItem(ITEM_MAGIC_BOOMERANG, 1);
    SetItem(ITEM_MIRROR_SHIELD, 1);
    SetItem(ITEM_LANTERN_OFF, 1);

    /* Independent gear. */
    SetItem(ITEM_GUST_JAR, 1);
    SetItem(ITEM_PACCI_CANE, 1);
    SetItem(ITEM_MOLE_MITTS, 1);
    SetItem(ITEM_ROCS_CAPE, 1);
    SetItem(ITEM_PEGASUS_BOOTS, 1);
    SetItem(ITEM_OCARINA, 1);
    SetItem(ITEM_FIRE_ROD, 1);

    /* Overworld map ("map" progress — an off-grid inventory bit). */
    SetItem(ITEM_MAP, 1);

    /* Sword technique scrolls ("scroll" progress). gPlayerState.skills is a
     * runtime field (not part of the save) that UpdatePlayerSkills() rebuilds
     * from these inventory bits at player init / room load. Grant the bits here
     * and call UpdatePlayerSkills() below so the scrolls are usable immediately
     * instead of only after the next room transition. */
    SetItem(ITEM_SKILL_SPIN_ATTACK, 1);
    SetItem(ITEM_SKILL_ROLL_ATTACK, 1);
    SetItem(ITEM_SKILL_DASH_ATTACK, 1);
    SetItem(ITEM_SKILL_ROCK_BREAKER, 1);
    SetItem(ITEM_SKILL_SWORD_BEAM, 1);
    SetItem(ITEM_SKILL_GREAT_SPIN, 1);
    SetItem(ITEM_SKILL_DOWN_THRUST, 1);
    SetItem(ITEM_SKILL_PERIL_BEAM, 1);
    SetItem(ITEM_SKILL_FAST_SPIN, 1);
    SetItem(ITEM_SKILL_FAST_SPLIT, 1);
    SetItem(ITEM_SKILL_LONG_SPIN, 1);

    /* Elements. */
    SetItem(ITEM_EARTH_ELEMENT, 1);
    SetItem(ITEM_FIRE_ELEMENT, 1);
    SetItem(ITEM_WATER_ELEMENT, 1);
    SetItem(ITEM_WIND_ELEMENT, 1);

    /* Reach upgrades + capacity bags. */
    SetItem(ITEM_GRIP_RING, 1);
    SetItem(ITEM_POWER_BRACELETS, 1);
    SetItem(ITEM_FLIPPERS, 1);
    SetItem(ITEM_WALLET, 1);
    SetItem(ITEM_BOMBBAG, 1);
    SetItem(ITEM_LARGE_QUIVER, 1);
    SetItem(ITEM_KINSTONE_BAG, 1);

    /* Bottles: own all four; a bottle with content byte 0 is undrawable, so
     * default each to empty (0x20 == ITEM_BOTTLE_EMPTY). */
    SetItem(ITEM_BOTTLE1, 1);
    SetItem(ITEM_BOTTLE2, 1);
    SetItem(ITEM_BOTTLE3, 1);
    SetItem(ITEM_BOTTLE4, 1);
    for (i = 0; i < 4; i++) {
        gSave.stats.bottles[i] = ITEM_BOTTLE_EMPTY;
    }

    /* Capacity tiers to max (3) FIRST, then fill counts to those tiers' caps. */
    gSave.stats.walletType = 3;
    gSave.stats.bombBagType = 3;
    gSave.stats.quiverType = 3;
    gSave.stats.rupees = gWalletSizes[3].size; /* 999 */
    gSave.stats.bombCount = gBombBagSizes[3];  /* 99  */
    gSave.stats.arrowCount = gQuiverSizes[3];  /* 99  */
    gSave.stats.shells = 999;

    /* Hearts: 20 containers (maxHealth in eighths, cap 0xA0), full health, and
     * no dangling heart piece (value 4 would wrap into another container). */
    gSave.stats.maxHealth = 0xA0;
    gSave.stats.health = gSave.stats.maxHealth;
    gSave.stats.heartPieces = 0;

    /* Every dungeon: Map+Compass+Big Key (bits 1|2|4 — the gameUtils readers are
     * authoritative; the save.h field comment is wrong) + a generous key count. */
    for (i = 0; i < 0x10; i++) {
        gSave.dungeonItems[i] |= 0x7;
        gSave.dungeonKeys[i] = 9;
    }

    /* Figurines are intentionally NOT granted here: "all figurines" must match
     * gSave.available_figurines — a per-gallery, region-dependent total set from
     * device data at runtime (130 USA vs 136 EU) — kept in lockstep with
     * figurineCount / _hasAllFigurines / hasAllFigurines. Fabricating a static
     * total desyncs Carlov's gallery, so it stays a follow-up. Kinstone fusions
     * remain under the separate "All kinstones" action. */

    /* Rebuild the runtime sword-skill bitfield from the granted skill bits so
     * the scrolls work in the current room (UpdatePlayerSkills normally only
     * runs on player init / room load). */
    UpdatePlayerSkills();

    /* Reload held-item VRAM so the upgraded bomb/boomerang variants don't render
     * with stale gfx (Link-as-pot). */
    LoadItemGfx();
}

void Port_DebugAction_MaxHearts(void) {
    /* health/maxHealth are stored in eighths-of-hearts (each heart-container
     * pickup in script.c:1663 adds 8 and caps at 0xA0). 20 hearts = 160 = 0xA0.
     * The previous value (80) silently set 10 hearts and matched the in-game
     * cap that fileselect/ui paths special-case at 10 — see #52. */
    gSave.stats.maxHealth = 0xA0;
    gSave.stats.health = gSave.stats.maxHealth;
}

void Port_DebugAction_HealFull(void) {
    gSave.stats.health = gSave.stats.maxHealth;
}

void Port_DebugAction_MaxRupees(void) {
    gSave.stats.rupees = 999;
}

void Port_DebugAction_MaxShells(void) {
    gSave.stats.shells = 999;
}

void Port_DebugAction_AllKinstones(void) {
    int i;
    for (i = 0; i < (int)sizeof(gSave.kinstones.fuserOffers); i++) {
        gSave.kinstones.fuserOffers[i] = 0xFF;
    }
    for (i = 0; i < (int)sizeof(gSave.kinstones.fusedKinstones); i++) {
        gSave.kinstones.fusedKinstones[i] = 0xFF;
    }
    gSave.kinstones.fusedCount = 100;
    gSave.kinstones.didAllFusions = 1;
}

/* ------------------------------------------------------------------ */
/*  Figurine completion                                               */
/* ------------------------------------------------------------------ */
/* The figurine gallery is the gSave.figurines[36] bitset (288 bits, 1-based:
 * figurine N is bit N, bit 0 unused) displayed against a ceiling of
 * (!gSave.saw_staffroll ? 130 : 136) — figurineMenu.c. The "130 vs 136" split
 * is PRE-credits vs POST-credits on ALL regions (not USA/EU); the region
 * branches there only swap sprite/data tables. Two grants are offered because
 * true 100% is entangled with marking the game beaten (see below). */
#define FIGURINE_COUNT_PRECREDITS 130
#define FIGURINE_COUNT_FULL 136

/* Set figurine bits 1..n (bit 0 stays clear). */
static void DebugSetFigurineBits(int n) {
    int i;
    for (i = 1; i <= n; i++) {
        gSave.figurines[i >> 3] |= (unsigned char)(1u << (i & 7));
    }
}

/* Grant every pre-credits figurine (130) WITHOUT touching game-clear state.
 * The gallery (bitset-driven) shows all 130; figurineCount drives Carlov's
 * dialog. saw_staffroll / hasAllFigurines stay 0 — true 100% needs 136, which
 * requires marking the game beaten. CAVEAT: Carlov's machine recomputes the
 * obtainable count on each visit, so on a low-progress save it may still offer
 * draws (the 130 set is only deterministically stable once saw_staffroll opens
 * every gate). Harmless — the gallery still shows all 130. */
void Port_DebugAction_AllFigurines130(void) {
    DebugSetFigurineBits(FIGURINE_COUNT_PRECREDITS);
    gSave.stats.figurineCount = FIGURINE_COUNT_PRECREDITS;
}

/* Grant true 100% figurines (136). The engine ties the 136 ceiling to
 * saw_staffroll, and setting saw_staffroll makes the device's obtainable-recount
 * pass all 136 gates — so this is the only deterministically self-consistent
 * complete state, but it ALSO marks the game beaten (post-credits world state).
 * Reproduces the engine end-state: all bits, count, both completion flags, the
 * FIGURE_ALLCOMP global flag (bank 0, so its value is the absolute bit) and the
 * Carlov medal. (SHOP07_COMPLETE is an area-local flag the device self-manages
 * on the next visit, so it isn't set here.) */
void Port_DebugAction_AllFigurines100(void) {
    gSave.saw_staffroll = 1;
    DebugSetFigurineBits(FIGURINE_COUNT_FULL);
    gSave.stats.figurineCount = FIGURINE_COUNT_FULL;
    gSave.stats._hasAllFigurines = 0xFF;
    gSave.stats.hasAllFigurines = 1;
    gSave.flags[FIGURE_ALLCOMP >> 3] |= (unsigned char)(1u << (FIGURE_ALLCOMP & 7));
    SetItem(ITEM_QST_CARLOV_MEDAL, 1);
}

/* Known-broken named areas — these have entries in kAreaNames but
 * triggering the area-warp path crashes during room load (Port_Read-
 * PackedRomPtr failures, entity-table walks past valid memory, etc.).
 * Block them at the UI + dispatch layer until each is investigated.
 * The list is the first arrival point for new "warping to X crashes"
 * reports — add the offending area code here as a fast workaround, then
 * fix the underlying loader bug separately. */
static const unsigned char kBrokenWarpAreas[] = {
    0x44, /* Simon's Simulation — entity 0x69 row triggers ROM-ptr fail */
};

/* Is (area) safe to warp to from the debug menu? Returns 1 if both
 * named in kAreaNames AND not in the broken list, else 0. */
int Port_DebugAction_AreaIsWarpable(unsigned char area) {
    if (!Port_DebugQuery_AreaName(area))
        return 0;
    for (size_t i = 0; i < sizeof(kBrokenWarpAreas) / sizeof(kBrokenWarpAreas[0]); ++i) {
        if (kBrokenWarpAreas[i] == area)
            return 0;
    }
    return 1;
}

/* Per-(area,room) spawn-coord overrides for the debug warp.
 *
 * Issue #94: blindly dropping Link at the room's geometric center lands
 * him inside walls for any room whose center isn't walkable (cross-
 * shaped boss arenas, dungeon rooms with central pillars/holes, towns
 * whose "center" is a building, etc.). Without per-room safe-spawn
 * coords, the F8 Warp tab is unusable for those rooms.
 *
 * This table records known-good positions and layers for high-traffic
 * warp targets. Lookup is linear (~few dozen entries); rooms not in the
 * table fall back to the (w/2, h/2, layer=1) default that the caller
 * computes from RoomDimensions. Add entries here when you find a room
 * the default deposits Link into a wall.
 *
 * Coords are in pixels relative to the room origin. Layer 1 = bottom
 * (default), 2 = top, useful for stairs / overlay layouts. */
typedef struct WarpSpawnOverride {
    unsigned char area;
    unsigned char room;
    unsigned short x;
    unsigned short y;
    unsigned char layer;
} WarpSpawnOverride;

static const WarpSpawnOverride kWarpSpawnOverrides[] = {
    /* Hyrule Town — south entrance (door from Hyrule Field) is the
     * canonical Link-arrival spot; center lands in a building. */
    { 0x01, 0x00, 0x88, 0x110, 1 },
    /* Hyrule Field — overworld rooms are wide; center is fine except
     * the Castle approach (0x00/0x06) which centers on a wall. */
    { 0x00, 0x06, 0x88, 0xd0, 1 },
    /* Minish Village — center is the swing tree, drop on the path. */
    { 0x0a, 0x00, 0x80, 0xa8, 1 },
    /* Deepwood Shrine (entry / boss) — center has the pit. */
    { 0x48, 0x00, 0x80, 0xe0, 1 },
    { 0x49, 0x00, 0x80, 0xe0, 1 },
    /* Cave of Flames — central rooms have lava channel through the
     * middle; spawn on the south ledge. */
    { 0x50, 0x00, 0x80, 0xe0, 1 },
    { 0x51, 0x00, 0x80, 0xe0, 1 },
    /* Fortress of Winds — Mazaal boss room center is the floor void. */
    { 0x58, 0x00, 0xb8, 0x78, 1 },
    /* Fortress of Winds — Inner Mazaal cross arena, south arm. */
    { 0x5a, 0x00, 0x88, 0xb8, 1 },
    { 0x5a, 0x01, 0x88, 0xb8, 1 },
    /* Temple of Droplets — Big Octorok / sun room has central platform. */
    { 0x60, 0x00, 0x88, 0xc0, 1 },
    /* Royal Crypt — center has the coffin. */
    { 0x68, 0x00, 0x80, 0xc8, 1 },
    /* Palace of Winds — entrance room has a central drop. */
    { 0x88, 0x00, 0x80, 0xc8, 1 },
    /* Dark Hyrule Castle — entrance, center is a wall. */
    { 0x88, 0x01, 0x80, 0xd0, 1 },
};

/* Look up a curated safe-spawn for (area, room). Returns 1 and writes
 * the out-params when found; returns 0 otherwise. */
int Port_DebugAction_WarpSpawnOverride(unsigned char area, unsigned char room, unsigned short* x, unsigned short* y,
                                       unsigned char* layer) {
    for (size_t i = 0; i < sizeof(kWarpSpawnOverrides) / sizeof(kWarpSpawnOverrides[0]); ++i) {
        const WarpSpawnOverride* e = &kWarpSpawnOverrides[i];
        if (e->area == area && e->room == room) {
            if (x)
                *x = e->x;
            if (y)
                *y = e->y;
            if (layer)
                *layer = e->layer;
            return 1;
        }
    }
    return 0;
}

/* Trigger a warp by building a Transition struct in place and handing it
 * to DoExitTransition() — the exact path the wallmaster + scripted area
 * exits use, so the player ends up properly initialized (correct spawn
 * type, facing direction, layer, fade type). Returns 0 if it can't fire
 * right now (game not in TASK_GAME, or player dying), 1 if armed.
 *
 * The previous hand-rolled version wrote gRoomTransition fields directly,
 * which (a) didn't run the area-warp transition-type setup so dungeon
 * entries arrived without proper spawn handling, and (b) didn't clear
 * stairs_idx, letting CheckRoomExit's StairsAreValid() cancel the warp
 * into a stairs-spawn state. */
int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                          unsigned char layer) {
    Transition t;
    if (gMain.task != TASK_GAME) {
        return 0;
    }
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE) {
        return 0;
    }
    /* Reject warps to areas without a friendly name (AREA_NULL_*,
     * numeric AREA_4D-style slots, etc.). These have no real map data
     * and triggering the area-warp path runs us into the asset-loader
     * Port_ReadPackedRomPtr failure or silently renders garbage. The
     * UI tabs already filter the list but the public API is used from
     * other callers too, so guard here for defence in depth. */
    if (Port_DebugAction_AreaIsWarpable(area) == 0) {
        return 0;
    }

    t.warp_type = WARP_TYPE_AREA;
    t.startX = 0;
    t.startY = 0;
    t.endX = x;
    t.endY = y;
    t.shape = 0;
    t.area = area;
    t.room = room;
    t.layer = layer;
    /* TRANSITION_TYPE_NORMAL — most overworld-edge transitions use this.
     * Earlier value (DROP_IN=2) was wallmaster-pickup-shaped which left
     * Link mid-fall on rooms that don't have a hole-receiving anim, so
     * he ended up invisible/off-camera. Issue #56 (Lake Hylia entry
     * crash) needs us to drive the same code path the in-game border
     * crossings hit. */
    t.transition_type = 0;
    t.facing_direction = 0; /* face down on arrival */
    t.transitionSFX = 0;    /* SFX_NONE */
    t.unk2 = 0;
    t.unk3 = 0;

    gRoomTransition.stairs_idx = 0; /* prevent StairsAreValid() cancellation */
    DoExitTransition(&t);
    /* Arm the post-warp safe-spawn nudge — after the destination room
     * has loaded and Link is placed, Port_DebugAction_WarpTick() spirals
     * outward from his arrival tile to find a walkable spot if he's
     * landed inside collision. Catches every room, including ones not
     * in the curated override table. See issue #94. */
    Port_DebugAction_ArmWarpNudge();
    return 1;
}

/* -------- Auto safe-spawn nudge (issue #94) ----------------------- */

/* After firing a debug warp, the room reload takes several frames
 * (fade-out, room swap, entity respawn, fade-in). We can't sample tile
 * collision until the destination room's tile/collision data is in
 * place. Arm a frame counter at warp time; the per-frame Tick checks
 * collision once it expires and, if Link is sitting on a non-walkable
 * tile, spirals outward to find the closest walkable tile and snaps
 * him there.
 *
 * Spiral search keeps Link near the user's intended destination rather
 * than warping him to some arbitrary safe square. Radius is bounded
 * so we never silently teleport him across the room. */

#include <stdio.h>

#define WARP_NUDGE_DELAY_FRAMES 45 /* ~0.75s — covers fade + spawn */
#define WARP_NUDGE_MAX_RADIUS 8    /* tiles (= 128 px) */

static int sWarpNudgePending = 0;

void Port_DebugAction_ArmWarpNudge(void) {
    sWarpNudgePending = WARP_NUDGE_DELAY_FRAMES;
}

/* Check whether the given room-relative (tileX, tileY) is walkable
 * on the specified collision layer. 0 from GetCollisionDataAtTilePos
 * means walkable, any other value means blocked. */
static int Port_DebugAction_TileIsWalkable(int tileX, int tileY, unsigned char layer) {
    if (tileX < 0 || tileX > 0x3f || tileY < 0 || tileY > 0x3f)
        return 0;
    u32 tilePos = ((u32)tileX & 0x3f) | (((u32)tileY & 0x3f) << 6);
    return GetCollisionDataAtTilePos(tilePos, layer) == 0;
}

/* Spiral outward from (tileX, tileY) up to maxRadius. Writes the first
 * walkable tile coords into the out-params and returns 1; returns 0
 * if no walkable tile is found within the radius. */
static int Port_DebugAction_FindWalkable(int tileX, int tileY, unsigned char layer, int maxRadius, int* outX,
                                         int* outY) {
    /* Box-spiral: for each ring radius r, walk the perimeter. */
    for (int r = 0; r <= maxRadius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                /* Only test the perimeter — interior tiles were covered
                 * by smaller radii. */
                if (r > 0 && dx != -r && dx != r && dy != -r && dy != r)
                    continue;
                int tx = tileX + dx, ty = tileY + dy;
                if (Port_DebugAction_TileIsWalkable(tx, ty, layer)) {
                    if (outX)
                        *outX = tx;
                    if (outY)
                        *outY = ty;
                    return 1;
                }
            }
        }
    }
    return 0;
}

void Port_DebugAction_WarpTick(void) {
    if (sWarpNudgePending == 0)
        return;
    if (--sWarpNudgePending != 0)
        return;

    /* Only nudge when we're back in gameplay and Link is alive. */
    if (gMain.task != TASK_GAME)
        return;
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE)
        return;

    /* Sample Link's current tile collision. If walkable, nothing to do. */
    u32 lx = gPlayerEntity.base.x.HALF.HI;
    u32 ly = gPlayerEntity.base.y.HALF.HI;
    unsigned char layer = gPlayerEntity.base.collisionLayer;
    u32 linkTile = TILE(lx, ly);
    if (GetCollisionDataAtTilePos(linkTile, layer) == 0) {
        return; /* already on walkable ground */
    }

    /* Walk the spiral starting from Link's tile. */
    int tileX = ((int)lx - (int)gRoomControls.origin_x) >> 4;
    int tileY = ((int)ly - (int)gRoomControls.origin_y) >> 4;
    int foundX = 0, foundY = 0;
    if (!Port_DebugAction_FindWalkable(tileX, tileY, layer, WARP_NUDGE_MAX_RADIUS, &foundX, &foundY)) {
        fprintf(stderr,
                "[warp-nudge] no walkable tile within radius %d of Link "
                "(tile %d,%d layer %u) — leaving in place\n",
                WARP_NUDGE_MAX_RADIUS, tileX, tileY, (unsigned)layer);
        return;
    }

    /* Snap to the center of the chosen tile (+ 8 to land mid-tile). */
    u16 newX = (u16)(gRoomControls.origin_x + (foundX << 4) + 8);
    u16 newY = (u16)(gRoomControls.origin_y + (foundY << 4) + 8);
    fprintf(stderr,
            "[warp-nudge] Link tile (%d,%d) blocked, snapped to (%d,%d) "
            "=> world (%u,%u)\n",
            tileX, tileY, foundX, foundY, (unsigned)newX, (unsigned)newY);
    gPlayerEntity.base.x.HALF.HI = newX;
    gPlayerEntity.base.y.HALF.HI = newY;
}

/* Read Link's current world position (pixels). Returns 0 (and leaves outputs
 * untouched) when not in live gameplay, so the UI can grey out / pre-fill. */
int Port_DebugQuery_PlayerXY(unsigned short* x, unsigned short* y) {
    if (gMain.task != TASK_GAME)
        return 0;
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE)
        return 0;
    if (x)
        *x = (unsigned short)gPlayerEntity.base.x.HALF.HI;
    if (y)
        *y = (unsigned short)gPlayerEntity.base.y.HALF.HI;
    return 1;
}

/* Drop Link at an arbitrary world position (pixels) in the CURRENT room.
 * Uses the exact coordinate write WarpTick does, gated to live gameplay so we
 * never poke the player entity during death / non-gameplay. Returns 1 on
 * success, 0 if ignored. */
int Port_DebugAction_TeleportXY(unsigned short x, unsigned short y) {
    if (gMain.task != TASK_GAME)
        return 0;
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE)
        return 0;
    gPlayerEntity.base.x.HALF.HI = x;
    gPlayerEntity.base.y.HALF.HI = y;
    return 1;
}

/* Noclip (walk-through-walls). In-memory debug state only (not persisted). The
 * engine hook in src/movement.c zeroes the player's tile collisions while this
 * is on; force-disabled under Console-Parity so parity runs stay GBA-faithful. */
static bool sNoclip = false;

void Port_DebugAction_SetNoclip(int on) {
    sNoclip = on != 0;
}

int Port_DebugQuery_Noclip(void) {
    return sNoclip ? 1 : 0;
}

int Port_Debug_NoclipEnabled(void) {
    extern bool Port_Config_GetConsoleParity(void);
    return (sNoclip && !Port_Config_GetConsoleParity()) ? 1 : 0;
}

void Port_DebugAction_ToggleMinish(void) {
    if (gMain.task != TASK_GAME)
        return;
    if (gSave.stats.health == 0 || gPlayerState.framestate == PL_STATE_DIE)
        return;

    if (gPlayerState.flags & PL_MINISH) {
        gPlayerState.flags &= ~PL_MINISH;
        gPlayerEntity.base.hitbox = (Hitbox*)&gPlayerHitbox;
        gPlayerEntity.base.spritePriority.b1 = 1;
        gPlayerEntity.base.spriteSettings.shadow = 1;
        gPlayerEntity.base.spriteRendering.b0 = 3;
        SetAffineInfo(&gPlayerEntity.base, 0x100, 0x100, 0);
        PlayerSetNormalAndCollide();
    } else {
        gPlayerEntity.base.action = PLAYER_MINISH;
        gPlayerEntity.base.subAction = 0;
    }
}

int Port_DebugQuery_IsMinish(void) {
    if (gMain.task != TASK_GAME)
        return 0;
    return (gPlayerState.flags & PL_MINISH) ? 1 : 0;
}


/* ------------------------------------------------------------------ */
/*  Per-item ownership toggles                                        */
/* ------------------------------------------------------------------ */
/* Backs the debug menu's "per-item toggle" grid. Each entry is a real
 * inventory item the player can own/un-own individually. The id is kept
 * in this C TU (it knows the ITEM_* enum); the C++ menu only sees an
 * index + name + group, so all item-id knowledge stays on this side of
 * the C/C++ boundary. Group strings drive the menu's section headers. */
/* Mutual-exclusivity groups for the per-item toggle. Items sharing the same
 * nonzero key occupy ONE pause-grid cell (genuine upgrade/variant sets), so
 * owning one must clear the others. Driven by this explicit key, NOT by
 * gItemMetaData[].menuSlot: that engine byte is only meaningful for the
 * activatable grid range (ids 1..0x1F); for elements/upgrades/quest items it
 * holds reused garbage that aliases the small slot numbers (0..15) and would
 * cross-clear unrelated held items (e.g. Big Wallet wiping every sword). */
enum {
    EXCL_NONE = 0,
    EXCL_SWORD,
    EXCL_BOMBS,
    EXCL_BOW,
    EXCL_BOOMERANG,
    EXCL_SHIELD,
    EXCL_LANTERN,
};

typedef struct DebugToggleItem {
    unsigned short id;  /* ITEM_* */
    const char* name;   /* display label */
    const char* group;  /* section heading in the menu */
    unsigned char excl; /* mutual-exclusivity key (EXCL_*); 0 = independent */
} DebugToggleItem;

static const DebugToggleItem kToggleItems[] = {
    /* Swords — one grid cell, mutually exclusive. */
    { ITEM_SMITH_SWORD, "Smith's Sword", "Sword", EXCL_SWORD },
    { ITEM_GREEN_SWORD, "Green Sword", "Sword", EXCL_SWORD },
    { ITEM_RED_SWORD, "Red Sword", "Sword", EXCL_SWORD },
    { ITEM_BLUE_SWORD, "Blue Sword", "Sword", EXCL_SWORD },
    { ITEM_FOURSWORD, "Four Sword", "Sword", EXCL_SWORD },
    /* Weapons. Only the genuine upgrade pairs share a grid cell; the rest are
     * independent items. */
    { ITEM_BOMBS, "Bombs", "Weapons", EXCL_BOMBS },
    { ITEM_REMOTE_BOMBS, "Remote Bombs", "Weapons", EXCL_BOMBS },
    { ITEM_BOW, "Bow", "Weapons", EXCL_NONE },
    { ITEM_LIGHT_ARROW, "Light Arrow", "Weapons", EXCL_NONE },
    { ITEM_BOOMERANG, "Boomerang", "Weapons", EXCL_BOOMERANG },
    { ITEM_MAGIC_BOOMERANG, "Magic Boomerang", "Weapons", EXCL_BOOMERANG },
    { ITEM_GUST_JAR, "Gust Jar", "Weapons", EXCL_NONE },
    { ITEM_PACCI_CANE, "Cane of Pacci", "Weapons", EXCL_NONE },
    { ITEM_FIRE_ROD, "Fire Rod", "Weapons", EXCL_NONE },
    /* Gear. Shield/mirror-shield share a cell; lantern is a single entry. */
    { ITEM_SHIELD, "Shield", "Gear", EXCL_SHIELD },
    { ITEM_MIRROR_SHIELD, "Mirror Shield", "Gear", EXCL_SHIELD },
    { ITEM_LANTERN_OFF, "Lantern", "Gear", EXCL_LANTERN },
    { ITEM_MOLE_MITTS, "Mole Mitts", "Gear", EXCL_NONE },
    { ITEM_ROCS_CAPE, "Roc's Cape", "Gear", EXCL_NONE },
    { ITEM_PEGASUS_BOOTS, "Pegasus Boots", "Gear", EXCL_NONE },
    { ITEM_OCARINA, "Ocarina", "Gear", EXCL_NONE },
    /* Bottles — each is its own cell; owning sets a drawable empty content. */
    { ITEM_BOTTLE1, "Bottle 1", "Bottles", EXCL_NONE },
    { ITEM_BOTTLE2, "Bottle 2", "Bottles", EXCL_NONE },
    { ITEM_BOTTLE3, "Bottle 3", "Bottles", EXCL_NONE },
    { ITEM_BOTTLE4, "Bottle 4", "Bottles", EXCL_NONE },
    /* Elements. */
    { ITEM_EARTH_ELEMENT, "Earth Element", "Elements", EXCL_NONE },
    { ITEM_FIRE_ELEMENT, "Fire Element", "Elements", EXCL_NONE },
    { ITEM_WATER_ELEMENT, "Water Element", "Elements", EXCL_NONE },
    { ITEM_WIND_ELEMENT, "Wind Element", "Elements", EXCL_NONE },
    /* Reach + capacity upgrades. */
    { ITEM_GRIP_RING, "Grip Ring", "Upgrades", EXCL_NONE },
    { ITEM_POWER_BRACELETS, "Power Bracelets", "Upgrades", EXCL_NONE },
    { ITEM_FLIPPERS, "Flippers", "Upgrades", EXCL_NONE },
    { ITEM_WALLET, "Big Wallet", "Upgrades", EXCL_NONE },
    { ITEM_BOMBBAG, "Bomb Bag", "Upgrades", EXCL_NONE },
    { ITEM_LARGE_QUIVER, "Large Quiver", "Upgrades", EXCL_NONE },
    { ITEM_KINSTONE_BAG, "Kinstone Bag", "Upgrades", EXCL_NONE },
    /* Quest items (pause page 2) — independent of each other. */
    { ITEM_QST_SWORD, "Quest Sword", "Quest", EXCL_NONE },
    { ITEM_QST_BROKEN_SWORD, "Broken Picori Blade", "Quest", EXCL_NONE },
    { ITEM_QST_DOGFOOD, "Dog Food", "Quest", EXCL_NONE },
    { ITEM_QST_LONLON_KEY, "Lon Lon Key", "Quest", EXCL_NONE },
    { ITEM_QST_MUSHROOM, "Mushroom", "Quest", EXCL_NONE },
    { ITEM_QST_BOOK1, "Library Book 1", "Quest", EXCL_NONE },
    { ITEM_QST_BOOK2, "Library Book 2", "Quest", EXCL_NONE },
    { ITEM_QST_BOOK3, "Library Book 3", "Quest", EXCL_NONE },
    { ITEM_QST_GRAVEYARD_KEY, "Graveyard Key", "Quest", EXCL_NONE },
    { ITEM_QST_TINGLE_TROPHY, "Tingle Trophy", "Quest", EXCL_NONE },
    { ITEM_QST_CARLOV_MEDAL, "Carlov Medal", "Quest", EXCL_NONE },
    /* Sword-technique scrolls — independent of each other. Each is an inventory
     * bit; toggling one calls UpdatePlayerSkills() (see SetToggleItem) so the
     * technique is usable/removed immediately, not only after the next room load.
     * Keep this group contiguous: the menu opens one CollapsingHeader per run of
     * same-group rows. */
    { ITEM_SKILL_SPIN_ATTACK, "Spin Attack", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_ROLL_ATTACK, "Roll Attack", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_DASH_ATTACK, "Dash Attack", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_ROCK_BREAKER, "Rock Breaker", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_SWORD_BEAM, "Sword Beam", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_GREAT_SPIN, "Great Spin Attack", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_DOWN_THRUST, "Down Thrust", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_PERIL_BEAM, "Peril Beam", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_FAST_SPIN, "Fast Spin", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_FAST_SPLIT, "Fast Split", "Sword Techniques", EXCL_NONE },
    { ITEM_SKILL_LONG_SPIN, "Long Spin", "Sword Techniques", EXCL_NONE },
};
#define TOGGLE_ITEM_COUNT ((int)(sizeof(kToggleItems) / sizeof(kToggleItems[0])))

int Port_DebugQuery_ToggleItemCount(void) {
    return TOGGLE_ITEM_COUNT;
}

const char* Port_DebugQuery_ToggleItemName(int i) {
    if (i < 0 || i >= TOGGLE_ITEM_COUNT)
        return NULL;
    return kToggleItems[i].name;
}

const char* Port_DebugQuery_ToggleItemGroup(int i) {
    if (i < 0 || i >= TOGGLE_ITEM_COUNT)
        return NULL;
    return kToggleItems[i].group;
}

int Port_DebugQuery_ToggleItemOwned(int i) {
    if (i < 0 || i >= TOGGLE_ITEM_COUNT)
        return 0;
    return GetItem(kToggleItems[i].id) == 1;
}

/* Strip the side effects of un-owning an item: drop a dangling A/B equip and
 * reset bottle content, so an item cleared either explicitly or by exclusivity
 * never leaves stale equip/bottle state behind. */
static void UnownItemCleanup(unsigned int id) {
    if (gSave.stats.equipped[SLOT_A] == id)
        gSave.stats.equipped[SLOT_A] = ITEM_NONE;
    if (gSave.stats.equipped[SLOT_B] == id)
        gSave.stats.equipped[SLOT_B] = ITEM_NONE;
    if (id >= ITEM_BOTTLE1 && id <= ITEM_BOTTLE4) {
        gSave.stats.bottles[(int)id - ITEM_BOTTLE1] = 0;
    }
}

void Port_DebugAction_SetToggleItem(int i, int owned) {
    unsigned int id;
    unsigned char excl;
    if (i < 0 || i >= TOGGLE_ITEM_COUNT)
        return;
    id = kToggleItems[i].id;
    excl = kToggleItems[i].excl;

    if (owned) {
        /* Mutual exclusivity within a real upgrade/variant group (swords,
         * bombs/remote, bow/light-arrow, the two boomerangs, shield/mirror,
         * lantern): these share one pause-grid cell, so owning one clears the
         * others — otherwise the cell collides (last write wins -> wrong/black
         * sprite) and the item-use dispatcher sees two variants. Keyed off the
         * table's explicit excl group, never gItemMetaData[].menuSlot (which
         * holds aliasing garbage for non-grid items and would wipe unrelated
         * inventory). Cleared siblings get the same un-own cleanup. */
        if (excl != EXCL_NONE) {
            int j;
            for (j = 0; j < TOGGLE_ITEM_COUNT; j++) {
                if (j == i)
                    continue;
                if (kToggleItems[j].excl == excl) {
                    SetItem(kToggleItems[j].id, 0);
                    UnownItemCleanup(kToggleItems[j].id);
                }
            }
        }
        SetItem(id, 1);
        /* A bottle with no content byte can't be drawn in the grid; give a
         * freshly-owned bottle a valid empty content. */
        if (id >= ITEM_BOTTLE1 && id <= ITEM_BOTTLE4) {
            int b = (int)id - ITEM_BOTTLE1;
            if (gSave.stats.bottles[b] == 0) {
                gSave.stats.bottles[b] = ITEM_BOTTLE_EMPTY;
            }
        }
    } else {
        SetItem(id, 0);
        UnownItemCleanup(id);
    }
    /* Reload held-item VRAM so toggling bomb/boomerang variants doesn't
     * leave Link rendering with stale gfx — same call GiveAllItems makes. */
    LoadItemGfx();

    /* Sword-technique scrolls feed gPlayerState.skills, a runtime field that
     * UpdatePlayerSkills() rebuilds from these inventory bits. Rebuild now so a
     * toggled technique becomes usable (or is removed) immediately, not only
     * after the next room load. */
    if (kToggleItems[i].group != NULL && strcmp(kToggleItems[i].group, "Sword Techniques") == 0) {
        UpdatePlayerSkills();
    }
}

/* ------------------------------------------------------------------ */
/*  Per-dungeon items / small keys                                    */
/* ------------------------------------------------------------------ */
/* dungeonItems[] is a per-dungeon bitmask; bit values are taken from the
 * gameUtils.c readers (HasDungeonMap/Compass/BigKey), which are
 * authoritative — the save.h field comment mislabels them.
 *   bit 0 (1) = Map, bit 1 (2) = Compass, bit 2 (4) = Big Key.
 * dungeonKeys[] holds the small-key COUNT per dungeon (a byte, not a bit).
 * Both arrays are indexed by dungeon id (0..0xF); the engine only ever
 * writes the current area's slot, so the menu writes the arrays directly
 * to reach any dungeon. */
#define DUNGEON_BIT_MAP 0x1
#define DUNGEON_BIT_COMPASS 0x2
#define DUNGEON_BIT_BIGKEY 0x4
#define DEBUG_DUNGEON_COUNT 0x10

/* Dungeon id the player is currently inside, or -1 when not in a keyed
 * dungeon (dungeon_idx is stale outside one — gate on the area's "has
 * keys" metadata bit, the same signal AreaHasKeys() reads). Used purely
 * to highlight the current dungeon in the menu. */
int Port_DebugQuery_CurrentDungeon(void) {
    if (((gArea.areaMetadata >> 1) & 1) == 0)
        return -1;
    return (int)gArea.dungeon_idx;
}

int Port_DebugQuery_DungeonItems(int dungeon) {
    if (dungeon < 0 || dungeon >= DEBUG_DUNGEON_COUNT)
        return 0;
    return gSave.dungeonItems[dungeon];
}

int Port_DebugQuery_DungeonKeys(int dungeon) {
    if (dungeon < 0 || dungeon >= DEBUG_DUNGEON_COUNT)
        return 0;
    return gSave.dungeonKeys[dungeon];
}

void Port_DebugAction_SetDungeonItem(int dungeon, int which, int owned) {
    unsigned char bit;
    if (dungeon < 0 || dungeon >= DEBUG_DUNGEON_COUNT)
        return;
    switch (which) {
        case 0:
            bit = DUNGEON_BIT_MAP;
            break;
        case 1:
            bit = DUNGEON_BIT_COMPASS;
            break;
        case 2:
            bit = DUNGEON_BIT_BIGKEY;
            break;
        default:
            return;
    }
    if (owned) {
        gSave.dungeonItems[dungeon] |= bit;
    } else {
        gSave.dungeonItems[dungeon] &= (unsigned char)~bit;
    }
}

void Port_DebugAction_SetDungeonKeys(int dungeon, int count) {
    if (dungeon < 0 || dungeon >= DEBUG_DUNGEON_COUNT)
        return;
    if (count < 0)
        count = 0;
    if (count > 255)
        count = 255;
    gSave.dungeonKeys[dungeon] = (unsigned char)count;
}

/* ------------------------------------------------------------------ */
/*  Charm / Picolyte timed buffs                                      */
/* ------------------------------------------------------------------ */
/* Both effects live in gSave.stats and are counted down once per frame by
 * HandlePlayerLife() (interrupts.c) — assigning the {type, timer} pair is
 * the whole activation; no init call is needed. Timers are in frames at
 * 60fps (charm normal = 3600 = 60s, picolyte normal = 900 = 15s) and the
 * fields are u16, so 0..65535. The type fields MUST hold only valid ids:
 * picolyteType is used as an UNCHECKED droptable index (itemUtils.c), and
 * the charm combat/palette code only switches on the three charm ids — any
 * other value would read out of bounds / behave undefined. */

void Port_DebugAction_SetCharm(int charmId, int timer) {
    if (charmId != 0 && (charmId < BOTTLE_CHARM_NAYRU || charmId > BOTTLE_CHARM_DIN)) {
        return;
    }
    if (timer < 0)
        timer = 0;
    if (timer > 0xFFFF)
        timer = 0xFFFF;
    if (charmId == 0)
        timer = 0; /* off => no timer */
    else if (timer == 0)
        charmId = 0; /* zero timer => off */
    gSave.stats.charm = (unsigned char)charmId;
    gSave.stats.charmTimer = (unsigned short)timer;
}

void Port_DebugAction_SetPicolyte(int picoId, int timer) {
    if (picoId != 0 && (picoId < ITEM_BOTTLE_PICOLYTE_RED || picoId > ITEM_BOTTLE_PICOLYTE_WHITE)) {
        return;
    }
    if (timer < 0)
        timer = 0;
    if (timer > 0xFFFF)
        timer = 0xFFFF;
    if (picoId == 0)
        timer = 0;
    else if (timer == 0)
        picoId = 0;
    gSave.stats.picolyteType = (unsigned char)picoId;
    gSave.stats.picolyteTimer = (unsigned short)timer;
}

/* Read current buff state for the menu. Returns 1 when active, 0 when off. */
int Port_DebugQuery_Charm(int* id, int* timer) {
    if (id)
        *id = gSave.stats.charm;
    if (timer)
        *timer = gSave.stats.charmTimer;
    return gSave.stats.charm != 0;
}

int Port_DebugQuery_Picolyte(int* id, int* timer) {
    if (id)
        *id = gSave.stats.picolyteType;
    if (timer)
        *timer = gSave.stats.picolyteTimer;
    return gSave.stats.picolyteType != 0;
}

/* ------------------------------------------------------------------ */
/*  Adjustable bottle contents                                        */
/* ------------------------------------------------------------------ */
/* gSave.stats.bottles[n] stores the content as the raw Item id (itemUtils.c
 * writes the id directly; 0x20 == ITEM_BOTTLE_EMPTY is the empty sentinel).
 * Valid contents span ITEM_BOTTLE_EMPTY..BOTTLE_CHARM_DIN. Setting a content
 * also owns the bottle so it shows in the grid and can be used. */
typedef struct DebugBottleContent {
    unsigned short id;
    const char* name;
} DebugBottleContent;

static const DebugBottleContent kBottleContents[] = {
    { ITEM_BOTTLE_EMPTY, "Empty" },
    { ITEM_BOTTLE_RED_POTION, "Red Potion" },
    { ITEM_BOTTLE_BLUE_POTION, "Blue Potion" },
    { ITEM_BOTTLE_MILK, "Milk" },
    { ITEM_BOTTLE_HALF_MILK, "Half Milk" },
    { ITEM_BOTTLE_BUTTER, "Lon Lon Butter" },
    { ITEM_BOTTLE_WATER, "Water" },
    { ITEM_BOTTLE_MINERAL_WATER, "Mineral Water" },
    { ITEM_BOTTLE_FAIRY, "Fairy" },
    { ITEM_BOTTLE_PICOLYTE_RED, "Picolyte (Red)" },
    { ITEM_BOTTLE_PICOLYTE_ORANGE, "Picolyte (Orange)" },
    { ITEM_BOTTLE_PICOLYTE_YELLOW, "Picolyte (Yellow)" },
    { ITEM_BOTTLE_PICOLYTE_GREEN, "Picolyte (Green)" },
    { ITEM_BOTTLE_PICOLYTE_BLUE, "Picolyte (Blue)" },
    { ITEM_BOTTLE_PICOLYTE_WHITE, "Picolyte (White)" },
    { BOTTLE_CHARM_NAYRU, "Charm (Nayru)" },
    { BOTTLE_CHARM_FARORE, "Charm (Farore)" },
    { BOTTLE_CHARM_DIN, "Charm (Din)" },
};
#define BOTTLE_CONTENT_COUNT ((int)(sizeof(kBottleContents) / sizeof(kBottleContents[0])))

int Port_DebugQuery_BottleContentCount(void) {
    return BOTTLE_CONTENT_COUNT;
}

const char* Port_DebugQuery_BottleContentName(int i) {
    if (i < 0 || i >= BOTTLE_CONTENT_COUNT)
        return NULL;
    return kBottleContents[i].name;
}

int Port_DebugQuery_BottleContentId(int i) {
    if (i < 0 || i >= BOTTLE_CONTENT_COUNT)
        return 0;
    return kBottleContents[i].id;
}

/* Map a content id back to its kBottleContents index (0 = Empty fallback). */
int Port_DebugQuery_BottleContentIndex(int contentId) {
    int i;
    for (i = 0; i < BOTTLE_CONTENT_COUNT; i++) {
        if (kBottleContents[i].id == contentId)
            return i;
    }
    return 0;
}

int Port_DebugQuery_BottleOwned(int bottle) {
    if (bottle < 0 || bottle > 3)
        return 0;
    return GetItem(ITEM_BOTTLE1 + bottle) == 1;
}

int Port_DebugQuery_BottleContent(int bottle) {
    if (bottle < 0 || bottle > 3)
        return 0;
    return gSave.stats.bottles[bottle];
}

void Port_DebugAction_SetBottleContent(int bottle, int contentId) {
    if (bottle < 0 || bottle > 3)
        return;
    if (contentId < ITEM_BOTTLE_EMPTY || contentId > BOTTLE_CHARM_DIN)
        return;
    gSave.stats.bottles[bottle] = (unsigned char)contentId;
    SetItem(ITEM_BOTTLE1 + bottle, 1); /* a bottle with content must be owned */
}

/* ------------------------------------------------------------------ */
/*  Numeric stat / capacity sliders                                   */
/* ------------------------------------------------------------------ */
/* A flat list of editable scalar save stats for the menu's slider block.
 * Bounds are computed in C so the UI stays a dumb min/max/value slider:
 * counts are capped to the live capacity tier (wallet/bomb-bag/quiver), and
 * the capacity tiers themselves run 0..3. (Kinstones are intentionally absent
 * — they're per-type with a separate fusion bitfield; "All kinstones fused"
 * covers the bulk case without risking a desync.) */
enum {
    DBG_STAT_RUPEES,
    DBG_STAT_SHELLS,
    DBG_STAT_HEARTS_MAX,
    DBG_STAT_HEALTH,
    DBG_STAT_HEART_PIECES,
    DBG_STAT_BOMBS,
    DBG_STAT_ARROWS,
    DBG_STAT_WALLET_TIER,
    DBG_STAT_BOMBBAG_TIER,
    DBG_STAT_QUIVER_TIER,
    DBG_STAT_COUNT
};

static const char* const kStatNames[DBG_STAT_COUNT] = {
    "Rupees", "Mysterious Shells", "Max hearts",    "Health (eighths)", "Heart pieces", "Bombs",
    "Arrows", "Wallet tier",       "Bomb-bag tier", "Quiver tier",
};

int Port_DebugQuery_StatCount(void) {
    return DBG_STAT_COUNT;
}

const char* Port_DebugQuery_StatName(int i) {
    if (i < 0 || i >= DBG_STAT_COUNT)
        return NULL;
    return kStatNames[i];
}

int Port_DebugQuery_StatMin(int i) {
    switch (i) {
        case DBG_STAT_HEARTS_MAX:
            return 1; /* never zero out the heart bar */
        default:
            return 0;
    }
}

int Port_DebugQuery_StatMax(int i) {
    switch (i) {
        case DBG_STAT_RUPEES:
            return gWalletSizes[gSave.stats.walletType & 3].size;
        case DBG_STAT_SHELLS:
            return 999;
        case DBG_STAT_HEARTS_MAX:
            return 20; /* 20 hearts == maxHealth 0xA0 */
        case DBG_STAT_HEALTH:
            return gSave.stats.maxHealth;
        case DBG_STAT_HEART_PIECES:
            return 3; /* 4th piece auto-forms a container */
        case DBG_STAT_BOMBS:
            return gBombBagSizes[gSave.stats.bombBagType & 3];
        case DBG_STAT_ARROWS:
            return gQuiverSizes[gSave.stats.quiverType & 3];
        case DBG_STAT_WALLET_TIER:
            return 3;
        case DBG_STAT_BOMBBAG_TIER:
            return 3;
        case DBG_STAT_QUIVER_TIER:
            return 3;
        default:
            return 0;
    }
}

int Port_DebugQuery_StatValue(int i) {
    switch (i) {
        case DBG_STAT_RUPEES:
            return gSave.stats.rupees;
        case DBG_STAT_SHELLS:
            return gSave.stats.shells;
        case DBG_STAT_HEARTS_MAX:
            return gSave.stats.maxHealth / 8;
        case DBG_STAT_HEALTH:
            return gSave.stats.health;
        case DBG_STAT_HEART_PIECES:
            return gSave.stats.heartPieces;
        case DBG_STAT_BOMBS:
            return gSave.stats.bombCount;
        case DBG_STAT_ARROWS:
            return gSave.stats.arrowCount;
        case DBG_STAT_WALLET_TIER:
            return gSave.stats.walletType;
        case DBG_STAT_BOMBBAG_TIER:
            return gSave.stats.bombBagType;
        case DBG_STAT_QUIVER_TIER:
            return gSave.stats.quiverType;
        default:
            return 0;
    }
}

void Port_DebugAction_SetStat(int i, int value) {
    const int lo = Port_DebugQuery_StatMin(i);
    const int hi = Port_DebugQuery_StatMax(i);
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    switch (i) {
        case DBG_STAT_RUPEES:
            gSave.stats.rupees = (unsigned short)value;
            break;
        case DBG_STAT_SHELLS:
            gSave.stats.shells = (unsigned short)value;
            break;
        case DBG_STAT_HEARTS_MAX:
            gSave.stats.maxHealth = (unsigned char)(value * 8);
            if (gSave.stats.health > gSave.stats.maxHealth) {
                gSave.stats.health = gSave.stats.maxHealth;
            }
            break;
        case DBG_STAT_HEALTH:
            gSave.stats.health = (unsigned char)value;
            break;
        case DBG_STAT_HEART_PIECES:
            gSave.stats.heartPieces = (unsigned char)value;
            break;
        case DBG_STAT_BOMBS:
            gSave.stats.bombCount = (unsigned char)value;
            break;
        case DBG_STAT_ARROWS:
            gSave.stats.arrowCount = (unsigned char)value;
            break;
        case DBG_STAT_WALLET_TIER:
            gSave.stats.walletType = (unsigned char)value;
            break;
        case DBG_STAT_BOMBBAG_TIER:
            gSave.stats.bombBagType = (unsigned char)value;
            break;
        case DBG_STAT_QUIVER_TIER:
            gSave.stats.quiverType = (unsigned char)value;
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Raw flag browser                                                  */
/* ------------------------------------------------------------------ */
/* gSave.flags[0x200] is a flat 4096-bit array. The engine partitions it into
 * 13 banks (FLAG_BANK_*, flags.h): bank 0 = global flags, banks 1..12 = the
 * per-area local-flag pools an area selects via gArea.flag_bank /
 * .localFlagOffset. A flag is addressed (bank, index); its absolute bit is
 * kFlagBanks[bank].offset + index. We read/write the raw bit directly: the
 * engine's Set*ByBank refuses index 0 as a "no flag" sentinel, but a raw
 * editor should expose every bit. The math is bit-identical to the engine's
 * ReadBit/WriteBit (LSB-first within each byte, common.c). */
static const struct {
    const char* name;
    unsigned short offset;
} kFlagBanks[] = {
    { "Bank 0 (Global)", FLAG_BANK_0 }, { "Bank 1", FLAG_BANK_1 },   { "Bank 2", FLAG_BANK_2 },
    { "Bank 3", FLAG_BANK_3 },          { "Bank 4", FLAG_BANK_4 },   { "Bank 5", FLAG_BANK_5 },
    { "Bank 6", FLAG_BANK_6 },          { "Bank 7", FLAG_BANK_7 },   { "Bank 8", FLAG_BANK_8 },
    { "Bank 9", FLAG_BANK_9 },          { "Bank 10", FLAG_BANK_10 }, { "Bank 11", FLAG_BANK_11 },
    { "Bank 12", FLAG_BANK_12 },
};
#define FLAG_BANK_COUNT ((int)(sizeof(kFlagBanks) / sizeof(kFlagBanks[0])))
#define FLAG_TOTAL_BITS ((int)(sizeof(gSave.flags) * 8))

int Port_DebugQuery_FlagBankCount(void) {
    return FLAG_BANK_COUNT;
}

const char* Port_DebugQuery_FlagBankName(int bank) {
    if (bank < 0 || bank >= FLAG_BANK_COUNT)
        return NULL;
    return kFlagBanks[bank].name;
}

unsigned int Port_DebugQuery_FlagBankOffset(int bank) {
    if (bank < 0 || bank >= FLAG_BANK_COUNT)
        return 0;
    return kFlagBanks[bank].offset;
}

/* Number of flag indices in a bank = gap to the next bank's offset (the last
 * bank runs to the end of the array). */
int Port_DebugQuery_FlagBankSize(int bank) {
    int next;
    if (bank < 0 || bank >= FLAG_BANK_COUNT)
        return 0;
    next = (bank + 1 < FLAG_BANK_COUNT) ? (int)kFlagBanks[bank + 1].offset : FLAG_TOTAL_BITS;
    return next - (int)kFlagBanks[bank].offset;
}

/* Bank the current area's local flags live in, or -1 if it matches none.
 * gArea.localFlagOffset is the area's bit offset into gSave.flags. */
int Port_DebugQuery_CurrentFlagBank(void) {
    unsigned int off = gArea.localFlagOffset;
    int i;
    for (i = 0; i < FLAG_BANK_COUNT; i++) {
        if (kFlagBanks[i].offset == off)
            return i;
    }
    return -1;
}

int Port_DebugQuery_Flag(int bank, int index) {
    unsigned int bit;
    if (bank < 0 || bank >= FLAG_BANK_COUNT)
        return 0;
    if (index < 0 || index >= Port_DebugQuery_FlagBankSize(bank))
        return 0;
    bit = (unsigned int)kFlagBanks[bank].offset + (unsigned int)index;
    if (bit >= (unsigned int)FLAG_TOTAL_BITS)
        return 0;
    return (gSave.flags[bit >> 3] >> (bit & 7)) & 1;
}

void Port_DebugAction_SetFlag(int bank, int index, int on) {
    unsigned int bit;
    if (bank < 0 || bank >= FLAG_BANK_COUNT)
        return;
    if (index < 0 || index >= Port_DebugQuery_FlagBankSize(bank))
        return;
    bit = (unsigned int)kFlagBanks[bank].offset + (unsigned int)index;
    if (bit >= (unsigned int)FLAG_TOTAL_BITS)
        return;
    if (on) {
        gSave.flags[bit >> 3] |= (unsigned char)(1u << (bit & 7));
    } else {
        gSave.flags[bit >> 3] &= (unsigned char)~(1u << (bit & 7));
    }
}

/* ------------------------------------------------------------------ */
/*  Debug-menu enumeration helpers                                    */
/* ------------------------------------------------------------------ */

/* Resolve and validate the per-area RoomHeader table pointer, refreshing
 * via Port_RefreshAreaData if the cached pointer doesn't look ROM-backed.
 * Returns NULL for unmapped/invalid areas. */
static RoomHeader* DebugResolveRoomTable(unsigned char area) {
    RoomHeader* table;
    if (area >= DEBUG_AREA_COUNT) {
        return NULL;
    }
    table = gAreaRoomHeaders[area];
    if (!Port_IsRoomHeaderPtrReadable(table)) {
        Port_RefreshAreaData(area);
        table = gAreaRoomHeaders[area];
    }
    if (!Port_IsRoomHeaderPtrReadable(table)) {
        return NULL;
    }
    return table;
}

/* Return one past the highest populated room slot (pixel_width != 0), or 0
 * when the area has no room data at all.
 *
 * This scans every slot rather than stopping at the first empty one: some
 * areas have an empty (pixel_width==0) slot followed by more valid rooms,
 * and breaking at the first hole dropped every room after it, so those rooms
 * went missing from the warp list (v0.6). Both callers (the F8 text menu and
 * the ImGui Warp tab) iterate 0..count and skip the holes via
 * Port_DebugQuery_RoomDimensions, so returning the full span is safe. */
int Port_DebugQuery_AreaRoomCount(unsigned char area) {
    RoomHeader* table = DebugResolveRoomTable(area);
    int lastValid = -1;
    int r;

    if (!table) {
        return 0;
    }
    for (r = 0; r < MAX_ROOMS; r++) {
        if (table[r].map_x == 0xFFFF)
            break; /* asset-loader terminator */
        if (table[r].pixel_width != 0) {
            lastValid = r;
        }
    }
    return lastValid + 1;
}

/* Fill *w / *h with room dimensions in pixels. Returns 1 on success, 0
 * if the area/room is not mapped or the room has zero size. Caller may
 * pass NULL out-pointers to just probe validity. */
int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room, unsigned short* w, unsigned short* h) {
    RoomHeader* table = DebugResolveRoomTable(area);
    if (!table || room >= MAX_ROOMS) {
        return 0;
    }
    if (table[room].pixel_width == 0) {
        return 0;
    }
    if (w) {
        *w = table[room].pixel_width;
    }
    if (h) {
        *h = table[room].pixel_height;
    }
    return 1;
}

/* Friendly area names for the warp menu. NULL entries mean "no useful
 * name" (typically NULL_xx unused slots from include/area.h); the menu
 * falls back to "Area 0xXX" for those. Indexed by AreaID. Keep aligned
 * with the AreaID enum in include/area.h. */
static const char* const kAreaNames[DEBUG_AREA_COUNT] = {
    [0x00] = "Minish Woods",
    [0x01] = "Minish Village",
    [0x02] = "Hyrule Town",
    [0x03] = "Hyrule Field",
    [0x04] = "Castor Wilds",
    [0x05] = "Wind Ruins",
    [0x06] = "Mt Crenel",
    [0x07] = "Castle Garden",
    [0x08] = "Cloud Tops",
    [0x09] = "Royal Valley",
    [0x0A] = "Veil Falls",
    [0x0B] = "Lake Hylia",
    [0x0C] = "Lake Woods Cave",
    [0x0D] = "Beanstalks",
    [0x0F] = "Hyrule Dig Caves",
    [0x10] = "Melari's Mines",
    [0x11] = "Minish Paths",
    [0x12] = "Crenel Minish Paths",
    [0x13] = "Dig Caves",
    [0x14] = "Crenel Dig Cave",
    [0x15] = "Festival Town",
    [0x16] = "Veil Falls Dig Cave",
    [0x17] = "Castor Wilds Dig Cave",
    [0x18] = "Outer Fortress of Winds",
    [0x19] = "Hylia Dig Caves",
    [0x1A] = "Veil Falls Top",
    [0x20] = "Minish House Interiors",
    [0x21] = "House Interiors 1",
    [0x22] = "House Interiors 2",
    [0x23] = "House Interiors 3",
    [0x24] = "Tree Interiors",
    [0x25] = "Dojos",
    [0x26] = "Crenel Caves",
    [0x27] = "Minish Cracks",
    [0x28] = "House Interiors 4",
    [0x29] = "Great Fairies",
    [0x2A] = "Castor Caves",
    [0x2B] = "Castor Darknut",
    [0x2C] = "Armos Interiors",
    [0x2D] = "Town Minish Holes",
    [0x2E] = "Minish Rafters",
    [0x2F] = "Goron Cave",
    [0x30] = "Wind Tribe Tower",
    [0x31] = "Wind Tribe Tower Roof",
    [0x32] = "Caves",
    [0x33] = "Veil Falls Caves",
    [0x34] = "Royal Valley Graves",
    [0x35] = "Minish Caves",
    [0x36] = "Castle Garden Minish Holes",
    [0x38] = "Ezlo Cutscene",
    [0x41] = "Hyrule Town Underground",
    [0x42] = "Garden Fountains",
    [0x43] = "Hyrule Castle Cellar",
    [0x44] = "Simon's Simulation",
    [0x48] = "Deepwood Shrine",
    [0x49] = "Deepwood Shrine - boss",
    [0x4A] = "Deepwood Shrine - entry",
    [0x50] = "Cave of Flames",
    [0x51] = "Cave of Flames - boss",
    [0x58] = "Fortress of Winds",
    [0x59] = "Fortress of Winds Top",
    [0x5A] = "Inner Mazaal",
    [0x60] = "Temple of Droplets",
    [0x62] = "Hyrule Town Minish Caves",
    [0x68] = "Royal Crypt",
    [0x70] = "Palace of Winds",
    [0x71] = "Palace of Winds - boss",
    [0x78] = "Sanctuary",
    [0x80] = "Hyrule Castle",
    [0x81] = "Sanctuary Entrance",
    [0x88] = "Dark Hyrule Castle",
    [0x89] = "Dark Hyrule Castle Outside",
    [0x8A] = "Vaati's Arms",
    [0x8B] = "Vaati 3",
    [0x8C] = "Vaati 2",
    [0x8D] = "Dark Hyrule Castle Bridge",
};

/* NULL when no friendly name is recorded (caller falls back to "Area 0xXX"). */
const char* Port_DebugQuery_AreaName(unsigned char area) {
    if (area >= DEBUG_AREA_COUNT) {
        return NULL;
    }
    return kAreaNames[area];
}
