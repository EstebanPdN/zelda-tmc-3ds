/*
 * port_repro_itemget.c — deterministic item-get performance repro.
 *
 * User report: "when we get an item the fps drops". This harness boots a
 * synthetic save straight into Smith's forge (like port_repro_npc_talk.c),
 * waits for player control, lets the scene settle for a baseline profiler
 * window, then triggers a real item-get cutscene via InitItemGetSequence
 * and HOLDS it (no input, so the message box stays open). With TMC_PROFILE
 * on, the [profile] line's game/render split is captured both BEFORE
 * (baseline) and DURING (item-get held) — a clean A/B in one headless run
 * that says whether the drop is engine-tick, rasterizer, or present.
 *
 * Enable: TMC_REPRO_ITEMGET=1  (desktop) or a "repro_itemget" marker file
 * in the app data dir (Android). Item id overridable via
 * TMC_REPRO_ITEMGET_ITEM=<n> (default ITEM_GUST_JAR).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "save.h"
#include "fileselect.h"
#include "room.h"
#include "player.h"
#include "item_ids.h"
#include "message.h"
#include "entity.h"
#include "port_gba_mem.h"
#include "port_repro.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h"

extern void SetActiveSave(u32 idx);
extern Message gMessage;

void Port_ReproItemGet_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warped = 0;
    static int control_since = 0;
    static int triggered_at = 0;
    static int last_log = 0;
    /* Smith's forge, prologue — same target as the npc-talk repro. */
    static unsigned int a = 0x22, r = 0x11, x = 0x78, y = 0x88, l = 0;
    static unsigned int item = ITEM_GUST_JAR;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_ITEMGET");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (!active) {
            FILE* marker = fopen("repro_itemget", "rb");
            if (marker) {
                fclose(marker);
                active = 1;
            }
        }
        if (active) {
            const char* it = getenv("TMC_REPRO_ITEMGET_ITEM");
            if (it && *it)
                item = (unsigned int)strtol(it, NULL, 0);
            fprintf(stderr, "[itemget] active — forge 0x%02x/0x%02x item=0x%02x\n", a, r, item);
        }
    }
    if (!active)
        return;

    /* Title -> file select -> synthetic save booted into the forge. */
    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        Port_Config_TestForceEdge(PORT_INPUT_START);
    }
    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gFileSelectState.saves[0];
        ResetSaveFile(0);
        sv->initialized = 1;
        sv->name[0] = 'A';
        sv->saved_status.area_next = (u8)a;
        sv->saved_status.room_next = (u8)r;
        sv->saved_status.start_pos_x = (s16)x;
        sv->saved_status.start_pos_y = (s16)y;
        sv->saved_status.layer = (u8)l;
        gFileSelectState.saveStatus[0] = 1;
        SetActiveSave(0);
        SetTask(TASK_GAME);
        booted = 1;
        fprintf(stderr, "[itemget] frame %u: bootstrapped -> TASK_GAME\n", frame);
    }

    if (booted && !warped && gMain.task == TASK_GAME && frame % 20 == 0) {
        if ((unsigned)gRoomControls.area == a && (unsigned)gRoomControls.room == r) {
            warped = 1;
            fprintf(stderr, "[itemget] frame %u: in forge\n", frame);
        } else {
            Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x, (unsigned short)y,
                                  (unsigned char)l);
        }
    }
    if (!warped)
        return;

    /* Mash A through the prologue cutscene until Link has control. */
    if (control_since == 0 && gPlayerState.controlMode != CONTROL_ENABLED) {
        if (frame % 40 < 2)
            Port_Config_TestForceEdge(PORT_INPUT_A);
        if ((int)frame - last_log > 120) {
            last_log = (int)frame;
            fprintf(stderr, "[itemget] frame %u: waiting for control (mode=%u)\n", frame,
                    (unsigned)gPlayerState.controlMode);
        }
        return;
    }
    /* Latch: once control is first seen, don't reset even if a scripted beat
     * momentarily drops it — we just want a stable baseline then a trigger. */
    if (control_since == 0) {
        control_since = (int)frame;
        fprintf(stderr, "[itemget] frame %u: control enabled — baseline window (120f) starts\n", frame);
    }

    /* Baseline window: ~120 frames standing in the forge (one clean [profile]
     * line with no item-get) before the trigger. */
    if (triggered_at == 0) {
        if ((int)frame >= control_since + 130) {
            InitItemGetSequence(item, 0, 0);
            triggered_at = (int)frame;
            fprintf(stderr, "[itemget] frame %u: >>> InitItemGetSequence(item=0x%02x) — item-get held from here\n",
                    frame, item);
        }
        return;
    }

    /* Item-get held: DO NOT advance (no A). Link stays in PLAYER_ITEMGET
     * with the message box open, so the drop — if any — is sustained and
     * the profiler captures it. Log the state periodically for correlation. */
    if ((int)frame - last_log > 60) {
        last_log = (int)frame;
        fprintf(stderr, "[itemget] frame %u: HELD action=%u fs=%u msg=0x%x (%d frames in)\n", frame,
                (unsigned)gPlayerEntity.base.action, (unsigned)gPlayerState.framestate, (unsigned)gMessage.state,
                (int)frame - triggered_at);
    }

    /* One-shot PPU snapshot of the item-get message-box scene (windows + blend
     * + OBJ — the composite's hardest layer-selection case). Dumped once, ~60
     * frames in so the box is fully open, when TMC_REPRO_ITEMGET_DUMP is set.
     * Feeds tools/ppu_bench as a parity oracle for the composite rewrite. */
    if ((gMessage.state & 0x7f) != 0 && (int)frame - triggered_at > 60) {
        const char* dump = getenv("TMC_REPRO_ITEMGET_DUMP");
        static int dumped = 0;
        if (dump && *dump && !dumped) {
            dumped = 1;
            FILE* f = fopen(dump, "wb");
            if (f) {
                const char magic[4] = { 'P', 'P', 'U', '1' };
                const unsigned sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
                fwrite(magic, 1, 4, f);
                fwrite(sizes, sizeof(unsigned), 5, f);
                fwrite(gIoMem, 1, 0x400u, f);
                fwrite(gVram, 1, 0x18000u, f);
                fwrite(gBgPltt, 1, 0x200u, f);
                fwrite(gObjPltt, 1, 0x200u, f);
                fwrite(gOamMem, 1, 0x400u, f);
                fclose(f);
                fprintf(stderr, "[itemget] dumped msgbox PPU snapshot -> %s (dispcnt=0x%04x)\n", dump,
                        (unsigned)(gIoMem[0] | (gIoMem[1] << 8)));
            }
            fflush(stderr);
            _Exit(0);
        }
    }
    if ((int)frame - triggered_at > 900) {
        fprintf(stderr, "[itemget] done (held 900 frames)\n");
        fflush(stderr);
        _Exit(0);
    }
}
