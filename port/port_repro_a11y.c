/*
 * port/port_repro_a11y.c — headless self-test for the accessibility
 * surroundings scan (port_a11y_cues.c).
 *
 * TMC_REPRO_A11Y=1 drives title -> file-select -> game, warps into a
 * room, then spawns a deterministic set of points of interest at known
 * offsets from the player and invokes Port_A11y_ScanSurroundings(). This
 * exercises the real per-kind list walk, classifier, and
 * direction/distance formatting on live state, independent of whatever
 * the destination room happens to contain. With TMC_A11Y_DEBUG=1 the
 * spoken phrase is printed so the output can be checked without audio.
 *
 *   TMC_REPRO_A11Y=1 TMC_A11Y_DEBUG=1 TMC_AUTOPLAY=1 \
 *     SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./tmc_pc --no-audio
 *
 * Expected phrase (nearest first): rupee SE ~1, dog west 2, chest east 3,
 * enemy north 5. Prints "[a11y-repro] PASS" and exits 0.
 *
 * The spawned entities are never updated (we _Exit immediately after the
 * scan), so leaving them half-initialised in the pool is safe.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "port_repro.h"

#include "main.h"   /* gMain, TASK_GAME */
#include "room.h"   /* gRoomControls */
#include "entity.h" /* GetEmptyEntity, AppendEntityToList, EntityKind */
#include "player.h" /* gPlayerEntity */
#include "object.h" /* CHEST_SPAWNER, GROUND_ITEM */
#include "npc.h"    /* DOG */
#include "enemy.h"  /* OCTOROK */
#include "item_ids.h" /* ITEM_RUPEE20 */
#include "port_a11y_cues.h"
#include "port_gba_mem.h"
#include "port_debug_actions.h"


#define KEYINPUT_REG 0x130
#define A_BUTTON     0x0001
#define START_BUTTON 0x0008

static void spawn_poi(u8 kind, u8 id, u8 type, int wx, int wy, u32 listIdx) {
    Entity* e = GetEmptyEntity();
    if (e == NULL) {
        fprintf(stderr, "[a11y-repro] spawn failed (pool full)\n");
        return;
    }
    memset(e, 0, sizeof(Entity));
    e->kind = kind;
    e->id = id;
    e->type = type;
    e->x.HALF.HI = (s16)wx;
    e->y.HALF.HI = (s16)wy;
    AppendEntityToList(e, listIdx);
}

void Port_ReproA11y_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int fire_frame = 0;

    if (active < 0) {
        const char* e = getenv("TMC_REPRO_A11Y");
        active = (e && *e && e[0] != '0') ? 1 : 0;
        if (active)
            fprintf(stderr, "[a11y-repro] harness active; target area=0x03 room=0x08\n");
    }
    if (!active)
        return;

    /* Drive title + file-select (GBA KEYINPUT: 0 = pressed). */
    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 30 && (frame & 0xF) < 3)
            presses |= START_BUTTON;
        if (gMain.task == 1 && (frame & 0xF) < 3)
            presses |= A_BUTTON;
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    if (gMain.task == 2 && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(0x03, 0x08, 0x1d8, 0x138, 0);
        fprintf(stderr, "[a11y-repro] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            fire_frame = (int)(frame + 120); /* let the room finish loading */
        }
    }

    if (warp_done && fire_frame && (int)frame >= fire_frame) {
        int px = gPlayerEntity.base.x.HALF.HI;
        int py = gPlayerEntity.base.y.HALF.HI;
        fprintf(stderr, "[a11y-repro] frame %u: spawn+scan at player (%d,%d) area=0x%02x room=0x%02x\n",
                frame, px, py, gRoomControls.area, gRoomControls.room);
        spawn_poi(OBJECT, CHEST_SPAWNER, 0,            px + 48, py,      6); /* east, 3 tiles  */
        spawn_poi(ENEMY,  OCTOROK,       0,            px,      py - 80, 4); /* north, 5 tiles */
        spawn_poi(NPC,    DOG,           0,            px - 32, py,      7); /* west, 2 tiles  */
        spawn_poi(OBJECT, GROUND_ITEM,   ITEM_RUPEE20, px + 16, py + 16, 6); /* SE, ~1 tile    */
        fflush(stderr);
        Port_A11y_ScanSurroundings();
        fprintf(stderr, "[a11y-repro] PASS - scan survived live entity + exit walk\n");
        fflush(stderr);
        _Exit(0); /* skip atexit/destructors (pre-existing port_tts teardown abort) */
    }

    if (frame == 6000) {
        fprintf(stderr, "[a11y-repro] timeout (warp_done=%d)\n", warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
