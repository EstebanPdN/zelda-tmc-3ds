/**
 * @file rainfallManager.c
 * @ingroup Managers
 *
 * @brief Rainfall on the crenel minish path.
 */
#include "manager/rainfallManager.h"
#include "asm.h"
#include "room.h"
#include "enemy.h"

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
#include "port_widescreen.h"
#endif

void RainfallManager_Init(RainfallManager*);
void RainfallManager_Action1(RainfallManager*);

void RainfallManager_Main(RainfallManager* this) {
    static void (*const RainfallManager_Actions[])(RainfallManager*) = {
        RainfallManager_Init,
        RainfallManager_Action1,
    };
    RainfallManager_Actions[super->action](this);
}

// Initial setup
void RainfallManager_Init(RainfallManager* this) {
    super->action = 1;
    super->timer = 1;
}

// Spawn water drops every 0xf frames
void RainfallManager_Action1(RainfallManager* this) {
    static const s16 gUnk_08108C6C[] = { -0x64, -0x3c, -0x14, 0x14, 0x3c, 0x64, 0x8c, 0xb4 };
    static const s8 gUnk_08108C7C[] = { -0x32, -0x19, 0x0, 0x19 };

    if (--super->timer == 0) {
        Entity* waterDrop;
        super->timer = 15;
        waterDrop = CreateEnemy(WATER_DROP, 0);
        if (waterDrop != NULL) {
            const u32 column = Random() & 7;
            const u32 row = Random() & 3;
#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
            const int viewWidth = Port_Widescreen_GameplayViewWidth();
            const int viewHeight = Port_Widescreen_GameplayViewHeight();
            if (viewWidth != DISPLAY_WIDTH || viewHeight != DISPLAY_HEIGHT) {
                /* Preserve the retail eight-column/four-row distribution,
                 * expanding its off-screen lead-in with the live viewport. */
                waterDrop->x.HALF.HI = gRoomControls.scroll_x + 0x14 + column * ((viewWidth + 0x50) / 8);
                waterDrop->y.HALF.HI = gRoomControls.scroll_y + 0x1e + row * ((viewHeight - 0x3c) / 4);
            } else
#endif
            {
                waterDrop->x.HALF.HI = gRoomControls.scroll_x + 0x78 + gUnk_08108C6C[column];
                waterDrop->y.HALF.HI = gRoomControls.scroll_y + 0x50 + gUnk_08108C7C[row];
            }
            waterDrop->z.HALF.HI = 0xff38;
        }
    }
}
