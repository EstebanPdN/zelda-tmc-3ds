#include "subtask.h"
#include "common.h"
#include "fileselect.h"
#include "game.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "save.h"
#include "screen.h"
#include "ui.h"
#ifdef PC_PORT
#include "manager.h"
#include "../port/port_tts.h"
#endif
#include "windcrest.h"
#include "affine.h"
#include "beanstalkSubtask.h"
#include "fade.h"

extern Screen gUnk_03001020;
extern u8 gPaletteBufferBackup[];
extern u8 gUnk_03000420[];

void sub_080A70AC(const KeyButtonLayout* layout);
void sub_080A5F48(u32, u32);

extern void sub_0805E974(void);
extern bool32 sub_080A51F4(void);
extern u32 sub_0807CB24(u32, u32);

extern void (*const gUnk_0812901C[])(void);

void sub_080A74F4(void);

extern void UpdateCarriedObject(void);

extern void (*const gSubtasks[])(void);

// subtaskUtils? menuUtils?

u32 sub_080A6F40(void) {
    extern u8 gUnk_08128F38[];
    int iVar1;
    u8* pcVar2;
    u32 uVar3;
    u32 uVar4;

    for (uVar3 = 0, pcVar2 = gUnk_08128F38, uVar4 = 0; *pcVar2 != 0; uVar3++, pcVar2 += 2) {
        uVar4 |= sub_0807CB24(*pcVar2, pcVar2[1]) << uVar3;
    }
    return ~uVar4;
}

void sub_080A6F6C(u32 textIndexOrPtr) {
    extern Font gUnk_08128FA8;
    if (gGenericMenu.unk2e.HWORD != textIndexOrPtr) {
        gGenericMenu.unk2e.HWORD = textIndexOrPtr;
        MemClear(&gBG0Buffer[0x220], 0x100);
        if ((textIndexOrPtr & 0xff) != 0) {
            ShowTextBox(textIndexOrPtr, &gUnk_08128FA8);
#ifdef PC_PORT
            /* Pause-menu / quest-menu focus text changed — announce
             * the new item label to TTS. The visual ShowTextBox is
             * already gated on textIndex change, so this fires once
             * per cursor move (not every frame). */
            Port_TTS_SpeakTextIndex(textIndexOrPtr);
#endif
        }
        gScreen.bg0.updated = 1;
    }
    gScreen.bg0.yOffset = 2;
}

/*
Show area name in a textbox.
param_2:    0: visited overworld area name
            2: overworld area name
            1: dungeon name
*/
void ShowAreaName(u32 windcrest, u32 type) {
    extern u16 gDungeonNames[];
    extern Font gUnk_08128FD8;
    extern Font gUnk_08128FC0;
    u32 textIndexOrPtr;
    Font* font;

    textIndexOrPtr = 0;
    switch (type) {
        case 0:
            if (!IS_BIT_SET(gSave.windcrests, windcrest))
                break;
        case 2:
            textIndexOrPtr = gOverworldLocations[windcrest].textIndex;
            font = &gUnk_08128FC0;
            break;
        case 1:
            textIndexOrPtr = gDungeonNames[windcrest];
            font = &gUnk_08128FD8;
            break;
        default:
            break;
    }

    if (gGenericMenu.unk2e.HWORD != textIndexOrPtr) {
        gGenericMenu.unk2e.HWORD = textIndexOrPtr;
        MemClear(&gBG0Buffer[0x220], 0x100);
        if ((textIndexOrPtr & 0xff) != 0) {
            ShowTextBox(textIndexOrPtr, font);
#ifdef PC_PORT
            Port_TTS_SpeakTextIndex(textIndexOrPtr);
#endif
        }
        gScreen.bg0.updated = 1;
    }
    gScreen.bg0.yOffset = 2;
}

void sub_080A7040(u32 param_1) {
    extern Font gUnk_08129004;
    if (gGenericMenu.unk2e.HWORD != param_1) {
        gGenericMenu.unk2e.HWORD = param_1;
        MemClear(&gBG1Buffer[0x100], 0x300);
        MemCopy(gBG1Buffer, (void*)0x600e000, 0x800);
        if (GetInventoryValue(gUnk_08128D70[param_1].item) != 0) {
            ShowTextBox(gUnk_08128D70[param_1].textIndex, &gUnk_08129004);
#ifdef PC_PORT
            Port_TTS_SpeakTextIndex(gUnk_08128D70[param_1].textIndex);
#endif
        }
        gScreen.bg1.yOffset = 3;
        gScreen.bg1.updated = 1;
    }
}

void sub_080A70AC(const KeyButtonLayout* layout) {
    MemClear(&gHUD.elements, 0x300);
    gHUD.unk_13 = 0x7f;
    gHUD.unk_14 = 0x7f;
    gHUD.buttonX[0] = layout->aButtonX;
    gHUD.buttonY[0] = (s8)layout->aButtonY;
    gHUD.buttonText[0] = layout->aButtonText;
    gHUD.buttonX[1] = layout->bButtonX;
    gHUD.buttonY[1] = (s8)layout->bButtonY;
    gHUD.buttonText[1] = layout->bButtonText;
    gHUD.buttonX[2] = layout->rButtonX;
    gHUD.buttonY[2] = (s8)layout->rButtonY;
    gHUD.buttonText[2] = layout->rButtonText;
    layout++;
    do {
        CreateUIElement(layout->aButtonX, layout->aButtonY);
        layout = (KeyButtonLayout*)&layout->aButtonText;
    } while ((s8)layout->aButtonX != -1);
}

void SetMenuType(u32 menuType) {
    gMenu.menuType = menuType;
    gMenu.overlayType = 0;
    gMenu.storyPanelIndex = 0;
}

void ResetUI(void) {
    MemClear(&gUI, sizeof(gUI));
}

void MenuFadeIn(u32 param_1, u32 param_2) {
    if (gUI.nextToLoad == 0) {
        MemClear(&gUI, sizeof(gUI));
        gUI.lastState = param_1;
        gUI.field_0x3 = param_2;
        gUI.pauseFadeIn = gMain.substate;
    } else {
        gUI.state = param_1;
        gUI.field_0x5 = param_2;
        gUI.nextToLoad = 1; // Subtask_Init
    }
    gUI.loadGfxOnRestore = FALSE;
    gUI.fadeType = -1;
    gUI.fadeInTime = 0x20;
    gMain.substate = GAMEMAIN_SUBTASK;
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x20);
    gUnk_02018EB0.unk_0 = 0;
    gUnk_02018EB0.unk_1 = 0;
}

void sub_080A71A4(u32 param_1, u32 param_2, u32 param_3) {
    if (gUI.nextToLoad == 0) {
        gUI.unk_e = param_3;
    } else {
        gUI.unk_f = param_3;
    }
    MenuFadeIn(param_1, param_2);
}

void sub_080A71C4(u32 param_1, u32 param_2, u32 fadeType, u32 param_4) {
    MenuFadeIn(param_1, param_2);
    gUI.fadeType = fadeType;
    gUI.fadeInTime = param_4;
}

void Subtask_Exit(void) {
    gUI.nextToLoad = 3; // Subtask_FadeOut
    SetFade(FADE_IN_OUT | FADE_INSTANT, 0x20);
}

// Used by Subtask_FastTravel and EnterPortalSubtask?
void sub_080A71F4(const Transition* exitTransition) {
    if (exitTransition != NULL) {
        DoExitTransition(exitTransition);
    }
    DeleteAllEntities();
    sub_0805E974();
    DeleteAllEntities();
    gMain.state = GAMETASK_INIT;
    gMain.substate = GAMEMAIN_INITROOM;
}

void GameMain_Subtask(void) {
    if (gUI.nextToLoad < 3) {
        if (gUI.state != 0) {
            gUI.field_0x3 = gUI.field_0x5;
            gUI.lastState = gUI.state;
            gUI.state = 0;
        }
    }
    gUnk_0812901C[gUI.nextToLoad]();
}

void Subtask_FadeIn(void) {
#ifdef PC_PORT
    {
        extern void Port_LogSubtaskEntry(const char* name, unsigned active, unsigned nextToLoad);
        Port_LogSubtaskEntry("FadeIn", gFadeControl.active, gUI.nextToLoad);
    }
#endif
    if (!gFadeControl.active) {
        MemCopy(&gScreen, &gUnk_03001020, sizeof(Screen));
        MemCopy(gPaletteBuffer, gPaletteBufferBackup, 0x400);
        MemCopy(&gGFXSlots, &gUI.gfxSlotList, sizeof(GfxSlotList));
        MemCopy(gPaletteList, gUI.palettes, sizeof(gUI.palettes));
        MemCopy(&gRoomControls, &gUI.roomControls, sizeof(RoomControls));
        MemCopy(gUnk_03000420, gUI.unk_2a8, sizeof(gUI.unk_2a8));
        MemCopy(&gActiveScriptInfo, &gUI.activeScriptInfo, sizeof(ActiveScriptInfo));
        sub_0805E958();
        gUI.unk_d = gRoomTransition.field2f;
        gUI.controlMode = gPlayerState.controlMode;
        gUI.currentRoomProperties = gCurrentRoomProperties;
        gUI.mapBottomBgSettings = gMapBottom.bgSettings;
        gUI.mapTopBgSettings = gMapTop.bgSettings;
        gUI.nextToLoad = 1; // Subtask_Init
    }
}

void Subtask_Init(void) {
#ifdef PC_PORT
    {
        extern void Port_LogSubtaskEntry(const char* name, unsigned active, unsigned nextToLoad);
        Port_LogSubtaskEntry("Init", gFadeControl.active, gUI.nextToLoad);
    }
#endif
    if (gFadeControl.active == 0) {
        DeleteAllEntities();
        MemClear(&gMenu, sizeof(FigurineMenu));
        MemClear(&gRoomControls, sizeof(gRoomControls));
        MemClear(gOAMControls.unk, 0x100);
        MemClear(&gActiveScriptInfo, sizeof(gActiveScriptInfo));
        gActiveScriptInfo.fadeSpeed = 8;
        DispReset(1);
        MessageInitialize();
        ResetPalettes();
        ResetPaletteTable(0);
        gGFXSlots.unk0 = 1;
        gUI.nextToLoad = 2; // Subtask_Update
        gRoomTransition.field30 = 1;
    }
}

void Subtask_FadeOut(void) {
#ifdef PC_PORT
    {
        extern void Port_LogSubtaskEntry(const char* name, unsigned active, unsigned nextToLoad);
        Port_LogSubtaskEntry("FadeOut", gFadeControl.active, gUI.nextToLoad);
    }
#endif
    if (!gFadeControl.active) {
        DeleteAllEntities();
        sub_0805E974();
        gCurrentRoomProperties = gUI.currentRoomProperties;
        gPlayerState.controlMode = gUI.controlMode;
        gMapBottom.bgSettings = gUI.mapBottomBgSettings;
        gMapTop.bgSettings = gUI.mapTopBgSettings;
        MemCopy(&gUI.activeScriptInfo, &gActiveScriptInfo, sizeof(ActiveScriptInfo));
        MemCopy(gUI.unk_2a8, gUnk_03000420, sizeof(gUI.unk_2a8));
        MemCopy(gUI.palettes, gPaletteList, sizeof(gUI.palettes));
        MemCopy(&gUI.gfxSlotList, &gGFXSlots, sizeof(gGFXSlots));
        MemCopy(&gUI.roomControls, &gRoomControls, sizeof(RoomControls));
        MemCopy(&gUnk_03001020, &gScreen, sizeof(Screen));
        gArea.localFlagOffset = GetFlagBankOffset(gRoomControls.area);
        gArea.pCurrentRoomInfo = GetCurrentRoomInfo();
        RestoreGameTask(gUI.loadGfxOnRestore);
#ifdef PC_PORT
        /* Issue #131: when the pause menu closes while Link is inside
         * a Deepwood Shrine rolling barrel, the barrel's gfx group
         * (0x16) and palette (0x28) are not restored — RestoreGameTask
         * → LoadGfxGroups only reloads the global slots (16, 23, …),
         * not the barrel-specific ones set up by
         * RollingBarrelManager_OnEnterRoom. Result: barrel sprite +
         * surrounding wall/stair tiles disappear and you see the
         * ground underneath. Re-run the barrel manager's enter
         * routine here so its palette + gfx + BG control regs are
         * put back exactly as they were before the menu opened.
         *
         * Symptom reproduces on both Linux and Windows builds, so
         * this is a logic gap in the restore path, not a port-side
         * heap-pointer issue. The fix mirrors the GBA behaviour the
         * decomp likely intended. */
        if (gUI.pauseFadeIn == GAMEMAIN_BARRELUPDATE) {
            extern void RollingBarrelManager_OnEnterRoom(void);
            RollingBarrelManager_OnEnterRoom();
        }
        /* Issue #103: kinstone-fusion world-map screen
         * (pauseMenuScreen6.c:165-176) sets up an HBlank DMA that
         * writes 0x1e0a to BG3CNT every scanline.  Its subtask exits
         * without disabling that DMA, so on return to gameplay BG3
         * keeps reading from the wrong charBase — Cloud Tops cloud
         * texture goes missing/garbled.  Disable any leftover
         * VBlankDMA here, then re-apply each AnimatedBackgroundManager's
         * bg3 control so the next frame's VBlank write overwrites
         * whatever the dead DMA left in BG3CNT.  Safe-by-default —
         * if a subtask wanted persistent DMA across its exit it
         * would have re-armed it post-restore anyway. */
        {
            extern void DisableVBlankDMA(void);
            extern Entity* FindEntityByID(u32 kind, u32 id, u32 idx);
            extern void AnimatedBackgroundManager_RestoreBgGfx(void* mgr);
            DisableVBlankDMA();
            for (u32 i = 0; i < 9; ++i) {
                Entity* mgr = FindEntityByID(MANAGER, ANIMATED_BACKGROUND_MANAGER, i);
                if (mgr != NULL) {
                    AnimatedBackgroundManager_RestoreBgGfx(mgr);
                }
            }
        }
#endif
        sub_0801D000(gUI.unk_d != 0);
        sub_080A74F4();
        if (gUI.fadeType != 0xffff) {
            SetFade(gUI.fadeType, gUI.fadeInTime);
        } else {
            SetFadeInverted(gUI.fadeInTime);
        }
        gUI.nextToLoad = 4; // Subtask_Die
        gRoomTransition.field30 = 0;
    }
}

void Subtask_Die(void) {
#ifdef PC_PORT
    {
        extern void Port_LogSubtaskEntry(const char* name, unsigned active, unsigned nextToLoad);
        Port_LogSubtaskEntry("Die", gFadeControl.active, gUI.nextToLoad);
    }
#endif
    sub_080A74F4();
    if (gFadeControl.active == 0) {
        gMain.substate = gUI.pauseFadeIn;
        gUI.nextToLoad = gFadeControl.active;
        gUI.lastState = gFadeControl.active;
        ClearEventPriority();
    }
}

void sub_080A74F4(void) {
    SetInitializationPriority();
    gMain.pad = 0;
    FlushSprites();
    UpdateEntities();
    UpdateManagers();
    DrawUI();
#ifdef PC_PORT
    /* Restored-gameplay frames of Subtask_FadeOut/Subtask_Die — the
     * gameplay view, so hide_top_hud applies here too (no HUD flash
     * while fading back from the pause menu). */
    DrawUIElementsGameplay();
#else
    DrawUIElements();
#endif
    UpdateCarriedObject();
    DrawEntities();
    CopyOAM();
}

void Subtask_Update(void) {
    gSubtasks[gUI.lastState]();
}
