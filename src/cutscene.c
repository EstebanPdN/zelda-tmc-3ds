/**
 * @file cutscene.c
 *
 * @brief Cutscenes
 */
#include "cutscene.h"

#include "backgroundAnimations.h"
#include "beanstalkSubtask.h"
#include "common.h"
#include "enemy.h"
#include "fade.h"
#include "fileselect.h"
#include "flags.h"
#include "functions.h"
#include "game.h"
#include "main.h"
#include "menu.h"
#include "manager/staticBackgroundManager.h"
#include "npc.h"
#include "object.h"
#include "port_scripts.h"
#include "screen.h"
#include "script.h"
#include "subtask.h"
#include "tiles.h"
#ifdef PC_PORT
#include "../port/port_tts.h"
#endif

void sub_08051F78(void);
void sub_08051FF0(void);
void sub_08052004(void);

const EntityData gUnk_080FCB94[] = {
    { OBJECT, 15, CHUCHU_BOSS_CUTSCENE, 0, 0, 0x1c8, 0x288, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

void sub_080535AC(void);
void sub_080535F4(void);
void nullsub_481(void);
void sub_08053618(void);
void (*const gUnk_080FCBB4[])(void) = {
    sub_080535AC,
    sub_080535F4,
    nullsub_481,
    sub_08053618,
};

void CutsceneMain_Init(void) {
    gUnk_080FCBB4[gMenu.overlayType]();
}

void sub_080535AC(void) {
    gMenu.overlayType = 1;
    gMenu.transitionTimer = 120;
    gUI.loadGfxOnRestore = TRUE;
    gUpdateVisibleTiles = 1;
    gScreen.lcd.displayControl &= 0xfeff;
    LoadRoomEntityList(gUnk_080FCB94);
    SetFade(FADE_BLACK_WHITE | FADE_INSTANT, 8);
}

void sub_080535F4(void) {
    if (gFadeControl.active == 0) {
        ClearEventPriority();
        gMenu.overlayType = 2;
    }
}

void nullsub_481(void) {
}

void sub_08053618(void) {
    gMenu.transitionTimer--;
    if (gMenu.transitionTimer == 0) {
        sub_08052004();
    }
}

void sub_08053634(void) {
    gUI.nextToLoad = 3; // Subtask_FadeOut
    MessageInitialize();
}

void sub_08053648(void) {
    Entity* obj = CreateObject(SMOKE, 0, 0);
    if (obj != NULL) {
        obj->x.HALF.HI = gRoomControls.origin_x + 0x2d0;
        obj->y.HALF.HI = gRoomControls.origin_y + 0x148;
    }
}

void sub_0805367C(void) {
    gMenu.overlayType++;
}

void sub_0805368C(void) {
    Entity* entity = FindEntityByID(OBJECT, HOUSE_DOOR_INT, 6);
    if (entity != NULL) {
        DeleteEntity(entity);
        SoundReq(SFX_F0); // TODO Door sound during intro
    }
}

void sub_080536A8(void) {
    sub_080A71C4(5, 5, FADE_INSTANT, 0x10);
}

void sub_080536B8(void) {
    sub_080A71C4(5, 3, FADE_INSTANT, 4);
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

extern Script script_IntroCameraTarget;
extern Script script_ZeldaMoveToLinksHouse;
extern Script script_HouseDoorIntro;
extern Script script_CutsceneOrchestratorIntro2;

const EntityData gUnk_080FCBC4[] = {
    { OBJECT, 79, CUTSCENE_ORCHESTRATOR, 0, 0, 0x230, 0x1a8, ENTITY_SCRIPT(script_IntroCameraTarget) },
    { NPC, 79, ZELDA, 0, 0, 0x230, 0x1a8, ENTITY_SCRIPT(script_ZeldaMoveToLinksHouse) },
    { OBJECT, 79, HOUSE_DOOR_EXT, 3, 3, 0x290, 0x193, ENTITY_SCRIPT(script_HouseDoorIntro) },
    { OBJECT, 79, CUTSCENE_ORCHESTRATOR, 0, 0, 0x2d0, 0x1a8, ENTITY_SCRIPT(script_CutsceneOrchestratorIntro2) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

extern Script script_CutsceneOrchestratorIntro;
extern Script script_SmithIntro;
extern Script script_ZeldaIntro;
const EntityData gUnk_080FCC14[] = {
    { OBJECT, 79, CUTSCENE_ORCHESTRATOR, 0, 0, 0x0, 0x0, ENTITY_SCRIPT(script_CutsceneOrchestratorIntro) },
    { NPC, 79, SMITH, 0, 0, 0xb8, 0x60, ENTITY_SCRIPT(script_SmithIntro) },
    { NPC, 79, ZELDA, 0, 0, 0x8, 0x5e, ENTITY_SCRIPT(script_ZeldaIntro) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

extern Script script_ZeldaLeaveLinksHouse;
const EntityData gUnk_080FCC54[] = {
    { NPC, 79, ZELDA, 0, 0, 0xa0, 0x5d, ENTITY_SCRIPT(script_ZeldaLeaveLinksHouse) },
    { OBJECT, 15, HOUSE_DOOR_INT, 4, 256, 0x78, 0x88, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

const Font gUnk_080FCC84 = {
    &gBG1Buffer[0x1cf], BG_TILE_ADDR(0x188), gTextGfxBuffer, 0, 0xf020, 0xf0, 1, 0, 0, 0, 0, 5, 0, 0, 0
};
const Font gUnk_080FCC9C = {
    &gBG1Buffer[0x96], BG_TILE_ADDR(0x188), gTextGfxBuffer, 0, 0xf020, 0x78, 1, 0, 0, 0, 0, 5, 0, 0, 0
};

const struct_080FCCB4 gUnk_080FCCB4[] = {
    { &gUnk_080FCC84, 240, 96, 193, 1 },  { &gUnk_080FCC84, 240, 96, 453, 6 }, { &gUnk_080FCC9C, 120, 160, 363, 4 },
    { &gUnk_080FCC9C, 120, 160, 498, 4 }, { &gUnk_080FCC84, 240, 96, 368, 4 }, { &gUnk_080FCC84, 240, 96, 358, 4 },
};

void sub_08053758(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053904(void);
void sub_08053974(void);
void nullsub_482(void);
void sub_080539BC(void);
void (*const gUnk_080FCCFC[])(void) = {
    sub_08053758, sub_08053800, sub_08053894, sub_08053800, sub_08053894, sub_08053800, sub_08053894, sub_08053800,
    sub_08053894, sub_08053800, sub_08053904, sub_08053974, nullsub_482,  sub_080539BC, nullsub_482,
};

void sub_080536D4(void) {
    gUnk_080FCCFC[gMenu.overlayType]();
}

void sub_0805370C(void);
void (*const gUnk_080FCD38[])(void) = {
    sub_0805370C,
    nullsub_482,
};

void sub_080536F0(void) {
    gUnk_080FCD38[gMenu.overlayType]();
}

void sub_0805370C(void) {
    gMenu.overlayType++;
    gUpdateVisibleTiles = 1;
    sub_08051FF0();
    LoadRoomEntityList((EntityData*)gUnk_080FCC54);
    SetFade(FADE_INSTANT, 0x10);
}

void sub_08053758(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_08053800(void);
void sub_08053894(void);
void sub_080539F4(void);
void sub_08053A1C(void);
void sub_08053A5C(void);
void sub_08053A90(void);
void (*const gUnk_080FCD40[])(void) = {
    sub_08053758, sub_08053800, sub_08053894, sub_08053800, sub_08053894, sub_08053800,
    sub_08053894, sub_08053800, sub_08053894, sub_08053800, sub_08053894, sub_08053800,
    sub_08053894, sub_080539F4, sub_08053A1C, sub_08053A5C, sub_08053A90,
};

void sub_0805373C(void) {
    gUnk_080FCD40[gMenu.overlayType]();
}

void sub_08053758(void) {
    gMenu.overlayType = 1;
    gMenu.transitionTimer = 120;
    gMenu.field_0xa = 0x1e;
#ifdef PC_PORT
    /* Raw +0x10 targets GenericMenu.unk10.a[0] (the story-page counter) on GBA,
     * where Menu is 0x10 bytes. On PC Menu.field_0xc is an 8-byte pointer, so the
     * Menu base grows to 0x18 and +0x10 lands inside that pointer — corrupting it
     * and never resetting the page counter. Write the named field instead. */
    gGenericMenu.unk10.a[0] = 0;
#else
    *((u8*)&gMenu + 0x10) = 0; // TODO
#endif
    gUI.loadGfxOnRestore = TRUE;
    gMapBottom.bgSettings = NULL;
    gMapTop.bgSettings = NULL;
    gRoomControls.camera_target = NULL;
    gRoomControls.scroll_y = 0;
    gRoomControls.scroll_x = 0;
    ClearBgAnimations();
    DispReset(0);
    gScreen.lcd.displayControl = 0x2640;
    gScreen.controls.layerFXControl = 0x2244;
    gScreen.controls.alphaBlend = 0x1000;
    gScreen.controls.windowInsideControl = 0x1f;
    gScreen.controls.windowOutsideControl = 0x3f;
    gScreen.controls.window0HorizontalDimensions = 0xf0;
    gScreen.controls.window0VerticalDimensions = 0x60;
    gScreen.bg1.control = 0x1c4e;
    gScreen.bg2.control = 0x1dc1;
    SoundReq(BGM_STORY);
    ClearEventPriority();
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

void sub_08053800(void) {
    u32 index;
    const struct_080FCCB4* ptr;
    if (gFadeControl.active == 0) {
        index = gGenericMenu.unk10.a[0];
        ptr = &gUnk_080FCCB4[index];
        gGenericMenu.base.transitionTimer = ptr->transitionTimer;
        gGenericMenu.base.field_0xa = 0x1e;
        gGenericMenu.unk10.a[0]++;
        gGenericMenu.base.overlayType++;
        gGenericMenu.base.storyPanelIndex = 0;
        LoadPaletteGroup(index + 0x8a);
        LoadGfxGroup(index + 0x3a);
        MemClear(&gBG1Buffer, 0x800);
        ShowTextBox(TEXT_INDEX(TEXT_PICORI, 1) + index, ptr->font);
#ifdef PC_PORT
        /* Intro "legend of the Picori" prologue renders each story
         * page via ShowTextBox (not the MessageMain dialog pipeline),
         * so it never reached TTS. Speak each page as it appears —
         * sub_08053800 runs once per page with `index` advancing, so
         * this fires once per page, mirroring the visual. */
        Port_TTS_SpeakTextIndex(TEXT_INDEX(TEXT_PICORI, 1) + index);
#endif
        gScreen.bg1.updated = 1;
        gScreen.controls.alphaBlend = 0x10;
        gScreen.controls.window0HorizontalDimensions = ptr->width;
        gScreen.controls.window0VerticalDimensions = ptr->height;
        SetFade(FADE_INSTANT, ptr->fadeSpeed);
    }
}

void sub_08053894(void) {
    u32 tmp;
    if (gFadeControl.active == 0) {
        gMenu.transitionTimer--;
        if (gMenu.field_0xa != 0) {
            gMenu.field_0xa--;
        } else {
            if (((gRoomTransition.frameCount & 1) == 0) && (gMenu.storyPanelIndex < 0x10)) {
                tmp = ++gMenu.storyPanelIndex << 0x18;
                gScreen.controls.alphaBlend = (tmp >> 0x10) | (0x10 - ((tmp) >> 0x19));
            }
        }
        if (gMenu.transitionTimer == 0) {
            gMenu.overlayType++;
            SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
        }
    }
}

void sub_08053904(void) {
    u32 tmp;
    if (gFadeControl.active == 0) {
        gMenu.transitionTimer--;
        if (gMenu.field_0xa != 0) {
            gMenu.field_0xa--;
        } else {
            if (((gRoomTransition.frameCount & 1) == 0) && (gMenu.storyPanelIndex < 0x10)) {
                tmp = ++gMenu.storyPanelIndex << 0x18;
                gScreen.controls.alphaBlend = (tmp >> 0x10) | (0x10 - (tmp >> 0x19));
            }
        }
        if (gMenu.transitionTimer == 0) {
            gMenu.overlayType++;
            SetFade(FADE_IN_OUT | FADE_INSTANT, 1);
        }
    }
}

void sub_08053974(void) {
    if (gFadeControl.active == 0) {
        InitFade();
        DispReset(1);
        SetBGDefaults();
        sub_08051F78();
        LoadRoomEntityList(gUnk_080FCBC4);
        SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
        gMenu.overlayType++;
    }
}

void nullsub_482(void) {
}

void sub_080539BC(void) {
    SetBGDefaults();
    DeleteAllEntities();
    sub_08051F9C(0x22, 0x11, 0, 0);
    sub_0804B0B0(0x22, 0x11);
    LoadRoomEntityList(gUnk_080FCC14);
    gMenu.overlayType++;
}

void sub_080539F4(void) {
    if (gFadeControl.active == 0) {
        DispReset(1);
        gMenu.overlayType++;
        gMenu.transitionTimer = 60;
    }
}

void sub_08053A1C(void) {
    gMenu.transitionTimer--;
    if (gMenu.transitionTimer == 0) {
        gMenu.overlayType++;
        gMenu.transitionTimer = 8;
        MessageFromTarget(TEXT_INDEX(TEXT_PICORI, 0x07));
        gMessage.textWindowPosX = 1;
        gMessage.textWindowPosY = 8;
        SetFade(FADE_INSTANT, 8);
    }
}

void sub_08053A5C(void) {
    if (((gMessage.state & MESSAGE_ACTIVE) == 0) && --gMenu.transitionTimer == 0) {
        gMenu.overlayType++;
        SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
    }
}

void sub_08053A90(void) {
    if (gFadeControl.active == 0) {
        gUI.nextToLoad = 3; // Subtask_FadeOut
        SetBGDefaults();
    }
}

extern Script script_CutsceneOrchestratorMinishVaati;
extern Script script_MinishEzlo;
extern Script script_CutsceneMiscObjectMinishCap;
extern Script script_Vaati;

const EntityData gUnk_080FCD84[] = {
    { OBJECT, 79, CUTSCENE_ORCHESTRATOR, 0, 0, 0x0, 0x0, ENTITY_SCRIPT(script_CutsceneOrchestratorMinishVaati) },
    { NPC, 79, MINISH_EZLO, 0, 0, 0x78, 0xd8, ENTITY_SCRIPT(script_MinishEzlo) },
    { OBJECT, 79, CUTSCENE_MISC_OBJECT, 1, 0, 0x78, 0x58, ENTITY_SCRIPT(script_CutsceneMiscObjectMinishCap) },
    { NPC, 79, VAATI, 1, 0, 0x78, 0x68, ENTITY_SCRIPT(script_Vaati) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

void sub_08053ACC(void);
void sub_08053B00(void);
void sub_08053B10(void);
void (*const gUnk_080FCDD4[])(void) = {
    sub_08053ACC,
    sub_08053B00,
    sub_08053B10,
};

void sub_08053AB0(void) {
    gUnk_080FCDD4[gMenu.overlayType]();
}

void sub_08053ACC(void) {
    gMenu.overlayType++;
    gUpdateVisibleTiles = 1;
    sub_08051FF0();
    LoadRoomEntityList(gUnk_080FCD84);
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

void sub_08053B00(void) {
    gMenu.overlayType++;
}

void sub_08053B10(void) {
    if (CheckRoomFlag(1)) {
        gMenu.menuType++;
        DispReset(1);
        SetFade(FADE_INSTANT, 0x100);
    }
}

void sub_08053B3C(void) {
    sub_080A71C4(5, 4, FADE_IN_OUT | FADE_INSTANT, 0x100);
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

extern Script script_CutsceneOrchestratorTakeoverCutscene;
extern Script script_KingDaltusTakeover;
extern Script script_VaatiTakeover;
extern Script script_ZeldaStoneTakeover;
const EntityData gUnk_080FCDE0[] = {
    { OBJECT, 79, CUTSCENE_ORCHESTRATOR, 0, 0, 0x0, 0x0, ENTITY_SCRIPT(script_CutsceneOrchestratorTakeoverCutscene) },
    { NPC, 79, KING_DALTUS, 0, 0, 0x88, 0x58, ENTITY_SCRIPT(script_KingDaltusTakeover) },
    { NPC, 79, VAATI, 1, 0, 0x88, 0xe0, ENTITY_SCRIPT(script_VaatiTakeover) },
    { NPC, 79, ZELDA, 0, 0, 0xb8, 0x58, ENTITY_SCRIPT(script_ZeldaStoneTakeover) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

extern Script script_MinisterPothoTakeover;
extern Script script_GuardTakeover;
extern Script script_GuardTakeover;
const EntityData gUnk_080FCE30[] = {
    { NPC, 79, MINISTER_POTHO, 0, 0, 0x68, 0x58, ENTITY_SCRIPT(script_MinisterPothoTakeover) },
    { NPC, 79, GUARD_1, 0, 0, 0x78, 0xe8, ENTITY_SCRIPT(script_GuardTakeover) },
    { NPC, 79, GUARD_1, 1, 0, 0x78, 0x108, ENTITY_SCRIPT(script_GuardTakeover) },
    { NPC, 79, GUARD_1, 2, 0, 0x78, 0x128, ENTITY_SCRIPT(script_GuardTakeover) },
    { NPC, 79, GUARD_1, 3, 0, 0x98, 0xe8, ENTITY_SCRIPT(script_GuardTakeover) },
    { NPC, 79, GUARD_1, 4, 0, 0x98, 0x108, ENTITY_SCRIPT(script_GuardTakeover) },
    { NPC, 79, GUARD_1, 5, 0, 0x98, 0x128, ENTITY_SCRIPT(script_GuardTakeover) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

void sub_08053B74(void);
void sub_08053BAC(void);
void sub_08053BBC(void);
void (*const gUnk_080FCEB0[])(void) = {
    sub_08053B74,
    sub_08053BAC,
    sub_08053BBC,
};

void sub_08053B58(void) {
    gUnk_080FCEB0[gMenu.overlayType]();
}

void sub_08053B74(void) {
    gMenu.overlayType++;
    gUpdateVisibleTiles = 1;
    LoadRoomEntityList(gUnk_080FCDE0);
    ClearEventPriority();
    ResetEntityPriority();
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

void sub_08053BAC(void) {
    gMenu.overlayType++;
}

#ifdef PC_PORT
#include <stdio.h>
#include "port_debug_verbose.h" /* Port_DebugVerbose — gates the [wd] restore log */
#endif
void sub_08053BBC(void) {
#ifdef PC_PORT
    /* Issue #93 / #109 (Vaati castle-takeover).
     *
     * On GBA the inner orchestrator (kind=6 id=0x69, running
     * script_CutsceneOrchestratorTakeoverCutscene) drives the throne-room
     * choreography via SetSyncFlag/WaitForSyncFlagAndClear handshakes with
     * its King/Vaati/Zelda helpers. The port used to orphan it: when Vaati
     * apparated, CreateVaatiApparateManager's DeleteManager corrupted
     * gEntityLists[6] and dropped the orchestrator from the entity-update
     * walk, so it froze one command after its first SetSyncFlag and the
     * whole handshake deadlocked. That corruption is now fixed at the
     * source (see src/manager/vaatiAppearingManager.c), and the native
     * script runs the cutscene to completion on its own — GBA-accurate.
     *
     * The C state machine below that re-drove the handshake is therefore
     * only an opt-in fallback now (TMC_TAKEOVER_WD=1); by default it does
     * nothing. The CheckRoomFlag(0) completion block still runs
     * unconditionally: it re-arms player control + restarts the overworld
     * BGM for the direct-launch repro (sub_08053BE8), which skips the outer
     * orchestrator that would otherwise do so. */
    static int sStep = 0;
    static int sFrameInStep = 0;
    /* One-shot guard for the post-cutscene overworld-BGM restart (#109). */
    static int sBgmRestored = 0;
    /* The native orchestrator now drives the takeover correctly, so the C
     * watchdog is opt-in (set TMC_TAKEOVER_WD=1 to force it as a fallback);
     * by default the GBA-native script choreography runs unaided. */
    static int sWd = -1;
    if (sWd < 0)
        sWd = (getenv("TMC_TAKEOVER_WD") != NULL);
    /* Each entry: setFlag = "wake helper" bit to set; doneFlag = "helper done"
     * bit to wait for (mirroring the GBA child orchestrator's
     * SetSyncFlag/WaitForSyncFlagAndClear pairs). minFrames = floor we hold
     * before advancing even if doneFlag is observed (so helpers always have
     * time to *begin* their script after the flag bit goes up). maxFrames =
     * cap to prevent permanent hang if a helper never replies. */
    /* #109: full C-side state machine mirroring
     * script_CutsceneOrchestratorTakeoverCutscene. PC's asset extractor
     * outputs a truncated script blob that bails out after 3 commands
     * (BeginBlock, SetFadeTime, SetFade4), so none of the script's
     * camera/scroll/wake/bgm side-effects fire naturally. Each kPhase
     * entry encodes one logical "beat" of the cutscene:
     *   - camNpcId: NPC the camera should be following during this beat
     *     (the script positions an orchestrator entity at the desired
     *     spot then CameraTargetEntity's onto itself; we approximate by
     *     pointing camera_target at the relevant NPC, who's already there)
     *   - wakeFlag / ackFlag: SetSyncFlag / WaitForSyncFlagAndClear pair
     *     the script issues at this beat
     *   - minFrames / maxFrames: lower-bound wait + timeout
     *   - actions bitmask:
     *       0x01 PLAY_VAATI_BGM
     *       0x02 LOAD_SOLDIERS + STOP_BGM + PLAY_DIGGING_BGM
     *       0x04 STOP_BGM (final)
     *       0x08 SET_ROOM_FLAG (cutscene complete)
     *
     * Camera-target NPCs (kind=NPC, id=...):
     *   VAATI (0x27)         — at (0x88, 0xe0), bottom of throne room
     *   KING_DALTUS (0x24)   — at (0x88, 0x58), top (the throne)
     *   GUARD_1 (0x15)       — soldier-phase camera focus
     *   MINISTER_POTHO (0x25)— minister-phase
     */
    enum {
        ACT_NONE              = 0,
        ACT_PLAY_VAATI_BGM    = 1 << 0,
        ACT_SOLDIER_SCENE     = 1 << 1,
        ACT_STOP_BGM          = 1 << 2,
        ACT_END               = 1 << 3,
    };
    static const struct { u32 camNpcId; u32 wakeFlag; u32 ackFlag;
                          int minFrames; int maxFrames; u8 actions; } kPhase[] = {
        /* Phase 0: throne intro — King + Zelda visible, fade-in. */
        { KING_DALTUS,  0x000, 0x000, 60,  90,  ACT_PLAY_VAATI_BGM },
        /* Phase 1: pan to Vaati teleporting in. */
        { VAATI,        0x010, 0x020, 90,  300, ACT_NONE },
        /* Phase 2: pan back to King, King reacts. */
        { KING_DALTUS,  0x004, 0x008, 90,  300, ACT_NONE },
        /* Phase 3: pan to Vaati. */
        { VAATI,        0x010, 0x020, 90,  300, ACT_NONE },
        /* Phase 4 (Vaati 3 — third Vaati wake, camera stays on Vaati). */
        { VAATI,        0x010, 0x020, 60,  240, ACT_NONE },
        /* Phase 5: pan to King. */
        { KING_DALTUS,  0x004, 0x008, 90,  300, ACT_NONE },
        /* Phase 6: pan to Vaati (4th wake — the "I am taking over" beat). */
        { VAATI,        0x010, 0x020, 90,  300, ACT_NONE },
        /* Phase 7: extra Vaati hold before scene cut. */
        { VAATI,        0x010, 0x020, 30,  120, ACT_NONE },
        /* Phase 8: scene cut — load soldiers, swap BGM, fade to throne-
         * soldiers shot. Wake first Guard. */
        { GUARD_1,      0x040, 0x080, 90,  300, ACT_SOLDIER_SCENE },
        /* Phase 9: wake Minister. */
        { MINISTER_POTHO, 0x001, 0x002, 90, 300, ACT_NONE },
        /* Phase 10: King talks to Minister. */
        { KING_DALTUS,  0x004, 0x008, 90,  300, ACT_NONE },
        /* Phase 11: one-way penultimate signal. */
        { KING_DALTUS,  0x200, 0x000, 30,  60,  ACT_NONE },
        /* Phase 12: King's final line. */
        { KING_DALTUS,  0x004, 0x008, 90,  300, ACT_NONE },
        /* Phase 13: cutscene-over fade + room-flag-set + helper-broadcast. */
        { KING_DALTUS,  0x400, 0x000, 60,  120, ACT_STOP_BGM | ACT_END },
    };

    if (sWd && !CheckRoomFlag(0)) {
        /* Belt-and-suspenders: clear any stuck fade so the inner orch's
         * (would-be) SetFade5/SetFade4/WaitForFadeFinish path's leftover
         * state doesn't gate our beats. Fades themselves are FADE_INSTANT
         * in this scene, so clearing per-frame is a no-op for real
         * fade pacing. */
        gFadeControl.active = 0;
        int nSteps = (int)(sizeof(kPhase) / sizeof(kPhase[0]));
        sBgmRestored = 0; /* cutscene in progress — arm the post-exit BGM restart */
        if (sStep < nSteps) {
            u32 setFlag = kPhase[sStep].wakeFlag;
            u32 doneFlag = kPhase[sStep].ackFlag;
            u32 camNpcId = kPhase[sStep].camNpcId;
            if (setFlag) gActiveScriptInfo.syncFlags |= setFlag;
            sFrameInStep++;
            {
                static int sLogStep = -1;
                if (sLogStep != sStep) {
                    sLogStep = sStep;
                    fprintf(stderr, "[wd] phase=%d cam=0x%X wake=0x%X ack=0x%X act=0x%X\n",
                        sStep, camNpcId, setFlag, doneFlag, kPhase[sStep].actions);
                    /* Fire one-shot actions at the start of the phase. */
                    u8 actions = kPhase[sStep].actions;
                    if (actions & ACT_PLAY_VAATI_BGM) {
                        fprintf(stderr, "[wd] PlayBgm BGM_VAATI_THEME\n");
                        SoundReq(BGM_VAATI_THEME);
                    }
                    if (actions & ACT_SOLDIER_SCENE) {
                        extern const EntityData gUnk_080FCE30[];
                        fprintf(stderr, "[wd] spawn soldiers + BGM_DIGGING_CAVE\n");
                        LoadRoomEntityList(gUnk_080FCE30);
                        SoundReq(BGM_DIGGING_CAVE);
                    }
                    if (actions & ACT_STOP_BGM) {
                        fprintf(stderr, "[wd] StopBgm\n");
                        SoundReq(SONG_STOP_BGM);
                    }
                }
            }
            /* #109: let the native inner orchestrator own the opening pans.
             * On GBA the takeover script positions a CUTSCENE_ORCHESTRATOR object
             * and CameraTargetEntity's onto it for the throne intro + Vaati reveal.
             * Our watchdog was clobbering those first two beats to NPC anchors,
             * flattening the choreography. From phase 2 onward we still take over
             * the camera so the workaround can finish the cutscene after the native
             * orchestrator stalls. */
            if (camNpcId != 0xFFFFFFFFu) {
                Entity* target = FindEntityByID(NPC, camNpcId, 7);
                if (target == NULL) target = FindEntityByID(NPC, camNpcId, 5);
                if (sStep >= 2 && target != NULL && gRoomControls.camera_target != target) {
                    fprintf(stderr, "[wd] camera -> NPC id=0x%X (was %p) tgt=(%d,%d)\n",
                            camNpcId, (void*)gRoomControls.camera_target,
                            target->x.HALF.HI, target->y.HALF.HI);
                    gRoomControls.camera_target = target;
                }
            }
            int advance = 0;
            if (sFrameInStep >= kPhase[sStep].maxFrames) {
                advance = 1;  /* timeout — helper isn't responding */
            } else if (sFrameInStep >= kPhase[sStep].minFrames) {
                if (doneFlag == 0) {
                    advance = 1;  /* one-way step, advance after minFrames */
                } else if ((gActiveScriptInfo.syncFlags & doneFlag) == doneFlag) {
                    gActiveScriptInfo.syncFlags &= ~doneFlag;
                    advance = 1;
                }
            }
            if (advance) {
                if (kPhase[sStep].actions & ACT_END) {
                    fprintf(stderr, "[wd] sequence done, setting room flag\n");
                    SetRoomFlag(0);
                }
                sStep++;
                sFrameInStep = 0;
            }
        }
        return;
    }
    sStep = 0;
    sFrameInStep = 0;
#endif
    if (CheckRoomFlag(0)) {
        gActiveScriptInfo.syncFlags |= 0x400u;
        /* Use SetFade(FADE_INSTANT, 0x100) — the GBA original.
         *
         * Post-cutscene black-screen fix:
         *
         * The takeover sets up via sub_08053BE8 → sub_080A71C4(5, 2,
         * FADE_IN_OUT|FADE_INSTANT, 0x100), which writes
         * gUI.fadeType = 0x5 + gUI.fadeInTime = 0x100.
         *
         * After AuxCutscene_Exit's SetFadeInverted toggles type 4→5
         * and the fade drains to factor=0 (black), Subtask_FadeOut
         * runs and — because gUI.fadeType != 0xffff — does
         *   SetFade(gUI.fadeType=0x5, 0x100)
         * That sets type=5/progress=256/active=1 and the next FadeMain
         * snaps progress to 0 with FADE_IN_OUT applied → factor=0 →
         * palRam all-zero / BLACK. Subtask_Die fires, main game
         * resumes, but the fade pipeline is dormant (active=0) so
         * factors stay at zero forever and palRam stays black.
         *
         * Forcing gUI.fadeType = -1 here makes Subtask_FadeOut take
         * the else branch (SetFadeInverted), which toggles 5→4 and
         * drains to factor=1024 (full color), restoring visibility. */
        gMenu.menuType++;
        DispReset(1);
        SetFade(FADE_INSTANT, 0x100);
#ifdef PC_PORT
        gUI.fadeType = (u16)-1;
        /* The child orchestrator's later script commands don't run on PC
         * (it stops being iterated mid-cutscene; see commit 62e65e45). One
         * of those missing commands is the post-cutscene
         * ScriptCommand_EnablePlayerControl that brings player input back
         * online. Without it, gPlayerState.controlMode stays at
         * CONTROL_DISABLED (set by the takeover's
         * ScriptCommand_DisablePlayerControl at start), UpdatePlayerInput
         * keeps zeroing the keys, gPlayerState.direction stays at
         * DIR_NONE (0xFF), v13 in PlayerNormal stays 0, and Link is
         * softlocked at his post-cutscene position even though
         * ctl/action/area all read sane.
         *
         * Force the re-enable here. CONTROL_ENABLED is the post-takeover
         * state the script would have left us in.
         *
         * Set BOTH gPlayerState.controlMode AND gUI.controlMode —
         * Subtask_FadeOut runs AFTER us and does
         *   gPlayerState.controlMode = gUI.controlMode;
         * (restoring the pre-cutscene controlMode that Subtask_FadeIn
         * saved, which was CONTROL_DISABLED). Without updating
         * gUI.controlMode too, that restore wipes our fix and the
         * softlock returns. */
        gPlayerState.controlMode = CONTROL_ENABLED;
        gUI.controlMode = CONTROL_ENABLED;
        /* The outer takeover orchestrator's post-cutscene `PlayBGM` (script
         * line 32 of script_CutsceneOrchestratorTakeover, i.e.
         * SoundReq(gArea.bgm)) is one of the "later script commands [that]
         * don't run on PC" noted above. Without it the overworld stays
         * SILENT after the takeover: the inner cutscene's StopBgm zeroed
         * gSoundPlayingInfo.currentBgm, but gArea.bgm still equals
         * gArea.queued_bgm, so GameMain_ChangeRoom's
         * `bgm != queued_bgm` re-request never fires either. Replay the
         * script's PlayBGM once here, exactly like the control re-enable. */
        if (!sBgmRestored) {
            sBgmRestored = 1;
            SoundReq(gArea.bgm);
            if (Port_DebugVerbose)
                fprintf(stderr, "[wd] restore overworld BGM: SoundReq(gArea.bgm=0x%X)\n",
                        (unsigned)gArea.bgm);
        }
#endif
    }
}

void sub_08053BE8(void) {
    sub_080A71C4(5, 2, FADE_IN_OUT | FADE_INSTANT, 0x100);
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x100);
}

extern Script script_ZeldaStoneInDHC;
extern Script script_ZeldaStoneDHC;
const EntityData gUnk_080FCEBC[] = {
    { NPC, 79, ZELDA, 0, 0, 0x78, 0x68, ENTITY_SCRIPT(script_ZeldaStoneInDHC) },
    { NPC, 79, VAATI, 1, 0, 0x78, 0x98, ENTITY_SCRIPT(script_ZeldaStoneDHC) },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
void sub_08053C20(void);
void nullsub_483(void);
void sub_08053C60(void);
void (*const gUnk_080FCEEC[])(void) = {
    sub_08053C20,
    nullsub_483,
    sub_08053C60,
};

void sub_08053C04(void) {
    gUnk_080FCEEC[gMenu.overlayType]();
}

void sub_08053C20(void) {
    gMenu.overlayType = 1;
    gMenu.transitionTimer = 120; // Go to game over after 2 minutes.
    gUpdateVisibleTiles = 1;
    sub_08051FF0();
    LoadStaticBackground(4);
    LoadRoomEntityList(gUnk_080FCEBC);
    SetFade(FADE_INSTANT, 0x10);
    SoundReq(BGM_FIGHT_THEME2);
}

void nullsub_483(void) {
}

void sub_08053C60(void) {
    SetFade(FADE_IN_OUT | FADE_INSTANT, 2);
    SoundReq(SFX_SUMMON);
    SoundReq(SONG_STOP_BGM);
    SetTask(TASK_GAMEOVER);
}

void sub_08053C84(void) {
    gMenu.overlayType = 2;
}
void sub_08053CAC(void);
void sub_08053CAC(void);
void sub_08053E58(void);
void (*const gUnk_080FCEF8[])(void) = {
    sub_08053CAC,
    sub_08053CAC,
    sub_08053E58,
};

void CutsceneMain_Exit(void) {
    gUnk_080FCEF8[gMenu.field_0x3]();
}

const EntityData gUnk_080FCF24[];
const EntityData gUnk_080FCF44[];
const EntityData gUnk_080FCF64[];
const EntityData gUnk_080FCF84[];

const struct_080FCF04 gUnk_080FCF04[] = {
    { gUnk_080FCF24, AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WEST_STAIRS_2F, 0, 0 },
    { gUnk_080FCF44, AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_WEST_STAIRS_1F, 0, 0 },
    { gUnk_080FCF64, AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_EAST_STAIRS_2F, 0, 0 },
    { gUnk_080FCF84, AREA_FORTRESS_OF_WINDS, ROOM_FORTRESS_OF_WINDS_EAST_STAIRS_1F, 0, 0 },
};

const EntityData gUnk_080FCF24[] = {
    { OBJECT, 15, GROUND_ITEM, 83, 512, 0x88, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

const EntityData gUnk_080FCF44[] = {
    { OBJECT, 15, GROUND_ITEM, 83, 512, 0x68, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

const EntityData gUnk_080FCF64[] = {
    { OBJECT, 15, GROUND_ITEM, 83, 512, 0x88, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

const EntityData gUnk_080FCF84[] = {
    { OBJECT, 15, GROUND_ITEM, 83, 512, 0x68, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

void sub_08053CC8(void);
void sub_08053D34(void);
void sub_08053D90(void);
void sub_08053DB4(void);
void sub_08053E30(void);
void (*const gUnk_080FCFA4[])(void) = {
    sub_08053CC8, sub_08053D34, sub_08053D90, sub_08053DB4, sub_08053E30,
};

void sub_08053CAC(void) {
    gUnk_080FCFA4[gMenu.overlayType]();
}

void sub_08053CC8(void) {
    const struct_080FCF04* ptr;
    ptr = gUnk_080FCF04 + gMenu.field_0x3 * 2;
    gMenu.field_0xc = (void*)&ptr[1];
    LoadRoomEntityList(ptr->entityData);
    LoadRoomEntityList(GetRoomProperty(ptr->area, ptr->room, 1));
    LoadRoomEntityList(GetRoomProperty(ptr->area, ptr->room, 2));
    gMenu.overlayType = 1;
    gScreen.lcd.displayControl &= 0xfeff;
    gUpdateVisibleTiles = 1;
    SetFade(FADE_INSTANT, 0x10);
    if (ptr->room == 0x1d) { // TODO what room is this?
        gMenu.field_0xa = 1;
    }
}

void sub_08053D34(void) {
    if (gMenu.field_0xa != 0) {
        gMenu.field_0xa = 0;
        if (CheckLocalFlagByBank(FLAG_BANK_7, 0x3d)) {
            SetTileType(TILE_TYPE_116, TILE_POS(4, 3), LAYER_BOTTOM);
        }
        if (CheckLocalFlagByBank(FLAG_BANK_7, 0x3e)) {
            SetTileType(TILE_TYPE_116, TILE_POS(12, 3), LAYER_BOTTOM);
        }
    }
    if (gFadeControl.active == 0) {
        gMenu.transitionTimer = 120;
        gMenu.overlayType++;
    }
}

void sub_08053D90(void) {
    gMenu.transitionTimer--;
    if (gMenu.transitionTimer == 0) {
        gMenu.overlayType++;
        SetFadeInverted(0x10);
    }
}

void sub_08053DB4(void) {
    if (gFadeControl.active == 0) {
        struct_080FCF04* ptr = (struct_080FCF04*)gMenu.field_0xc;
        sub_08052FF4(ptr->area, ptr->room);
        InitializeCamera();
        gUpdateVisibleTiles = 1;
        gRoomControls.scroll_x = (s8)ptr->scrollX + gRoomControls.scroll_x;
        gRoomControls.scroll_y = (s8)ptr->scrollY + gRoomControls.scroll_y;
        LoadRoomEntityList(ptr->entityData);
        LoadRoomEntityList((EntityData*)GetRoomProperty(ptr->area, ptr->room, 1));
        LoadRoomEntityList((EntityData*)GetRoomProperty(ptr->area, ptr->room, 2));
        gMenu.transitionTimer = 120;
        gMenu.overlayType++;
        SetFadeInverted(0x10);
    }
}

void sub_08053E30(void) {
    if ((gFadeControl.active == 0) && --gMenu.transitionTimer == 0) {
        gMenu.menuType++;
    }
}

const EntityData gUnk_080FCFE8[];
const EntityData gUnk_080FD008[];
const EntityData gUnk_080FD028[];
const EntityData gUnk_080FD048[];
const EntityData gUnk_080FD078[];
const EntityData gUnk_080FD098[];
const EntityData gUnk_080FD0C8[];
const EntityData gUnk_080FD0E8[];
const struct_080FCFB8 gUnk_080FCFB8[] = {
    { gUnk_080FCFE8, gUnk_080FD008, AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_LEFT_DARKNUT, 16, 16 },
    { gUnk_080FD028, gUnk_080FD048, AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_TOP_RIGHT_DARKNUTS, 16, 16 },
    { gUnk_080FD078, gUnk_080FD098, AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_LEFT_DARKNUTS, 16, 16 },
    { gUnk_080FD0C8, gUnk_080FD0E8, AREA_DARK_HYRULE_CASTLE, ROOM_DARK_HYRULE_CASTLE_2F_BOTTOM_RIGHT_DARKNUT, 16, 16 },
};

const EntityData gUnk_080FCFE8[] = {
    { OBJECT, 15, BOSS_DOOR, 8, 0, 0x88, 0x28, 65535 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD008[] = {
    { ENEMY, 47, DARK_NUT, 2, 0, 0x88, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD028[] = {
    { OBJECT, 15, BOSS_DOOR, 8, 0, 0x88, 0x28, 65535 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD048[] = {
    { ENEMY, 47, DARK_NUT, 1, 0, 0x70, 0x68, 0 },
    { ENEMY, 47, DARK_NUT, 0, 0, 0xa0, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD078[] = {
    { OBJECT, 15, BOSS_DOOR, 10, 0, 0x88, 0xa8, 65535 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD098[] = {
    { ENEMY, 47, DARK_NUT, 0, 0, 0x70, 0x68, 0 },
    { ENEMY, 47, DARK_NUT, 1, 0, 0xa0, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD0C8[] = {
    { OBJECT, 15, BOSS_DOOR, 10, 0, 0x88, 0xa8, 65535 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};
const EntityData gUnk_080FD0E8[] = {
    { ENEMY, 47, DARK_NUT, 2, 0, 0x88, 0x68, 0 },
    { 0xff, 0, 0, 0, 0, 0x0, 0x0, 0 },
};

void sub_08053E74(void);
void sub_08053EC4(void);
void sub_08053EFC(void);
void sub_08053F20(void);
void sub_08053EC4(void);
void sub_08053EFC(void);
void sub_08053F20(void);
void sub_08053EC4(void);
void sub_08053EFC(void);
void sub_08053F20(void);
void sub_08053EC4(void);
void sub_08053F88(void);
void (*const gUnk_080FD108[])(void) = {
    sub_08053E74, sub_08053EC4, sub_08053EFC, sub_08053F20, sub_08053EC4, sub_08053EFC,
    sub_08053F20, sub_08053EC4, sub_08053EFC, sub_08053F20, sub_08053EC4, sub_08053F88,
};

void sub_08053E58(void) {
    gUnk_080FD108[gMenu.overlayType]();
}

void sub_08053E74(void) {
    const struct_080FCFB8* ptr = gUnk_080FCFB8;
    gMenu.field_0xc = (u8*)ptr;
    LoadRoomEntityList(ptr->entityData1);
    gMenu.transitionTimer = 120;
    gMenu.field_0xa = 0x3c;
    gMenu.overlayType++;
    gScreen.lcd.displayControl &= 0xfeff;
    gUpdateVisibleTiles = 1;
    SetMinPriority(1);
    SetFade(FADE_INSTANT, 8);
}

void sub_08053EC4(void) {
    if ((gFadeControl.active == 0) && (--gMenu.field_0xa == 0)) {
        struct_080FCFB8* ptr = (struct_080FCFB8*)gMenu.field_0xc;
        gMenu.field_0xc += sizeof(struct_080FCFB8);
        LoadRoomEntityList(ptr->entityData2);
        gMenu.overlayType++;
    }
}

void sub_08053EFC(void) {
    if (--gMenu.transitionTimer == 0) {
        gMenu.overlayType++;
        SetFadeInverted(8);
    }
}

void sub_08053F20(void) {
    struct_080FCFB8* ptr;
    if (gFadeControl.active == 0) {
        DeleteAllEntities();
        ptr = (struct_080FCFB8*)gMenu.field_0xc;
        sub_08052FF4(ptr->area, ptr->room);
        InitializeCamera();
        gUpdateVisibleTiles = 1;
        gRoomControls.scroll_x = (s8)ptr->scrollX + gRoomControls.scroll_x;
        gRoomControls.scroll_y = (s8)ptr->scrollY + gRoomControls.scroll_y;
        LoadRoomEntityList((ptr)->entityData1);
        gMenu.transitionTimer = 120;
        gMenu.field_0xa = 0x3c;
        gMenu.overlayType++;
        SetFadeInverted(8);
    }
}

void sub_08053F88(void) {
    if ((gFadeControl.active == 0) && --gMenu.transitionTimer == 0) {
        gMenu.menuType++;
        ResetEntityPriority();
    }
}

void sub_080536D4(void);
void sub_08053B58(void);
void sub_0805373C(void);
void sub_08053AB0(void);
void sub_080536F0(void);
void sub_08053C04(void);
void (*const gUnk_080FD138[])(void) = {
    sub_080536D4, sub_08053B58, sub_0805373C, sub_08053AB0, sub_080536F0, sub_08053C04,
};

void CutsceneMain_Update(void) {
    gUnk_080FD138[gMenu.field_0x3]();
}
