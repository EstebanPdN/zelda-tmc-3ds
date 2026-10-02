/*
 * Rolling-barrel doorway HDMA lifecycle regression.
 *
 * The room-exit callback runs as soon as the door fade starts, while the old
 * room is still visible.  Retail keeps the barrel's affine HBlank DMA alive
 * through those visible fade frames and stops DMA0 only at the display reset.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "area.h"
#include "common.h"
#include "flags.h"
#include "fade.h"
#include "game.h"
#include "main.h"
#include "manager/rollingBarrelManager.h"
#include "player.h"
#include "port_hdma.h"
#include "room.h"
#include "screen.h"

void RollingBarrelManager_Init(RollingBarrelManager* manager);

Area gArea;
FadeControl gFadeControl;
Main gMain;
PlayerEntity gPlayerEntity;
RoomControls gRoomControls;
Screen gScreen;
OAMControls gOAMControls;
u16 gPaletteBuffer[32 * 16];
u8 gIoMem[0x400];
u8 gEwram[0x40000];
u8 gIwram[0x8000];
u16 gBgPltt[256];
u16 gObjPltt[256];
u16 gOamMem[0x400 / 2];
u8 gVram[0x18000];
u8* gRomData;
u32 gRomSize;
u8 gUnk_03003DE0;
u8 gUpdateVisibleTiles;
u32 gUsedPalettes;

static void* s_transitionManager;
static void (*s_onEnter)(void);
static void (*s_onExit)(void*);

bool32 CheckLocalFlagsB(u32 ord, u32 count) {
    (void)ord;
    (void)count;
    return FALSE;
}

bool32 CheckGlobalFlag(u32 flag) {
    (void)flag;
    return FALSE;
}

void PortRollingBarrelTest_LoadPaletteGroup(u32 group) {
    (void)group;
}

void PortRollingBarrelTest_LoadGfxGroup(u32 group) {
    (void)group;
}

void PortRollingBarrelTest_MemCopy(const void* src, void* dest, u32 size) {
    memcpy(dest, src, size);
}

void MemClear(void* dest, u32 size) {
    memset(dest, 0, size);
}

void gba_write16(uint32_t addr, uint16_t value) {
    if (addr >= 0x07000000u && addr < 0x07000400u) {
        uint32_t offset = addr - 0x07000000u;
        memcpy((uint8_t*)gOAMControls.oam + offset, &value, sizeof(value));
    }
}

void Port_LogRomAccess(u32 gbaAddress, const char* caller) {
    (void)gbaAddress;
    (void)caller;
}

void* port_resolve_write_addr(uintptr_t value) {
    return (void*)value;
}

void RegisterTransitionHandler(void* mgr, void (*onEnter)(void), void (*onExit)(void*)) {
    s_transitionManager = mgr;
    s_onEnter = onEnter;
    s_onExit = onExit;
}

void DisableVBlankDMA(void) {
    port_hdma_unregister(0);
}

static int Check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "rolling_barrel_transition_test: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void) {
    RollingBarrelManager manager;
    uint16_t scanlineAffine[8] = { 0x100, 1, 2, 0x100, 3, 4, 5, 6 };
    uint16_t bg2Registers[8] = { 0 };

    memset(&manager, 0, sizeof(manager));
    gPlayerEntity.base.x.HALF.HI = 120;
    gPlayerEntity.base.y.HALF.HI = 80;

    /* DMA_DEST_RELOAD | DMA_REPEAT | DMA_START_HBLANK; the port helper only
     * needs the high-half destination/source/unit flags for this oracle. */
    port_hdma_register(0, scanlineAffine, bg2Registers, 0x0060, 8);
    RollingBarrelManager_Init(&manager);

    if (!Check(s_transitionManager == &manager, "manager did not register its transition lifecycle") ||
        !Check(s_onEnter != NULL, "barrel enter restore callback is missing")) {
        return 1;
    }

    /* Match RoomExitCallback: invoke the registered callback at fade start.
     * An exit callback that stops HDMA here flattens the still-visible barrel. */
    if (s_onExit != NULL) {
        s_onExit(&manager);
    }
    if (!Check(port_hdma_has_active_channels(),
               "barrel HDMA stopped at visible door-fade start instead of display reset")) {
        return 1;
    }
    if (!Check(s_onExit == NULL, "barrel diverged from the retail NULL room-exit callback")) {
        return 1;
    }

    port_hdma_step_line(0);
    if (!Check(memcmp(scanlineAffine, bg2Registers, sizeof(bg2Registers)) == 0,
               "affine matrix was not preserved during the visible fade")) {
        return 1;
    }

    /* Exercise the real production reset, not a test-side approximation.
     * This is the host branch that must pair DmaStop(0) with the port HDMA
     * channel once the fade reaches black. */
    gMain.interruptFlag = 0;
    gUnk_03003DE0 = 7;
    gFadeControl.active = 1;
    gScreen.vBlankDMA.ready = TRUE;
    gScreen.vBlankDMA.readyBackup = TRUE;
    memset(gBG0Buffer, 0xA5, sizeof(gBG0Buffer));
    DispReset(TRUE);
    if (!Check(!port_hdma_has_active_channels(), "barrel HDMA leaked past display reset")) {
        return 1;
    }
    if (!Check(gMain.interruptFlag == 1 && gUnk_03003DE0 == 0 && gFadeControl.active == 0,
               "production DispReset did not reset main/fade state") ||
        !Check(!gScreen.vBlankDMA.ready && !gScreen.vBlankDMA.readyBackup,
               "production DispReset did not clear VBlank DMA readiness") ||
        !Check(gScreen.bg0.updated == TRUE && gScreen.bg0.subTileMap == &gBG0Buffer,
               "production DispReset did not rebuild BG0 state") ||
        !Check(gBG0Buffer[0] == 0 && gBG0Buffer[0x3FF] == 0,
               "production DispReset did not clear the BG0 tilemap")) {
        return 1;
    }

    puts("rolling_barrel_transition_test: PASS");
    return 0;
}
