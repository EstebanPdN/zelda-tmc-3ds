/*
 * port/port_repro_roomcap.c — generic in-game room capture harness.
 *
 * Bootstraps a fresh new game directly (no save file required): presses START
 * at the title to reach file-select, then synthesizes a clean save whose start
 * room is the target, activates it, and enters TASK_GAME. Once in-game it warps
 * to the target room (re-rendered from the ACTIVE ROM) and dumps the base
 * framebuffer. The capture therefore reflects the loaded region's true
 * rendering of a known room — ideal for per-region comparison.
 *
 * Enable with TMC_ROOMCAP=1 (headless: TMC_AUTOPLAY=1, SDL_VIDEODRIVER=dummy).
 *   TMC_ROOMCAP_WARP="a,r,x,y,l"    target area,room,x,y,layer (decimal or 0x-hex)
 *   TMC_ROOMCAP_SETTLE=<frames>     frames to settle after warp (default 300)
 *   TMC_ROOMCAP_GORON_STAGE=1..6    exercise a regional Goron world event
 *   TMC_ROOMCAP_OUT=<path.png>      output PNG (default roomcap.png)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "area.h"       /* gArea, AREA_* */
#include "main.h"       /* gMain, SetTask, TASK_* */
#include "save.h"       /* SaveFile, gSave */
#include "fileselect.h" /* gMapDataBottomSpecial, ResetSaveFile */
#include "flags.h"      /* SetGlobalFlag, prologue flags */
#include "room.h"       /* gRoomControls */
#include "scroll.h"     /* DoExitTransition */
#include "message.h"    /* gMessage, MessageRequest (TMC_ROOMCAP_MSG hook) */
#include "script.h"     /* gActiveScriptInfo, ScriptExecutionContext (TMC_ROOMCAP_PAN_PROBE) */
#include "port_repro.h"
#include "kinstone.h"
#include "subtask.h"
#include "menu.h"
#include "npc.h"
#include "port_rom.h"
#include "port_gba_mem.h" /* gIoMem, gVram, gBgPltt, gObjPltt, gOamMem */
#include "port_debug_actions.h"

extern int Port_CaptureBaseFramebufferPNG(const char* path);
extern void SetActiveSave(u32 idx); /* fileselect.c (no header decl) */

/* Dump the mode1 PPU memory (io/vram/bg-pltt/obj-pltt/oam) for JP-vs-USA diff. */
static void RoomCap_DumpPpu(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    const char magic[4] = { 'T', 'M', 'C', 'P' };
    const uint32_t sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
    fwrite(magic, 1, 4, f);
    fwrite(sizes, sizeof(uint32_t), 5, f);
    fwrite(gIoMem, 1, 0x400u, f);
    fwrite(gVram, 1, 0x18000u, f);
    fwrite(gBgPltt, 1, 0x200u, f);
    fwrite(gObjPltt, 1, 0x200u, f);
    fwrite(gOamMem, 1, 0x400u, f);
    fclose(f);
    fprintf(stderr, "[roomcap] dumped PPU snapshot -> %s (dispcnt=0x%04x)\n", path,
            (unsigned)(gIoMem[0] | (gIoMem[1] << 8)));
}

#define KEYINPUT_REG 0x130
#define START_BUTTON 0x0008

void Port_ReproRoomCap_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warp_done = 0, cap_frame = 0, settle = 300;
    static unsigned int a = 0x03, r = 0x08, x = 0x1d8, y = 0x138, l = 0;

    if (active < 0) {
        const char* env = getenv("TMC_ROOMCAP");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active) {
            const char* st = getenv("TMC_ROOMCAP_SETTLE");
            if (st && *st)
                settle = atoi(st);
            const char* w = getenv("TMC_ROOMCAP_WARP");
            if (w && *w)
                sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            fprintf(stderr, "[roomcap] active warp=0x%02x/0x%02x (%u,%u) layer=%u settle=%d\n", a, r, x, y, l, settle);
        }
    }
    if (!active)
        return;

    /* Press START at the title to reach file-select. */
    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~START_BUTTON;
    }

    /* FILE-SELECT animation probe: sit at file-select and capture two frames a
     * fixed distance apart so the caller can diff them. Does NOT bootstrap a
     * save / enter TASK_GAME. Enable with TMC_ROOMCAP_FILESELECT=1.
     * Outputs <OUT base>_a.png and <OUT base>_b.png. */
    {
        static int fsel = -1;
        static int fsel_first = 0;
        if (fsel < 0) {
            const char* fe = getenv("TMC_ROOMCAP_FILESELECT");
            fsel = (fe && *fe && strcmp(fe, "0") != 0) ? 1 : 0;
        }
        if (fsel) {
            extern int Port_FselAnimTrace;
            unsigned dispcnt = (unsigned)(gIoMem[0] | (gIoMem[1] << 8));
            /* Wait until file-select is fully presented: OBJ layer on (bit 12). */
            int objon = (dispcnt & 0x1000) != 0;
            /* Anim trace: once OBJ is on and stable, trace UpdateAnimationSingleFrame
             * for ~3 frames, then dump call counter. */
            if (getenv("TMC_ROOMCAP_FSEL_ANIMTRACE") && gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                static int at_first = 0;
                extern unsigned long Port_UASF_Calls;
                if (at_first == 0) {
                    at_first = (int)frame;
                    fprintf(stderr, "[fselcnt] frame %u objon: UASF calls so far = %lu (state=%u lastState=%u)\n",
                            frame, Port_UASF_Calls, (unsigned)gMain.state, (unsigned)gUI.lastState);
                }
                if ((int)frame >= at_first && (int)frame <= at_first + 2) {
                    Port_FselAnimTrace = 1;
                } else {
                    Port_FselAnimTrace = 0;
                }
                if ((int)frame == at_first + 4) {
                    fprintf(stderr, "[fselcnt] frame %u: UASF calls now = %lu\n", frame, Port_UASF_Calls);
                    fflush(stderr);
                    _Exit(0);
                }
            }
            /* Time-series mode: dump OAM every 4 frames for 12 samples. */
            if (getenv("TMC_ROOMCAP_FSEL_SERIES") && gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                static int s_n = 0;
                static unsigned s_last = 0;
                if (s_n < 12 && (s_last == 0 || frame >= s_last + 4)) {
                    char d[512];
                    const char* dp = getenv("TMC_ROOMCAP_DUMP");
                    if (!dp || !*dp)
                        dp = "fsel_series";
                    snprintf(d, sizeof(d), "%s_%02d.bin", dp, s_n);
                    RoomCap_DumpPpu(d);
                    s_last = frame;
                    s_n++;
                    if (s_n >= 12) {
                        fflush(stderr);
                        _Exit(0);
                    }
                }
                return;
            }
            if (gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                const char* base = getenv("TMC_ROOMCAP_OUT");
                if (!base || !*base)
                    base = "fsel.png";
                if (fsel_first == 0) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s_a.png", base);
                    int ok = Port_CaptureBaseFramebufferPNG(p);
                    fprintf(stderr, "[roomcap-fsel] frame %u: captured %s -> %d (state=%u substate=%u)\n", frame, p, ok,
                            (unsigned)gMain.state, (unsigned)gUI.state);
                    fsel_first = (int)frame;
                    const char* dumpa = getenv("TMC_ROOMCAP_DUMP");
                    if (dumpa && *dumpa) {
                        char d[512];
                        snprintf(d, sizeof(d), "%s_a.bin", dumpa);
                        RoomCap_DumpPpu(d);
                    }
                } else if ((int)frame >= fsel_first + 60) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s_b.png", base);
                    int ok = Port_CaptureBaseFramebufferPNG(p);
                    fprintf(stderr, "[roomcap-fsel] frame %u: captured %s -> %d\n", frame, p, ok);
                    const char* dumpb = getenv("TMC_ROOMCAP_DUMP");
                    if (dumpb && *dumpb) {
                        char d[512];
                        snprintf(d, sizeof(d), "%s_b.bin", dumpb);
                        RoomCap_DumpPpu(d);
                    }
                    fflush(stderr);
                    _Exit(ok ? 0 : 2);
                }
            }
            if (frame == 5000) {
                fprintf(stderr, "[roomcap-fsel] timeout (task=%u)\n", (unsigned)gMain.task);
                fflush(stderr);
                _Exit(3);
            }
            return; /* don't fall through to the room-bootstrap path */
        }
    }

    /* At file-select, bootstrap a clean new game whose start room is the target,
     * then jump straight into TASK_GAME (skips the intro cutscene entirely). */
    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gFileSelectState.saves[0];
        ResetSaveFile(0);
        sv->initialized = 1;
        sv->name[0] = 'A'; /* non-empty: skip FinalizeSave's default-name copy */
        sv->saved_status.area_next = (u8)a;
        sv->saved_status.room_next = (u8)r;
        sv->saved_status.start_pos_x = (s16)x;
        sv->saved_status.start_pos_y = (s16)y;
        sv->saved_status.layer = (u8)l;
        gFileSelectState.saveStatus[0] = 1; /* SAVE_VALID */
        SetActiveSave(0);
        if (getenv("TMC_ROOMCAP_STORY_SKIP")) {
            SetGlobalFlag(START);
            SetGlobalFlag(EZERO_1ST);
            SetGlobalFlag(TABIDACHI);
        }
        SetTask(TASK_GAME);
        booted = 1;
        fprintf(stderr, "[roomcap] frame %u: bootstrapped new game -> TASK_GAME (start 0x%02x/0x%02x)\n", frame, a, r);
    }

    /* Once stable in TASK_GAME, warp to the target (re-renders from active ROM). */
    if (booted && !warp_done && gMain.task == TASK_GAME && frame % 20 == 0) {
        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x, (unsigned short)y,
                                       (unsigned char)l);
        fprintf(stderr, "[roomcap] frame %u: warp -> %d (task=%u area=0x%02x room=0x%02x)\n", frame, rc,
                (unsigned)gMain.task, (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        if (rc == 1) {
            warp_done = 1;
            cap_frame = (int)(frame + settle);
        }
    }

    /* Exercise real regional Goron scripts, world-event entry and restoration.
     * Run from an isolated working directory: the normal save layer is live. */
    {
        static unsigned started;
        static int visitedGoronRoom;
        const char* stageEnv = getenv("TMC_ROOMCAP_GORON_STAGE");
        const unsigned stage = stageEnv ? (unsigned)strtoul(stageEnv, NULL, 0) : 0;
        if (stage >= 1 && stage <= 6 && warp_done && !started &&
            (int)frame >= cap_frame - settle + 120) {
            const KinstoneWorldEvent* fusions = REGION_IS_EU ?
                gKinstoneWorldEvents_eu : gKinstoneWorldEvents;
            for (unsigned id = 1; id <= 100; ++id) {
                if (fusions[id].subtask != SUBTASK_WORLDEVENT) continue;
                const WorldEvent* event = &GetWorldEvents()[fusions[id].worldEventId];
                if ((event->type == 17 || event->type == 18) && event->entity_idx < stage)
                    gSave.kinstones.fusedKinstones[id / 8] |= 1u << (id % 8);
            }
            for (unsigned id = 1; id <= 100; ++id) {
                if (fusions[id].subtask != SUBTASK_WORLDEVENT) continue;
                const WorldEvent* event = &GetWorldEvents()[fusions[id].worldEventId];
                if ((event->type == 17 || event->type == 18) && event->entity_idx == stage - 1) {
                    gFuseInfo.kinstoneId = id;
                    MenuFadeIn(SUBTASK_WORLDEVENT, fusions[id].worldEventId);
                    started = frame;
                    cap_frame = frame + settle;
                    fprintf(stderr, "[roomcap-goron] stage=%u fusion=%u event=%u started=%u\n",
                            stage, id, fusions[id].worldEventId, frame);
                    break;
                }
            }
            if (!started) { fputs("[roomcap-goron] no event found\n", stderr); _Exit(4); }
        }
        if (stage && started && (frame - started) % 40 < 2) {
            extern void Port_Config_TestForceEdge(int input);
            Port_Config_TestForceEdge(0); /* Advance scripted dialogue. */
        }
        if (stage && started && frame == started + 240) {
            const char* out = getenv("TMC_ROOMCAP_OUT");
            if (out) {
                char path[1024];
                snprintf(path, sizeof(path), "%s-event.png", out);
                Port_CaptureBaseFramebufferPNG(path);
            }
        }
        if (stage && started && gUI.nextToLoad == 2 && gRoomControls.area == 0x2f)
            visitedGoronRoom = 1;
        if (visitedGoronRoom && gUI.nextToLoad == 0 && frame > started + 600)
            cap_frame = (int)frame;
        if (stage && started && (int)frame == cap_frame) {
            fprintf(stderr, "[roomcap-goron] end next=%u task=%u area=%u room=%u menu=%u/%u sync=%04x\n",
                    gUI.nextToLoad, gMain.task, gRoomControls.area, gRoomControls.room,
                    gMenu.menuType, gMenu.overlayType, gActiveScriptInfo.syncFlags);
            fprintf(stderr, "[roomcap-goron] message=%u render=%u flagff=%u\n",
                    gMessage.state, gTextRender.renderStatus, CheckRoomFlag(0xff));
            for (unsigned i = 0; i < MAX_ENTITIES; ++i) {
                Entity* ent = &gEntities[i].base;
                if (ent->next && ent->kind == NPC && ent->id == GORON) {
                    const ScriptExecutionContext* ctx = gEntities[i].scriptContext;
                    fprintf(stderr, "[roomcap-goron] npc%u action=%u anim=%u xy=%d,%d script=%lx wait=%u cond=%u\n",
                            i, ent->action, ent->animIndex, ent->x.HALF.HI, ent->y.HALF.HI,
                            ctx ? (unsigned long)((uintptr_t)ctx->scriptInstructionPointer - (uintptr_t)gRomData) : 0,
                            ctx ? ctx->wait : 0, ctx ? ctx->condition : 0);
                }
            }
            if (gUI.nextToLoad != 0 || gRoomControls.area != a || gRoomControls.room != r)
                { fputs("[roomcap-goron] scene did not return\n", stderr); _Exit(5); }
        }
    }

    /* ---- TMC_ROOMCAP_COF_PROBE: issue #159 repro -----------------------
     * After the warp settles: set the puzzle's local flags (fight done 0x16,
     * knob seated 0x17), warp again to reload the room so roomInit/object
     * init paths observe them, then probe the seated-chest interaction:
     * teleport Link under the seat, face north, press R, and log the tile
     * type + chest flag 0x18 + inventory small-key count each step. */
    {
        static int probe = -1;
        static int stage = 0, stage_frame = 0;
        if (probe < 0)
            probe = (getenv("TMC_ROOMCAP_COF_PROBE") != NULL) ? 1 : 0;
        if (probe && warp_done) {
            extern void SetLocalFlag(u32 flag);
            extern u32 CheckLocalFlag(u32 flag);
            extern u32 GetTileTypeAtTilePos(u32 position, u32 layer);
            extern u8 GetDungeonState(void);
            /* seat = world (0x108,0x38) -> tilePos 0xd0, layer 2 per room data */
            const u32 seatPos = 0xd0;
            const u32 seatLayer = 2;
            switch (stage) {
                case 0: /* wait for settle, then set flags + reload room */
                    if ((int)frame >= cap_frame - settle + 120) {
                        SetLocalFlag(0x16);
                        SetLocalFlag(0x17);
                        fprintf(stderr, "[cof-probe] frame %u: set flags 0x16/0x17 (chest 0x18=%u), re-warping\n",
                                frame, CheckLocalFlag(0x18));
                        stage = 1;
                    }
                    break;
                case 1: /* re-warp to reload the room with flags set */
                    if (frame % 20 == 0) {
                        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x,
                                                       (unsigned short)y, (unsigned char)l);
                        if (rc == 1) {
                            stage = 2;
                            stage_frame = (int)frame + 180;
                        }
                    }
                    break;
                case 2: /* room reloaded: log tile state, teleport under seat */
                    if ((int)frame >= stage_frame) {
                        u32 tt_bot = GetTileTypeAtTilePos(seatPos, 1);
                        u32 tt_top = GetTileTypeAtTilePos(seatPos, 2);
                        fprintf(
                            stderr,
                            "[cof-probe] frame %u: post-reload seat tile bottom=0x%x top=0x%x flag17=%u flag18=%u\n",
                            frame, tt_bot, tt_top, CheckLocalFlag(0x17), CheckLocalFlag(0x18));
                        /* stand ON the tile south of the seat (seat y=0x38,
                         * so stand at y=0x48), facing north — the facing
                         * offset then lands exactly on the seat tile */
                        Port_DebugAction_TeleportXY((unsigned short)(gRoomControls.origin_x + 0x108),
                                                    (unsigned short)(gRoomControls.origin_y + 0x48));
                        gPlayerEntity.base.animationState = 0; /* north */
                        gPlayerEntity.base.collisionLayer = (unsigned char)seatLayer;
                        stage = 3;
                        stage_frame = (int)frame + 30;
                    }
                    break;
                case 3: /* press R for a few frames, pinning layer + facing */
                    if ((int)frame >= stage_frame) {
                        extern void Port_Config_TestForceEdge(int input);
                        extern u32 GetActTileForTileType(u32 tileType);
                        gPlayerEntity.base.collisionLayer = (unsigned char)seatLayer;
                        gPlayerEntity.base.animationState = 0;
                        if ((int)frame == stage_frame) {
                            u32 px = (u32)(u16)(gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x);
                            u32 py = (u32)(u16)(gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y);
                            u32 pos_faced = (px >> 4) | (((py - 0x10) >> 4) << 6);
                            u32 tt = GetTileTypeAtTilePos(pos_faced, gPlayerEntity.base.collisionLayer);
                            fprintf(stderr,
                                    "[cof-probe] frame %u: link=(0x%x,0x%x) layer=%u faced=0x%x tt=0x%x act=0x%x\n",
                                    frame, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI,
                                    gPlayerEntity.base.collisionLayer, pos_faced, tt,
                                    (tt < 0x4000) ? GetActTileForTileType(tt) : tt);
                        }
                        Port_Config_TestForceEdge(8 /* PORT_INPUT_R */);
                        if ((int)frame >= stage_frame + 8) {
                            stage = 4;
                            stage_frame = (int)frame + 90;
                        }
                    }
                    break;
                case 4: /* observe result */
                    if ((int)frame >= stage_frame) {
                        u32 tt_top = GetTileTypeAtTilePos(seatPos, 2);
                        fprintf(stderr,
                                "[cof-probe] frame %u: post-R seat tile top=0x%x flag18=%u smallkeys(dungeon)=%u\n",
                                frame, tt_top, CheckLocalFlag(0x18), (unsigned)gSave.dungeonKeys[1]);
                        stage = 5;
                    }
                    break;
                default:
                    break;
            }
            if (stage < 5)
                return; /* hold off the normal capture until the probe finishes */
        }
    }

    /* ---- TMC_ROOMCAP_LEVER_PROBE: issue #159 root cause -----------------
     * RollobiteSwitch (0x50/0x11): seat the cog (flag 0x2f) so re-entry
     * spawns HITTABLE_LEVER via ObjectOnPillar case 2, then dump the
     * lever's memory at PC offsets 0xAC/0xAE/0xB0/0xB2 (where hitFlag may
     * land depending on struct padding) and simulate a sword hit
     * (contactFlags |= CONTACT_NOW). Watch flags 0x2e (lever) / 0x3d
     * (secret->bollard) to see whether the gate chain fires. */
    {
        static int lprobe = -1;
        static int lstage = 0, lstage_frame = 0;
        if (lprobe < 0)
            lprobe = (getenv("TMC_ROOMCAP_LEVER_PROBE") != NULL) ? 1 : 0;
        if (lprobe && warp_done) {
            extern void SetLocalFlag(u32 flag);
            extern u32 CheckLocalFlag(u32 flag);
            extern Entity* DeepFindEntityByID(u32 kind, u32 id);
            switch (lstage) {
                case 0: /* seat the cog, then reload the room */
                    if ((int)frame >= cap_frame - settle + 120) {
                        SetLocalFlag(0x2f);
                        fprintf(stderr, "[lever-probe] frame %u: set cog-seated flag 0x2f, re-warping\n", frame);
                        lstage = 1;
                    }
                    break;
                case 1:
                    if (frame % 20 == 0) {
                        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x,
                                                       (unsigned short)y, (unsigned char)l);
                        if (rc == 1) {
                            lstage = 2;
                            lstage_frame = (int)frame + 180;
                        }
                    }
                    break;
                case 2: /* find the spawned lever, dump candidate offsets */
                    if ((int)frame >= lstage_frame) {
                        Entity* lever = DeepFindEntityByID(6 /* OBJECT */, 0x2f /* HITTABLE_LEVER */);
                        if (lever == NULL) {
                            fprintf(stderr, "[lever-probe] frame %u: NO lever entity found\n", frame);
                            lstage = 5;
                            break;
                        }
                        unsigned char* p = (unsigned char*)lever;
                        fprintf(stderr,
                                "[lever-probe] frame %u: lever=%p type=%u pos=(0x%x,0x%x) "
                                "mem[0xAC]=0x%04x mem[0xAE]=0x%04x mem[0xB0]=0x%04x mem[0xB2]=0x%04x "
                                "flag2e=%u flag3d=%u\n",
                                frame, (void*)lever, lever->type, lever->x.HALF.HI, lever->y.HALF.HI,
                                *(unsigned short*)(p + 0xAC), *(unsigned short*)(p + 0xAE),
                                *(unsigned short*)(p + 0xB0), *(unsigned short*)(p + 0xB2), CheckLocalFlag(0x2e),
                                CheckLocalFlag(0x3d));
                        /* simulate a sword hit */
                        lever->contactFlags = 0x80 /* CONTACT_NOW */;
                        lever->iframes = 0;
                        lstage = 3;
                        lstage_frame = (int)frame + 60;
                    }
                    break;
                case 3: /* observe flag chain */
                    if ((int)frame >= lstage_frame) {
                        fprintf(stderr, "[lever-probe] frame %u: post-hit flag2e=%u flag3d=%u\n", frame,
                                CheckLocalFlag(0x2e), CheckLocalFlag(0x3d));
                        lstage = 5;
                    }
                    break;
                default:
                    break;
            }
            if (lstage < 5)
                return;
        }
    }

    /* ---- TMC_ROOMCAP_PAN_PROBE: scripted-pan camera wait (widescreen) ----
     * Reproduces the WaitForCameraTouchRoomBorder deadlock headlessly: the
     * intro festival pan / Zelda-talk scripts CameraTargetEntity onto an
     * orchestrator, Scroll1 pans, then the script busy-waits for scroll_x to
     * EQUAL the rest position it recomputes itself. Teleport the player (the
     * camera target) across the room, let Scroll1 pan, and evaluate the
     * script's own wait each frame via its command handler. PASS = the wait
     * sets flags bit0 within the window; FAIL = deadlock (the reported
     * freeze). Exits 0/5. */
    {
        static int pprobe = -1;
        static int pstage = 0;
        static unsigned int pdeadline = 0;
        static Entity panTarget; /* physics-free coordinate holder, like the
                                  * CUTSCENE_ORCHESTRATOR the intro scripts
                                  * CameraTargetEntity onto */
        if (pprobe < 0)
            pprobe = (getenv("TMC_ROOMCAP_PAN_PROBE") != NULL) ? 1 : 0;
        if (pprobe && warp_done) {
            extern void WaitForCameraTouchRoomBorder(Entity * entity, ScriptExecutionContext * context);
            extern int Port_Widescreen_EffectiveViewWidth(void);
            switch (pstage) {
                case 0: /* park the pan target at an interior rest position */
                    if ((int)frame >= cap_frame - settle + 120) {
                        /* The deadlock under test only exists with the
                         * widescreen option ON (persisted config may have it
                         * off, e.g. toggled off by a user escaping the very
                         * freeze this reproduces). Force it for the probe. */
                        extern void Port_Config_SetWidescreenEnabled(bool enabled);
                        Port_Config_SetWidescreenEnabled(true);
                    }
                    if ((int)frame >= cap_frame - settle + 120) {
                        /* Interior rest: in a 480px room at viewW=384 the
                         * camera rests at target-192 (in (0, 96)) while the
                         * pre-fix wait recomputed target-120 — guaranteed
                         * mismatch, so this probe FAILS on the old code. An
                         * edge target would NOT discriminate (both formulas
                         * clamp to the same bound there). */
                        memset(&panTarget, 0, sizeof(panTarget));
                        panTarget.x.HALF.HI = (s16)(gRoomControls.origin_x + 240);
                        panTarget.y.HALF.HI = (s16)(gRoomControls.origin_y + 0x138);
                        gRoomControls.camera_target = &panTarget;
                        gRoomControls.scrollSpeed = 4;
                        pdeadline = frame + 600; /* 480px room / 4px per frame << 600 */
                        fprintf(stderr, "[pan-probe] frame %u: target -> x=%d (scroll_x=%d viewW=%d), panning\n", frame,
                                (int)panTarget.x.HALF.HI, (int)gRoomControls.scroll_x,
                                Port_Widescreen_EffectiveViewWidth());
                        pstage = 1;
                    }
                    break;
                case 1: { /* evaluate the script wait every frame */
                    ScriptExecutionContext ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    gActiveScriptInfo.flags &= ~1u;
                    WaitForCameraTouchRoomBorder(NULL, &ctx);
                    if ((frame % 20) == 0) {
                        extern int Port_Widescreen_FallbackNative(void);
                        extern int Port_Widescreen_TargetViewWidth(void);
                        extern bool Port_Config_WidescreenEnabled(void);
                        fprintf(stderr,
                                "[pan-probe] frame %u: scroll_x=%d target_x=%d viewW=%d task=%u cfg=%d fallback=%d "
                                "targetW=%d roomW=%d\n",
                                frame, (int)gRoomControls.scroll_x, (int)panTarget.x.HALF.HI,
                                Port_Widescreen_EffectiveViewWidth(), (unsigned)gMain.task,
                                (int)Port_Config_WidescreenEnabled(), Port_Widescreen_FallbackNative(),
                                Port_Widescreen_TargetViewWidth(), (int)gRoomControls.width);
                    }
                    if (gActiveScriptInfo.flags & 1) {
                        fprintf(stderr, "[pan-probe] frame %u: PASS wait satisfied (scroll_x=%d viewW=%d)\n", frame,
                                (int)gRoomControls.scroll_x, Port_Widescreen_EffectiveViewWidth());
                        fflush(stderr);
                        _Exit(0);
                    }
                    if (frame >= pdeadline) {
                        fprintf(stderr, "[pan-probe] frame %u: FAIL deadlock (scroll_x=%d target_x=%d viewW=%d)\n",
                                frame, (int)gRoomControls.scroll_x, (int)panTarget.x.HALF.HI,
                                Port_Widescreen_EffectiveViewWidth());
                        fflush(stderr);
                        _Exit(5);
                    }
                    break;
                }
                default:
                    break;
            }
            return; /* probe owns the run; skip capture */
        }
    }

    /* ---- TMC_ROOMCAP_TOWN_PROBE: rando festival-softlock repro ---------
     * Story-skip leaves global_progress==1 (until Deepwood) with TABIDACHI
     * set. sub_unk3_HyruleTown_0 must NOT redirect the real Hyrule Town
     * (area 0x02) to the festival town (0x15) once TABIDACHI is set, or the
     * town loads in Picori-festival state while its exits are post-festival
     * -> softlock. Warp to town: TMC_ROOMCAP_WARP=0x02,0,<x>,<y>.
     *   =1  set TABIDACHI  -> PASS if the town stays area 0x02 (the fix)
     *   =2  leave it clear -> PASS if it still redirects to 0x15 (intro intact)
     * Exits 0 (pass) / 5 (fail). */
    {
        static int tprobe = -1, tstage = 0;
        extern void SetGlobalFlag(u32 flag);
        extern u32 CheckGlobalFlag(u32 flag);
        if (tprobe < 0) {
            const char* e = getenv("TMC_ROOMCAP_TOWN_PROBE");
            tprobe = (e && *e) ? atoi(e) : 0;
        }
        if (tprobe && warp_done) {
            switch (tstage) {
                case 0: {
                    extern void ClearGlobalFlag(u32 flag);
                    if (tprobe == 1)
                        SetGlobalFlag(0x15); /* TABIDACHI = intro over */
                    /* Clear the residue of the bootstrap warp's redirect so
                     * ZELDA_CHASE becomes a clean signal: the fix leaves it
                     * clear (redirect not taken); the control re-sets it. */
                    ClearGlobalFlag(0x1c); /* ZELDA_CHASE */
                    tstage = 1;
                    break;
                }
                case 1: {
                    /* Re-warp so sub_unk3_HyruleTown_0 re-evaluates with the
                     * flag now in its intended (post-intro) state. */
                    int rc = Port_DebugAction_Warp(2, 0, (unsigned short)x, (unsigned short)y, (unsigned char)l);
                    if (rc == 1) {
                        tstage = 2;
                        cap_frame = (int)(frame + 90);
                    }
                    break;
                }
                case 2:
                    if ((int)frame >= cap_frame) {
                        unsigned area = (unsigned)gRoomControls.area;
                        int chase = (int)CheckGlobalFlag(0x1c); /* ZELDA_CHASE */
                        unsigned want = (tprobe == 1) ? 0x02u : 0x15u;
                        fprintf(stderr, "[town-probe] mode=%d area=0x%02x zelda_chase=%d (want area=0x%02x)\n", tprobe,
                                area, chase, want);
                        fflush(stderr);
                        _Exit(area == want ? 0 : 5);
                    }
                    break;
                default:
                    break;
            }
            return; /* probe owns the run */
        }
    }

    /* ---- TMC_ROOMCAP_FESTIVAL_EXIT_PROBE ------------------------------
     * Reproduce the intro route from Festival Town into North Hyrule Field.
     * Festival Town has its own coordinate space, so its north exit must use
     * the valid North Hyrule doorway rather than preserving an incompatible
     * absolute X. The probe supplies a stale camera target, fires the real
     * transition, then verifies Link still arrives at x=0x1f8. */
    {
        static int fprobe = -1, fstage = 0, fredirectPrepared = 0;
        static unsigned int fdeadline;
        static Entity staleCameraTarget;
        if (fprobe < 0) {
            const char* e = getenv("TMC_ROOMCAP_FESTIVAL_EXIT_PROBE");
            fprobe = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
        }
        if (fprobe && warp_done) {
            if (!fredirectPrepared && gRoomControls.area == AREA_HYRULE_TOWN) {
                extern void ClearGlobalFlag(u32 flag);
                gSave.global_progress = 1;
                ClearGlobalFlag(TABIDACHI);
                if (Port_DebugAction_Warp(AREA_HYRULE_TOWN, 0, (unsigned short)x, (unsigned short)y,
                                          (unsigned char)l) == 1) {
                    fredirectPrepared = 1;
                }
                return;
            }
            if (fstage == 0 && gRoomControls.area == AREA_FESTIVAL_TOWN &&
                gArea.pCurrentRoomInfo != NULL && gArea.pCurrentRoomInfo->exits != NULL) {
                const Transition* exit = gArea.pCurrentRoomInfo->exits;
                gPlayerEntity.base.x.HALF.HI = gRoomControls.origin_x + gRoomControls.width / 2;
                gPlayerEntity.base.y.HALF.HI = gRoomControls.origin_y + gRoomControls.height - 8;
                memset(&staleCameraTarget, 0, sizeof(staleCameraTarget));
                gRoomControls.camera_target = &staleCameraTarget;
                fprintf(stderr,
                        "[festival-exit] source origin=(%u,%u) player=(%d,%d) camera=(%d,%d) end=(0x%x,0x%x)\n",
                        gRoomControls.origin_x, gRoomControls.origin_y, gPlayerEntity.base.x.HALF.HI,
                        gPlayerEntity.base.y.HALF.HI, staleCameraTarget.x.HALF.HI,
                        staleCameraTarget.y.HALF.HI, exit->endX, exit->endY);
                DoExitTransition(exit);
                fstage = 1;
                fdeadline = frame + 600;
                return;
            }
            if (fstage == 1 && gRoomControls.area == AREA_HYRULE_FIELD &&
                gRoomControls.room == ROOM_HYRULE_FIELD_NORTH_HYRULE_FIELD) {
                fstage = 2;
                fdeadline = frame + 90;
            }
            if (fstage == 2 && frame >= fdeadline) {
                int relativeX = gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x;
                int pass = relativeX == 0x1f8;
                fprintf(stderr,
                        "[festival-exit] %s destination origin=(%u,%u) player=(%d,%d) relativeX=0x%x\n",
                        pass ? "PASS" : "FAIL", gRoomControls.origin_x, gRoomControls.origin_y,
                        gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI, relativeX);
                fflush(stderr);
                _Exit(pass ? 0 : 5);
            }
            if (fstage == 1 && frame >= fdeadline) {
                fprintf(stderr, "[festival-exit] FAIL transition timeout area=0x%02x room=0x%02x\n",
                        (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
                fflush(stderr);
                _Exit(5);
            }
            return;
        }
    }

    /* TMC_ROOMCAP_MSG=<index>[,posY]: open a textbox <settle/2> frames after
     * the warp so the capture shows live dialogue (widescreen centering
     * test). Optional posY overrides the box row (1 = top-anchored). */
    {
        static int msg_done = 0;
        static unsigned int msg_opened_at = 0;
        const char* m = getenv("TMC_ROOMCAP_MSG");
        if (m && *m && !msg_done && warp_done && cap_frame && (int)frame >= cap_frame - settle / 2) {
            unsigned int idx = 0;
            int posY = -1;
            sscanf(m, "%i,%i", &idx, &posY);
            MessageRequest(idx);
            if (posY >= 0)
                gMessage.textWindowPosY = (u8)posY;
            fprintf(stderr, "[roomcap] frame %u: opened message 0x%x posY=%d\n", frame, idx, posY);
            msg_done = 1;
            msg_opened_at = frame;
        }
        /* TMC_ROOMCAP_MSG_ADVANCE=1: tap A every ~40 frames after the box
         * opens so multi-page text rolls forward — but stop on the enquiry
         * (choice) page so the capture shows the live selector instead of
         * confirming through it. renderStatus 5 == RENDER_ENQUIRY. */
        const char* adv = getenv("TMC_ROOMCAP_MSG_ADVANCE");
        if (msg_done && adv && *adv && strcmp(adv, "0") != 0 && gTextRender.renderStatus != 5 &&
            frame > msg_opened_at + 30 && (frame - msg_opened_at) % 40 < 2) {
            extern void Port_Config_TestForceEdge(int input);
            Port_Config_TestForceEdge(0 /* PORT_INPUT_A */);
        }
    }

    if (warp_done && cap_frame && (int)frame >= cap_frame) {
        /* TMC_ROOMCAP_SAVE: write a quicksave (state_quick.bin) at the warped
         * spot so it can be F6-loaded interactively, then exit. */
        if (getenv("TMC_ROOMCAP_SAVE")) {
            extern int Port_QuickSave(void);
            int sv = Port_QuickSave();
            fprintf(stderr, "[roomcap] quicksave -> %d (state_quick.bin) area=0x%02x room=0x%02x\n", sv,
                    (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
            fflush(stderr);
            _Exit(sv ? 0 : 4);
        }
        const char* out = getenv("TMC_ROOMCAP_OUT");
        if (!out || !*out)
            out = "roomcap.png";
        int ok = Port_CaptureBaseFramebufferPNG(out);
        fprintf(stderr, "[roomcap] frame %u: captured %s -> %d (area=0x%02x room=0x%02x)\n", frame, out, ok,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        const char* dump = getenv("TMC_ROOMCAP_DUMP");
        if (dump && *dump)
            RoomCap_DumpPpu(dump);
        fflush(stderr);
        _Exit(ok ? 0 : 2);
    }

    if (frame == (unsigned)(cap_frame > 5000 ? cap_frame + 600 : 5000)) {
        fprintf(stderr, "[roomcap] timeout (booted=%d task=%u warp=%d)\n", booted, (unsigned)gMain.task, warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
