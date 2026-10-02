/**
 * @file verticalMinishPathBackgroundManager.c
 * @ingroup Managers
 *
 * @brief Parallax scrolling for MinishPaths vertical.
 */
#include "manager/verticalMinishPathBackgroundManager.h"
#include "common.h"
#include "game.h"
#include "screen.h"
#include "room.h"

extern void VerticalMinishPathBackgroundManager_OnEnterRoom(void*);
extern void sub_0805754C(VerticalMinishPathBackgroundManager*);

extern u16 gMapDataTopSpecial[];

#ifdef PC_PORT
/* Same-family fix as bigGoron / horizontalMinishPath / minishRafters
 * (issue #102 cluster): subTileMap pointer arithmetic depends on
 * `bgOffset` staying in bounds. When scroll_y < origin_y (camera
 * approaching room top during a transition), bgOffset goes negative;
 * (bgOffset/0x40)*0x200 becomes a negative byte delta and the resulting
 * subTileMap pointer lands BEFORE gMapDataTopSpecial. On GBA this still
 * reads adjacent EWRAM; on PC the buffer's a standalone allocation and
 * the renderer dereferences unmapped memory. gMapDataTopSpecial is
 * 0x8000 bytes; the renderer reads BG_SCREEN_SIZE (0x800) from
 * subTileMap, so clamp the byte delta to [0, 0x7800]. */
static inline s32 VMP_ClampBgDelta(s32 bgOffset, s32 baseAdd) {
    s32 delta = (bgOffset / 0x40) * 0x200;
    s32 lo = -baseAdd;
    s32 hi = 0x7800 - baseAdd;
    if (delta < lo)
        delta = lo;
    if (delta > hi)
        delta = hi;
    return delta;
}
#endif

void VerticalMinishPathBackgroundManager_Main(VerticalMinishPathBackgroundManager* this) {
    if (super->action == 0) {
        super->action = 1;
        gScreen.bg3.updated = 0;
        gScreen.bg1.updated = 0;
        RegisterTransitionHandler(this, VerticalMinishPathBackgroundManager_OnEnterRoom, NULL);
    }
    sub_0805754C(this);
}

void sub_0805754C(VerticalMinishPathBackgroundManager* this) {
    s32 bgOffset;

    bgOffset = (gRoomControls.scroll_y - gRoomControls.origin_y);
    bgOffset += bgOffset >> 3;
    gScreen.bg3.yOffset = bgOffset & 0x3f;
#ifdef PC_PORT
    gScreen.bg3.subTileMap = gMapDataTopSpecial + VMP_ClampBgDelta(bgOffset, 0);
#else
    gScreen.bg3.subTileMap = gMapDataTopSpecial + (bgOffset / 0x40) * 0x200;
#endif
    if (this->field_0x38 != gScreen.bg3.subTileMap) {
        this->field_0x38 = gScreen.bg3.subTileMap;
        gScreen.bg3.updated = 1;
    }
    bgOffset = (gRoomControls.scroll_y - gRoomControls.origin_y);
    bgOffset += bgOffset >> 2;
    gScreen.bg1.yOffset = bgOffset & 0x3f;
#ifdef PC_PORT
    gScreen.bg1.subTileMap = gMapDataTopSpecial + 0x2000 + VMP_ClampBgDelta(bgOffset, 0x2000);
#else
    gScreen.bg1.subTileMap = gMapDataTopSpecial + 0x2000 + (bgOffset / 0x40) * 0x200;
#endif
    if (this->field_0x3c != gScreen.bg1.subTileMap) {
        this->field_0x3c = gScreen.bg1.subTileMap;
        gScreen.bg1.updated = 1;
    }
}

void sub_080575C8(u32 param) {
    s32 bgOffset;

    gMapTop.bgSettings = 0;
    REG_DISPCNT = 0;
    LoadGfxGroup(param);
    gRoomVars.graphicsGroups[0] = param;

    bgOffset = (gRoomControls.scroll_y - gRoomControls.origin_y);
    bgOffset += bgOffset >> 3;
    gScreen.bg3.yOffset = bgOffset & 0x3f;
    gScreen.bg3.xOffset = 0;
#ifdef PC_PORT
    gScreen.bg3.subTileMap = (u16*)((u8*)gMapDataTopSpecial + VMP_ClampBgDelta(bgOffset, 0));
#else
    gScreen.bg3.subTileMap = (u16*)((u8*)gMapDataTopSpecial + (bgOffset / 0x40) * 0x200);
#endif
    gScreen.bg3.control = BGCNT_SCREENBASE(29) | BGCNT_PRIORITY(1) | BGCNT_CHARBASE(2) | BGCNT_MOSAIC;
    gScreen.bg3.updated = 1;

    bgOffset = (gRoomControls.scroll_y - gRoomControls.origin_y);
    bgOffset += bgOffset >> 2;
    gScreen.bg1.yOffset = bgOffset & 0x3f;
    gScreen.bg1.xOffset = 0;
#ifdef PC_PORT
    gScreen.bg1.subTileMap = (u16*)((u8*)gMapDataTopSpecial + 0x2000 + VMP_ClampBgDelta(bgOffset, 0x2000));
#else
    gScreen.bg1.subTileMap = (u16*)((u8*)gMapDataTopSpecial + 0x2000 + (bgOffset / 0x40) * 0x200);
#endif
    gScreen.bg1.control = BGCNT_SCREENBASE(30) | BGCNT_PRIORITY(1) | BGCNT_CHARBASE(2) | BGCNT_MOSAIC;
    gScreen.bg1.updated = 1;
    gScreen.controls.layerFXControl =
        BLDCNT_TGT1_BG3 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_BG3 | BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD;
    gScreen.controls.alphaBlend = BLDALPHA_BLEND(9, 6);
    gScreen.lcd.displayControl |= DISPCNT_BG3_ON | DISPCNT_BG1_ON;
}

void sub_08057688(void) {
    gScreen.bg3.control = BGCNT_SCREENBASE(29) | BGCNT_CHARBASE(2) | BGCNT_MOSAIC;
    gScreen.bg1.control = BGCNT_SCREENBASE(30) | BGCNT_CHARBASE(2) | BGCNT_MOSAIC;
}

void VerticalMinishPathBackgroundManager_OnEnterRoom(void* this) {
    LoadGfxGroup(gRoomVars.graphicsGroups[0]);
    ((VerticalMinishPathBackgroundManager*)this)->field_0x38 = NULL;
    ((VerticalMinishPathBackgroundManager*)this)->field_0x3c = NULL;
    sub_0805754C((VerticalMinishPathBackgroundManager*)this);
}
