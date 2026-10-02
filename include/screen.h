#ifndef SCREEN_H
#define SCREEN_H

#include "fade.h"
#include "global.h"

typedef struct {
    /*0x00*/ u16 displayControl;
    /*0x02*/ u8 filler2[0x2];
    /*0x04*/ u16 unk4;
    /*0x06*/ u16 displayControlMask;
} LcdControls;

typedef struct {
    u16 control;
    u16 xOffset;
    u16 yOffset;
    u16 updated;
    void* subTileMap;
} BgSettings;

typedef struct {
    u16 control;
    s16 xOffset;
    s16 yOffset;
    u16 updated;
    void* subTileMap;
} BgAffSettings;

typedef struct {
    u16 dx;
    u16 dmx;
    u16 dy;
    u16 dmy;
    u16 xPointLeastSig;
    u16 xPointMostSig;
    u16 yPointLeastSig;
    u16 yPointMostSig;
} BgTransformationSettings;

typedef struct {
    BgTransformationSettings bg2;
    BgTransformationSettings bg3;
    u16 window0HorizontalDimensions;
    u16 window1HorizontalDimensions;
    u16 window0VerticalDimensions;
    u16 window1VerticalDimensions;
    u16 windowInsideControl;
    u16 windowOutsideControl;
    u16 mosaicSize;
    u16 layerFXControl;
    u16 alphaBlend;
    u16 layerBrightness;
} BgControls;

typedef struct {
    bool8 ready;
    bool8 readyBackup;
    u16 unused;
    u16* src;
    u16* dest;
    u32 size;
} VBlankDMA;

typedef struct {
    /* GBA / PC64 offsets */
    /*0x00 / 0x00*/ LcdControls lcd;
    /*0x08 / 0x08*/ BgSettings bg0;
    /*0x14 / 0x18*/ BgSettings bg1;
    /*0x20 / 0x28*/ BgAffSettings bg2;
    /*0x2c / 0x38*/ BgAffSettings bg3;
    /*0x38 / 0x48*/ BgControls controls;
    /*0x6c / 0x80*/ VBlankDMA vBlankDMA;
} Screen;

PORT_STATIC_ASSERT_SIZE(BgSettings,    0x0C, 0x10, "BgSettings size drift");
PORT_STATIC_ASSERT_SIZE(BgAffSettings, 0x0C, 0x10, "BgAffSettings size drift");
PORT_STATIC_ASSERT_SIZE(VBlankDMA,     0x10, 0x20, "VBlankDMA size drift");
PORT_STATIC_ASSERT_OFFSET(BgSettings,    subTileMap, 0x08, 0x08, "BgSettings.subTileMap");
PORT_STATIC_ASSERT_OFFSET(BgAffSettings, subTileMap, 0x08, 0x08, "BgAffSettings.subTileMap");
PORT_STATIC_ASSERT_OFFSET(VBlankDMA,     src,        0x04, 0x08, "VBlankDMA.src");
PORT_STATIC_ASSERT_OFFSET(VBlankDMA,     dest,       0x08, 0x10, "VBlankDMA.dest");
PORT_STATIC_ASSERT_OFFSET(VBlankDMA,     size,       0x0C, 0x18, "VBlankDMA.size");
PORT_STATIC_ASSERT_OFFSET(Screen, bg0,       0x08, 0x08, "Screen.bg0");
PORT_STATIC_ASSERT_OFFSET(Screen, bg1,       0x14, 0x18, "Screen.bg1");
PORT_STATIC_ASSERT_OFFSET(Screen, bg2,       0x20, 0x28, "Screen.bg2");
PORT_STATIC_ASSERT_OFFSET(Screen, bg3,       0x2c, 0x38, "Screen.bg3");
PORT_STATIC_ASSERT_OFFSET(Screen, controls,  0x38, 0x48, "Screen.controls");
PORT_STATIC_ASSERT_OFFSET(Screen, vBlankDMA, 0x6c, 0x80, "Screen.vBlankDMA");
PORT_STATIC_ASSERT_SIZE(Screen, 0x7C, 0xA0, "Screen size drift");

#ifndef OAM_COMMAND_DEFINED
#define OAM_COMMAND_DEFINED
typedef struct {
    s16 x;
    s16 y;
    u16 _4;
    u16 _6;
    u16 _8;
} OAMCommand;
#endif

extern Screen gScreen;
extern OAMCommand gOamCmd;

extern void sub_080ADA04(OAMCommand*, void*);

#endif // SCREEN_H
