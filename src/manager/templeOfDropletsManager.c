/**
 * @file templeOfDropletsManager.c
 * @ingroup Managers
 *
 * @brief Various managers in Temple of Droplets, e.g. Light rays
 */
#include "manager/templeOfDropletsManager.h"
#include "enemy.h"
#include "flags.h"
#include "common.h"
#include "object.h"
#ifdef PC_PORT
#include <stdio.h>
#endif
#include "room.h"
#include "player.h"
#include "screen.h"
#include "sound.h"
#include "game.h"
#include "vram.h"
#include "pauseMenu.h"
#include "asm.h"

static const u16 gUnk_081085B8[] = { 0x1008, 0x1007, 0x1006, 0x1005, 0x1006, 0x1007, 0x1008, 0x1009,
                                     0x1008, 0x1007, 0x1006, 0x1005, 0x1006, 0x1007, 0x1008, 0x1009 };

void TempleOfDropletsManager_Type0(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type5(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type6(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type7(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1_Action1(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1_Action2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1_Action3(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1_Action4(TempleOfDropletsManager*);
void sub_0805A89C(TempleOfDropletsManager*);
void sub_0805A8EC(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type2_Action4(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type2_Action3(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type2_Action2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type1_Action1(TempleOfDropletsManager*);
void sub_0805AAF0(u32);
void sub_0805AA58(TempleOfDropletsManager*);
void sub_0805A9CC(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4_Action1(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4_Action2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4_Action3(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4_Action4(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type4_Action5(TempleOfDropletsManager*);
void sub_0805A68C(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type6_Action3(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type6_Action2(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type6_Action1(TempleOfDropletsManager*);
void TempleOfDropletsManager_Type6_Init(TempleOfDropletsManager*);

void TempleOfDropletsManager_Main(TempleOfDropletsManager* this) {
    static void (*const TempleOfDropletsManager_Types[])(TempleOfDropletsManager*) = {
        TempleOfDropletsManager_Type0, TempleOfDropletsManager_Type1, TempleOfDropletsManager_Type2,
        TempleOfDropletsManager_Type2, TempleOfDropletsManager_Type4, TempleOfDropletsManager_Type5,
        TempleOfDropletsManager_Type6, TempleOfDropletsManager_Type7,
    };
    TempleOfDropletsManager_Types[super->type](this);
}

void TempleOfDropletsManager_Type0(TempleOfDropletsManager* this) {
    if (super->action == 0) {
        sub_0805A89C(this);
        if (!CheckLocalFlag(this->localFlag)) {
            this->unk_23 = 1;
        } else {
            this->unk_23 = 2;
        }
        sub_0805AAF0(this->unk_23);
        gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
    }
#ifdef PC_PORT
    /* Issue #75: scroll.c and various overlay managers clear WIN1 / BG3
     * from displayControl per-frame on PC. Original GBA Type 0 set the
     * bits once at init and trusted them to stick. Re-set every frame
     * so the sunbeam stays visible. */
    gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
#endif
    sub_0805A8EC(this);
}

void TempleOfDropletsManager_Type1(TempleOfDropletsManager* this) {
    static void (*const TempleOfDropletsManager_Type1_Actions[])(TempleOfDropletsManager*) = {
        NULL,
        TempleOfDropletsManager_Type1_Action1,
        TempleOfDropletsManager_Type1_Action2,
        TempleOfDropletsManager_Type1_Action3,
        TempleOfDropletsManager_Type1_Action4,
    };
    if (super->action == 0) {
        sub_0805A89C(this);
        this->unk_23 = 1;
        sub_0805AAF0(1);
        if (!CheckLocalFlag(this->localFlag)) {
            super->action = 1;
            gScreen.lcd.displayControl &= 0xB7FF;
        } else {
            if (CheckLocalFlagB(0x48)) {
                super->action = 4;
            } else {
                super->action = 3;
            }
            gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
        }
    } else {
        TempleOfDropletsManager_Type1_Actions[super->action](this);
    }
    if (super->next) {
        sub_0805A8EC(this);
    }
}

extern Entity gUnk_080E4C08;

void TempleOfDropletsManager_Type1_Action2(TempleOfDropletsManager* this) {
    if (super->subAction != 0) {
        LoadRoomEntityList((EntityData*)&gUnk_080E4C08);
        super->action = 3;
        SoundReq(SONG_STOP_BGM);
    }
}

void TempleOfDropletsManager_Type1_Action3(TempleOfDropletsManager* this) {
    Entity* ent;
    if ((ent = FindEntityByID(ENEMY, CHUCHU_BOSS, 0x4))) {
        if (ent->type != 4) {
            return;
        }
    }
    SetLocalFlag(LV1_16_1STEND);
    ClearRoomFlag(0);
    gScreen.lcd.displayControl &= ~(DISPCNT_WIN1_ON | DISPCNT_BG3_ON);
    DeleteThisEntity();
}

void TempleOfDropletsManager_Type1_Action4(TempleOfDropletsManager* this) {
}

void TempleOfDropletsManager_Type2(TempleOfDropletsManager* this) {
    static void (*const TempleOfDropletsManager_Type2_Actions[])(TempleOfDropletsManager*) = {
        NULL,
        TempleOfDropletsManager_Type1_Action1,
        TempleOfDropletsManager_Type2_Action2,
        TempleOfDropletsManager_Type2_Action3,
        TempleOfDropletsManager_Type2_Action4,
    };
    if (super->action == 0) {
        sub_0805A89C(this);
        if (super->type == 3) {
            this->unk_23 = 2;
        } else {
            this->unk_23 = 1;
        }
        sub_0805AAF0(this->unk_23);
        if (!CheckLocalFlag(this->localFlag)) {
            ClearFlag(this->flag);
            super->action = 1;
            gScreen.lcd.displayControl &= ~(DISPCNT_WIN1_ON | DISPCNT_BG3_ON);
        } else {
            SetFlag(this->flag);
            super->action = 3;
            gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
        }
    } else {
        TempleOfDropletsManager_Type2_Actions[super->action](this);
    }
#ifdef PC_PORT
    /* Issue #75: scroll.c::Scroll0 (line 420) clears DISPCNT_WIN1_ON on
     * every scroll-end on PC; on GBA the bits set at init/action-3 stick
     * but here they get nuked whenever the camera settles. When the
     * puzzle is in the "beam visible" states (action >= 3, i.e. after
     * the player pushed the lever and the LightRay animation completed),
     * re-assert both bits every frame so the sunbeam keeps rendering. */
    if (super->action >= 3) {
        gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
    }
#endif
    sub_0805A8EC(this);
}

void sub_0805A4CC(TempleOfDropletsManager*, u32);

void TempleOfDropletsManager_Type1_Action1(TempleOfDropletsManager* this) {
    if (CheckLocalFlag(this->localFlag)) {
        super->action = 2;
        sub_0805A4CC(this, 0);
    }
}

void TempleOfDropletsManager_Type2_Action2(TempleOfDropletsManager* this) {
    if (super->subAction != 0) {
        super->action = 3;
        SetFlag(this->flag);
    }
}

void TempleOfDropletsManager_Type2_Action3(TempleOfDropletsManager* this) {
    if (!CheckLocalFlag(this->localFlag)) {
        super->action = 4;
        sub_0805A4CC(this, 1);
    }
}

void TempleOfDropletsManager_Type2_Action4(TempleOfDropletsManager* this) {
    if (super->subAction != 0) {
        super->action = 1;
        ClearFlag(this->flag);
    }
}

void sub_0805A4CC(TempleOfDropletsManager* this, u32 unk_0) {
    Entity* tmp;
    tmp = CreateObject(LIGHT_RAY, unk_0, 0);
    if (tmp) {
        tmp->x.HALF.HI = this->unk_38 + gRoomControls.origin_x;
        tmp->y.HALF.HI = this->unk_3a + gRoomControls.origin_y - 0x30;
        tmp->parent = (Entity*)this;
        super->subAction = 0;
    }
}

void TempleOfDropletsManager_Type4(TempleOfDropletsManager* this) {
    static void (*const TempleOfDropletsManager_Type4_Actions[])(TempleOfDropletsManager*) = {
        NULL,
        TempleOfDropletsManager_Type4_Action1,
        TempleOfDropletsManager_Type4_Action2,
        TempleOfDropletsManager_Type4_Action3,
        TempleOfDropletsManager_Type4_Action4,
        TempleOfDropletsManager_Type4_Action5,
    };
    if (super->action == 0) {
        sub_0805A89C(this);
        this->unk_28 = 0;
        if (CheckLocalFlagB(0x36)) {
            this->unk_23 = 3;
            super->action = 5;
            gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
        } else {
            if (CheckLocalFlagB(0x34)) {
                this->unk_23 = 4;
                super->action = 3;
                gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
            } else {
                this->unk_23 = 4;
                super->action = 1;
                gScreen.lcd.displayControl &= ~(DISPCNT_WIN1_ON | DISPCNT_BG3_ON);
            }
        }
        sub_0805AAF0(this->unk_23);
    } else {
        TempleOfDropletsManager_Type4_Actions[super->action](this);
    }
    sub_0805AA58(this);
    sub_0805A9CC(this);
    if (gRoomControls.reload_flags == 1) {
        gScreen.lcd.displayControl |= DISPCNT_WIN1_ON;
        super->subAction = 1;
        return;
    }
    if (super->subAction == 0)
        return;
    super->subAction = 0;
    if (this->unk_20 == gRoomControls.room)
        return;
    gScreen.lcd.displayControl &= ~(DISPCNT_WIN1_ON | DISPCNT_BG3_ON);
    DeleteThisEntity();
}

void TempleOfDropletsManager_Type4_Action1(TempleOfDropletsManager* this) {
    if (CheckLocalFlagB(0x34)) {
        super->action = 2;
        sub_0805A4CC(this, 2);
    }
}

void TempleOfDropletsManager_Type4_Action2(TempleOfDropletsManager* this) {
    if (super->subAction != 0) {
        super->action = 3;
    }
}

void TempleOfDropletsManager_Type4_Action3(TempleOfDropletsManager* this) {
    if (CheckLocalFlagB(0x36)) {
        super->action = 4;
        this->unk_23 = 3;
        sub_0805A4CC(this, 3);
    }
}

void TempleOfDropletsManager_Type4_Action4(TempleOfDropletsManager* this) {
    if (super->subAction != 0) {
        super->action = 5;
    }
}

void TempleOfDropletsManager_Type4_Action5(TempleOfDropletsManager* this) {
}

void TempleOfDropletsManager_Type5(TempleOfDropletsManager* this) {
}

void TempleOfDropletsManager_Type6(TempleOfDropletsManager* this) {
    static void (*const TempleOfDropletsManager_Type6_Actions[])(TempleOfDropletsManager*) = {
        TempleOfDropletsManager_Type6_Init,
        TempleOfDropletsManager_Type6_Action1,
        TempleOfDropletsManager_Type6_Action2,
        TempleOfDropletsManager_Type6_Action3,
    };
    TempleOfDropletsManager_Type6_Actions[super->action](this);
    sub_0805A8EC(this);
    sub_0805A68C(this);
}

void sub_0805A68C(TempleOfDropletsManager* this) {
    if (CheckPlayerProximity(this->unk_38 - 0x18, this->unk_3a - 0x18, 0x30, 0x30)) {
        if (this->unk_2c <= 0x1007) {
            gScreen.controls.alphaBlend = ++this->unk_2c;
            this->unk_22 = 0;
        }
    } else {
        if (this->unk_2c > 0x1000) {
            this->unk_2c--;
        }
        gScreen.controls.alphaBlend = this->unk_2c;
    }
}

u32 sub_0805A73C(TempleOfDropletsManager*);

void TempleOfDropletsManager_Type6_Init(TempleOfDropletsManager* this) {
    u16 tmp;
    sub_0805A89C(this);
    this->unk_23 = 0;
    sub_0805AAF0(0);
    this->unk_38 += gRoomControls.origin_x;
    this->unk_3a += gRoomControls.origin_y;
    if (!sub_0805A73C(this)) {
        super->action++;
    }
    this->unk_2c = 0x1000;
    tmp = gScreen.lcd.displayControl | DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
    gScreen.lcd.displayControl = tmp;
}

u32 sub_0805A73C(TempleOfDropletsManager* this) {
    return CheckPlayerProximity(this->unk_38 - 6, this->unk_3a - 6, 12, 12);
}

void TempleOfDropletsManager_Type6_Action1(TempleOfDropletsManager* this) {
    if (!sub_0805A73C(this)) {
        super->action++;
    }
}

void TempleOfDropletsManager_Type6_Action2(TempleOfDropletsManager* this) {
    if ((gPlayerEntity.base.health != 0) && (gPlayerEntity.base.z.HALF.HI == 0) && (!gPlayerState.item)) {
        switch (gPlayerState.framestate_last) {
            case PL_STATE_IDLE:
            case PL_STATE_WALK:
                if (sub_0805A73C(this)) {
                    super->action++;
                    SnapToTile(&gPlayerEntity.base);
                    gPlayerEntity.base.animationState = 4;
                    RequestPriorityDuration((Entity*)this, 600);
                    SetPlayerControl(0xFF);
                    gPauseMenuOptions.disabled = 1;
                    gRoomControls.camera_target = NULL;
                    PutAwayItems();
                }
        }
    }
}

void TempleOfDropletsManager_Type6_Action3(TempleOfDropletsManager* this) {
    if (gPlayerState.queued_action != PLAYER_WARP) {
        gPlayerState.queued_action = PLAYER_WARP;
        gPlayerState.field_0x38 = 0;
        gPlayerState.field_0x39 = 0;
    }
}

void TempleOfDropletsManager_Type7(TempleOfDropletsManager* this) {
    switch (super->action) {
        case 0:
            if (CheckLocalFlag(this->localFlag)) {
                super->action = 1;
            } else {
                super->action = 2;
            }
            super->subAction = 1;
            super->flags |= ENT_PERSIST;
            SetEntityPriority((Entity*)this, PRIO_PLAYER_EVENT);
            break;
        case 1:
            if (CheckLocalFlag(this->localFlag))
                break;
            super->action = 2;
            sub_0805A4CC(this, 4);
            break;
        case 2:
            if (super->subAction != 0) {
                super->action = 3;
            }
            break;
        case 3:
            if (!CheckLocalFlag(this->localFlag))
                break;
            super->action = 4;
            sub_0805A4CC(this, 4);
            break;
        case 4:
            if (super->subAction != 0) {
                super->action = 1;
            }
            break;
        default:
            DeleteThisEntity();
    }
}

void TempleOfDropletsManager_OnEnterRoom(TempleOfDropletsManager*);

void sub_0805A89C(TempleOfDropletsManager* this) {
    SetEntityPriority((Entity*)this, PRIO_PLAYER_EVENT);
    super->action = 1;
    super->flags |= ENT_PERSIST;
    super->timer = 8;
    super->subtimer = 16;
    this->unk_21 = 0;
    this->unk_22 = 0;
    this->unk_20 = gRoomControls.room;
    this->unk_24 = gRoomControls.origin_x;
    this->unk_26 = gRoomControls.origin_y;
    RegisterTransitionHandler(this, TempleOfDropletsManager_OnEnterRoom, NULL);
}

void sub_0805A94C(TempleOfDropletsManager* this);

void sub_0805A8EC(TempleOfDropletsManager* this) {
#ifdef PC_PORT
    /* Issue #75 diagnostic: dump every ~2 seconds while the manager is
     * alive. The previous version filtered on super->id == 0x2e but that
     * never matched — Manager.id for a TempleOfDropletsManager is
     * TEMPLE_OF_DROPLETS_MANAGER (= 21 = 0x15), 0x2e is TILE_PUZZLE_MANAGER.
     * The filter silently suppressed every log line, leaving the
     * sunbeam-not-rendering chase blind. */
    if (getenv("TMC_TRACE_BEAM")) {
        extern u16 gPaletteBuffer[];
        static u32 frame_counter = 0;
        if ((frame_counter++ % 120u) == 0u) {
            u32 i;
            u32 bg3_nonzero = 0;
            for (i = 0; i < sizeof(gBG3Buffer) / sizeof(gBG3Buffer[0]); ++i) {
                if (gBG3Buffer[i]) ++bg3_nonzero;
            }
            fprintf(stderr,
                "[ToD-mgr] sub_0805A8EC type=%u action=%u subAction=%u unk_20=%u "
                "room=%u reload=%u localFlag=0x%04x flag=0x%04x "
                "unk_23=%u unk_34=%d unk_36=%d "
                "dispCtl=0x%04x bg3.ctl=0x%04x bg3.off=(%d,%d) bg3.upd=%u "
                "win1H=0x%04x win1V=0x%04x pal5[0..3]=%04x %04x %04x %04x "
                "gBG3Buffer nonzero=%u/%u\n",
                (unsigned)super->type, (unsigned)super->action,
                (unsigned)super->subAction, (unsigned)this->unk_20,
                (unsigned)gRoomControls.room, (unsigned)gRoomControls.reload_flags,
                (unsigned)this->localFlag, (unsigned)this->flag,
                (unsigned)this->unk_23, (int)this->unk_34, (int)this->unk_36,
                (unsigned)gScreen.lcd.displayControl,
                (unsigned)gScreen.bg3.control,
                (int)(s16)gScreen.bg3.xOffset, (int)(s16)gScreen.bg3.yOffset,
                (unsigned)gScreen.bg3.updated,
                (unsigned)gScreen.controls.window1HorizontalDimensions,
                (unsigned)gScreen.controls.window1VerticalDimensions,
                (unsigned)gPaletteBuffer[5 * 16 + 0],
                (unsigned)gPaletteBuffer[5 * 16 + 1],
                (unsigned)gPaletteBuffer[5 * 16 + 2],
                (unsigned)gPaletteBuffer[5 * 16 + 3],
                (unsigned)bg3_nonzero,
                (unsigned)(sizeof(gBG3Buffer) / sizeof(gBG3Buffer[0])));
        }
    }
#endif
    sub_0805AA58(this);
    sub_0805A94C(this);
    if (gRoomControls.reload_flags == 1) {
#ifdef PC_PORT
        /* Issue #75: GBA original only sets WIN1 here, relying on BG3
         * having been set at init and persisting. On PC, scroll.c
         * (line 420) clears WIN1 on scroll-end and some overlay
         * managers (steamOverlayManager, cloudOverlayManager) clear
         * BG3 on room transitions; the per-frame reload-branch needs
         * to re-enable BOTH so the sunbeam stays visible across all
         * TempleOfDroplets Types (every Type's update chain ends at
         * sub_0805A8EC). */
        gScreen.lcd.displayControl |= DISPCNT_WIN1_ON | DISPCNT_BG3_ON;
#else
        gScreen.lcd.displayControl |= DISPCNT_WIN1_ON;
#endif
        super->subAction = 1;
    } else {
        if (super->subAction == 0)
            return;
        super->subAction = 0;
        if (this->unk_20 == gRoomControls.room)
            return;
        gScreen.lcd.displayControl &= ~(DISPCNT_WIN1_ON | DISPCNT_BG3_ON);
        DeleteThisEntity();
    }
}

void sub_0805A94C(TempleOfDropletsManager* this) {
    int tmp1, tmp2;
    gScreen.bg3.xOffset = gRoomControls.scroll_x - this->unk_24 + this->unk_34;
    gScreen.bg3.yOffset = gRoomControls.scroll_y - this->unk_26 + this->unk_36;
    tmp1 = -gScreen.bg3.xOffset;
    tmp2 = tmp1 + 0x100;
    if (tmp1 < 0)
        tmp1 = 0;
    if (tmp1 > DISPLAY_WIDTH)
        tmp1 = DISPLAY_WIDTH;
    if (tmp2 < 0)
        tmp2 = 0;
    if (tmp2 > DISPLAY_WIDTH)
        tmp2 = DISPLAY_WIDTH;
    gScreen.controls.window1HorizontalDimensions = (tmp1 << 8 | tmp2);
    tmp1 = -gScreen.bg3.yOffset;
    tmp2 = tmp1 + 0x100;
    if (tmp1 < 0)
        tmp1 = 0;
    if (tmp1 > DISPLAY_HEIGHT)
        tmp1 = DISPLAY_HEIGHT;
    if (tmp2 < 0)
        tmp2 = 0;
    if (tmp2 > DISPLAY_HEIGHT)
        tmp2 = DISPLAY_HEIGHT;
    gScreen.controls.window1VerticalDimensions = (tmp1 << 8 | tmp2);
}

void sub_0805A9CC(TempleOfDropletsManager* this) {
    int tmp1, tmp2;
    void* tmp3;
    gScreen.bg3.xOffset = gRoomControls.scroll_x - this->unk_24 + this->unk_34;
    tmp1 = -gScreen.bg3.xOffset;
    tmp2 = tmp1 + 0x100;
    if (tmp1 < 0)
        tmp1 = 0;
    if (tmp1 > DISPLAY_WIDTH)
        tmp1 = DISPLAY_WIDTH;
    if (tmp2 < 0)
        tmp2 = 0;
    if (tmp2 > DISPLAY_WIDTH)
        tmp2 = DISPLAY_WIDTH;
    gScreen.controls.window1HorizontalDimensions = tmp1 << 8 | tmp2;
    tmp1 = gRoomControls.scroll_y - this->unk_26 + this->unk_36;
    gScreen.bg3.yOffset = tmp1 & 0x3F;
    tmp3 = (&gBG3Buffer[((tmp1 / 0x40) << 8)]);
    gScreen.bg3.subTileMap = (u32*)tmp3;
    gScreen.controls.window1VerticalDimensions = DISPLAY_HEIGHT;
    if (this->unk_28 == tmp3)
        return;
    this->unk_28 = tmp3;
    gScreen.bg3.updated = 1;
}

extern struct { u8 unk_00[0x20]; } gUnk_085A97A0[];

void sub_0805AA58(TempleOfDropletsManager* this) {
    if (--super->timer == 0) {
        super->timer = 8;
        this->unk_21++;
        this->unk_21 &= 3;
        LoadPalettes(gUnk_085A97A0[this->unk_21].unk_00, 5, 1);
    }
    if (--super->subtimer == 0) {
        super->subtimer = 16;
        this->unk_22++;
        this->unk_22 &= 0xF;
        gScreen.controls.alphaBlend = gUnk_081085B8[this->unk_22];
    }
}

void TempleOfDropletsManager_OnEnterRoom(TempleOfDropletsManager* this) {
    sub_0805AAF0(this->unk_23);
    TempleOfDropletsManager_Main(this);
}

void sub_0805AADC(u32 unk0) {
    static const u16 gUnk_08108648[] = {
        0x31, 0x32, 0x33, 0x34, 0x35, 0,
    };
    LoadGfxGroup(gUnk_08108648[unk0]);
}

void sub_0805AAF0(u32 unk0) {
    sub_0805AADC(unk0);
    gScreen.controls.layerFXControl = 0x3E48;
    gScreen.controls.alphaBlend = BLDALPHA_BLEND(8, 16);
    gScreen.bg3.control = BGCNT_SCREENBASE(30) | BGCNT_CHARBASE(1);
    gScreen.bg3.subTileMap = &gBG3Buffer;
    gScreen.bg3.xOffset = 0;
    gScreen.bg3.yOffset = 0;
    gScreen.bg3.updated = 1;
    gScreen.controls.windowInsideControl =
        WININ_WIN0_BG_ALL | WININ_WIN0_OBJ | WININ_WIN1_BG_ALL | WININ_WIN1_OBJ | WININ_WIN0_CLR | WININ_WIN1_CLR;
    gScreen.controls.windowOutsideControl =
        WINOUT_WIN01_BG0 | WINOUT_WIN01_BG1 | WINOUT_WIN01_BG2 | WINOUT_WIN01_OBJ | WINOUT_WIN01_CLR;
    gScreen.controls.window1HorizontalDimensions = DISPLAY_WIDTH;
    gScreen.controls.window1VerticalDimensions = DISPLAY_HEIGHT;
}
