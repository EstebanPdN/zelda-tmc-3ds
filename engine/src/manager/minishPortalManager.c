/**
 * @file minishPortalManager.c
 * @ingroup Managers
 *
 * @brief Facilitates the usage of minish portals.
 */
#include "manager/minishPortalManager.h"
#include "area.h"
#include "asm.h"
#include "flags.h"
#include "object.h"
#include "common.h"
#include "effects.h"
#include "player.h"
#include "room.h"
#include "sound.h"
#include "tiles.h"

bool32 PortalReadyForMinish(void);

void MinishPortalManager_Main(MinishPortalManager* this) {
    /* Decomp originally declared this as `{ -3, -3, -3, 0 }` (size 4),
     * but entity_headers spawns this manager with paramA values up to
     * 0xa — notably the Lake Hylia → Temple of Droplets entry uses
     * paramA=PT_TOD (=6). The GBA's index-6 read lands on the high byte
     * of the adjacent function pointer at 0x08107C70 (= 0x0805786d),
     * which happens to be 0x05; that 5-pixel y-bias is what the ToD
     * portal needs to align the player on the entry tile. On PC the
     * `.rodata` neighbours are unrelated, so the OOB read returned
     * garbage — portal_y drifted, the player oscillated, and on a
     * lucky frame stood several pixels off the visual entry. Restore
     * the GBA bytes verbatim through paramA=0xa so all portal types
     * (TREESTUMP/ROCK/PT_2/DUNGEON, plus the post-array bytes the GBA
     * code happens to read) align to the same offsets the original ROM
     * produced. */
    static const s8 gUnk_08107C6C[] = { -3, -3, -3, 0, 0x6d, 0x78, 0x05, 0x08, 0x21, 0x79, 0x05 };
    s8 tmp;
    if (super->action == 0) {
        super->action = 1;
        this->unk_20 = this->unk_38 + gRoomControls.origin_x - 0x20;
        this->unk_24 = this->unk_3a + gRoomControls.origin_y - 0x20;
        return;
    }
    if (CheckPlayerProximity(this->unk_20, this->unk_24, 0x40, 0x40)) {
        gArea.portal_x = this->unk_20 + 0x20;
        gArea.portal_y = this->unk_24 + 0x20 + gUnk_08107C6C[super->type];
        gArea.portal_exit_dir = this->unk_34;
        gArea.portal_type = super->type;
        if (!CheckGlobalFlag(EZERO_1ST)) {
            gArea.portal_mode = 1;
            gArea.portal_type = PT_5;
        } else {
            if ((gPlayerState.flags & PL_USE_PORTAL) && gPlayerState.jump_status == 0) {
                gArea.portal_mode = 2;
            } else {
                if (PortalReadyForMinish()) {
                    gArea.portal_mode = 3;
                }
            }
            if (GetActTileAtRoomCoords(this->unk_38, this->unk_3a, super->timer) == ACT_TILE_61) {
                CreateMagicSparklesFxAt(this->unk_38 + gRoomControls.origin_x, this->unk_3a + gRoomControls.origin_y,
                                        super->timer);
                if (super->subtimer == 0) {
                    super->subtimer = 1;
                    SoundReq(SFX_NEAR_PORTAL);
                }
            }
        }

    } else {
        super->subtimer = 0;
    }
}

void CreateMagicSparklesFxAt(u32 baseX, u32 baseY, u32 layer) {
    u32 r;
    int offsetX, offsetY;
    Entity* spark;
    r = Random();
    if (r & 0x7)
        return;
    spark = CreateObject(SPECIAL_FX, FX_SPARKLE4, 0);
    if (spark == NULL)
        return;
    offsetX = (r >> 0x8) & 0xF;
    offsetY = ((r >> 0x10) & 0xF);
    if (offsetY > 0x4) {
        offsetY = -offsetY;
    }
    if ((r >> 0x18) & 0x1) {
        offsetX = -offsetX;
    }
    spark->x.HALF.HI = baseX + offsetX;
    spark->y.HALF.HI = baseY + offsetY;
    spark->collisionLayer = layer;
    UpdateSpriteForCollisionLayer(spark);
}

bool32 PortalReadyForMinish(void) {
    if ((gPlayerState.flags & PL_MINISH) && gPlayerState.attachedBeetleCount == 0 && (gArea.portal_type != PT_TOD) &&
        (gPlayerState.heldObject == 0)) {
        switch (gPlayerState.framestate) {
            case PL_STATE_IDLE:
            case PL_STATE_WALK:
                return TRUE;
        }
    }
    return FALSE;
}
