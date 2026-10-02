/**
 * @file minishRaftersBackgroundManager.c
 * @ingroup Managers
 *
 * @brief Parallax background for minish rafters
 */
#include "manager/minishRaftersBackgroundManager.h"
#include "common.h"
#include "room.h"
#include "game.h"
#include "vram.h"

void MinishRaftersBackgroundManager_OnEnterRoom(MinishRaftersBackgroundManager*);
void sub_08058210(MinishRaftersBackgroundManager*);
u32 sub_08058244(int);
void sub_080582A0(u32, u32*, u16*);
void sub_080582F8(u8*, u8*);

extern u16 gMapDataTopSpecial[];
extern u8 gUnk_02006F00[];

void MinishRaftersBackgroundManager_Main(MinishRaftersBackgroundManager* this) {
    sub_08058210(this);
    if (super->action == 0) {
        super->action = 1;
        gScreen.bg1.updated = 0;
        RegisterTransitionHandler(this, MinishRaftersBackgroundManager_OnEnterRoom, NULL);
    }
}

void MinishRaftersBackgroundManager_OnEnterRoom(MinishRaftersBackgroundManager* this) {
    sub_08058324(super->type);
}

void sub_08058210(MinishRaftersBackgroundManager* this) {
    u32 tmp = sub_08058244(super->type);
    if (this->unk_3c == tmp)
        return;
    this->unk_3c = tmp;
    sub_080582A0(tmp, (u32*)gUnk_02006F00, gBG3Buffer);
    gScreen.bg1.updated = 1;
}

u32 sub_08058244(int i) {
    static const u16 gUnk_081081EC[] = { 0x30, 0x30, 0x30, 0x38 };
    u32 tmp;
    s32 tmp2;
    u32 tmp3;
    s32 tmp4;
    tmp = ((gRoomControls.scroll_y - gRoomControls.origin_y) * 0x20) / (gRoomControls.height - DISPLAY_HEIGHT);
    gScreen.bg1.yOffset = gRoomControls.origin_y + tmp;
    tmp = (((gRoomControls.scroll_x - gRoomControls.origin_x) * gUnk_081081EC[i]) /
           (gRoomControls.width - DISPLAY_WIDTH));
    gScreen.bg1.xOffset = tmp & 0xf;
    return tmp;
}

void sub_080582A0(u32 unk, u32* unk2, u16* unk3) {
    int i = 0x20;
#ifdef PC_PORT
    /* Same-class fix as bigGoron.c::sub_0806D164 (issue #102) /
     * horizontalMinishPathBackgroundManager.c::sub_08058004.
     * Callers pass unk2 = gUnk_02006F00 (16 KB). The loop reads
     * 32 × 0x100 = 0x2000 bytes forward starting at unk2 + (unk>>4)*4
     * (unk2 is u32*, so += 0x40 == +0x100 bytes). When `unk` comes from
     * scroll_x derivations and goes negative under camera-wrap, the
     * start offset balloons and the read walks off into unmapped host
     * memory — SIGSEGV inside DmaCopy16. Skip the update if the start
     * would push the trailing 0x2000-byte read out of bounds. */
    {
        u32 startBytes = (unk >> 4) * 4;
        if ((u8*)unk2 < (u8*)gUnk_02006F00 || (u8*)unk2 >= (u8*)gUnk_02006F00 + 0x4000 ||
            startBytes + 0x2000u > 0x4000u - (u32)((u8*)unk2 - (u8*)gUnk_02006F00))
            return;
    }
#endif
    unk2 += unk >> 4;
    for (; i != 0; i--) {
        DmaCopy16(3, unk2, unk3, 0x20 * 2);
        unk2 += 0x40;
        unk3 += 0x20;
    }
}

void sub_080582D0(void) {
    u8* tmp = (u8*)gMapDataTopSpecial;
#ifdef PC_PORT
    /* Same-class fix as bigGoron.c::sub_0806D110 (issue #102).
     * On GBA `tmp + 0x4000` resolves to gUnk_02006F00 (16 KB BG tilemap
     * buffer) via the contiguous EWRAM layout (gMapDataTopSpecial @
     * 0x02002F00 + 0x4000 = 0x02006F00 = gUnk_02006F00).  On PC the two
     * are separate host allocations, so the bridge silently writes into
     * the middle of gMapDataTopSpecial instead — leaving gUnk_02006F00
     * uninitialised, which is what sub_080582A0 then DmaCopies back out
     * (and what can SIGSEGV when its start offset goes wild). Route
     * through the real buffer. */
    u8* tmp2 = (u8*)gUnk_02006F00;
#else
    u8* tmp2 = tmp + 0x4000;
#endif
    sub_080582F8(tmp, tmp2);
    tmp += 0x800;
    tmp2 += 0x40;
    sub_080582F8(tmp, tmp2);
}

void sub_080582F8(u8* unk, u8* unk2) {
    u32 i;
    for (i = 0; i < 0x20; i++, unk += 0x40, unk2 += 0x100) {
        DmaCopy16(3, unk, unk2, 0x20 * 2);
    }
}

void sub_08058324(u32 unk) {
    gMapTop.bgSettings = 0;
    LoadPaletteGroup(unk + 0x86);
    LoadGfxGroup(unk + 0x36);
    sub_080582D0();
    sub_080582A0(sub_08058244(unk), (u32*)gUnk_02006F00, gBG3Buffer);
    gScreen.bg1.control = 0x1D47;
    gScreen.bg1.subTileMap = gBG3Buffer;
    gScreen.bg1.updated = 1;
    gScreen.lcd.displayControl |= 0x200;
}
