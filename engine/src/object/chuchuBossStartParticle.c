/**
 * @file chuchuBossStartParticle.c
 * @ingroup Objects
 *
 * @brief Chuchu Boss Start Particle object
 */
#include "object.h"
#include "asm.h"
#include "sound.h"
#include "room.h"
#include "physics.h"
#include "player.h"
#include "color.h"
#ifdef PC_PORT
#include "port/port_generic_entity.h"
#endif

#ifndef PC_PORT
typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ u8 unused1[5];
    /*0x6d*/ u8 unk_6d;
    /*0x6e*/ u8 unused2[2];
    /*0x70*/ u32 unk_70;
    /*0x74*/ u32 unk_74;
    /*0x78*/ u32 unk_78;
    /*0x7c*/ u8 unused3[3];
    /*0x7f*/ u8 unk_7f;
    /*0x80*/ u8 unused4[4];
    /*0x84*/ u32 unk_84;
} ChuchuBossStartParticleEntity;
#else
typedef GenericEntity ChuchuBossStartParticleEntity;

static inline u32* ChuchuBossStartParticle_U32Field(ChuchuBossStartParticleEntity* this, size_t geFieldOffset) {
    return (u32*)Port_GEFieldAddr(&this->base, geFieldOffset);
}

#define CBSP_UNK_6D(this) (GE_FIELD(&((this)->base), field_0x6c)->HALF.HI)
#define CBSP_UNK_70(this) (GE_FIELD(&((this)->base), field_0x70)->WORD_U)
#define CBSP_UNK_74(this) (*ChuchuBossStartParticle_U32Field((this), offsetof(GenericEntity, field_0x74)))
#define CBSP_UNK_78(this) (*ChuchuBossStartParticle_U32Field((this), offsetof(GenericEntity, field_0x78)))
#define CBSP_UNK_7F(this) (GE_FIELD(&((this)->base), field_0x7c)->BYTES.byte3)
#define CBSP_UNK_84(this) (*ChuchuBossStartParticle_U32Field((this), offsetof(GenericEntity, cutsceneBeh)))
#endif

typedef struct {
    /*0x00*/ Entity base;
    /*0x68*/ Entity* unused_68;
    /*0x6c*/ u8 unused_6c;
    /*0x6d*/ u8 unk_6d;
    /*0x6e*/ u8 unused_6e[0x10];
    /*0x7e*/ union SplitHWord unk_7e;
} ChuchuBossStartParticleParentEntity;

PORT_STATIC_ASSERT_OFFSET(ChuchuBossStartParticleParentEntity, unk_6d, 0x6D, 0x99,
                          "ChuchuBossStartParticleParentEntity unk_6d offset incorrect");
PORT_STATIC_ASSERT_OFFSET(ChuchuBossStartParticleParentEntity, unk_7e, 0x7E, 0xAA,
                          "ChuchuBossStartParticleParentEntity unk_7e offset incorrect");

#define CBSP_PARENT_UNK_6D(ent) (((ChuchuBossStartParticleParentEntity*)(ent))->unk_6d)
#define CBSP_PARENT_UNK_7F(ent) (((ChuchuBossStartParticleParentEntity*)(ent))->unk_7e.HALF.HI)

#ifndef PC_PORT
#define CBSP_UNK_6D(this) ((this)->unk_6d)
#define CBSP_UNK_70(this) ((this)->unk_70)
#define CBSP_UNK_74(this) ((this)->unk_74)
#define CBSP_UNK_78(this) ((this)->unk_78)
#define CBSP_UNK_7F(this) ((this)->unk_7f)
#define CBSP_UNK_84(this) ((this)->unk_84)
#endif

static void sub_0808F2B0(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type0_Action1(ChuchuBossStartParticleEntity* this);
void sub_0808F244(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type0(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type1(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type2(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type3(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type5(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type9(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type10(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type0_Init(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type0_Action2(ChuchuBossStartParticleEntity* this);
void ChuchuBossStartParticle_Type0_Action3(ChuchuBossStartParticleEntity* this);
void sub_0808F244(ChuchuBossStartParticleEntity* this);
void sub_0808F1F8(ChuchuBossStartParticleEntity* this);
void sub_0808F5EC(ChuchuBossStartParticleEntity* this);

void ChuchuBossStartParticle(ChuchuBossStartParticleEntity* this) {
    static void (*const ChuchuBossStartParticle_Types[])(ChuchuBossStartParticleEntity*) = {
        ChuchuBossStartParticle_Type0, ChuchuBossStartParticle_Type1,  ChuchuBossStartParticle_Type2,
        ChuchuBossStartParticle_Type3, ChuchuBossStartParticle_Type0,  ChuchuBossStartParticle_Type5,
        ChuchuBossStartParticle_Type5, ChuchuBossStartParticle_Type5,  ChuchuBossStartParticle_Type5,
        ChuchuBossStartParticle_Type9, ChuchuBossStartParticle_Type10,
    };
    ChuchuBossStartParticle_Types[super->type](this);
}

void ChuchuBossStartParticle_Type0(ChuchuBossStartParticleEntity* this) {
    static void (*const ChuchuBossStartParticle_Type0_Actions[])(ChuchuBossStartParticleEntity*) = {
        ChuchuBossStartParticle_Type0_Init,
        ChuchuBossStartParticle_Type0_Action1,
        ChuchuBossStartParticle_Type0_Action2,
        ChuchuBossStartParticle_Type0_Action3,
    };
    ChuchuBossStartParticle_Type0_Actions[super->action](this);
}

void ChuchuBossStartParticle_Type0_Init(ChuchuBossStartParticleEntity* this) {
    u32 offsetX;
    u32 offsetY;
    Entity* ent;
    u32 uVar3;

    ent = CreateObjectWithParent(super, CHUCHU_BOSS_START_PARTICLE, 2, 0);
    super->child = ent;
    if (ent == NULL) {
        DeleteThisEntity();
    }
    super->action = 1;
    super->z.HALF.HI = -0xc0;
    offsetX = Random() % 64;
    if ((Random() & 1) != 0) {
        offsetX = -offsetX;
    }
    super->x.HALF.HI = super->parent->x.HALF.HI + offsetX;
    offsetY = Random() % 32;
    if ((Random() & 1) != 0) {
        offsetY = -offsetY;
    }
    super->y.HALF.HI = super->parent->y.HALF.HI + offsetY;
    CBSP_UNK_74(this) = 0x240;
    CBSP_UNK_78(this) = 0x140;
    InitializeAnimation(super, 3);
    ChuchuBossStartParticle_Type0_Action1(this);
}

void ChuchuBossStartParticle_Type0_Action1(ChuchuBossStartParticleEntity* this) {
    sub_0808F2B0(this);
    if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
        super->action++;
        SoundReq(SFX_WATER_SPLASH);
    }
}

void ChuchuBossStartParticle_Type0_Action2(ChuchuBossStartParticleEntity* this) {
    CBSP_UNK_74(this) -= 0x20;
    CBSP_UNK_78(this) += 0x20;
    sub_0806FCF4(super, CBSP_UNK_78(this), 8, 2);
    if (CBSP_UNK_78(this) > 0x1ff) {
        super->action++;
    }
    sub_0808F2B0(this);
}

void ChuchuBossStartParticle_Type0_Action3(ChuchuBossStartParticleEntity* this) {
    CBSP_UNK_78(this) += 0x10;
    CBSP_UNK_74(this) += 0x10;
    sub_0806FCF4(super, CBSP_UNK_78(this), 8, 2);
    if (CBSP_UNK_78(this) > 0x3ff) {
        super->child->action = 0xff;
        DeleteThisEntity();
    } else {
        sub_0808F2B0(this);
    }
}

void ChuchuBossStartParticle_Type1(ChuchuBossStartParticleEntity* this) {
    static void (*const actionFuncs[])(ChuchuBossStartParticleEntity*) = {
        sub_0808F1F8,
        sub_0808F244,
    };
    actionFuncs[super->action](this);
}

void sub_0808F1F8(ChuchuBossStartParticleEntity* this) {
    u8 bVar1;

    super->spriteRendering.b0 = 3;
    super->action++;
    super->spriteRendering.b3 = super->parent->spriteRendering.b3;
    super->spriteOrientation.flipY = super->parent->spriteOrientation.flipY;
    super->spritePriority.b0 = 7;
    InitializeAnimation(super, 1);
    sub_0808F244(this);
}

void sub_0808F244(ChuchuBossStartParticleEntity* this) {

    super->spriteSettings.draw = super->parent->spriteSettings.draw;
    super->y.HALF.HI = super->parent->y.HALF.HI + 3;
    super->x.HALF.HI = (*(s8*)&super->child->spriteOffsetX + super->child->x.HALF.HI);
    super->z.WORD = 0;
    CBSP_UNK_74(this) = 0x80 - super->parent->z.HALF.HI;
    CBSP_UNK_78(this) = 0x100 - super->parent->z.HALF.HI;
    CBSP_UNK_70(this) = CBSP_PARENT_UNK_7F(super->parent);
    sub_0808F2B0(this);
    if ((CBSP_PARENT_UNK_6D(super->parent) & 2) != 0) {
        DeleteThisEntity();
    }
}

static void sub_0808F2B0(ChuchuBossStartParticleEntity* this) {
    SetAffineInfo(super, CBSP_UNK_74(this), CBSP_UNK_78(this), CBSP_UNK_70(this));
}

void ChuchuBossStartParticle_Type2(ChuchuBossStartParticleEntity* this) {
    if (super->action == 0) {
        super->action++;
        super->spriteRendering.b3 = super->parent->spriteRendering.b3;
        super->spriteOrientation.flipY = super->parent->spriteOrientation.flipY;
        super->spriteSettings.draw = 1;
        super->spritePriority.b0 = 7;
        super->z.WORD = 0;

        super->y.HALF.HI = super->parent->y.HALF.HI + 3;
        super->x.HALF.HI = super->parent->x.HALF.HI;
        InitializeAnimation(super, 3);
    }
    if (super->parent->z.HALF.HI == 0) {
        CBSP_UNK_74(this) = CBSP_UNK_74((ChuchuBossStartParticleEntity*)super->parent);
        CBSP_UNK_78(this) = CBSP_UNK_78((ChuchuBossStartParticleEntity*)super->parent);
    } else {
        CBSP_UNK_74(this) = 0x200 - super->parent->z.HALF.HI;
        CBSP_UNK_78(this) = super->parent->z.HALF.HI * -2 + 0x300;
    }
    CBSP_UNK_70(this) = 0;
    sub_0808F2B0(this);
    sub_0806FCF4(super, CBSP_UNK_78(this), 8, 2);
    if (super->action == 0xff) {
        DeleteThisEntity();
    }
}

void ChuchuBossStartParticle_Type3(ChuchuBossStartParticleEntity* this) {
    static const u16 gUnk_08121EA0[] = { 0x1a4, 0x12c };
    if (super->action == 0) {
        if (super->parent->subAction == 1) {
            super->action = 1;
            CBSP_UNK_70(this) = 0;
            sub_0808F5EC(this);
        }
    } else {
        u8 flag = super->parent->subAction - 1;
        if (flag < 5) {
            if (CBSP_UNK_70(this) == 0) {
                if (--CBSP_UNK_74(this) == (u32)-1) {
                    CBSP_UNK_70(this) = gUnk_08121EA0[Random() & 1];
                }
            } else {
                if (--CBSP_UNK_70(this) == 0) {
                    sub_0808F5EC(this);
                }
            }
        } else {
            super->action = 0;
            CBSP_UNK_74(this) = 0;
        }
    }
}

void ChuchuBossStartParticle_Type5(ChuchuBossStartParticleEntity* this) {
    if (super->action == 0) {
        super->spriteSettings.draw = 1;
        super->action = 1;
        super->timer = 120;
        InitializeAnimation(super, super->type + 1);
        // TODO: This block of code might supposed to be a switch statement.
        if (super->type != 8) {
            if (super->type == 7) {
                SortEntityBelow(super->child, super);
                return;
            }
        } else {
            super->hitType = super->child->hitType;
            super->child->hitType = 0x7E;
        }
        SortEntityAbove(super->child, super);
    } else {
        if (CBSP_UNK_74((ChuchuBossStartParticleEntity*)super->parent) == 0) {
            if (super->type == 8) {
                super->child->hitType = super->hitType;
            }
            DeleteThisEntity();
        }

        if (super->type == 5 && (super->frame & 1)) {
            Entity* entity = CreateObjectWithParent(super->child, CHUCHU_BOSS_START_PARTICLE, 8, 0);
            if (entity != NULL) {
                entity->parent = super->parent;
                entity->child = super->parent->parent;
            }
        }

        GetNextFrame(super);
        CopyPositionAndSpriteOffset(super->child, super);
    }
}

void ChuchuBossStartParticle_Type9(ChuchuBossStartParticleEntity* this) {
    u32 var0;

    if (super->action != 0) {
        if (sub_0806F3E4(super)) {
            if (gRoomTransition.frameCount % 16 == 0) {
                SoundReq(SFX_EF);
            }
            DeleteThisEntity();
        }

        if (super->timer-- == 0) {
            DeleteThisEntity();
        }

        if (gPlayerState.gustJarState != 1) {
            DeleteThisEntity();
        }
    } else {
        super->action = 1;
        super->timer = 64;
        var0 = super->type2 != 0 ? 44 : 43;
        super->x.HALF.HI -= super->parent->hitbox->width;
        super->x.HALF.HI += (s32)Random() % (super->parent->hitbox->width * 2);
        super->y.HALF.HI -= super->parent->hitbox->height;
        super->y.HALF.HI += (s32)Random() % (super->parent->hitbox->height * 2);
        ChangeObjPalette(super, var0);
        InitializeAnimation(super, 4);
    }
}

void ChuchuBossStartParticle_Type10(ChuchuBossStartParticleEntity* this) {
    CopyPositionAndSpriteOffset(super->parent, super);
    super->spriteOffsetY -= 24;
    if ((s8)super->parent->spriteOffsetX != 0) {
        super->spriteOffsetX += (s8)super->parent->spriteOffsetX / 2;
    }

    if (super->action != 0) {
        if (CBSP_UNK_84(this)) {
            if (--CBSP_UNK_84(this) == 0) {
                DeleteThisEntity();
            }
        }
        GetNextFrame(super);
    } else {
        super->action = 1;
        super->spriteRendering.b3 = super->parent->spriteRendering.b3;
        super->spriteOrientation.flipY = super->parent->spriteOrientation.flipY;
        super->spritePriority.b0 = 0;
        InitializeAnimation(super, 3);
    }
}

void sub_0808F5EC(ChuchuBossStartParticleEntity* this) {
    Entity* entity = CreateObjectWithParent(super->child, CHUCHU_BOSS_START_PARTICLE, 5, 0);
    if (entity != NULL) {
        entity->parent = super;
        entity->child = super->child;
    }

    entity = CreateObjectWithParent(super->child, CHUCHU_BOSS_START_PARTICLE, 6, 0);
    if (entity != NULL) {
        entity->parent = super;
        entity->child = super->child;
    }

    entity = CreateObjectWithParent(super->child, CHUCHU_BOSS_START_PARTICLE, 7, 0);
    if (entity != NULL) {
        entity->parent = super;
        entity->child = super->child;
    }

    CBSP_UNK_74(this) = 600;
}
