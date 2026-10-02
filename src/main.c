/**
 * @file main.c
 *
 * @brief Contains the main game loop.
 */
#include "main.h"

#include "common.h"
#include "game.h"
#include "interrupts.h"
#include "message.h"
#include "save.h"
#include "screen.h"
#include "sound.h"
#include "fade.h"
#ifdef PC_PORT
#include "port_hdma.h"
#include "port_second_screen_state.h"
#include <setjmp.h>
#endif
#ifdef TMC_3DS
#include "platform_3ds.h"
#endif
#include "gba/io_reg.h"

#ifdef TMC_N64
/* N64 bring-up: paint a boot-phase marker (port/n64/n64_main.c) so a hang in
 * init — before the engine's first VBlankIntrWait can render — is localizable.
 * Compiles to nothing off N64. Diagnostic scaffolding. */
extern void n64_post(int);
#define N64_POST(n) n64_post(n)
#else
#define N64_POST(n)
#endif

extern u32 gRand;

static void InitOverlays(void);
static bool32 SoftResetKeysPressed(void);
/*static*/ u32 CheckHeaderValid(void);
/*static*/ void InitSaveHeader(void);

void (*const sTaskHandlers[])(void) = {
    [TASK_TITLE] = TitleTask,         [TASK_FILE_SELECT] = FileSelectTask,

    [TASK_GAME] = GameTask,           [TASK_GAMEOVER] = GameOverTask,
    [TASK_STAFFROLL] = StaffrollTask, [TASK_DEBUG] = DebugTask,
};

void AgbMain(void) {
#ifdef PC_PORT
    /* Soft-reset re-entry point. SoftReset() (port_bios.c) longjmp()s here so
     * the credits end, the reset combo, and game-over restart to the title by
     * re-running the init below — RegisterRamReset preserves EWRAM (save data),
     * everything else is reset. Native equivalent of the GBA BIOS soft reset;
     * see DoSoftReset() / SoftReset(). */
    {
        extern jmp_buf gPortSoftResetJmp;
        extern int gPortSoftResetArmed;
        setjmp(gPortSoftResetJmp);
        gPortSoftResetArmed = 1;
    }
#endif
    // Initialization
    N64_POST(10);
    InitOverlays();
    N64_POST(11);
    InitSound();
    N64_POST(12);
    InitDMA();
    N64_POST(13);
    InitSaveData();
    N64_POST(14);
    InitSaveHeader();
    N64_POST(15);
    InitVBlankDMA();
    N64_POST(16);
    gUnk_02000010.field_0x4 = 0xc1;
    InitFade();
    DmaCopy32(3, BG_PLTT, gPaletteBuffer, BG_PLTT_SIZE);
    SetBrightness(1);
    N64_POST(17);
    MessageInitialize();
    ResetPalettes();
    N64_POST(18);
    gRand = 0x1234567;
    MemClear(&gMain, sizeof(gMain));
    SetTask(TASK_TITLE);
    N64_POST(20);

    // Game Loop
    while (
#ifdef TMC_3DS
        Platform3DS_IsRunning()
#else
        TRUE
#endif
    ) {
        ReadKeyInput();
        if (SoftResetKeysPressed()) {
            DoSoftReset();
        }
        switch (gMain.sleepStatus) {
            case SLEEP:
                SetSleepMode();
                break;
            case DEFAULT:
            default:
                if (gMain.pauseFrames != 0) {
                    do {
                        VBlankIntrWait();
                    } while (--gMain.pauseFrames);
                }

                if (gMain.pauseCount != 0) {
                    int cnt;
                    gMain.pauseCount--;
                    cnt = gMain.pauseInterval;
                    while (cnt-- > 0) {
                        VBlankIntrWait();
                    }
                }

                gMain.ticks++;
                sTaskHandlers[gMain.task]();
#ifdef TMC_N64
                {
                    static int s_once = 0;
                    if (!s_once) {
                        s_once = 1;
                        n64_post(22);
                    }
                }
#endif

                MessageMain();

                FadeMain();

                AudioMain();
                break;
        }
#ifdef PC_PORT
        Port_SecondScreenState_Publish();
#endif
        WaitForNextFrame();
    }
}

extern u8 gUnk_02000030[];
// Interrupt handlers that are loaded into RAM.
extern u8 sub_080B197C[];
extern u8 ram_sub_080B197C[];
extern u8 RAMFUNCS_END[];

extern u8 gCopyToEndOfEwram_Start[];
extern u8 gCopyToEndOfEwram_End[];
extern u8 gEndOfEwram[];

static void InitOverlays(void) {
#ifdef PC_PORT
    // On PC, skip linker-symbol EWRAM/RAM copies (not applicable)
    DisableInterruptsAndDMA();
    RegisterRamReset(RESET_ALL & ~RESET_EWRAM);
    gba_write16(BG_PLTT, 0x7FFF);
    gba_write16(REG_ADDR_WAITCNT, WAITCNT_PREFETCH_ENABLE | WAITCNT_WS0_S_1 | WAITCNT_WS0_N_3);
    DispReset(0);
    EnableVBlankIntr();
#else
    u32 size;

    DisableInterruptsAndDMA();
    RegisterRamReset(RESET_ALL & ~RESET_EWRAM);
    *(vu16*)BG_PLTT = 0x7FFF;
    *(vu16*)REG_ADDR_WAITCNT = WAITCNT_PREFETCH_ENABLE | WAITCNT_WS0_S_1 | WAITCNT_WS0_N_3;
    size = 0x3FFD0;
    MemClear(gUnk_02000030, size);
    size = (u32)RAMFUNCS_END - (u32)sub_080B197C;
    if (size != 0) {
        MemCopy(sub_080B197C, ram_sub_080B197C, size);
    }

    size = (u32)gCopyToEndOfEwram_End - (u32)gCopyToEndOfEwram_Start;
    if (size != 0) {
        MemCopy(gCopyToEndOfEwram_Start, gEndOfEwram, size);
    }

    DispReset(0);
    EnableVBlankIntr();
#endif
}

#define SOFT_RESET_KEYS (A_BUTTON | B_BUTTON | SELECT_BUTTON | START_BUTTON)

static bool32 SoftResetKeysPressed(void) {
    return (gInput.heldKeys & SOFT_RESET_KEYS) == SOFT_RESET_KEYS;
}

void SetTask(u32 task) {
    gMain.task = task;
    gMain.state = GAMETASK_TRANSITION;
    gMain.substate = GAMEMAIN_INITROOM;
}

void DisableInterruptsAndDMA(void) {
#ifdef PC_PORT
    gba_write16(REG_ADDR_IME, 0);
    gba_write16(REG_ADDR_IE, 0);
    gba_write16(REG_ADDR_DISPSTAT, 0);
    gba_write16(REG_ADDR_IF, 0);
    gba_write16(REG_ADDR_IME, 0);
#else
    REG_IME = 0;
    REG_IE = 0;
    REG_DISPSTAT = 0;
    REG_IF = 0;
    REG_IME = 0;
#endif

    DmaStop(0);
    DmaStop(1);
    DmaStop(2);
    DmaStop(3);
}

void DoSoftReset(void) {
    DisableInterruptsAndDMA();
    SoftReset(RESET_ALL & ~(RESET_EWRAM | RESET_SIO_REGS));
}

const SaveHeader sDefaultSettings = {
    .signature = SIGNATURE,
    .saveFileId = 0,
    .msg_speed = 1,
    .brightness = 1,
    .language = GAME_LANGUAGE,
    .name = "LINK",
    .invalid = 0,
    .initialized = 0,
};

void InitSaveHeader(void) {
    u32 b;

    if (!CheckHeaderValid()) {
        switch ((s32)ReadSaveHeader(gSaveHeader)) {
            case 1:
                if (CheckHeaderValid())
                    break;
            case 0:
            case -1:
            default:
                MemCopy(&sDefaultSettings, gSaveHeader, sizeof(SaveHeader));
                gSaveHeader->language = RegionDefaultLanguage();
                WriteSaveHeader(gSaveHeader);
                break;
        }
    }

    if (gUnk_02000010.signature ^ SIGNATURE) {
        b = TRUE;
    } else {
        b = FALSE;
    }

    if ((gUnk_02000010.field_0x4 != 0) && (gUnk_02000010.field_0x4 != 0xc1)) {
        b = TRUE;
    }
    if (b) {
        struct_02000010* ptr = &gUnk_02000010;
        MemClear(ptr, sizeof gUnk_02000010);
        ptr->signature = SIGNATURE;
    }
}

/*static*/ u32 CheckHeaderValid(void) {

    if ((gSaveHeader->signature != SIGNATURE) || (gSaveHeader->saveFileId >= NUM_SAVE_SLOTS) ||
        (gSaveHeader->msg_speed >= MAX_MSG_SPEED) ||
        (gSaveHeader->brightness >= MAX_BRIGHTNESS)
        /* The multi-region binary is compiled USA-baseline (GAME_LANGUAGE == EN),
         * but the loaded ROM's region is only known at runtime. Region-specific
         * language-gated gfx (title, name-entry, file-select) must use the active
         * ROM's save-header slots: JP=0, USA=1, EU=2..6. */
        || !RegionSaveLanguageValid(gSaveHeader->language) ||
        (gSaveHeader->invalid))
        return FALSE;

    return TRUE;
}

void InitDMA(void) {
    SoundReq(SONG_VSYNC_OFF);
    gScreen.vBlankDMA.readyBackup = gScreen.vBlankDMA.ready;
    gScreen.vBlankDMA.ready = FALSE;

    DmaStop(0);

    DmaWait(0);
    DmaWait(1);
    DmaWait(2);
    DmaWait(3);
}

void InitVBlankDMA(void) {
    SoundReq(SONG_VSYNC_ON);
    gScreen.vBlankDMA.ready = gScreen.vBlankDMA.readyBackup;
    gScreen.vBlankDMA.readyBackup = FALSE;
}

void SetVBlankDMA(u16* src, u16* dest, u32 size) {
    gScreen.vBlankDMA.src = src;
    gScreen.vBlankDMA.dest = dest;
    gScreen.vBlankDMA.size = size;
    gScreen.vBlankDMA.ready = TRUE;
    gUnk_03003DE4[0] ^= 1;
}

void DisableVBlankDMA(void) {
    gScreen.vBlankDMA.ready = FALSE;
#ifdef PC_PORT
    port_hdma_unregister(0);
#endif
}

void SetSleepMode(void) {
    // simulate a sleep
    Main* main;

    REG_DISPCNT = DISPCNT_FORCED_BLANK;

    do {
        VBlankIntrWait();
    } while (REG_KEYINPUT != 0x03FF);

    do {
        VBlankIntrWait();
    } while (REG_KEYINPUT == 0x03FF);

    main = &gMain;
    *(vu8*)&main->sleepStatus;
    main->sleepStatus = DEFAULT;
    return;
}

// Convert AABB to screen coordinates and check if it's within the viewport
u32 CheckRegionOnScreen(u32 x0, u32 y0, u32 x1, u32 y1) {
    u32 result;
    u32 x = ((gRoomControls.scroll_x - gRoomControls.origin_x) - x0 + DISPLAY_WIDTH);
    u32 y = ((gRoomControls.scroll_y - gRoomControls.origin_y) - y0 + DISPLAY_HEIGHT);
    u32 a = x1 + DISPLAY_WIDTH;
    u32 b = y1 + DISPLAY_HEIGHT;
    if ((x < a) && (y < b))
        result = TRUE;
    else
        result = FALSE;
    return result;
}

/**
 * Iterate over array of AABBs and check if any fit on screen
 */
u32 CheckRegionsOnScreen(const u16* arr) {
    for (; *arr != 0xff; arr += 5) {
        if (CheckRegionOnScreen(arr[1], arr[2], arr[3], arr[4]))
            return *arr;
    }
    return 0xff;
}

void PlayerItemNulled2(Entity* this) {
    DeleteThisEntity();
}

void PlayerItemNulled(Entity* this) {
    DeleteThisEntity();
}
