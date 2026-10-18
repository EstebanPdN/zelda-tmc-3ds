/**
 * @file minishPortalCloseup.c
 * @ingroup Objects
 *
 * @brief Minish Portal Closeup object
 */
#include "area.h"
#include "main.h"
#include "object.h"
#include "common.h"
#include "room.h"
#include "screen.h"
#include "fade.h"

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u16 unk_68;
} MinishPortalCloseupEntity;

void MinishPortalCloseup_Init(MinishPortalCloseupEntity*);
void MinishPortalCloseup_Action1(MinishPortalCloseupEntity*);
void MinishPortalCloseup_Action2(MinishPortalCloseupEntity*);
void sub_0808D030(void);

void MinishPortalCloseup(MinishPortalCloseupEntity* this) {
    static void (*const MinishPortalCloseup_Actions[])(MinishPortalCloseupEntity*) = {
        MinishPortalCloseup_Init,
        MinishPortalCloseup_Action1,
        MinishPortalCloseup_Action2,
    };
    MinishPortalCloseup_Actions[super->action](this);
}

void MinishPortalCloseup_Init(MinishPortalCloseupEntity* this) {
    super->action = 1;
    super->x.HALF.HI = gArea.portal_x - gRoomControls.scroll_x;
    super->y.HALF.HI = gArea.portal_y - gRoomControls.scroll_y;
    this->unk_68 = 0x80;
    super->updatePriority = 6;
    sub_0801E1B8(0x1f17, 0);
    sub_0801E1EC(super->x.HALF.HI, super->y.HALF.HI, this->unk_68);
}

void MinishPortalCloseup_Action1(MinishPortalCloseupEntity* this) {
#ifdef EU
    static const u16 gUnk_081216C8[] = { 206, 19, 333, 208, 16, 333, 207, 1, 333, 0 };
#else
    static const u16 gUnk_081216C8[] = { 206, 19, 334, 208, 16, 334, 207, 1, 334, 0 };
#endif
    const u16* ptr;
    this->unk_68 -= 2;
    if (this->unk_68 >= 0x15) {
        sub_0801E1EC(super->x.HALF.HI, super->y.HALF.HI, this->unk_68);
    } else {
        gScreen.controls.windowOutsideControl = 0x10;
        sub_0808D030();
        ResetPaletteTable(0);
        ResetPalettes();
        gGFXSlots.unk0 = 1;
        {
#ifdef PC_PORT
            /* gUnk_081216C8 holds 3 triples (TREESTUMP/ROCK/PT_2) plus a
             * sentinel 0 — total 10 u16 entries. super->type comes from
             * gArea.portal_type which can in principle be PT_DUNGEON/JAR/5;
             * the upstream gating (PortalEnterUpdate split on PT_TOD,
             * PortalUsePortal returning early on PT_DUNGEON) keeps the
             * cutscene confined to types 0..2 in current data, but defend
             * the index so a stray higher type can't read into the
             * adjacent unrelated u16s (would yield bogus gfxId/paletteId/
             * spriteIndex and likely a sprite-cache miss). */
            u32 closeupType = super->type;
            if (closeupType > 2) {
                closeupType = 2;
            }
            ptr = &gUnk_081216C8[closeupType * 3];
#else
            ptr = &gUnk_081216C8[super->type * 3];
#endif
        }
        LoadFixedGFX(super, ptr[0]);
        LoadObjPalette(super, ptr[1]);
        super->spriteIndex = ptr[2];
#ifdef MULTI_REGION
        if (REGION_IS_EU) {
            super->spriteIndex = 333;
        }
#endif
        if (super->type == 2) {
            super->frameIndex = 2;
        }
        super->action = 2;
        super->spriteSettings.draw = 2;
        super->spriteOrientation.flipY = 0;
        super->spriteRendering.b3 = 0;
        super->spritePriority.b0 = 0;
        super->timer = 30;
        super->subtimer = 255;
        super->spriteRendering.b0 = 3;
        SetAffineInfo(super, 0x100, 0x100, 0);
        gArea.field_0x10 = 1;
        SetFade(FADE_IN_OUT | FADE_INSTANT, 8);
    }
}

void sub_0808D030(void) {
    struct OamData* ptr;
    s32 index;

    ptr = gOAMControls.oam;
    index = ARRAY_COUNT(gOAMControls.oam);
    MemClear(&gOAMControls, sizeof(gOAMControls));
    while (index != 0) {
        // TODO split up into bitfield writes?
        *(u16*)ptr = 0x2a0;
        ptr++;
        index--;
    }
    gOAMControls.field_0x0 = 1;
}

void MinishPortalCloseup_Action2(MinishPortalCloseupEntity* this) {
    u32 tmp = super->timer--;
    if (tmp != 0) {
        if (this->unk_68 != 0) {
            this->unk_68 -= 2;
        } else {
            this->unk_68 = 0;
        }
        sub_0801E1EC(super->x.HALF.HI, super->y.HALF.HI, this->unk_68);
        if (0x80 < super->subtimer) {
            super->subtimer -= 8;
        }
        SetAffineInfo(super, super->subtimer, super->subtimer, 0);
    } else {
        gArea.filler3[0]++;
        gArea.field_0x10 = 0;
        DeleteThisEntity();
    }
}
