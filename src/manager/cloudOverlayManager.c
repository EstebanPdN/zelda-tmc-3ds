/**
 * @file cloudOverlayManager.c
 * @ingroup Managers
 *
 * @brief Cloud bg overlay for Hyrule Fields.
 */
#include "manager/cloudOverlayManager.h"
#include "area.h"
#include "screen.h"
#include "game.h"
#include "room.h"
#include "common.h"
#ifdef PC_PORT
#include <stdio.h>
#include "port_gba_mem.h"
#endif

void CloudOverlayManager_OnEnterRoom(CloudOverlayManager*);
void CloudOverlayManager_OnExitRoom(CloudOverlayManager*);

void CloudOverlayManager_Main(CloudOverlayManager* this) {
    static const u16 gUnk_0810865C[] = {
        0xf01, 0xe02, 0xe03, 0xe04, 0, 0,
    };
    if (this == NULL) {
        if (gArea.onEnter != CloudOverlayManager_OnEnterRoom) {
            CloudOverlayManager_OnEnterRoom(NULL);
        }
    } else {
        if (super->action == 0) {
            super->action = 1;
            super->flags |= ENT_PERSIST;
            super->timer = 0;
            super->subtimer = 8;
            this->field_0x20 = gUnk_0810865C[0];
            SetEntityPriority((Entity*)this, PRIO_PLAYER_EVENT);
            if (gArea.onEnter == NULL) {
                RegisterTransitionHandler(this, CloudOverlayManager_OnEnterRoom, CloudOverlayManager_OnExitRoom);
            } else {
                DeleteThisEntity();
            }
        } else {
            if ((gUnk_0810865C[super->timer] != 0) && (--super->subtimer == 0)) {
                super->subtimer = 4;
                if (gUnk_0810865C[++super->timer] != 0) {
                    this->field_0x20 = gUnk_0810865C[super->timer];
                    gScreen.controls.alphaBlend = this->field_0x20;
                }
            }
            gRoomControls.bg3OffsetX.WORD -= 0x2000;
            gRoomControls.bg3OffsetY.WORD -= 0x1000;
            gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
            gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
        }
    }
}

#ifdef PC_PORT
/* Snapshot of the BG3 cloud-overlay VRAM regions captured the first time
 * we enter Hyrule Field with a working overlay (CHARBASE 1 chardata +
 * SCREENBASE 30 tilemap). Hyrule Castle's parallax overwrites these on
 * area transition; on PC the asset loader doesn't re-run the originating
 * gfx-group load, so the cloud data is gone. Restore from this snapshot
 * on every Hyrule Field room enter so the overlay works after castle
 * exit (#25). */
static u8 sCloudCharSnapshot[0x2000];
static u8 sCloudScreenSnapshot[0x800];
static int sCloudSnapshotValid = 0;
#endif

void CloudOverlayManager_OnEnterRoom(CloudOverlayManager* this) {
#ifdef PC_PORT
    if (gRoomControls.area == AREA_HYRULE_FIELD) {
        if (!sCloudSnapshotValid) {
            /* First time we see a working cloud overlay — capture it. */
            MemCopy((void*)0x6004000, sCloudCharSnapshot, sizeof(sCloudCharSnapshot));
            MemCopy((void*)0x600F000, sCloudScreenSnapshot, sizeof(sCloudScreenSnapshot));
            sCloudSnapshotValid = 1;
            {
                u32 nonzero = 0;
                u8* vram = (u8*)gba_TryMemPtr(0x6004000);
                if (vram) {
                    for (u32 i = 0; i < 0x2000; i++)
                        if (vram[i]) nonzero++;
                }
                fprintf(stderr, "[CLOUD] snapshot captured: chardata non-zero=%u/8192, first16=", nonzero);
                if (vram) {
                    for (int i = 0; i < 16; i++)
                        fprintf(stderr, "%02X ", vram[i]);
                }
                fprintf(stderr, "\n");
                u8* tmap = (u8*)gba_TryMemPtr(0x600F000);
                u32 tnz = 0;
                if (tmap) {
                    for (u32 i = 0; i < 0x800; i++)
                        if (tmap[i]) tnz++;
                }
                fprintf(stderr, "[CLOUD] tilemap non-zero=%u/2048\n", tnz);
            }
        } else {
            /* Subsequent entries: restore. Castle may have trashed the data. */
            MemCopy(sCloudCharSnapshot, (void*)0x6004000, sizeof(sCloudCharSnapshot));
            MemCopy(sCloudScreenSnapshot, (void*)0x600F000, sizeof(sCloudScreenSnapshot));
        }
    }
#endif
    gScreen.bg3.control = BGCNT_SCREENBASE(30) | BGCNT_PRIORITY(1) | BGCNT_CHARBASE(1);
    gScreen.lcd.displayControl |= DISPCNT_BG3_ON;
    gScreen.controls.layerFXControl =
        BLDCNT_TGT1_BG3 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG1 | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD;
    gScreen.controls.alphaBlend = (this != NULL) ? this->field_0x20 : BLDALPHA_BLEND(0, 16);
    gScreen.bg3.xOffset = gRoomControls.scroll_x + gRoomControls.bg3OffsetX.HALF.HI;
    gScreen.bg3.yOffset = gRoomControls.scroll_y + gRoomControls.bg3OffsetY.HALF.HI;
    if (this != NULL) {
        CloudOverlayManager_Main(this);
    }
}

void CloudOverlayManager_OnExitRoom(CloudOverlayManager* this) {
    super->flags &= ~ENT_PERSIST;
    gScreen.lcd.displayControl &= ~DISPCNT_BG3_ON;
    gScreen.controls.layerFXControl = 0;
}
