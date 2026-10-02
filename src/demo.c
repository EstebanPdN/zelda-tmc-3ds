/**
 * @file demo.c
 * @ingroup Tasks
 *
 * @brief Demo task
 */

#include "common.h"
#include "fileselect.h"
#include "game.h"
#include "global.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "screen.h"
#include "sound.h"
#include "affine.h"
#include "fade.h"
#ifdef PC_PORT
#include "port_rom.h"
#endif

void sub_080A30AC(void);
void sub_080A2E40(void);
void sub_080A2FD0(void);
void sub_080A2F8C(void);

#ifndef PC_PORT
extern u8 gUnk_02000004;
#endif
void sub_080A3198(u32, u32);

extern u8 gUnk_08A05751[];

// sprite_table
extern const u8 gUnk_089FD1B4[];
extern const u8 gUnk_089FD2F4[];

extern u8 gUnk_08A068BF[129];

void DemoTask(void) {
    static void (*const demoFunctions[])(void) = {
        sub_080A2E40,
        sub_080A2FD0,
        sub_080A2F8C,
    };
    FlushSprites();
    demoFunctions[gMain.state]();
    sub_080A30AC();
    CopyOAM();
}

void sub_080A2E40(void) {
    DispReset(1);
    InitSoundPlayingInfo();
    MemClear((void*)VRAM, 0x80);
    MessageInitialize();
    EraseAllEntities();
    ResetPalettes();
    ResetPaletteTable(0);
    MemClear(&gUI, sizeof gUI);
    MemClear(&gChooseFileState, sizeof gChooseFileState);
    MemClear(&gBG0Buffer, sizeof gBG0Buffer);
    LoadGfxGroups();
    LoadPaletteGroup(0xb5);
    LoadGfxGroup(0x56);
    MemCopy(gUnk_089FD1B4, gPaletteBuffer + 96, 0x100);
    MemCopy(gUnk_089FD1B4, gPaletteBuffer + 352, 0x100);
    MemCopy(gUnk_089FD2F4, (void*)0x6000000, 0x8000);
    MemCopy(gUnk_089FD2F4, (void*)0x6010000, 0x8000);
    MemCopy(&gUnk_08A05751, &gBG1Buffer, 0x800);
    MemCopy(&gUnk_08A05751[0x800], &gBG2Buffer, 0x800);
    gScreen.lcd.displayControl = 0x1f00;
    gScreen.bg1.control = 0x1c81;
    gScreen.bg2.control = 0x1d82;
    gScreen.bg3.control = 0x1e0b;
    gScreen.bg1.updated = 1;
    gScreen.bg2.updated = 1;
    gScreen.bg3.updated = 1;
    gScreen.controls.layerFXControl = 0x1044;
    gScreen.controls.alphaBlend = 0xb04;
#ifndef PC_PORT
    gUnk_02000004 = 0;
#endif
    sub_080A3198(0, 0);
    gMain.state = GAMETASK_INIT;
    SoundReq(BGM_FILE_SELECT);
    SetFade(FADE_INSTANT, 8);
}

void sub_080A2F8C(void) {
    if (gFadeControl.active == 0) {
        switch (gMain.substate) {
            case GAMEMAIN_INITROOM:
                gMain.substate = GAMEMAIN_CHANGEROOM;
#ifndef PC_PORT
                *(u8*)0x02000005 = 1;
                *(u8*)0x02000006 = 1;
#endif
                gMain.state = GAMETASK_MAIN;
                SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
                break;
            case GAMEMAIN_CHANGEROOM:
                SetTask(TASK_GAME);
                break;
        }
    }
}

void sub_080A2FD0(void) {
    u32 val;
    s32 tmp3;

    if (gFadeControl.active == 0) {
        val = 0;
        switch (gInput.heldKeys) {
            case DPAD_LEFT:
                if (gChooseFileState.unk_0x0 == 0) {
                    val = -1;
                    gChooseFileState.unk_0x20 = -4;
                    SoundReq(SFX_TEXTBOX_CHOICE);
                }
                break;
            case DPAD_RIGHT:
                if (gChooseFileState.unk_0x0 == 0) {
                    val = 1;
                    gChooseFileState.unk_0x20 = 4;
                    SoundReq(SFX_TEXTBOX_CHOICE);
                }
                break;
            case START_BUTTON:
            case A_BUTTON:
                if (gChooseFileState.unk_0x0 == 0) {
                    gMain.state = 2;
                    gMain.substate = gChooseFileState.unk_0x0;
                    SoundReq(SFX_TEXTBOX_SELECT);
                }
                break;
            case DPAD_UP:
            case DPAD_DOWN:
                break;
        }
        gSaveHeader->saveFileId = (int)(gSaveHeader->saveFileId + 3 + val) % 3;

        tmp3 = gChooseFileState.unk_0x10;

        if (gSaveHeader->saveFileId * 0x68 != tmp3) {
            gChooseFileState.unk_0x10 = (tmp3 + gChooseFileState.unk_0x20 + 0x138) % 0x138;
            gChooseFileState.unk_0x0 = 1;
        } else {
            gChooseFileState.unk_0x0 = 0;
        }

        sub_080A3198(gSaveHeader->saveFileId, 0);
    }
}

void sub_080A30AC(void) {
    s32 unk_0x10;
    u8* ptr;
    u32 offset;
    gOamCmd._4 = 0x2000;
    gOamCmd._6 = 0;
    gOamCmd._8 = 0xc00;
    gOamCmd.y = 0x40;
    unk_0x10 = (s16)gGenericMenu.unk10.h[0];
    gOamCmd.x = -0x128 - unk_0x10;
#ifdef PC_PORT
    /* GBA reads three u32 frame-object offsets from a table at
     * gUnk_08A068BF-0xc (the first two entries sit BEFORE gUnk_08A068BF) and
     * draws each 3x; the offsets are ROM-relative to that table base. On PC
     * gUnk_08A068BF is a standalone .rodata slice, so both the -0xc arithmetic
     * and adding a ROM-relative offset (~0x3C0100) leave the object and read
     * out of bounds. Resolve the table and every target against the live ROM
     * instead. 0x08A068B3 = 0x08A068BF - 0xc. */
    ptr = (u8*)Port_ResolveRomData(0x08A068B3u);
    if (ptr != NULL) {
        u32 off4 = Port_ReadU32(ptr + 4);
        u32 off8 = Port_ReadU32(ptr + 8);
        offset = Port_ReadU32(ptr + 0xc);
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + offset));
        gOamCmd.x = -0xc0 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off4));
        gOamCmd.x = -0x58 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off8));
        gOamCmd.x = 0x10 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + offset));
        gOamCmd.x = 0x78 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off4));
        gOamCmd.x = 0xe0 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off8));
        gOamCmd.x = 0x148 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + offset));
        gOamCmd.x = 0x1b0 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off4));
        gOamCmd.x = 0x218 - unk_0x10;
        sub_080ADA04(&gOamCmd, Port_ResolveRomData(0x08A068B3u + off8));
    }
#else
    offset = *(u32*)gUnk_08A068BF;
    ptr = gUnk_08A068BF - 0xc;
    sub_080ADA04(&gOamCmd, ptr + offset);
    gOamCmd.x = -0xc0 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 4));
    gOamCmd.x = -0x58 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 8));
    gOamCmd.x = 0x10 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(gUnk_08A068BF));
    gOamCmd.x = 0x78 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 4));
    gOamCmd.x = 0xe0 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 8));
    gOamCmd.x = 0x148 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(gUnk_08A068BF));
    gOamCmd.x = 0x1b0 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 4));
    gOamCmd.x = 0x218 - unk_0x10;
    sub_080ADA04(&gOamCmd, ptr + *(u32*)(ptr + 8));
#endif
}

void sub_080A3198(u32 param_1, u32 param_2) {
    static const Font gUnk_08127C98[] = {
        { &gBG0Buffer[0x1ef], BG_TILE_ADDR(0x1a0), gTextGfxBuffer, 0, 0x1080, 0xd0, 1, 0, 0, 1, 9, 0, 0, 1, 1 },
        { &gBG0Buffer[0x22f], BG_TILE_ADDR(0x1a0), gTextGfxBuffer, 0, 0x80, 0xd0, 1, 0, 0, 1, 9, 0, 0, 1, 1 },
    };
    static const u16 gUnk_08127CC8[4] = {
        TEXT_INDEX(TEXT_WINDCRESTS, 0x1c),
        TEXT_INDEX(TEXT_WINDCRESTS, 0x20),
        TEXT_INDEX(TEXT_WINDCRESTS, 0x21),
        0x0,
    };
    u16 textIndex = gUnk_08127CC8[param_1];

    if (gChooseFileState.unk_0x12 != textIndex) {
        gChooseFileState.unk_0x12 = textIndex;
        MemClear(gUnk_08127C98[0].dest - 0xf, 0x180);

        if (textIndex != 0) {
            ShowTextBox(textIndex, gUnk_08127C98);
        }

        gScreen.bg0.updated = 1;
    }
}
