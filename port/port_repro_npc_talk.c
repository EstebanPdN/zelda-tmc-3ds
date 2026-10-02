/*
 * port/port_repro_npc_talk.c — headless end-to-end "talk to an NPC" check.
 *
 * Repro for the Android bug report "can't talk to NPCs". Drives the REAL
 * input path (Port_Config_TestForceEdge -> Port_UpdateInput -> KEYINPUT ->
 * UpdatePlayerInput -> gPossibleInteraction -> message system), so a failure
 * here is an engine/input regression, not a touch-layer artifact.
 *
 * Enable with TMC_REPRO_NPC_TALK=1 (headless: TMC_AUTOPLAY=1
 * SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy). On Android, where env vars
 * can't be passed, a marker file "repro_npc_talk" in the app data dir (the
 * CWD) enables it too.
 *
 * Optional: TMC_REPRO_NPC_TALK_WARP="area,room,x,y,layer" (0x-hex ok)
 *           default: Smith's forge (Link's house ground floor), prologue.
 *
 * Sequence:
 *   1. Title -> file select: synthesize save 0 spawning at the target room.
 *   2. If a cutscene owns control (prologue Zelda/Smith scene), mash A
 *      through it until CONTROL_ENABLED.
 *   3. Find the nearest NPC (gEntityLists[7], same defensive walk the
 *      a11y scanner uses), hold the direction toward it every frame.
 *   4. Once adjacent (<= 24px), keep holding INTO it and stamp R.
 *   5. PASS when the message system activates (gMessage.state & 1) or the
 *      player enters the talk state. FAIL on timeout.
 *
 * Exits 0 PASS / 1 FAIL / 3 bootstrap-timeout. Prints [npc-talk] lines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "save.h"
#include "fileselect.h"
#include "room.h"
#include "player.h"
#include "message.h"
#include "entity.h"
#include "port_repro.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h"

extern void SetActiveSave(u32 idx);
extern int Port_IsValidEntityAddr(const void* p);

/* Message globals (engine): gMessage.state bit 0 = message active. */
extern Message gMessage;

static Entity* NearestNpc(int px, int py) {
    LinkedList* list = &gEntityLists[7]; /* NPC list */
    Entity* e;
    Entity* best = NULL;
    int bestD2 = 0x7fffffff;
    int steps = 0;
    for (e = list->first; e != NULL && (intptr_t)e != (intptr_t)list && steps < 256 && Port_IsValidEntityAddr(e);
         e = e->next, ++steps) {
        int dx, dy, d2;
        if (e->flags & ENT_DELETED)
            continue;
        dx = (int)e->x.HALF.HI - px;
        dy = (int)e->y.HALF.HI - py;
        d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = e;
        }
    }
    return best;
}

void Port_ReproNpcTalk_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warped = 0;
    static int adjacent_since = 0, r_stamped_at = 0;
    static int last_log = 0;
    static int mash_a = 0;
    static int hold_dir = -1;
    static int bootstrap_only = 0;
    static unsigned int a = 0x22, r = 0x11, x = 0x78, y = 0x88, l = 0;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_NPC_TALK");
        const char* roundtrip = getenv("TMC_REPRO_QUICKSAVE_ROUNDTRIP");
        bootstrap_only = roundtrip && *roundtrip && strcmp(roundtrip, "0") != 0;
        active = (env && *env && strcmp(env, "0") != 0) || bootstrap_only;
        if (!active) {
            FILE* marker = fopen("repro_npc_talk", "rb");
            if (marker) {
                fclose(marker);
                active = 1;
            }
        }
        if (active) {
            const char* w = getenv("TMC_REPRO_NPC_TALK_WARP");
            if (w && *w)
                sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            mash_a = getenv("TMC_REPRO_MASH_A") != NULL; /* ponytail: debug knob — mash A every 40f */
            {
                /* ponytail: debug knob — hold a direction every frame (walk through doors) */
                const char* hd = getenv("TMC_REPRO_HOLD_DIR");
                if (hd && *hd) {
                    hold_dir = (*hd == 'u')   ? PORT_INPUT_UP
                               : (*hd == 'd') ? PORT_INPUT_DOWN
                               : (*hd == 'l') ? PORT_INPUT_LEFT
                                              : PORT_INPUT_RIGHT;
                }
            }
            fprintf(stderr, "[npc-talk] active warp=0x%02x/0x%02x (%u,%u) layer=%u\n", a, r, x, y, l);
        }
    }
    if (!active)
        return;

    if (mash_a && warped && frame % 40 < 2) {
        Port_Config_TestForceEdge(PORT_INPUT_A);
        if (getenv("TMC_REPRO_MASH_B")) /* ponytail: debug knob — sword mash for boss-fight repros */
            Port_Config_TestForceEdge(PORT_INPUT_B);
    }
    if (hold_dir >= 0 && gMain.task == TASK_GAME)
        Port_Config_TestForceEdge(hold_dir);

    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        Port_Config_TestForceEdge(PORT_INPUT_START);
    }

    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gFileSelectState.saves[0];
        int slot = 0;
        /* ponytail: debug knob — keep the on-disk saves (real player data,
           first initialized slot) instead of synthesizing; warp still applies. */
        if (getenv("TMC_REPRO_KEEP_SAVE")) {
            while (slot < 2 && !gFileSelectState.saves[slot].initialized) {
                slot++;
            }
            fprintf(stderr, "[npc-talk] keep-save: using slot %d (init=%d)\n", slot,
                    (int)gFileSelectState.saves[slot].initialized);
        } else {
            ResetSaveFile(0);
            sv->initialized = 1;
            sv->name[0] = 'A';
            sv->saved_status.area_next = (u8)a;
            sv->saved_status.room_next = (u8)r;
            sv->saved_status.start_pos_x = (s16)x;
            sv->saved_status.start_pos_y = (s16)y;
            sv->saved_status.layer = (u8)l;
            gFileSelectState.saveStatus[0] = 1;
        }
        SetActiveSave(slot);
        SetTask(TASK_GAME);
        booted = 1;
        fprintf(stderr, "[npc-talk] frame %u: bootstrapped -> TASK_GAME\n", frame);
    }

    if (bootstrap_only && booted) {
        return;
    }

    if (booted && !warped && gMain.task == TASK_GAME && frame % 20 == 0) {
        if ((unsigned)gRoomControls.area == a && (unsigned)gRoomControls.room == r) {
            warped = 1;
            fprintf(stderr, "[npc-talk] frame %u: in target room (area=0x%02x room=0x%02x)\n", frame,
                    (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        } else if (Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x, (unsigned short)y,
                                         (unsigned char)l) == 1) {
            fprintf(stderr, "[npc-talk] frame %u: warp fired\n", frame);
        }
    }
    if (!warped)
        return;

    /* Success oracle: message box opened (talk succeeded). MESSAGE_ACTIVE
     * is 0x7f — any live message phase counts. Check FIRST so the
     * mash/talk logic below can't race it. */
    if ((gMessage.state & 0x7f) != 0 && r_stamped_at != 0) {
        fprintf(stderr, "[npc-talk] frame %u: PASS — message active after R (stamped at %d)\n", frame, r_stamped_at);
        fflush(stderr);
        _Exit(0);
    }

    /* Cutscene / scripted intro: advance any dialogue with A until the
     * player has control. (Fresh flags in the prologue forge trigger the
     * Zelda+Smith scene.) */
    if (gPlayerState.controlMode != CONTROL_ENABLED) {
        if (frame % 40 < 2) {
            Port_Config_TestForceEdge(PORT_INPUT_A);
        }
        return;
    }

    {
        int px = (int)gPlayerEntity.base.x.HALF.HI;
        int py = (int)gPlayerEntity.base.y.HALF.HI;
        Entity* npc = NearestNpc(px, py);
        int dx, dy, adx, ady, adjacent;
        if (npc == NULL) {
            if (frame - last_log > 120) {
                last_log = (int)frame;
                fprintf(stderr, "[npc-talk] frame %u: no NPC in room yet (px=%d py=%d)\n", frame, px, py);
            }
            if (frame > 20000) {
                fprintf(stderr, "[npc-talk] FAIL: no NPC ever appeared\n");
                fflush(stderr);
                _Exit(1);
            }
            return;
        }

        dx = (int)npc->x.HALF.HI - px;
        dy = (int)npc->y.HALF.HI - py;
        adx = dx < 0 ? -dx : dx;
        ady = dy < 0 ? -dy : dy;
        adjacent = (adx <= 14 && ady <= 24) || (adx <= 24 && ady <= 14);

        if (frame - last_log > 120) {
            last_log = (int)frame;
            fprintf(stderr, "[npc-talk] frame %u: npc id=0x%02x d=(%d,%d) player=(%d,%d) anim=%u fs=%u msg=0x%x\n",
                    frame, (unsigned)npc->id, dx, dy, px, py, (unsigned)gPlayerEntity.base.animationState,
                    (unsigned)gPlayerState.framestate, (unsigned)gMessage.state);
        }

        /* Hold the dominant direction toward the NPC — walking into it is
         * exactly how a player talks. Keep holding while stamping R. */
        if (adx > ady) {
            Port_Config_TestForceEdge(dx > 0 ? PORT_INPUT_RIGHT : PORT_INPUT_LEFT);
        } else {
            Port_Config_TestForceEdge(dy > 0 ? PORT_INPUT_DOWN : PORT_INPUT_UP);
        }

        if (adjacent) {
            if (adjacent_since == 0) {
                adjacent_since = (int)frame;
                fprintf(stderr, "[npc-talk] frame %u: adjacent to npc id=0x%02x — stamping R\n", frame,
                        (unsigned)npc->id);
            }
            /* Give facing one frame to settle, then stamp R every other
             * frame (a held key fires the edge once per commit anyway). */
            if ((int)frame > adjacent_since + 2) {
                Port_Config_TestForceEdge(PORT_INPUT_R);
                if (r_stamped_at == 0)
                    r_stamped_at = (int)frame;
            }
            if (r_stamped_at != 0 && (int)frame > r_stamped_at + 240) {
                fprintf(stderr,
                        "[npc-talk] FAIL: R stamped for 240 frames, message never opened "
                        "(msg=0x%x fs=%u interactType-check never fired)\n",
                        (unsigned)gMessage.state, (unsigned)gPlayerState.framestate);
                fflush(stderr);
                _Exit(1);
            }
        } else {
            adjacent_since = 0;
        }
    }

    if (frame >= 30000) {
        fprintf(stderr, "[npc-talk] timeout: booted=%d warped=%d task=%u ctl=%u\n", booted, warped,
                (unsigned)gMain.task, (unsigned)gPlayerState.controlMode);
        fflush(stderr);
        _Exit(3);
    }
}
