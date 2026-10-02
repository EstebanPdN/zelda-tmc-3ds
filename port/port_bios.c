#ifdef TMC_3DS
#include "gba/io_reg.h"
#include "main.h"
#include "port_audio.h"
#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_types.h"
#include "platform_3ds.h"
#include <math.h>
#include <setjmp.h>
#else
#include "gba/io_reg.h"
#include "save.h" /* gSave for Discord-RPC stat reads */
#include "main.h"
#include "port_a11y_cues.h"
#include "port_audio.h"
#include "port_asset_loader.h"
#include "port_debug_actions.h"
#include "port_bugreport.h"
#include "port_debug_menu.h"
#include "port_debug_query.h"
#include "port_discord_rpc.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_imgui_menu.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_practice.h"
#include "port_runtime_config.h"
#include "port_roll_attack_macro.h"
#include "port_softslots.h"
#include "port_touch_controls.h"
#include "port_tts.h"
#include "port_types.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <setjmp.h>
#include "port_repro.h"
#endif

#ifdef TMC_3DS
jmp_buf gPortSoftResetJmp;
int gPortSoftResetArmed = 0;

static const void* Port_ResolveCopySrc(const void* src, u32 size) {
    return port_resolve_copy_src(src, size);
}

extern Main gMain;
extern void VBlankIntr(void);

u64 DivAndModCombined(s32 num, s32 denom) {
    if (denom == 0) {
        return 0;
    }
    const s32 quotient = num / denom;
    const s32 remainder = num % denom;
    return ((u64)(u32)remainder << 32) | (u32)quotient;
}
#else

static bool gQuitRequested = false;
static bool sFastForward = false;
static int sFrameNum = 0;

/* Soft-reset re-entry. AgbMain arms this with setjmp() at the top of the
 * game loop; SoftReset() longjmp()s back to re-run AgbMain's init (which
 * RegisterRamResets everything except EWRAM, preserving save data) — the
 * native equivalent of the GBA BIOS soft reset back to the title. Shared
 * with src/main.c (extern there). */
jmp_buf gPortSoftResetJmp;
int gPortSoftResetArmed = 0;

static const void* Port_ResolveCopySrc(const void* src, u32 size) {
    return port_resolve_copy_src(src, size);
}

typedef struct {
    PortInput input;
    u16 gbaMask;
} PortInputMapEntry;

static const PortInputMapEntry sInputMap[] = {
    { PORT_INPUT_A, A_BUTTON },         { PORT_INPUT_B, B_BUTTON },       { PORT_INPUT_SELECT, SELECT_BUTTON },
    { PORT_INPUT_START, START_BUTTON }, { PORT_INPUT_RIGHT, DPAD_RIGHT }, { PORT_INPUT_LEFT, DPAD_LEFT },
    { PORT_INPUT_UP, DPAD_UP },         { PORT_INPUT_DOWN, DPAD_DOWN },   { PORT_INPUT_R, R_BUTTON },
    { PORT_INPUT_L, L_BUTTON },
};

extern Main gMain;
extern void VBlankIntr(void);

u64 DivAndModCombined(s32 num, s32 denom) {
    s32 quotient;
    s32 remainder;

    if (denom == 0)
        return 0;

    quotient = num / denom;
    remainder = num % denom;
    return ((u64)(u32)remainder << 32) | (u32)quotient;
}

static void Port_UpdateInput(void) {
    Port_ApplyLanguage();
    u16 keyinput = 0x03FF;

    /* Generic framebuffer capture for verification: set TMC_CAPTURE_FRAME=N and
     * TMC_CAPTURE_OUT=<path.png> to dump one PNG of the base framebuffer at frame
     * N, then exit. Region-agnostic; works under the headless video driver. */
    {
        static int sCapFrame = -2; /* -2 unread, -1 disabled */
        static const char* sCapOut = NULL;
        if (sCapFrame == -2) {
            const char* fe = getenv("TMC_CAPTURE_FRAME");
            sCapOut = getenv("TMC_CAPTURE_OUT");
            sCapFrame = (fe && sCapOut) ? atoi(fe) : -1;
        }
        if (sCapFrame >= 0 && (int)sFrameNum >= sCapFrame) {
            extern int Port_CaptureBaseFramebufferPNG(const char* path);
            int ok = Port_CaptureBaseFramebufferPNG(sCapOut);
            fprintf(stderr, "[capture] frame %u -> %s (ok=%d)\n", sFrameNum, sCapOut, ok);
            exit(ok ? 0 : 1);
        }
    }

    /* Roll-attack macro end-to-end test (TMC_REPRO_ROLL_MACRO=1). Runs BEFORE
     * Port_RollAttackMacro_Tick() below so its forced UP + ROLL_ATTACK edges
     * are visible to the macro through the real input path this same frame. */
    { Port_ReproRollMacro_Tick(sFrameNum); }

    /* NPC-talk end-to-end test (TMC_REPRO_NPC_TALK=1 or a "repro_npc_talk"
     * marker file, for Android). Walks to the nearest NPC and stamps R
     * through the real input path; PASS = message box opens. */
    { Port_ReproNpcTalk_Tick(sFrameNum); }

    /* Item-get perf repro (TMC_REPRO_ITEMGET / "repro_itemget" marker):
     * boots into the forge, settles a baseline profiler window, then fires
     * one item-get cutscene and holds it so TMC_PROFILE captures the drop. */
    { Port_ReproItemGet_Tick(sFrameNum); }

    {
        /* While either overlay is open, hold all GBA buttons released so
         * the game doesn't observe stray input from key presses we routed
         * to the overlay. The soft-slot configuration overlay piggybacks
         * on this behaviour while it's the active focus. */
        if (Port_DebugMenu_IsOpen() || Port_SoftSlots_ConfigIsOpen() || Port_InGameSettingsModalIsOpen()) {
            *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = keyinput;
            Port_SoftSlots_TickPause();
            sFrameNum++;
            return;
        }
    }

    for (size_t i = 0; i < sizeof(sInputMap) / sizeof(sInputMap[0]); i++) {
        if (Port_Config_InputPressed(sInputMap[i].input)) {
            keyinput &= ~sInputMap[i].gbaMask;
        }
    }

    /* Soft-slots (X / Y / L2 / R2): when one is held with an item
     * assigned, force GBA B_BUTTON pressed so the engine spawns the
     * soft-slot's item via the regular B-dispatch path. The override of
     * which item to spawn lives in src/playerUtils.c via
     * Port_SoftSlots_GetEffectiveBItem(); the save data is untouched. */
    Port_SoftSlots_Update();
    Port_RollAttackMacro_Tick(&keyinput);
    if (Port_SoftSlots_IsBHeld() || Port_RollAttackMacro_IsBHeld()) {
        keyinput &= ~B_BUTTON;
    }

    /* Decay the pause-active grace counter. The engine's Subtask_PauseMenu
     * pumps it back up to N each frame the start menu is open, so this
     * naturally drops to 0 a few frames after the menu closes. */
    Port_SoftSlots_TickPause();

    *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = keyinput;

    /* Edge cache served its purpose for this frame's KEYINPUT — clear
     * so the next frame starts fresh and a held key reverts to the
     * polled-state path. */
    Port_Config_ClearInputEdges();

    sFrameNum++;
    if (gMain.task == 0 && sFrameNum > 300 && sFrameNum < 310) {
        *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) &= ~START_BUTTON;
    }

    /* Accessibility passive cues (footsteps / hazards / enemy radar / wall
     * bumps). Internally gated on TASK_GAME and per-category toggles; runs
     * after the overlay early-return above so cues stay silent in menus. */
    {
        extern void Port_A11y_Update(void);
        Port_A11y_Update();
    }

    /* Performance-capture harness (TMC_PERFCAP=1): drive into gameplay and
     * dump a complete PPU snapshot for the standalone render microbench. */
    { Port_ReproPerfcap_Tick(sFrameNum); }

    /* Accessibility surroundings-scan self-test (TMC_REPRO_A11Y=1): drive
     * into a room and invoke Port_A11y_ScanSurroundings on live state. */
    { Port_ReproA11y_Tick(sFrameNum); }

    /* Generic in-game room capture (TMC_ROOMCAP=1): bootstrap to TASK_GAME via
     * a quicksave snapshot, warp to a target room (re-rendered from the active
     * ROM), and dump the base framebuffer. Used to compare per-region render. */
    { Port_ReproRoomCap_Tick(sFrameNum); }

    /* Post-warp safe-spawn nudge (issue #94). No-op except in the few
     * frames after a debug-menu warp completes. */
    { Port_DebugAction_WarpTick(); }

}

static void Port_PumpEvents(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        /* Forward every event to ImGui so its window/keyboard tracking
         * stays in sync. ImGui only consumes input when a widget is
         * actively hovered/focused; game input passes through. */
        Port_ImGui_HandleEvent(&e);
        if (e.type == SDL_EVENT_QUIT) {
            /* Route the close-button (X) / OS-quit signal through the
             * ImGui modal first so users get a chance to save. The
             * modal sets Port_ImGui_QuitConfirmed() once the user
             * picks Save&Quit or Quit-Without-Saving; the per-frame
             * check below promotes that into gQuitRequested. */
            Port_ImGui_RequestQuitModal();
            continue;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            /* Soft-slot config overlay is highest priority: it consumes
             * navigation keys before the rest of the routing fires. */
            if (Port_SoftSlots_ConfigIsOpen()) {
                if (Port_SoftSlots_ConfigHandleKey((int)e.key.key)) {
                    continue;
                }
            } else if (e.key.key == SDLK_BACKSLASH && Port_SoftSlots_IsPauseActive()) {
                Port_SoftSlots_ConfigOpen();
                continue;
            }
            bool altHeld = (e.key.mod & SDL_KMOD_ALT) != 0;
            if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && altHeld)) {
                Port_PPU_ToggleFullscreen();
                Port_Config_SetFullscreen(Port_PPU_IsFullscreen()); /* persist (#146) */
                continue;
            }
            if (e.key.key == SDLK_F12) {
                Port_PPU_ToggleSmoothing();
                continue;
            }
            if (e.key.key == SDLK_F8) {
                Port_DebugMenu_Toggle();
                continue;
            }
            if (e.key.key == SDLK_F7) {
                /* TTS master toggle — works whether the F8 menu is
                 * open or not so screen-reader users don't need to
                 * navigate the menu to flip TTS off. */
                bool now = !Port_TTS_GetEnabled();
                Port_TTS_SetEnabled(now);
                /* Announce the new state if we just turned ON; if
                 * turning OFF, SetEnabled already cleared the queue. */
                if (now)
                    Port_TTS_AnnounceMessage("Text to speech enabled.");
                continue;
            }
            if (e.key.key == SDLK_F10) {
                /* Accessibility navigation cues (blind / low-vision):
                 *   F10        — speak nearby points of interest
                 *   Shift+F10  — step to the next nearby object
                 *   Ctrl+F10   — orientation: surface, walls, exits
                 * All no-op outside gameplay / when TTS is off. */
                if (e.key.mod & SDL_KMOD_SHIFT)
                    Port_A11y_CycleNext();
                else if (e.key.mod & SDL_KMOD_CTRL)
                    Port_A11y_LookAround();
                else
                    Port_A11y_ScanSurroundings();
                continue;
            }
            if (e.key.key == SDLK_F6 && (e.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0) {
                /* F6 (no modifiers) stops TTS. The unmodified F6
                 * was previously quickload — that one keeps its
                 * Shift+F6 / Ctrl+F6 bindings. Plain F6 is now the
                 * universal "shut up" key for the TTS user. */
                Port_TTS_Stop();
                /* Don't `continue` — fall through so quickload still
                 * fires on shifted F6 below. Actually unconditional
                 * continue here changes existing behaviour; leave
                 * quickload as-is and let F6 do BOTH (stop speech +
                 * quickload). Stopping speech is harmless during a
                 * load. */
            }
            if (e.key.key == SDLK_F9) {
                /* Capture a bug-report bundle (screenshot + save + state
                 * dump) into a timestamped folder next to the binary so
                 * playtesters can attach it to a GitHub issue. */
                char* dir = Port_BugReport_Capture("user");
                if (dir) {
                    /* Resolve absolute path so the user knows exactly
                     * where the bundle ended up (CWD varies by launcher
                     * — Steam, double-click, terminal). realpath needs
                     * stdlib.h; declared inline so we don't pull a
                     * heavy header chain just for this.  Windows/MinGW
                     * uses _fullpath with the same signature shape. */
                    char abs[4096];
                    char* resolved;
#ifdef _WIN32
                    extern char* _fullpath(char*, const char*, size_t);
                    resolved = _fullpath(abs, dir, sizeof(abs));
#else
                    extern char* realpath(const char*, char*);
                    resolved = realpath(dir, abs);
#endif
                    const char* shown = resolved ? abs : dir;
                    fprintf(stderr, "[BUG] F9 capture → %s\n", shown);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Bug report saved: %.230s", resolved ? abs : dir);
                    Port_DebugMenu_ToastFromExternal(msg);
                    free(dir);
                } else {
                    fprintf(stderr, "[BUG] F9 capture FAILED — check stderr for mkdir errors\n");
                    Port_DebugMenu_ToastFromExternal("Bug report capture FAILED — see stderr");
                }
                continue;
            }
            /* Console-Parity mode makes every save-state hotkey inert —
             * save-states let a run restore arbitrary state mid-glitch, which
             * has no hardware equivalent. Swallow F1-F6 here so neither save
             * nor load fires. */
            if ((e.key.key >= SDLK_F1 && e.key.key <= SDLK_F6) && Port_Config_GetConsoleParity()) {
                Port_DebugMenu_ToastFromExternal("Save-states disabled (Console-Parity)");
                continue;
            }
            if (e.key.key == SDLK_F5) {
                extern int Port_QuickSave(void);
                Port_QuickSave();
                continue;
            }
            if (e.key.key == SDLK_F6) {
                extern int Port_QuickLoad(void);
                Port_QuickLoad();
                continue;
            }
            /* F1..F4 — numbered save-state slots. Plain press = load,
             * Shift+Fn = save. Mirrors emulator-style hotkey conventions.
             * (Matheo's branch bound F1 to an InGameSettingsModal; we
             * already surface those settings via F8 → Reborn / Display
             * tabs, so the save-state binding wins here.) */
            if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F4) {
                extern int Port_QuickSave_SaveSlot(int slot);
                extern int Port_QuickSave_LoadSlot(int slot);
                const int slot = (int)(e.key.key - SDLK_F1) + 1;
                const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                if (shift) {
                    Port_QuickSave_SaveSlot(slot);
                } else {
                    Port_QuickSave_LoadSlot(slot);
                }
                continue;
            }
            /* Speedrun practice-mode hotkeys. Always active (like the
             * save-state F-keys above), but suppressed while an ImGui text
             * field is focused so typing a seed/filename doesn't trip pause.
             * Keys avoid the taken F1..F12 / TAB / BACKSLASH bindings. */
            if (!Port_ImGui_WantsTextInput()) {
                bool handled = true;
                switch (e.key.key) {
                    case SDLK_LEFTBRACKET: /* [  set practice point */
                        if (Port_Practice_SetPoint())
                            Port_DebugMenu_ToastFromExternal("Practice point set");
                        break;
                    case SDLK_RIGHTBRACKET: /* ]  reload practice point */
                        Port_DebugMenu_ToastFromExternal(Port_Practice_LoadPoint() ? "Practice point loaded"
                                                                                   : "No practice point set");
                        break;
                    case SDLK_P: /* P  pause / resume */
                        Port_Practice_TogglePause();
                        Port_DebugMenu_ToastFromExternal(Port_Practice_IsPaused() ? "Paused" : "Resumed");
                        break;
                    case SDLK_PERIOD: /* .  frame-advance (while paused) */
                        Port_Practice_RequestStep();
                        break;
                    case SDLK_APOSTROPHE: /* '  reset IGT timer */
                        Port_Practice_TimerReset();
                        Port_DebugMenu_ToastFromExternal("Timer reset");
                        break;
                    case SDLK_SEMICOLON: /* ;  record split */
                        Port_Practice_AddSplit();
                        break;
                    default:
                        handled = false;
                        break;
                }
                if (handled)
                    continue;
            }
            /* When the debug menu is open, route key presses to it and
             * suppress further handling so the game itself doesn't see
             * the keystroke. */
            {
                if (Port_DebugMenu_IsOpen() && Port_DebugMenu_HandleKey((int)e.key.key)) {
                    continue;
                }
            }
            if (e.key.key == SDLK_TAB) {
                sFastForward = true;
                continue;
            }
        }
        if (e.type == SDL_EVENT_KEY_UP && e.key.key == SDLK_TAB) {
            sFastForward = false;
            continue;
        }
        /* Fast-forward via keyboard TAB only. The previous RIGHT_TRIGGER
         * gamepad shortcut conflicted with the default soft-slot R2 binding
         * (port_softslots.c) — pulling the trigger would simultaneously
         * fast-forward and fire a soft-slot item. */

        /* Gamepad shortcut to open the F8 debug menu: Select + Start.
         * Neither button alone is overloaded (Select opens the map in
         * the engine; Start opens the pause menu), but the combo is
         * unused by the game so it's a safe binding for Steam Deck
         * users who don't have a keyboard within reach. Tracker pair is
         * file-scope-static so it survives across SDL events; we update
         * on both up and down edges so a release-and-re-press re-arms.
         * The toggle fires once on the down-edge of whichever button
         * completes the pair. */
        {
            static bool s_select_held = false, s_start_held = false;
            const bool is_down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            const bool is_up = (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP);
            if (is_down || is_up) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)
                    s_select_held = is_down;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START)
                    s_start_held = is_down;
                if (is_down && s_select_held && s_start_held) {
                    extern void Port_DebugMenu_Toggle(void);
                    Port_DebugMenu_Toggle();
                    s_select_held = false;
                    s_start_held = false;
                }
                /* Practice-mode combos: hold Select(Back) + a face / d-pad
                 * button. Mirrors the Select+Start menu combo above. Fires on
                 * the second button's down-edge; Select is not consumed so
                 * repeated taps (e.g. frame-advance) work while held. Start is
                 * excluded — that pair is the menu toggle. */
                if (is_down && s_select_held && e.gbutton.button != SDL_GAMEPAD_BUTTON_BACK &&
                    e.gbutton.button != SDL_GAMEPAD_BUTTON_START) {
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_SOUTH: /* A: reload point */
                            Port_DebugMenu_ToastFromExternal(Port_Practice_LoadPoint() ? "Practice point loaded"
                                                                                       : "No practice point set");
                            break;
                        case SDL_GAMEPAD_BUTTON_EAST: /* B: set point */
                            if (Port_Practice_SetPoint())
                                Port_DebugMenu_ToastFromExternal("Practice point set");
                            break;
                        case SDL_GAMEPAD_BUTTON_WEST: /* X: pause/resume */
                            Port_Practice_TogglePause();
                            Port_DebugMenu_ToastFromExternal(Port_Practice_IsPaused() ? "Paused" : "Resumed");
                            break;
                        case SDL_GAMEPAD_BUTTON_NORTH: /* Y: frame-advance */
                            Port_Practice_RequestStep();
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: /* reset timer */
                            Port_Practice_TimerReset();
                            Port_DebugMenu_ToastFromExternal("Timer reset");
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: /* record split */
                            Port_Practice_AddSplit();
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        Port_Config_HandleEvent(&e);
    }

    if (Port_TouchControls_ConsumeSettingsRequest()) {
        if (!Port_DebugMenu_IsOpen() && !Port_SoftSlots_ConfigIsOpen() && !Port_InGameSettingsModalIsOpen()) {
            Port_OpenInGameSettingsModal();
        }
    }
}

static u64 lastFrameNs = 0;
static u64 sFpsWindowStartNs = 0;
static u32 sFpsFrameCount = 0;

/* --- Performance phase timers (TMC_PROFILE=1). Off by default; cheap (two
 * SDL_GetTicksNS reads per frame). Decomposes each uncapped frame into
 * game-tick (engine + entity update), present (all of Port_PPU_PresentFrame),
 * and the PPU rasterizer (virtuappu_render_frame, accumulated by
 * port_ppu.cpp). Reports a rolling average to stderr every 600 frames. */
static u64 sProfAccGameNs = 0;
static u64 sProfAccPresentNs = 0;
static u64 sProfMaxGameNs = 0;    /* worst single-frame game tick in the window */
static u64 sProfMaxPresentNs = 0; /* worst single-frame present in the window */
static u32 sProfFrames = 0;
static u32 sProfPresents = 0;     /* decoupled pacing: presents != ticks */
static u64 sProfWindowExitNs = 0; /* decoupled pacing: game-time stamp */
u64 gPortProfileRenderNs = 0; /* updated from port_ppu.cpp */

/* --- Decoupled tick/render pacing state ---
 * Game-logic ticks run on a fixed deadline grid (Port_Config_TickTimeNs:
 * 60 Hz, or GBA-exact 59.7275 Hz under console parity) while presents run on
 * their own wall-clock grid derived from the Target FPS cap. Legacy mode
 * (decouple_render=false) keeps the historical loop where the FPS cap paces
 * the engine itself and therefore changes game speed. */
static u64 sNextPresentNs = 0;          /* render-cadence grid deadline */
static u64 sLastPresentEndNs = 0;       /* starvation guard timestamp */
static u64 sPresentCostEmaNs = 2000000; /* rolling present cost, seeds 2 ms */
/* Tick windows left in conservative (EMA fit-check) presenting after a tick
 * closed late under eager-VSync presenting; 0 = eager. See decoupled loop. */
static u32 sVsyncOverloadHold = 0;
static u32 sPresentsThisSec = 0;        /* presents in the 1 s FPS-title window */

/* Live rates for the on-screen FPS counter (port_imgui_menu.cpp externs
 * these). Refreshed once per second alongside the window title. */
double gPortPaceFps = 0.0;
double gPortPaceTps = 0.0;
bool gPortPaceDecoupled = false;

int Port_Profile_Enabled(void); /* defined below */

/* TMC_TEST_INPUT="420:start,480:a,520:down": fire synthetic input edges at
 * exact engine ticks through the same test-only seam the repro harnesses
 * use (Port_Config_TestForceEdge). Lets scripted test runs drive menus on
 * compositors where no display-server input injection reaches the window
 * (e.g. XWayland under KWin). Buttons: a b select start right left up down
 * r l. */
static void Port_TestInputTick(u32 tick) {
    static int parsed = -1;
    static struct {
        u32 tick;
        PortInput input;
    } seq[32];
    static int n = 0;
    int i;

    if (parsed < 0) {
        const char* e = getenv("TMC_TEST_INPUT");
        parsed = 1;
        if (e && *e) {
            static const struct {
                const char* name;
                PortInput input;
            } kMap[] = {
                { "a", PORT_INPUT_A },         { "b", PORT_INPUT_B },       { "select", PORT_INPUT_SELECT },
                { "start", PORT_INPUT_START }, { "right", PORT_INPUT_RIGHT }, { "left", PORT_INPUT_LEFT },
                { "up", PORT_INPUT_UP },       { "down", PORT_INPUT_DOWN }, { "r", PORT_INPUT_R },
                { "l", PORT_INPUT_L },
            };
            char buf[512];
            char* tok = buf;
            strncpy(buf, e, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            while (tok && n < (int)(sizeof(seq) / sizeof(seq[0]))) {
                char* next = strchr(tok, ',');
                char* colon;
                size_t k;
                if (next)
                    *next++ = '\0';
                colon = strchr(tok, ':');
                if (colon) {
                    *colon = '\0';
                    for (k = 0; k < sizeof(kMap) / sizeof(kMap[0]); k++) {
                        if (strcmp(colon + 1, kMap[k].name) == 0) {
                            seq[n].tick = (u32)strtoul(tok, NULL, 0);
                            seq[n].input = kMap[k].input;
                            n++;
                            break;
                        }
                    }
                }
                tok = next;
            }
        }
    }
    for (i = 0; i < n; i++) {
        if (seq[i].tick == tick) {
            Port_Config_TestForceEdge(seq[i].input);
        }
    }
}

static bool Port_PacingForceLegacy(void) {
    static int f = -1;
    if (f < 0) {
        /* Capture/repro/parity harnesses dump or hash the framebuffer at
         * exact tick numbers (and perfcap measures raster throughput), so
         * they need the one-rasterization-per-tick coupling. Pin them to the
         * legacy loop so their outputs stay comparable across versions.
         * TMC_LEGACY_PACING is the manual escape hatch. */
        f = (getenv("TMC_CAPTURE_FRAME") || getenv("TMC_PERFCAP") || getenv("TMC_ROOMCAP") ||
             getenv("TMC_LEGACY_PACING"))
                ? 1
                : 0;
    }
    return f != 0;
}

/* One full presentation (rasterize + post-process + SDL present), plus the
 * HBlank-DMA rewind so a re-rasterization of the same game frame re-runs
 * per-line effects from scanline 0. Maintains the present-cost EMA the
 * decoupled pacer uses to decide whether another present still fits before
 * the next tick deadline (EMA weight 1/8: one vsync-blocked outlier must not
 * whip the budget around). */
static void Port_PresentOnce(bool firstOfTick) {
    u64 t0 = SDL_GetTicksNS();
    u64 end, dt;
    /* LCD persistence accumulates once per tick (rho is defined per GBA
     * frame); repeat presents blend against the accumulator read-only. */
    Port_PPU_SetPresentIsFirstOfTick(firstOfTick);
    Port_PPU_PresentFrame();
    port_hdma_vblank_reset();
    end = SDL_GetTicksNS();
    dt = end - t0;
    sPresentCostEmaNs += (u64)(((s64)dt - (s64)sPresentCostEmaNs) / 8);
    sLastPresentEndNs = end;
    sPresentsThisSec++;
    if (Port_Profile_Enabled()) {
        sProfAccPresentNs += dt;
        if (dt > sProfMaxPresentNs)
            sProfMaxPresentNs = dt;
        sProfPresents++;
    }
}

/* Emit the rolling TMC_PROFILE report once per 120 ticks. Shared by the
 * legacy and decoupled paths; under decoupling `presents` can differ from
 * the tick count (repeat-presents above 60 FPS, frameskip below / during
 * fast-forward), so it's reported alongside. */
static void Port_ProfileReportMaybe(void) {
    /* 120-frame (~2s) window so a transient item-get hitch is visible; the
     * worst-frame maxima catch a single stall an average would smooth away. */
    if (++sProfFrames >= 120) {
        double n = (double)sProfFrames;
        double game = sProfAccGameNs / 1e6 / n;
        double present = sProfAccPresentNs / 1e6 / n;
        double render = gPortProfileRenderNs / 1e6 / n;
        double total = game + present;
        double maxGame = sProfMaxGameNs / 1e6;
        double maxPresent = sProfMaxPresentNs / 1e6;
        fprintf(stderr,
                "[profile] %u frames: game=%.3f present=%.3f (render=%.3f) ms | "
                "%.0f fps uncapped | worst: game=%.2f present=%.2f ms | presents=%u\n",
                sProfFrames, game, present, render, total > 0.0 ? 1000.0 / total : 0.0, maxGame, maxPresent,
                sProfPresents);
        sProfAccGameNs = sProfAccPresentNs = 0;
        sProfMaxGameNs = sProfMaxPresentNs = 0;
        gPortProfileRenderNs = 0;
        sProfFrames = 0;
        sProfPresents = 0;
    }
}

int Port_Profile_Enabled(void) {
    static int en = -1;
    if (en < 0) {
        const char* e = getenv("TMC_PROFILE");
        if (e && *e && e[0] != '0') {
            en = 1;
        } else {
            /* Android has no env vars; a marker file in the app data dir (CWD)
             * enables it, same convention as the 'verbose' marker:
             *   adb shell touch .../files/profile  */
            FILE* m = fopen("profile", "rb");
            if (m) {
                fclose(m);
                en = 1;
            } else {
                en = 0;
            }
        }
    }
    return en;
}
#endif

void VBlankIntrWait(void) {
#ifdef TMC_3DS
    const bool present = Platform3DS_BeginFrameBoundary();
    if (present) {
        Port_PPU_SetPresentIsFirstOfTick(true);
        Port_PPU_PresentFrame();
    }
    port_hdma_vblank_reset();
    if (present) {
        Platform3DS_WaitForVBlank();
    } else {
        Platform3DS_PumpWithoutVBlank();
    }
    {
        extern bool Port_SecondScreen_3DS_UpdateOpen(void);
        extern void Port_Audio_3DSSetPaused(bool paused);
        if (Port_SecondScreen_3DS_UpdateOpen()) {
            Port_Audio_3DSSetPaused(true);
            while (Platform3DS_IsRunning() && Port_SecondScreen_3DS_UpdateOpen()) {
                Port_PPU_PresentFrame();
                Platform3DS_WaitForVBlank();
                port_hdma_vblank_reset();
            }
            Port_Audio_3DSSetPaused(false);
            Platform3DS_MarkFrameDiscontinuity(OLD3DS_FRAME_PACER_DISCONTINUITY_APT);
        }
    }
    gba_write16(REG_ADDR_KEYINPUT, Platform3DS_ReadKeyInput());
    {
        extern void Port_QuickSave_AutoTick(void);
        Port_QuickSave_AutoTick();
    }
    VBlankIntr();
    Platform3DS_EndFrameBoundary();
#else
    u64 nowNs;
    bool decoupled;

    /* Practice-mode pause: hold the engine here, presenting and pumping
     * events only (no Port_UpdateInput, so the game state can't advance),
     * until unpaused or a single frame-advance step is requested. The F8
     * menu and the practice overlay stay live while the game is frozen;
     * Port_Practice_ConsumeStep() lets exactly one frame through per step. */
    while (Port_Practice_IsPaused() && !Port_Practice_ConsumeStep()) {
        /* Port_PresentOnce (not a bare PresentFrame) so the HBlank-DMA line
         * clock is rewound between re-rasterizations of the same frame —
         * without it, per-line effects (affine wobble) rendered wrong from
         * the second paused present onward. */
        Port_PresentOnce(true);
        Port_PumpEvents();
        if (Port_ImGui_QuitConfirmed())
            gQuitRequested = true;
        if (gQuitRequested)
            exit(0);
        SDL_Delay(4); /* ~4ms: responsive UI without a busy spin */
    }

    /* Toggle VSync based on whether we're trying to run faster than the
     * display refresh: fast-forward, or a target FPS preset > 60. With
     * VSync on, SDL_RenderPresent caps us at the display rate regardless
     * of the busy-wait timer below — so #26 reports of fast-forward and
     * the FPS preset menu having no effect on Windows are actually the
     * display refresh holding us. */
    {
        u32 targetFps = Port_Config_TargetFps();
        /* Honour the user's VSync preference (persisted, #146), but force it
         * off in the cases where VSync would fight the frame-pacing timer and
         * leave the FPS preset / fast-forward with no effect (#26): fast-
         * forward, uncapped, or a target above the display refresh. The
         * ceiling is the REAL refresh, not 60: on a 120 Hz panel a 75/90/120
         * target fits under VSync fine, and forcing it off there just tears
         * (visible as horizontal shear while walking with Smooth motion).
         * Unknown refresh (headless/dummy) keeps the old 60 Hz assumption. */
        unsigned refresh = Port_PPU_DisplayRefreshRate();
        u32 vsyncCeiling = refresh > 0 ? (u32)refresh : 60;
        bool mustDisable = sFastForward || targetFps == 0 || targetFps > vsyncCeiling;
        Port_PPU_SetVSync(Port_Config_GetVSync() && !mustDisable);
    }

    decoupled = Port_Config_GetDecoupleRender() && !Port_PacingForceLegacy();

    if (!decoupled) {
        /* ---- Legacy coupled loop: one present per engine tick, then pace
         * the tick itself. The FPS cap IS the game speed here (a 120 cap
         * runs the game at 2x); kept for the capture/repro harnesses and as
         * a config escape hatch. ---- */
        if (Port_Profile_Enabled()) {
            u64 entry = SDL_GetTicksNS();
            if (sLastPresentEndNs != 0) {
                u64 g = entry - sLastPresentEndNs;
                sProfAccGameNs += g;
                if (g > sProfMaxGameNs)
                    sProfMaxGameNs = g;
            }
            Port_PresentOnce(true);
            Port_ProfileReportMaybe();
        } else {
            Port_PresentOnce(true);
        }

        /* Deadline-based pacing: each frame's target is the previous
         * frame's target + frameTimeNs (a fixed cadence on an ideal grid),
         * not "now + frameTimeNs" (which drifts as game-tick work load
         * varies). The drift version produced visible micro-stutter when
         * a heavy frame consumed a few ms more than usual; deadline pacing
         * absorbs that variance into the next wait without lagging the
         * cadence. If we fall more than one frame behind real time
         * (e.g. paused at a breakpoint, OS hitch), snap forward so we
         * don't burn CPU catching up. */
        if (!sFastForward) {
            u64 frameTimeNs = Port_Config_FrameTimeNs();
            /* Practice slow-motion: stretch the target frame interval. Factor is
             * in (0,1]; 1.0 = normal speed, 0.5 = half speed. When the target is
             * uncapped (0), synthesise a 60fps base so slow-mo still applies.
             * Bypassed during TAB fast-forward (we're inside !sFastForward). */
            {
                float sm = Port_Config_GetPracticeSlowmo();
                if (sm > 0.0f && sm < 0.999f) {
                    u64 base = frameTimeNs ? frameTimeNs : 16666667ULL;
                    frameTimeNs = (u64)((double)base / sm);
                }
            }
            if (frameTimeNs != 0) {
                u64 deadline = lastFrameNs + frameTimeNs;
                u64 now = SDL_GetTicksNS();
                if (now > deadline + frameTimeNs) {
                    /* Fell behind the ideal grid by >1 frame — snap forward. */
                    deadline = now;
                }
                /* Sleep the bulk of the wait, spin only the sub-ms tail
                 * (SDL_DelayPrecise does exactly that). The previous raw
                 * `while (SDL_GetTicksNS() < deadline)` spin burned the
                 * whole inter-frame gap on one core — ~90% of a core at
                 * 60 fps — which on passively-cooled phones/tablets means
                 * heat, throttling, and then real jank. */
                if (now < deadline) {
                    SDL_DelayPrecise(deadline - now);
                }
                lastFrameNs = deadline;
            } else {
                lastFrameNs = SDL_GetTicksNS();
            }
        } else {
            lastFrameNs = SDL_GetTicksNS();
        }
    } else {
        /* ---- Decoupled pacing: the engine ticks on a fixed grid
         * (Port_Config_TickTimeNs — game speed never follows the FPS cap)
         * and presents run on their own wall-clock grid (the FPS cap,
         * reinterpreted as render cadence). Above the tick rate that means
         * re-presenting the same game frame (identical until interpolation
         * lands; still keeps the menu overlay and VRR displays fed); below
         * it, or during fast-forward, whole presents are skipped — which is
         * also what makes fast-forward fast, since raster+present dominates
         * an engine tick. ---- */
        u64 entryNs = SDL_GetTicksNS();
        u32 targetFps = Port_Config_TargetFps();
        bool uncappedTicks = sFastForward || targetFps == 0;
        u64 tickPeriodNs = 0;
        u64 renderPeriodNs;
        u64 now;

        if (Port_Profile_Enabled() && sProfWindowExitNs != 0) {
            u64 g = entryNs - sProfWindowExitNs;
            sProfAccGameNs += g;
            if (g > sProfMaxGameNs)
                sProfMaxGameNs = g;
        }

        if (!uncappedTicks) {
            tickPeriodNs = Port_Config_TickTimeNs();
            /* Practice slow-motion stretches the tick period only; presents
             * keep their real-time cadence, so slow-mo stays visually fluid. */
            float sm = Port_Config_GetPracticeSlowmo();
            if (sm > 0.0f && sm < 0.999f) {
                tickPeriodNs = (u64)((double)tickPeriodNs / sm);
            }
        }
        /* Render cadence. During fast-forward / uncapped ticks the preset
         * has no meaningful relation to ticks, so present at a fixed 60 Hz
         * real-time cadence for feedback while the engine runs free. Under
         * console parity Port_Config_FrameTimeNs pins this to the GBA rate,
         * which intentionally also pins presentation to one-per-tick. */
        renderPeriodNs = (targetFps != 0 && !sFastForward) ? Port_Config_FrameTimeNs() : 16666667ULL;

        now = entryNs;
        if (tickPeriodNs == 0) {
            /* Uncapped ticks: run the engine flat out; present only when the
             * render grid comes due. "now + period" (not grid + period): with
             * most presents skipped there is no cadence to preserve, and grid
             * catch-up would burst-present. */
            if (now >= sNextPresentNs) {
                Port_PresentOnce(true);
                sNextPresentNs = SDL_GetTicksNS() + renderPeriodNs;
            }
            lastFrameNs = SDL_GetTicksNS();
        } else {
            u64 deadline = lastFrameNs + tickPeriodNs;
            if (now > deadline + tickPeriodNs) {
                /* Fell behind the tick grid by >1 tick (breakpoint, OS
                 * hitch, sustained overload) — snap forward rather than
                 * burn CPU catching up. */
                deadline = now;
            }
            /* Present-and-wait until the tick deadline. Each pass either
             * presents (render grid due AND the present is expected to fit
             * before the deadline, judged by the cost EMA) or sleeps to the
             * nearer of the two grids. The starvation override trades
             * display rate for game speed under oversubscription — correct
             * speed at a lower FPS — but never lets more than ~100 ms pass
             * without showing a frame. */
            bool firstOfTick = true;
            for (;;) {
                now = SDL_GetTicksNS();
                if (now >= deadline) {
                    if (now - sLastPresentEndNs > 100000000ULL) {
                        Port_PresentOnce(firstOfTick);
                        sNextPresentNs = SDL_GetTicksNS() + renderPeriodNs;
                    }
                    /* Closed-loop overload guard for the eager-VSync path
                     * below. Vsync-blocked presents legitimately fill the
                     * whole tick window (2 x ~8.3 ms at 120-on-120), so a
                     * tick closing a few ms late is ROUTINE — game-logic
                     * spikes of 2-5 ms happen constantly in busy scenes, the
                     * grid absorbs them, and tripping on them flaps the
                     * display 120<->60, which reads as rubber-banding (user
                     * report, Hyrule Field). Only a close more than half a
                     * tick late — a genuine can't-keep-up signal (GPU stall,
                     * sustained oversubscription) — drops to the
                     * conservative EMA fit check (~1 present/tick) for a
                     * stretch. */
                    if (now > deadline + tickPeriodNs / 2) {
                        sVsyncOverloadHold = 8;
                    } else if (sVsyncOverloadHold > 0) {
                        sVsyncOverloadHold--;
                    }
                    break;
                }
                if (now >= sNextPresentNs) {
                    /* With VSync active the fit check must not apply: the
                     * measured "cost" is mostly the blocking wait to the next
                     * refresh boundary, which puts a 120-target on a 120 Hz
                     * panel exactly on the refusal knife edge (now + ~8.3ms
                     * EMA vs a deadline ~8.3ms away) — refusing drops a whole
                     * refresh (visible hitch), while presenting can overrun
                     * the deadline by at most one refresh, which the tick
                     * grid absorbs (lastFrameNs advances by deadline, not by
                     * when we break out). VSync itself paces us — EXCEPT
                     * while the overload hold (set above) is draining. */
                    if ((Port_PPU_VSyncEnabled() && sVsyncOverloadHold == 0) ||
                        now + sPresentCostEmaNs <= deadline ||
                        now - sLastPresentEndNs > 100000000ULL) {
                        Port_PresentOnce(firstOfTick);
                        firstOfTick = false;
                        /* Advance on the grid while keeping up (even
                         * cadence); snap forward when behind it. */
                        sNextPresentNs = (sNextPresentNs + renderPeriodNs > now) ? sNextPresentNs + renderPeriodNs
                                                                                 : now + renderPeriodNs;
                        /* Keep the window/menu live between ticks. */
                        Port_PumpEvents();
                        continue;
                    }
                    /* Present due but wouldn't fit before the deadline:
                     * hold it for the next window, close this tick out. */
                    SDL_DelayPrecise(deadline - now);
                    continue;
                }
                {
                    u64 until = sNextPresentNs < deadline ? sNextPresentNs : deadline;
                    if (until > now)
                        SDL_DelayPrecise(until - now);
                }
            }
            lastFrameNs = deadline;
        }

        if (Port_Profile_Enabled()) {
            sProfWindowExitNs = SDL_GetTicksNS();
            Port_ProfileReportMaybe();
        }
    }

    nowNs = lastFrameNs;

    if (sFpsWindowStartNs == 0) {
        sFpsWindowStartNs = nowNs;
    }

    sFpsFrameCount++;

    if (nowNs - sFpsWindowStartNs >= 1000000000ULL) {
        double elapsedSec = (double)(nowNs - sFpsWindowStartNs) / 1000000000.0;
        /* Under decoupled pacing ticks and presents are separate counts:
         * "FPS" is what the display sees (presents), "TPS" is game speed. */
        double tps = (elapsedSec > 0.0) ? (double)sFpsFrameCount / elapsedSec : 0.0;
        double fps = decoupled ? ((elapsedSec > 0.0) ? (double)sPresentsThisSec / elapsedSec : 0.0) : tps;
        char title[96];

        gPortPaceFps = fps;
        gPortPaceTps = tps;
        gPortPaceDecoupled = decoupled;

        /* Headless-verifiable pacing probe: TMC_PACE_LOG=1 prints one line
         * per second so tests can assert game speed stays at the tick rate
         * while the render cadence varies. */
        {
            static int paceLog = -1;
            if (paceLog < 0) {
                const char* e = getenv("TMC_PACE_LOG");
                paceLog = (e && *e && e[0] != '0') ? 1 : 0;
            }
            if (paceLog) {
                fprintf(stderr, "[pace] tps=%.2f fps=%.2f target=%u decoupled=%d ff=%d\n", tps, fps,
                        Port_Config_TargetFps(), decoupled ? 1 : 0, sFastForward ? 1 : 0);
            }
        }

/* TMC_PORT_VERSION is set by xmake.lua's add_defines; the fallback below
 * is just for IDE indexers that don't see the build flags. */
#ifndef TMC_PORT_VERSION
#define TMC_PORT_VERSION "0.5.0"
#endif
        if (decoupled) {
            SDL_snprintf(title, sizeof(title), "The Minish Cap " TMC_PORT_VERSION " - %.1f FPS / %.1f TPS", fps, tps);
        } else {
            SDL_snprintf(title, sizeof(title), "The Minish Cap " TMC_PORT_VERSION " - %.1f FPS", fps);
        }
        Port_PPU_SetWindowTitle(title);
#ifdef __ANDROID__
        fprintf(stderr, "[perf] %.1f FPS\n", fps);
#endif

        sFpsWindowStartNs = nowNs;
        sFpsFrameCount = 0;
        sPresentsThisSec = 0;
    }

    /* The ImGui quit-confirm modal is rendered once per frame from
     * port_imgui_menu.cpp; when the user picks Save & Quit or Quit
     * Without Saving it sets the confirmed flag, which we promote
     * into gQuitRequested here so the loop unwinds normally. */
    if (Port_ImGui_QuitConfirmed())
        gQuitRequested = true;

    if (gQuitRequested) {
        exit(0);
    }

    Port_PumpEvents();
    Port_UpdateInput();
    Port_TestInputTick(gMain.ticks);

    /* Speedrun practice: advance the IGT frame counter and sample the input
     * display. After Port_UpdateInput() so the sampled mask is this frame's. */
    Port_Practice_Tick();

    /* Auto-save (no-op unless enabled in F8 menu). Runs at frame
     * boundaries so capture is always at a consistent post-tick state. */
    {
        extern void Port_QuickSave_AutoTick(void);
        Port_QuickSave_AutoTick();
    }

    /* Discord Rich Presence (no-op unless toggled on in F8 → Display).
     * Self-throttled to 1 update per 15s inside the client, so per-
     * frame calls are cheap. Health is stored as eighths of a heart
     * (24 = 3 hearts), hence the >>3. */
    {
        const char* areaName = Port_DebugQuery_AreaName(gRoomControls.area);
        Port_DiscordRpc_Update(areaName, (int)(gSave.stats.health >> 3), (int)(gSave.stats.maxHealth >> 3),
                               (int)gSave.stats.rupees);
    }

    VBlankIntr();
#endif
}

/* ---- BIOS functions ---- */

/* Bytes addressable from a GBA address to the end of its memory region,
 * mirroring the ranges in gba_TryMemPtr. 0 = region unknown (caller treats
 * as unbounded). Used to clamp decompressor output so a corrupt ROM header
 * can't overrun a fixed host buffer (gVram/gEwram/gIwram). */
static size_t Port_GbaRegionBytesLeft(uintptr_t gbaAddr) {
    if (gbaAddr >= 0x02000000u && gbaAddr < 0x02040000u)
        return 0x02040000u - gbaAddr; /* EWRAM */
    if (gbaAddr >= 0x03000000u && gbaAddr < 0x03008000u)
        return 0x03008000u - gbaAddr; /* IWRAM */
    if (gbaAddr >= 0x06000000u && gbaAddr < 0x06018000u)
        return 0x06018000u - gbaAddr; /* VRAM  */
    return 0;
}

/* If `src` lies inside the loaded ROM image, return one-past-the-last ROM
 * byte so a decompressor can't read off the end of a truncated/corrupt ROM.
 * For a heap-resolved asset blob (e.g. a compressed tileset from a pak), return
 * that blob's end: unlike GBA ROM sources, heap blobs have no readable trailing
 * bytes, so a normally-benign trailing over-read in lz77_decomp runs off the
 * allocation. NULL (unbounded) only for sources in neither region. */
static const u8* Port_RomBufferEnd(const void* src) {
    if (gRomData && (const u8*)src >= gRomData && (const u8*)src < gRomData + gRomSize)
        return gRomData + gRomSize;
    return Port_LoadedAssetBytesEnd(src);
}

/* LZ77 decompressor (SWI 0x11/0x12) */
static void lz77_decomp(const u8* src, u8* dst, size_t dstCap, const u8* srcEnd) {
    if (srcEnd && src + 4 > srcEnd)
        return;
    u32 header = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    u32 decompSize = header >> 8;
    src += 4;

    /* Clamp the declared output size to the destination region so a
     * corrupt/crafted header can't overrun the host buffer. For a valid
     * ROM decompSize is always <= the region, so behavior is unchanged. */
    if (dstCap && decompSize > dstCap)
        decompSize = (u32)dstCap;

    u32 written = 0;
    while (written < decompSize) {
        if (srcEnd && src >= srcEnd)
            break;
        u8 flags = *src++;
        for (int i = 7; i >= 0 && written < decompSize; i--) {
            if (flags & (1 << i)) {
                /* Compressed block: 2 bytes → length + distance */
                if (srcEnd && src + 2 > srcEnd)
                    return;
                u8 b1 = *src++;
                u8 b2 = *src++;
                u32 length = ((b1 >> 4) & 0xF) + 3;
                u32 distance = ((b1 & 0xF) << 8) | b2;
                distance += 1;
                /* A back-reference pointing before the output start is
                 * malformed; refuse rather than wild-read host memory. */
                if (distance > written)
                    return;
                for (u32 j = 0; j < length && written < decompSize; j++) {
                    dst[written] = dst[written - distance];
                    written++;
                }
            } else {
                /* Uncompressed byte */
                if (srcEnd && src >= srcEnd)
                    return;
                dst[written++] = *src++;
            }
        }
    }
}

void LZ77UnCompVram(const void* src, void* dst) {
    const void* resolvedSrc = port_resolve_copy_src(src, 4);
    void* resolved = port_resolve_write_addr((uintptr_t)dst);
    lz77_decomp((const u8*)resolvedSrc, (u8*)resolved, Port_GbaRegionBytesLeft((uintptr_t)dst),
                Port_RomBufferEnd(resolvedSrc));
}

void LZ77UnCompWram(const void* src, void* dst) {
    const void* resolvedSrc = port_resolve_copy_src(src, 4);
    void* resolved = port_resolve_write_addr((uintptr_t)dst);
    lz77_decomp((const u8*)resolvedSrc, (u8*)resolved, Port_GbaRegionBytesLeft((uintptr_t)dst),
                Port_RomBufferEnd(resolvedSrc));
}

/* CpuSet (SWI 0x0B) */
void CpuSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF;
    int fill = (cnt >> 24) & 1;
    int is32 = (cnt >> 26) & 1;
    u32 byteCount = is32 ? wordCount * 4 : wordCount * 2;

    void* resolvedDst = port_resolve_write_addr((uintptr_t)dst);
    const void* resolvedSrc = Port_ResolveCopySrc(src, byteCount);

    if (is32) {
        const u32* s = (const u32*)resolvedSrc;
        u32* d = (u32*)resolvedDst;
        u32 val = *s;
        for (u32 i = 0; i < wordCount; i++) {
            d[i] = fill ? val : s[i];
        }
    } else {
        const u16* s = (const u16*)resolvedSrc;
        u16* d = (u16*)resolvedDst;
        u16 val = *s;
        for (u32 i = 0; i < wordCount; i++) {
            d[i] = fill ? val : s[i];
        }
    }
}

/* CpuFastSet (SWI 0x0C) */
void CpuFastSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF; /* low 21 bits = 32-bit word count */
    int fill = (cnt >> 24) & 1;

    void* resolvedDst = port_resolve_write_addr((uintptr_t)dst);
    const void* resolvedSrc = Port_ResolveCopySrc(src, wordCount * 4);

    const u32* s = (const u32*)resolvedSrc;
    u32* d = (u32*)resolvedDst;

    if (fill) {
        u32 val = *s;
        for (u32 i = 0; i < wordCount; i++)
            d[i] = val;
    } else {
        memcpy(d, s, wordCount * 4);
    }
}

/* RegisterRamReset — stub */
void RegisterRamReset(u32 flags) {
    if (flags & RESET_EWRAM) {
        memset(gEwram, 0, sizeof(gEwram));
    }

    if (flags & RESET_IWRAM) {
        memset(gIwram, 0, sizeof(gIwram));
    }

    if (flags & RESET_PALETTE) {
        memset(gBgPltt, 0, sizeof(gBgPltt));
        memset(gObjPltt, 0, sizeof(gObjPltt));
    }

    if (flags & RESET_VRAM) {
        memset(gVram, 0, sizeof(gVram));
    }

    if (flags & RESET_OAM) {
        memset(gOamMem, 0, sizeof(gOamMem));
    }

    if (flags & RESET_SIO_REGS) {
        // SIO register range (subset in IO space): 0x120-0x12A.
        memset(gIoMem + 0x120, 0, 0x0C);
    }

    if (flags & RESET_SOUND_REGS) {
        // Sound register blocks in IO space.
        memset(gIoMem + 0x060, 0, 0x28);
        memset(gIoMem + 0x090, 0, 0x18);
        Port_Audio_Reset();
    }

    if (flags & RESET_REGS) {
        memset(gIoMem, 0, sizeof(gIoMem));
        // GBA KEYINPUT idle state: all keys released.
        *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;
        Port_Audio_Reset();
    }
}

/* Sqrt (SWI 0x08) */
u16 Sqrt(u32 num) {
    /* Bit-by-bit integer sqrt — O(16) and overflow-free. The old
     * `while (r*r <= num) r++` looped forever near num=0xFFFFFFFF
     * because r*r wraps in u32. Returns floor(sqrt(num)), matching the
     * GBA BIOS Sqrt (SWI 0x08). */
    u32 result = 0;
    u32 bit = 1u << 30; /* highest power of four <= 2^31 */
    while (bit > num)
        bit >>= 2;
    while (bit != 0) {
        if (num >= result + bit) {
            num -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (u16)result;
}

/* Div (SWI 0x06) */
s32 Div(s32 num, s32 denom) {
    if (denom == 0)
        return 0;
    /* INT_MIN / -1 overflows the signed result and is UB on x86 (SIGFPE).
     * ARM SDIV yields INT_MIN here, so match that explicitly. */
    if (denom == -1 && num == (s32)0x80000000)
        return (s32)0x80000000;
    return num / denom;
}

/* SoftReset (SWI 0x00) — restart the game to the title, GBA-faithful.
 *
 * The GBA BIOS soft reset re-runs the cartridge from its entry point,
 * preserving EWRAM (so save data survives) and resetting the rest. Here we
 * longjmp back to AgbMain's setjmp at the top of its init, which re-runs
 * RegisterRamReset(RESET_ALL & ~RESET_EWRAM) + the full re-init. This is the
 * end-of-credits path (staffroll.c StaffrollTask_State3 -> DoSoftReset), the
 * Start+Select+A+B reset combo, and the game-over -> title path.
 *
 * It must NOT exit(): besides being the wrong behaviour (closing the app when
 * the player beats the game), process exit runs C++ static destructors over
 * still-joinable worker threads (TTS/audio) -> std::terminate -> SIGABRT. */
void SoftReset(u32 flags) {
    (void)flags;
    if (gPortSoftResetArmed) {
        longjmp(gPortSoftResetJmp, 1);
    }
    /* Reset requested before the game loop armed the jump target (should not
     * happen in practice). Leave without running the crashing teardown. */
    _Exit(0);
}

/* BgAffineSet (SWI 0x0E) */
void BgAffineSet(struct BgAffineSrcData* src, struct BgAffineDstData* dst, s32 count) {
    for (s32 i = 0; i < count; i++) {
        dst[i].pa = src[i].sx;
        dst[i].pb = 0;
        dst[i].pc = 0;
        dst[i].pd = src[i].sy;
        dst[i].dx = src[i].texX - src[i].scrX * src[i].sx;
        dst[i].dy = src[i].texY - src[i].scrY * src[i].sy;
    }
}

/* ObjAffineSet (SWI 0x0F)
 *
 * GBA BIOS computes the *inverse* texture-mapping matrix: hardware applies
 * pa/pb/pc/pd to screen-relative coordinates to produce texture coordinates.
 * For a visible scale of sx, the matrix uses 1/sx — so doubling sx halves
 * the sampled-texture step per screen pixel and the sprite *grows*.
 *
 *   pa =  cos(θ) / sx
 *   pb = -sin(θ) / sy
 *   pc =  sin(θ) / sx
 *   pd =  cos(θ) / sy
 *
 * Inputs sx/sy are 8.8 fixed point (0x100 = 1.0). Output pa/pb/pc/pd are
 * also 8.8 fixed point. Each is written as one s16 at `offset`-byte
 * intervals — for OAM (offset=8), that puts the four values in the
 * affineParam field of 4 consecutive OAM entries.
 */
/* BIOS-accurate sin/cos: the GBA BIOS ObjAffineSet/BgAffineSet quantize the
 * angle to its high 8 bits (256 steps, low byte discarded) and look up a Q1.14
 * (0x4000 = 1.0) sine table, then floor the scaled products with `asr`. The
 * earlier port used full-precision libm cos/sin over all 65536 angles and
 * truncated toward zero, so rotated affine sprites landed on slightly different
 * pixels than console. The table is generated once via round(sin·0x4000) — this
 * matches the BIOS *algorithm* (256-step, Q1.14, floor); individual entries may
 * differ from Nintendo's hand-built table by at most ~1 LSB. */
static s16 sBiosSinLut[256];
static int sBiosSinLutInit = 0;

static void InitBiosSinLut(void) {
    int k;
    for (k = 0; k < 256; k++) {
        double v = sin((double)k * 3.14159265358979323846 * 2.0 / 256.0) * 16384.0;
        sBiosSinLut[k] = (s16)(v >= 0.0 ? (v + 0.5) : (v - 0.5));
    }
    sBiosSinLutInit = 1;
}

void ObjAffineSet(struct ObjAffineSrcData* src, void* dst, s32 count, s32 offset) {
    u8* d = (u8*)dst;
    if (!sBiosSinLutInit)
        InitBiosSinLut();
    for (s32 i = 0; i < count; i++) {
        s32 sx = src[i].xScale;
        s32 sy = src[i].yScale;
        u16 theta = src[i].rotation;
        /* High 8 bits of the angle index the 256-entry table; cos = sin(θ+90°). */
        u32 idx = (theta >> 8) & 0xFFu;
        s32 cosv = sBiosSinLut[(idx + 0x40u) & 0xFFu];
        s32 sinv = sBiosSinLut[idx];

        /* sx/sy are 8.8, the LUT is Q1.14, so >>14 returns 8.8. The shift on a
         * signed product floors (arithmetic), matching the BIOS `asr #14`. No
         * zero-scale guard: the BIOS has none, and with this pure-multiply form
         * sx==0 simply yields pa=pb=0 (no division to protect against). */
        s16 pa = (s16)((sx * cosv) >> 14);
        s16 pb = (s16)((-sx * sinv) >> 14);
        s16 pc = (s16)((sy * sinv) >> 14);
        s16 pd = (s16)((sy * cosv) >> 14);

        *(s16*)(d + 0 * offset) = pa;
        *(s16*)(d + 1 * offset) = pb;
        *(s16*)(d + 2 * offset) = pc;
        *(s16*)(d + 3 * offset) = pd;

        d += 4 * offset;
    }
}

/* Second-screen ROM decodes (port_second_screen_worldmap.c) need BIOS-format
 * LZ77 data expanded into a PRIVATE buffer. LZ77UnCompVram/Wram above always
 * route the destination through port_resolve_addr into live GBA regions, so
 * they can't be used for an off-screen decode without clobbering whatever the
 * game currently has in VRAM/EWRAM. Same bounded decoder, caller-owned
 * destination: dstCap clamps a corrupt/hostile size header, and the source is
 * fenced to its ROM/asset buffer exactly like the engine paths. */
void Port_LZ77DecompressToBuffer(const void* src, void* dst, size_t dstCap) {
    const void* resolvedSrc = port_resolve_copy_src(src, 4);
    lz77_decomp((const u8*)resolvedSrc, (u8*)dst, dstCap, Port_RomBufferEnd(resolvedSrc));
}
