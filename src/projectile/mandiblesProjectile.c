/**
 * @file mandiblesProjectile.c
 * @ingroup Projectiles
 *
 * @brief Mandibles Projectile
 */
#include "enemy.h"
#include "sound.h"
#include "entity.h"
#include "room.h"
#include "hitbox.h"
#include "physics.h"
#include "tiles.h"
#include "player.h"
#include "asm.h"
#include "game.h"

typedef struct {
    /*0x00*/ Entity base;
#if defined(PC_PORT) && (__SIZEOF_POINTER__ == 8)
    /*0x68*/ u8 unused1[16 + 4];
#else
    /*0x68*/ u8 unused1[16];
#endif
    /*0x78*/ u16 unk_78;
    /*0x7a*/ u8 unused2[6];
    /*0x80*/ u16 unk_80;
    /*0x82*/ u8 unk_82;
} MandiblesProjectileEntity;

PORT_STATIC_ASSERT_SIZE(MandiblesProjectileEntity, 0x84, 0xB0, "MandiblesProjectileEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(MandiblesProjectileEntity, unk_80, 0x80, 0xAC,
                          "MandiblesProjectileEntity unk_80 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(MandiblesProjectileEntity, unk_82, 0x82, 0xAE,
                          "MandiblesProjectileEntity unk_82 offset incorrect");

extern void (*const MandiblesProjectile_Functions[])(MandiblesProjectileEntity*);
extern void (*const MandiblesProjectile_Actions[])(MandiblesProjectileEntity*);
extern const u8 gUnk_08129CA4[];
extern const Hitbox* const gUnk_08129CF4[];
extern const u8 gUnk_08129CAC[];
extern const s8 gUnk_08129D14[];

void sub_080AA1D8(Entity*);
void sub_080AA270(MandiblesProjectileEntity*);
bool32 sub_080AA2E0(Entity*);
void sub_080AA320(MandiblesProjectileEntity*);
void sub_080AA3E0(MandiblesProjectileEntity*, u32);
bool32 sub_080AA374(Entity*);

void MandiblesProjectile(MandiblesProjectileEntity* this) {
    Entity* entity = super->child;
    if (entity == NULL) {
        entity = super->parent;
    }
#ifdef PC_PORT
    /* #101 — mandibles can survive both their parent (body) being
     * killed AND their child slot being self-unlinked in Action4
     * (line `super->child = NULL`).  When that happens both child
     * and parent are NULL and the legacy `entity->confusedTime`
     * deref SIGSEGVs on PC.  On GBA the NULL read hit BIOS and
     * returned 0, so the original code "worked" by accident.  Skip
     * the COLLISION_ON gating in that case — it's only an
     * iframes/collide-toggle convenience and the projectile will
     * shortly delete itself via the next-NULL checks elsewhere. */
    if (entity != NULL && (entity->confusedTime == 0) && ((super->flags & ENT_COLLIDE) == 0)) {
        COLLISION_ON(super);
    }
#else
    if ((entity->confusedTime == 0) && ((super->flags & ENT_COLLIDE) == 0)) {
        COLLISION_ON(super);
    }
#endif
    MandiblesProjectile_Functions[GetNextFunction(super)](this);
}

void MandiblesProjectile_OnTick(MandiblesProjectileEntity* this) {
    if (super->action < 3) {
        sub_080AA1D8(super);
    }
    MandiblesProjectile_Actions[super->action](this);
}

void MandiblesProjectile_OnCollision(MandiblesProjectileEntity* this) {
    Entity* parent;

    if ((super->contactFlags & CONTACT_NOW) != 0) {
        switch (super->action) {
            case 4:
                super->subtimer = 0;
                ModHealth(-2);
                ProcessMovement3(super);
                UpdateAnimationSingleFrame(super);
                break;
            case 3:
                if (super->confusedTime != 0) {
                    this->unk_82 = 2;
                    super->animationState = 0xff;
                    sub_080AA270(this);
                    super->subtimer = 32;
                }
                break;
            default:
                parent = super->parent;
#ifdef PC_PORT
                /* #101 — same NULL-parent hazard as sub_080AA270. */
                if (parent == NULL)
                    break;
#endif
                parent->iframes = super->iframes;
                parent->knockbackDirection = super->knockbackDirection;
                parent->knockbackDuration = super->knockbackDuration;
                if (super->action == 1) {
                    sub_080AA320(this);
                }
                break;
        }
        super->knockbackDuration = 0;
    }
}

void MandiblesProjectile_Init(MandiblesProjectileEntity* this) {
    super->action = 1;
    this->unk_80 = 0;
    super->animationState = 0xff;
    this->unk_82 = 0;
    super->spritePriority.b0 = 4;
    SortEntityBelow(super, super->parent);
    sub_080AA270(this);
}

void MandiblesProjectile_Action1(MandiblesProjectileEntity* this) {
    if (this->unk_80 != 0) {
        this->unk_80--;
    } else {
        if (--super->subtimer == 0) {
            sub_080AA270(this);
        } else {
            if (sub_080AA2E0(super) != 0) {
                sub_080AA320(this);
            }
        }
    }
    UpdateAnimationSingleFrame(super);
}

void MandiblesProjectile_Action2(MandiblesProjectileEntity* this) {
    UpdateAnimationSingleFrame(super);
    if ((super->frame & 0x40) != 0) {
        super->frame &= ~0x40;
        EnqueueSFX(SFX_15D);
    }
    this->unk_78 = TILE(super->x.HALF.HI, super->y.HALF.HI);
    if (GetTileTypeAtTilePos(this->unk_78, super->collisionLayer) == SPECIAL_TILE_0) {
        SetTile(SPECIAL_TILE_5, this->unk_78, super->collisionLayer);
    }
}

void MandiblesProjectile_Action3(MandiblesProjectileEntity* this) {
    u32 uVar1;
    s8* tmp;
    Entity* entity;

    entity = super->child;
    if (entity == NULL) {
        DeleteThisEntity();
#ifdef PC_PORT
        /* #101 sibling: DeleteThisEntity() only queues deletion (no return), so
           the `entity->next` deref below reads NULL. On GBA that hit BIOS and
           returned garbage; on PC it SIGSEGVs. The #101 fix guarded the
           dispatcher but not this Action3 path — bail like the dispatcher does. */
        return;
#endif
    }
    if (entity->next == NULL) {
        DeleteThisEntity();
    } else {
        tmp = GetSpriteSubEntryOffsetDataPointer((u16)entity->spriteIndex, entity->frameIndex);
        if ((entity->animationState & 4) != 0) {
            PositionRelative(entity, super, Q_16_16(-tmp[0]), Q_16_16(tmp[1]));
        } else {
            PositionRelative(entity, super, Q_16_16(tmp[0]), Q_16_16(tmp[1]));
        }
    }
    if (entity->confusedTime == 0) {
        if (super->subtimer != 0) {
            super->subtimer--;
        } else {
            UpdateAnimationSingleFrame(super);
            if ((super->frame & ANIM_DONE) != 0) {
                super->action = 4;
                this->unk_82 = 3;
                super->subtimer = 64;
                uVar1 = entity->animationState;
                super->direction = uVar1 << 2;
                super->animationState = uVar1 << 0x1a >> 0x1a;
                if (REGION_IS_EU) {
                    super->spriteOrientation.flipY = 1;
                    super->hitbox = (Hitbox*)&gHitbox_0;
                } else {
                    super->hitbox = (Hitbox*)&gHitbox_0;
                    if (AreaIsDungeon() != 0) {
                        super->spriteOrientation.flipY = 1;
                    }
                }
                sub_080AA3E0(this, 0);
            }
        }
    }
}

void MandiblesProjectile_Action4(MandiblesProjectileEntity* this) {
    s32 iVar1;
    u32 uVar2;
    MandiblesProjectileEntity* entity;

    entity = (MandiblesProjectileEntity*)super->child;
    if (entity != NULL) {
        if (entity->base.next == NULL) {
            super->child = NULL;
        }
        if (super->subtimer != 0) {
            super->subtimer--;
        } else {
            if (sub_080AA374(super) != 0) {
                if (entity->base.health == 0) {
                    DeleteThisEntity();
                }
                EnqueueSFX(SFX_F2);
                super->action = 1;
                super->animationState = 0xff;
                this->unk_82 = 0;
                super->timer = 0;
                this->unk_80 = 0x20;
                super->spriteOrientation.flipY = 2;
                super->parent = super->child;
                super->child = NULL;
                sub_080AA270(this);
                entity = (MandiblesProjectileEntity*)super->parent;
                entity->base.action = 1;
                entity->base.direction = entity->base.animationState << 2;
                entity->base.speed = 0;
                entity->unk_82 = 1;
                entity->base.timer = 0;
                entity->base.subtimer = 120;
                entity->unk_80 = 0x20;
                sub_080AA3E0(entity, 1);
            }
        }
    } else {
        if (CheckOnScreen(super) == 0) {
            DeleteThisEntity();
        }
    }
    LinearMoveUpdate(super);
    UpdateAnimationSingleFrame(super);
}

void sub_080AA1D8(Entity* this) {
    s8* tmp;
    Entity* parent;

    parent = this->parent;
    if (parent != NULL) {
        if (parent->next == NULL) {
            DeleteThisEntity();
        }
        if ((parent->frameIndex & 0x20) == 0) {
            tmp = GetSpriteSubEntryOffsetDataPointer((u16)parent->spriteIndex, parent->frameIndex);
            if ((parent->animationState & 4) != 0) {
                PositionRelative(parent, this, Q_16_16(-tmp[0]), Q_16_16(tmp[1]));
            } else {
                PositionRelative(parent, this, Q_16_16(tmp[0]), Q_16_16(tmp[1]));
            }
            if (parent->confusedTime != 0) {
                if ((this->flags & ENT_COLLIDE) != 0) {
                    COLLISION_OFF(this);
                }
            } else {
                if ((this->flags & ENT_COLLIDE) == 0) {
                    COLLISION_ON(this);
                }
            }
        }
    } else {
        DeleteThisEntity();
    }
}

void sub_080AA270(MandiblesProjectileEntity* this) {
    u32 animationState;
    Entity* parent;
    parent = super->parent;
    super->subtimer = gUnk_08129CA4[Random() & 7];
#ifdef PC_PORT
    /* #101 — parent can be NULL after the body is killed mid-detach
     * (sub_08038A70:131 NULLs the mandibles' parent at launch, and
     * Action4 only re-points it to the child slot which is itself
     * stale once the body dies).  GBA tolerated the NULL deref via
     * BIOS-at-0 reads; PC SIGSEGVs. Skip the anim-state update — the
     * projectile will self-delete via the entity->next == NULL checks
     * elsewhere shortly. */
    if (parent == NULL)
        return;
#endif
    animationState = parent->animationState;
    if (super->animationState == 0xff) {
        /* #91 / #97 pattern: gUnk_08129CF4 has 8 entries; parent's raw
         * animationState may exceed 7 during transient states. Clamp. */
        if (animationState >= 8)
            animationState = 7;
        super->animationState = animationState;
        super->hitbox = (Hitbox*)gUnk_08129CF4[animationState];
        sub_080AA3E0(this, 0);
    } else {
        animationState = (gUnk_08129CAC[Random() & 7] + animationState) & 7;
        if (animationState != super->animationState) {
            super->animationState = animationState;
            super->hitbox = (Hitbox*)gUnk_08129CF4[animationState];
            sub_080AA3E0(this, 0);
        }
    }
}

bool32 sub_080AA2E0(Entity* this) {
    Entity* entity;
    u32 a;
    u32 b;

    entity = sub_08049DF4(0);
    if (entity == NULL) {
        return FALSE;
    }
    a = GetFacingDirection(this, entity);
    b = this->animationState * 4;
    if (Direction8Round(a + 4) != Direction8Round(b + 4)) {
        return FALSE;
    }
    if (sub_08049F1C(this, entity, 0x40) == 0) {
        return FALSE;
    }
    return TRUE;
}

void sub_080AA320(MandiblesProjectileEntity* this) {
    u32 uVar2;
    MandiblesProjectileEntity* parent;

    parent = (MandiblesProjectileEntity*)super->parent;
    super->action = 2;
    this->unk_82 = 1;
    super->subtimer = 0;
    parent->base.action = 2;
    parent->base.subtimer = 48;
    parent->unk_80 = 0x50;
    parent->base.speed = 0;
    parent->base.direction = sub_08049F84(&parent->base, 0);
    uVar2 = Direction8Round(parent->base.direction + 4);
    super->animationState = uVar2 >> 2;
    parent->base.animationState = DirectionRound(uVar2) >> 2;
    sub_080AA3E0(parent, 1);
    sub_080AA1D8(super);
}

bool32 sub_080AA374(Entity* this) {
    u32 uVar1;
    Entity* child;
    bool32 result;
    const s8* ptr;

    result = FALSE;
    child = this->child;
    if ((child != NULL) && (child->next != NULL)) {
        ptr = &gUnk_08129D14[child->animationState];
        uVar1 = sub_0806F824(this, child, ptr[0], ptr[1]);
        if (EntityWithinDistance(this, child->x.HALF.HI + ptr[0], child->y.HALF.HI + ptr[1], 8) != 0) {
            result = TRUE;
        } else {
            sub_08004596(this, uVar1);
        }
    }
    return result;
}

void sub_080AA3E0(MandiblesProjectileEntity* this, u32 param) {
    u32 tmp;

    tmp = super->animationState | this->unk_82 << 3;
    if (param != 0) {
        tmp >>= 1;
    }
    InitAnimationForceUpdate(super, tmp);
}

void (*const MandiblesProjectile_Functions[])(MandiblesProjectileEntity*) = {
    MandiblesProjectile_OnTick,
    MandiblesProjectile_OnCollision,
    (void (*)(MandiblesProjectileEntity*))DeleteEntity,
    (void (*)(MandiblesProjectileEntity*))DeleteEntity,
    (void (*)(MandiblesProjectileEntity*))DeleteEntity,
};
void (*const MandiblesProjectile_Actions[])(MandiblesProjectileEntity*) = {
    MandiblesProjectile_Init,    MandiblesProjectile_Action1, MandiblesProjectile_Action2,
    MandiblesProjectile_Action3, MandiblesProjectile_Action4,
};
const u8 gUnk_08129CA4[] = { 24, 36, 52, 32, 60, 28, 44, 40 };
const u8 gUnk_08129CAC[] = {
    1, 0, 7, 1, 0, 7, 0, 0,
};
const Hitbox gUnk_08129CB4 = { 0, -4, { 5, 5, 3, 7 }, 6, 8 };
const Hitbox gUnk_08129CBC = { 3, -3, { 5, 3, 3, 5 }, 6, 6 };
const Hitbox gUnk_08129CC4 = { 4, 0, { 7, 3, 5, 5 }, 8, 6 };
const Hitbox gUnk_08129CCC = { 3, 3, { 5, 3, 3, 5 }, 6, 6 };
const Hitbox gUnk_08129CD4 = { 0, 4, { 5, 5, 3, 7 }, 6, 8 };
const Hitbox gUnk_08129CDC = { -3, 3, { 5, 3, 3, 5 }, 6, 6 };
const Hitbox gUnk_08129CE4 = { -4, 0, { 7, 3, 5, 5 }, 8, 6 };
const Hitbox gUnk_08129CEC = { -3, -3, { 5, 3, 3, 5 }, 6, 6 };
const Hitbox* const gUnk_08129CF4[] = {
    &gUnk_08129CB4, &gUnk_08129CBC, &gUnk_08129CC4, &gUnk_08129CCC,
    &gUnk_08129CD4, &gUnk_08129CDC, &gUnk_08129CE4, &gUnk_08129CEC,
};
const s8 gUnk_08129D14[] = { 0, -16, 6, -10, 0, 4, -6, -10 };
