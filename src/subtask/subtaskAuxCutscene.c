/**
 * @file subtaskAuxCutscene.c
 *
 * @brief Aux Cutscene Subtask
 */
#include "area.h"
#include "asm.h"
#include "common.h"
#include "game.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "room.h"
#include "screen.h"
#include "sound.h"
#include "subtask.h"
#include "affine.h"
#include "fade.h"
#ifdef PC_PORT
#include <stdio.h>
#endif

typedef void(AuxCutsceneState)(void);
typedef void(CutsceneMainState)(void);

static AuxCutsceneState AuxCutscene_Init;
static AuxCutsceneState AuxCutscene_Main;
static AuxCutsceneState AuxCutscene_Exit;

CutsceneMainState CutsceneMain_Init;
CutsceneMainState CutsceneMain_Update;
CutsceneMainState CutsceneMain_Exit;

typedef struct {
    u8 area;
    RoomID room : 8;
    u8 _2;
    u8 _3;
    u16 x;
    u16 y;
} CutsceneData;
static const CutsceneData sCutsceneData[];

void Subtask_AuxCutscene(void) {
#ifdef PC_PORT
    /* #93 chase: log AuxCutscene state transitions. Subtask_AuxCutscene
     * fires every frame; dedup on (menuType, field_0x0) so we only see
     * the actual phase changes — Init → Main(CutsceneMain_Init) →
     * Main(CutsceneMain_Update) → Main(CutsceneMain_Exit) → Exit.
     * The post-takeover black-screen happens because something in this
     * chain doesn't fire or fires with wrong fade state. */
    static u8 sLastMenuType = 0xFF;
    static u8 sLastInner = 0xFF;
    if (sLastMenuType != gMenu.menuType || sLastInner != gMenu.field_0x0) {
        fprintf(stderr,
                "[aux-cs] menuType=%u inner=%u overlayType=%u xferTimer=%u uiField3=%u\n",
                gMenu.menuType, gMenu.field_0x0, gMenu.overlayType,
                (unsigned)gMenu.transitionTimer, gUI.field_0x3);
        sLastMenuType = gMenu.menuType;
        sLastInner = gMenu.field_0x0;
    }
#endif
    static AuxCutsceneState* const sStates[] = {
        AuxCutscene_Init,
        AuxCutscene_Main,
        AuxCutscene_Exit,
    };

    sStates[gMenu.menuType]();
}

static void AuxCutscene_Init(void) {
    const CutsceneData* p = &sCutsceneData[gUI.field_0x3];
    gRoomControls.area = p->area;
    gRoomControls.room = p->room;
    LoadGfxGroups();
    gArea.localFlagOffset = GetFlagBankOffset(gRoomControls.area);
    SetCurrentRoomPropertyList(p->area, p->room);
    LoadAuxiliaryRoom(p->area, p->room);
    gRoomControls.scroll_x = gRoomControls.origin_x + p->x;
    gRoomControls.scroll_y = gRoomControls.origin_y + p->y;
    gMenu.field_0x0 = p->_2;
    gMenu.field_0x3 = p->_3 & 0xf;
    gMenu.field_0xc = (void*)p;
    gMenu.menuType++;
    gMenu.overlayType = 0;
    gMenu.transitionTimer = 300;
    AuxCutscene_Main(); // init
}

static void AuxCutscene_Main(void) {
    static CutsceneMainState* const sStates[] = {
        CutsceneMain_Init,
        CutsceneMain_Update,
        CutsceneMain_Exit,
    };

    sStates[gMenu.field_0x0]();
    FlushSprites();
    UpdateEntities();
    DrawEntities();
    CopyOAM();
    UpdateScroll();
    UpdateBgAnimations();
    UpdateManagers();
    UpdateScrollVram();
}

static const CutsceneData sCutsceneData[] = {
    { AREA_MINISH_WOODS, 0, 0, 0, 336, 528 },
    { AREA_HYRULE_FIELD, 1, 1, 0, 472, 312 },
    { AREA_HYRULE_CASTLE, 2, 1, 1, 16, 16 },
    { AREA_SANCTUARY, 2, 1, 2, 0, 0 },
    { AREA_EZLO_CUTSCENE, 0, 1, 3, 0, 0 },
    { AREA_HOUSE_INTERIORS_2, 16, 1, 4, 0, 0 },
    { AREA_DARK_HYRULE_CASTLE_OUTSIDE, 0, 1, 5, 0, 40 },
    { AREA_FORTRESS_OF_WINDS, 28, 2, 0, 0, 0 },
    { AREA_FORTRESS_OF_WINDS, 29, 2, 1, 0, 0 },
    { AREA_DARK_HYRULE_CASTLE, 14, 2, 2, 16, 16 },
};

static void AuxCutscene_Exit(void) {
    u32 flag = sCutsceneData[gUI.field_0x3]._3;
#ifdef PC_PORT
    /* #93 chase: this is where the post-takeover fade-IN should be
     * staged. Log which branch fires (MenuFadeIn vs SetFadeInverted
     * path) and the active gFadeControl values just before the call,
     * so we can see whether the fade-in actually flips active=1 with
     * type cleared of FADE_IN_OUT (i.e. fade-in direction). */
    fprintf(stderr,
            "[aux-cs-exit] flag=0x%X uiField3=%u branch=%s "
            "fade.before active=%u type=0x%X progress=%u sustain=%u\n",
            flag, gUI.field_0x3,
            (flag & 0xF0) ? "MenuFadeIn" : "SetFadeInverted",
            (unsigned)gFadeControl.active, gFadeControl.type,
            gFadeControl.progress, gFadeControl.sustain);
#endif
    if (flag & 0xF0) {
        MenuFadeIn(2, flag >> 4);
    } else {
        gUI.nextToLoad = 3; // Subtask_FadeOut
        SetFadeInverted(0x10);
        MessageInitialize();
    }
#ifdef PC_PORT
    fprintf(stderr,
            "[aux-cs-exit] fade.after  active=%u type=0x%X progress=%u sustain=%u "
            "nextToLoad=%u\n",
            (unsigned)gFadeControl.active, gFadeControl.type,
            gFadeControl.progress, gFadeControl.sustain,
            (unsigned)gUI.nextToLoad);
#endif
}

// end of auxCutscene?

void sub_08051F78(void) {
    u32 idx = gUI.field_0x3;
    const CutsceneData* p = &sCutsceneData[idx];
    sub_08051F9C(p->area, p->room, p->x, p->y);
}

void sub_08051F9C(u32 a1, u32 a2, u32 a3, u32 a4) {
    u32 tmp = gScreen.lcd.displayControl & (DISPCNT_WIN0_ON | DISPCNT_WIN1_ON);
    sub_08052FF4(a1, a2);
    gRoomControls.scroll_x = gRoomControls.origin_x + a3;
    gRoomControls.scroll_y = gRoomControls.origin_y + a4;
    InitializeCamera();
    gUpdateVisibleTiles = 1;
    gUsedPalettes = 0;
    gScreen.lcd.displayControl |= tmp;
}

void sub_08051FF0(void) {
    sub_0804B0B0(gMenu.field_0xc[0], gMenu.field_0xc[1]);
}

void sub_08052004(void) {
    gMenu.menuType = 2;
}

void sub_08052010(void) {
    InitSoundPlayingInfo();
    MessageInitialize();
    DispReset(1);
    MemClear(gBG1Buffer, sizeof(gBG1Buffer));
    MemClear(gBG2Buffer, sizeof(gBG2Buffer));
    sub_080A4D34();
    LoadPaletteGroup(0xA);
    SetColor(0, 0);
    LoadGfxGroup(4);
    MemClear((void*)0x06000000, 0x20);
    MemClear(&gGenericMenu, sizeof(gGenericMenu));
    gScreen.lcd.displayControl |= DISPCNT_OBJ_ON;
    gScreen.bg1.control = BGCNT_PRIORITY(1) | BGCNT_SCREENBASE(28) | BGCNT_CHARBASE(0);
    gScreen.bg2.control = BGCNT_PRIORITY(1) | BGCNT_SCREENBASE(29) | BGCNT_CHARBASE(1);
    gScreen.bg1.updated = 1;
    gScreen.bg2.updated = 1;
}
