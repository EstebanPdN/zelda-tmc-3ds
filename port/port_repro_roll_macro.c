/*
 * port/port_repro_roll_macro.c — headless end-to-end test for the one-button
 * roll-attack macro (PR #160, port_roll_attack_macro.c).
 *
 * Enable with TMC_REPRO_ROLL_MACRO=1 (headless: TMC_AUTOPLAY=1,
 * SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy). Optional warp override:
 *   TMC_REPRO_ROLL_MACRO_WARP="area,room,x,y,layer"  (decimal or 0x-hex)
 *
 * Reproduces the macro's documented usage — HOLD a direction (so Link is
 * walking and R's context-action is roll) then press the bind — entirely
 * through the real input path: the test-only edge seam
 * (Port_Config_TestForceEdge) stamps UP + ROLL_ATTACK exactly as an SDL
 * KEY_DOWN would, and everything downstream is untouched game code
 * (Port_Config_InputPressed -> Port_RollAttackMacro_Tick state machine ->
 * roll physics -> PlayerRollUpdate's frame&0x40 window -> UpdateActiveItems
 * -> the macro's virtual best-sword override).
 *
 * Setup that mirrors reality: grant all items (learns SKILL_ROLL_ATTACK +
 * every sword), clear PL_NO_CAP so the roll uses ANIM_ROLL (the post-Ezlo roll
 * with the item window; roll attack is always learned long after Ezlo), and
 * force a NON-sword (Remote Bombs) onto BOTH A and B. A landed roll *attack*
 * (lastSwordMove == SWORD_MOVE_ROLL) with a non-sword on B can therefore ONLY
 * come from the macro's Port_RollAttackMacro_GetEffectiveBItem() override.
 *
 * Oracle (all four required to PASS): PL_ROLLING seen, the frame&0x40 item
 * window seen, lastSwordMove == SWORD_MOVE_ROLL, attack_status peaked non-zero,
 * with B provably non-sword. Exits 0 PASS / 1 FAIL / 3 timeout.
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
#include "game.h"
#include "ui.h"
#include "port_repro.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h"

extern void SetActiveSave(u32 idx);

void Port_ReproRollMacro_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warped = 0, setup = 0;
    static int walk_from = 0, fired = 0, observe_until = 0;
    static int saw_rolling = 0, saw_window = 0, saw_roll_move = 0, best_attack = 0;
    static unsigned int a = 0x03, r = 0x08, x = 0x1d8, y = 0x138, l = 0;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_ROLL_MACRO");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active) {
            const char* w = getenv("TMC_REPRO_ROLL_MACRO_WARP");
            if (w && *w)
                sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            fprintf(stderr, "[roll-macro-test] active warp=0x%02x/0x%02x (%u,%u) layer=%u\n",
                    a, r, x, y, l);
        }
    }
    if (!active)
        return;

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
        fprintf(stderr, "[roll-macro-test] frame %u: bootstrapped -> TASK_GAME\n", frame);
    }

    if (booted && !warped && gMain.task == TASK_GAME && frame % 20 == 0) {
        if (Port_DebugAction_Warp((unsigned char)a, (unsigned char)r,
                                  (unsigned short)x, (unsigned short)y, (unsigned char)l) == 1) {
            warped = 1;
            fprintf(stderr, "[roll-macro-test] frame %u: warped (area=0x%02x room=0x%02x)\n",
                    frame, (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        }
    }

    if (warped && !setup && gMain.task == TASK_GAME && frame % 4 == 0 &&
        gPlayerState.controlMode == CONTROL_ENABLED &&
        !(gPlayerState.flags & (PL_ROLLING | PL_BUSY | PL_DISABLE_ITEMS))) {
        Port_DebugAction_GiveAllItems();
        gSave.stats.equipped[SLOT_A] = ITEM_REMOTE_BOMBS;
        gSave.stats.equipped[SLOT_B] = ITEM_REMOTE_BOMBS;
        setup = 1;
        walk_from = (int)frame + 30;
        fprintf(stderr, "[roll-macro-test] frame %u: items granted; A=B=Remote Bombs (sword=%d); walk at %d\n",
                frame, (int)ItemIsSword(gSave.stats.equipped[SLOT_B]), walk_from);
    }

    /* Keep PL_NO_CAP cleared so PlayerRollInit picks ANIM_ROLL (real post-Ezlo
     * roll with the frame&0x40 item window), not the intro-only ANIM_ROLL_NOCAP. */
    if (setup && !observe_until) {
        gPlayerState.flags &= ~PL_NO_CAP;
    }

    /* Hold UP until Link is actually walking and R's context-action is roll,
     * then stamp the ROLL_ATTACK bind on that frame (documented usage). */
    if (setup && walk_from && (int)frame >= walk_from && !fired) {
        Port_Config_TestForceEdge(PORT_INPUT_UP);
        if (gPlayerState.framestate == PL_STATE_WALK &&
            gHUD.rActionInteractObject == R_ACTION_ROLL &&
            !(gPlayerState.flags & PL_ROLLING)) {
            Port_Config_TestForceEdge(PORT_INPUT_ROLL_ATTACK);
            fired = 1;
            observe_until = (int)frame + 90;
            fprintf(stderr, "[roll-macro-test] frame %u: FIRE (walking; rAction=ROLL; +ROLL_ATTACK)\n", frame);
        } else if ((int)frame > walk_from + 120) {
            fprintf(stderr, "[roll-macro-test] frame %u: never reached WALK+ROLL (fs=%u rAction=%u) -> FAIL\n",
                    frame, (unsigned)gPlayerState.framestate, (unsigned)gHUD.rActionInteractObject);
            fflush(stderr);
            _Exit(1);
        }
    }

    if (observe_until) {
        if (gPlayerState.flags & PL_ROLLING)
            saw_rolling = 1;
        if (gPlayerEntity.base.action == PLAYER_ROLL && (gPlayerEntity.base.frame & 0x40))
            saw_window = 1;
        if ((int)gPlayerState.attack_status > best_attack)
            best_attack = (int)gPlayerState.attack_status;
        if (gPlayerState.lastSwordMove == SWORD_MOVE_ROLL)
            saw_roll_move = 1;

        if ((int)frame >= observe_until) {
            int b_is_sword = (int)ItemIsSword(gSave.stats.equipped[SLOT_B]);
            int pass = saw_rolling && saw_window && saw_roll_move && (best_attack != 0) && !b_is_sword;
            fprintf(stderr,
                    "[roll-macro-test] RESULT: rolling=%d window=%d roll_sword_move=%d attack_peak=%d "
                    "B_equip=0x%02x(sword=%d) -> %s\n",
                    saw_rolling, saw_window, saw_roll_move, best_attack,
                    (unsigned)gSave.stats.equipped[SLOT_B], b_is_sword, pass ? "PASS" : "FAIL");
            fflush(stderr);
            _Exit(pass ? 0 : 1);
        }
    }

    if (frame == 8000) {
        fprintf(stderr, "[roll-macro-test] timeout booted=%d warped=%d setup=%d fired=%d task=%u\n",
                booted, warped, setup, fired, (unsigned)gMain.task);
        fflush(stderr);
        _Exit(3);
    }
}
