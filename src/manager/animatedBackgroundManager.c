/**
 * @file animatedBackgroundManager.c
 * @ingroup Managers
 *
 * @brief Set up bg3 for cloud tops, vaati2 and some beanstalks.
 */
#ifdef PC_PORT
#include "area.h"
#include "beanstalkSubtask.h"
#endif
#include "manager/animatedBackgroundManager.h"
#include "room.h"
#include "screen.h"

void AnimatedBackgroundManager_Init(AnimatedBackgroundManager*);
void AnimatedBackgroundManager_Action1(AnimatedBackgroundManager*);
void AnimatedBackgroundManager_RestoreBgGfx(AnimatedBackgroundManager*);
static void AnimatedBackgroundManager_SetBgControls(AnimatedBackgroundManager*);
#ifdef PC_PORT
static void AnimatedBackgroundManager_ReloadBgGfx(AnimatedBackgroundManager*);
#endif

void AnimatedBackgroundManager_Main(AnimatedBackgroundManager* this) {
    static void (*const AnimatedBackgroundManager_Actions[])(AnimatedBackgroundManager*) = {
        AnimatedBackgroundManager_Init,
        AnimatedBackgroundManager_Action1,
    };
    AnimatedBackgroundManager_Actions[super->action](this);
}

static const u16 gAnimatedBackgroundControls[] = { 0x1e07, 0x1e07 };

void AnimatedBackgroundManager_Init(AnimatedBackgroundManager* this) {
    super->action = 1;

    AnimatedBackgroundManager_SetBgControls(this);
    gRoomControls.bg3OffsetY.WORD = 0;
    gRoomControls.bg3OffsetX.WORD = 0;

    switch (super->type) {
        case 0:
        default:
            gScreen.bg3.yOffset = 0;
            gScreen.bg3.xOffset = 0;
            break;
        case 1:
            gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
            gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
            break;
    }
}

void AnimatedBackgroundManager_Action1(AnimatedBackgroundManager* this) {
#ifdef PC_PORT
    if (super->subtimer != 0) {
        super->subtimer--;
        AnimatedBackgroundManager_ReloadBgGfx(this);
        AnimatedBackgroundManager_SetBgControls(this);
    }
#endif
    if (super->type == 1) {
        gRoomControls.bg3OffsetX.WORD = gRoomControls.bg3OffsetX.WORD + 0x2000;
        gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
        gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
    }
}

static void AnimatedBackgroundManager_SetBgControls(AnimatedBackgroundManager* this) {
    gScreen.lcd.displayControl |= DISPCNT_BG3_ON;
    gScreen.bg3.control = gAnimatedBackgroundControls[super->type];
#ifdef PC_PORT
    gScreen.bg3.updated = 0;
    gScreen.bg3.subTileMap = NULL;
#endif
}

#ifdef PC_PORT
static void AnimatedBackgroundManager_ReloadBgGfx(AnimatedBackgroundManager* this) {
    if (gArea.pCurrentRoomInfo != NULL && gArea.pCurrentRoomInfo->tileSet != NULL) {
        LoadMapData(gArea.pCurrentRoomInfo->tileSet);
    }
}
#endif

void AnimatedBackgroundManager_RestoreBgGfx(AnimatedBackgroundManager* this) {
#ifdef PC_PORT
    /*
     * RestoreGameTask can complete before the port's next BG DMA pass. Keep
     * the direct BG3 screenblock alive for a couple of gameplay frames so a
     * stale menu BG3 update cannot overwrite it again.
     */
    super->subtimer = 2;
    AnimatedBackgroundManager_ReloadBgGfx(this);
#endif
    AnimatedBackgroundManager_SetBgControls(this);
}
