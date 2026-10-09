/*
 * tloz-tmc-ua: Ukrainian title screen (included at the end of src/title.c).
 *
 * The Ukrainian ROM redraws the title: a localized "ЛЕГЕНДА ПРО ЗЕЛЬДУ" logo
 * and the "Диво-Ковпак" subtitle rise from the bottom of the screen, the sword
 * background is not shown, and "PRESS START" sits higher. That layout lives in
 * code, so it is reproduced here, mirroring tloz-tmc-ua/tmc/src/title.c
 * (HandleTitlescreen, HandleCustomAnimationIntro, UpdatePressStartIcon).
 *
 * Port_UA_HandleTitlescreen() replaces HandleTitlescreen() only when the TMC-UA
 * ROM marker is present (see port/port_ua.h); other ROMs never reach this code.
 * This file is a textual include so it can use title.c's static helpers
 * (GetAdvanceState, AdvanceIntroSequence, UpdateLightRays, gIntroState).
 */
#ifdef PC_PORT
#include "port_ua.h"

#define PORT_UA_PRESS_START_X 120
#define PORT_UA_PRESS_START_Y 126

static void Port_UA_UpdatePressStartIcon(void) {
    /* copyright line, as in the USA title */
    gOamCmd._4 = 0;
    gOamCmd._6 = 0;
    gOamCmd._8 = 0xE020;
    gOamCmd.x = 120;
    gOamCmd.y = 152;
    DrawDirect(511, 1);

    /* blinking PRESS START, moved up under the new logo */
    if ((gIntroState.timer & 0x20) == 0) {
        gOamCmd._8 = 0xe000;
        gOamCmd.x = PORT_UA_PRESS_START_X;
        gOamCmd.y = PORT_UA_PRESS_START_Y;
        DrawDirect(0x1ff, 0);
    }
}

/* The title screen became interactive: same screen-reader line as upstream
 * fires when its state passes 2, once per boot. */
static void Port_UA_TitleInteractive(void) {
    PortTtsOptions opts = { 0 };
    opts.priority = PORT_TTS_PRIO_URGENT;
    opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
    opts.dedupe = false;
    Port_TTS_Speak("Title screen. The Legend of Zelda, The Minish Cap. Press Start.", &opts);
}

static void Port_UA_HandleCustomAnimationIntro(void) {
    /* A or START skips the animation: make sure every piece exists, then jump
     * to the interactive state. */
    if (gInput.newKeys & (A_BUTTON | START_BUTTON)) {
        if (gIntroState.subState < 2) {
            CreateObject(TITLE_SCREEN_OBJECT, 0, 0); /* logo */
            CreateObject(TITLE_SCREEN_OBJECT, 1, 0); /* subtitle */
        }
        if (gIntroState.subState < 3) {
            CreateObject(JAPANESE_SUBTITLE, 0, 0); /* cap + flying letters */
        }
        gIntroState.state = 2;
        gIntroState.timer = 3600;
        Port_UA_TitleInteractive();
        return;
    }

    switch (gIntroState.subState) {
        case 0:
            if (!gFadeControl.active) {
                gIntroState.subState = 1;
            }
            break;
        case 1:
            CreateObject(TITLE_SCREEN_OBJECT, 0, 0);
            CreateObject(TITLE_SCREEN_OBJECT, 1, 0);
            gIntroState.timer = 70;
            gIntroState.subState = 2;
            break;
        case 2:
            if (--gIntroState.timer == 0) {
                CreateObject(JAPANESE_SUBTITLE, 0, 0);
                gIntroState.timer = 60;
                gIntroState.subState = 3;
            }
            break;
        case 3:
            if (--gIntroState.timer == 0) {
                gIntroState.state++;
                gIntroState.timer = 3600;
                gIntroState.subState = 4;
                Port_UA_TitleInteractive();
            }
            break;
        case 4:
            break;
    }
}

static bool32 Port_UA_HandleTitlescreen(void) {
    int advance;

    if (!Port_IsUkrainianRom()) {
        return FALSE;
    }

    gIntroState.counter++;
    switch (gIntroState.state) {
        case 0:
            gIntroState.state = 1;
            gIntroState.subState = 0;
            gIntroState.timer = 30;
            gIntroState.language = 7;
            EraseAllEntities();
            ResetPaletteTable(0);
            ResetPalettes();
            gGFXSlots.unk0 = 1;

            LoadGfxGroup(2);
            LoadGfxGroup(3);
            LoadPaletteGroup(3);
            LoadPaletteGroup(4);

            gScreen.controls.layerFXControl = BLDCNT_TGT1_BG0 | BLDCNT_TGT2_BG1 | BLDCNT_EFFECT_BLEND;
            gScreen.controls.alphaBlend = BLDALPHA_BLEND(9, 9);
            gScreen.bg0.control = BGCNT_SCREENBASE(29) | BGCNT_PRIORITY(2) | BGCNT_CHARBASE(0);
            gScreen.bg1.control = BGCNT_SCREENBASE(30) | BGCNT_PRIORITY(3) | BGCNT_CHARBASE(0);
            gScreen.bg2.control = BGCNT_SCREENBASE(28) | BGCNT_PRIORITY(1) | BGCNT_CHARBASE(2) | BGCNT_256COLOR |
                                  BGCNT_WRAP | BGCNT_TXT512x256;
            /* BG2 (the sword backdrop) stays off. */
            gScreen.lcd.displayControl =
                DISPCNT_MODE_1 | DISPCNT_BG0_ON | DISPCNT_BG1_ON | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP;
            gIntroState.swordBgScaleRatio = 400;

            InitSoundPlayingInfo();
            SoundReq(BGM_TITLE_SCREEN);
            SetFade(FADE_BLACK_WHITE | FADE_INSTANT, 8);
            break;
        case 1:
            if (gFadeControl.active) {
                return TRUE;
            }
            Port_UA_HandleCustomAnimationIntro();
            break;
        case 2:
            /* animation done: fall through to the interactive handling */
        default:
            advance = GetAdvanceState();
            if (advance != ADVANCE_NONE) {
                if (advance == ADVANCE_KEY_PRESSED) {
                    SoundReq(SFX_TEXTBOX_SELECT);
                }
                /* As in tloz-tmc-ua: an expired timer replays the title
                 * (handler 1) instead of returning to the company logos. */
                AdvanceIntroSequence(advance);
                SoundReq(SONG_VOL_FADE_OUT);
            }
            Port_UA_UpdatePressStartIcon();
            break;
    }
    if (gIntroState.language != gSaveHeader->language) {
        gIntroState.language = gSaveHeader->language;
        LoadGfxGroup(3);
    }
    UpdateLightRays();
    UpdateEntities();
    DrawEntities();
    return TRUE;
}
#endif /* PC_PORT */
