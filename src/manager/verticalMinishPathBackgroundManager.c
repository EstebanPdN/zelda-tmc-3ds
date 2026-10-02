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

#ifdef PC_PORT
#include "port_vertical_minish_path.h"
#endif

extern void VerticalMinishPathBackgroundManager_OnEnterRoom(void*);
extern void sub_0805754C(VerticalMinishPathBackgroundManager*);

extern u16 gMapDataTopSpecial[];

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
    gScreen.bg3.subTileMap = Port_VerticalMinishPathSubTileMap(gMapDataTopSpecial, bgOffset, 0);
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
    gScreen.bg1.subTileMap = Port_VerticalMinishPathSubTileMap(gMapDataTopSpecial, bgOffset, 0x2000);
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
    gScreen.bg3.subTileMap = Port_VerticalMinishPathSubTileMap(gMapDataTopSpecial, bgOffset, 0);
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
    gScreen.bg1.subTileMap = Port_VerticalMinishPathSubTileMap(gMapDataTopSpecial, bgOffset, 0x2000);
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
