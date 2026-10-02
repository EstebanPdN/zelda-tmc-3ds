/**
 * @file mazaalHead.c
 * @ingroup Enemies
 *
 * @brief Mazaal Head enemy
 */
#include "area.h"
#include "enemy.h"
#include "sound.h"
#include "effects.h"
#include "projectile.h"
#include "object.h"
#include "asm.h"
#include "room.h"
#include "physics.h"
#include "player.h"
#include "roomid.h"
#include "screen.h"
#ifdef PC_PORT
#include "port_entity_ctx.h"
#endif

typedef struct MazaalHeadEntity_ {
    /*0x00*/ Entity base;
#if defined(PC_PORT) && (__SIZEOF_POINTER__ == 8)
    /* #99 fix: same #98 pattern as EyegoreEntity. On 64-bit PC,
       Enemy::child is an 8-byte pointer (vs 4 on GBA), so the bytes
       between Entity base and the Enemy::child slot are 4 more on PC.
       Without the +4 pad, `unk_6d` aliases byte 5 of Enemy::child's
       pointer value (which the framework writes when wiring up
       super->child), corrupting Mazaal head state on every spawn —
       which causes phase 2 / phase 3 to fail to re-initialise pillars
       properly (issue #99). Pad so unk_6d / unk_74 / unk_78 land in
       the Enemy filler regions the GBA code expects. */
    u8 unused1[0x5 + 4];
#else
    /*0x68*/ u8 unused1[5];
#endif
    /*0x6d*/ u8 unk_6d;
    /*0x6e*/ u8 unused2[6];
    /* Issue #162: these two links are read/written CROSS-STRUCT — the head
       writes/reads a MazaalBraceletEntity's slots through this struct's layout
       (the GBA decomp type-puns both at 0x74/0x78). MazaalBraceletEntity stores
       them as 4-byte EntityRef slots (#127-class, must fit the 0xB8 pool slot),
       so the head MUST use the identical representation and offsets (PC
       0xA0/0xA4), or a bracelet's slot index gets dereferenced as a pointer —
       the Fortress of Winds boss-entry SIGSEGV. */
    union {
        /*0x74*/ EntityRef entity; /* 4B both targets (slot on PC) */
    } unk_74;
    union {
        /*0x78*/ EntityRef entity; /* 4B both targets (slot on PC) */
    } unk_78;
    /*0x7c*/ u8 unk_7c;
    /*0x7d*/ u8 unk_7d;
    /*0x7e*/ u16 unk_7e;
    /*0x80*/ u8 unk_80;
    /*0x81*/ u8 unk_81;
} MazaalHeadEntity;

PORT_STATIC_ASSERT_OFFSET(MazaalHeadEntity, unk_6d, 0x6d, 0x99, "MazaalHeadEntity unk_6d offset (Enemy::child +4 pad)");
PORT_STATIC_ASSERT_OFFSET(MazaalHeadEntity, unk_74, 0x74, 0xA0,
                          "MazaalHeadEntity unk_74 must alias MazaalBraceletEntity unk_74 (#162)");
PORT_STATIC_ASSERT_OFFSET(MazaalHeadEntity, unk_78, 0x78, 0xA4,
                          "MazaalHeadEntity unk_78 must alias MazaalBraceletEntity unk_78 (#162)");

/* Link accessors (same shape as mazaalBracelet.c's MB74/MB78). On PC these
 * resolve/store 4-byte EntityRef slots; on GBA they compile to the original
 * raw pointer accesses. */
#ifdef PC_PORT
#define MH74(x) ((MazaalHeadEntity*)ENTITY_REF_GET((x)->unk_74.entity))
#define MH74_SET(x, e) ENTITY_REF_SET((x)->unk_74.entity, e)
#define MH78(x) ((MazaalHeadEntity*)ENTITY_REF_GET((x)->unk_78.entity))
#define MH78_SET(x, e) ENTITY_REF_SET((x)->unk_78.entity, e)
#else
#define MH74(x) ((MazaalHeadEntity*)(x)->unk_74.entity)
#define MH74_SET(x, e) ((x)->unk_74.entity = (EntityRef)(e))
#define MH78(x) ((MazaalHeadEntity*)(x)->unk_78.entity)
#define MH78_SET(x, e) ((x)->unk_78.entity = (EntityRef)(e))
#endif

extern void UnloadOBJPalette(Entity*);

void sub_0803499C(MazaalHeadEntity*);
void sub_08034420(MazaalHeadEntity*);
void sub_08034830(MazaalHeadEntity*);
bool32 sub_080349D8(MazaalHeadEntity*);
void sub_0803442C(MazaalHeadEntity*, u32);
void sub_0803443C(MazaalHeadEntity*);
void sub_080347B4(MazaalHeadEntity*);
void sub_0803473C(MazaalHeadEntity*);
u32 sub_08034A10(MazaalHeadEntity*);
void sub_080347FC(MazaalHeadEntity*);
void sub_080344BC(MazaalHeadEntity*);
void sub_08034498(MazaalHeadEntity*);
void sub_08034474(MazaalHeadEntity*);
void sub_080345A0(MazaalHeadEntity*);
void sub_080345B8(MazaalHeadEntity*);
void sub_08034638(MazaalHeadEntity*);
void sub_08034618(MazaalHeadEntity*);
void sub_08034658(MazaalHeadEntity*);
void sub_0803467C(MazaalHeadEntity*);
u32 sub_080348A4(MazaalHeadEntity*, MazaalHeadEntity*, u32);
void sub_08034C00(MazaalHeadEntity*);

void MazaalHead_OnTick(MazaalHeadEntity* this);
void MazaalHead_OnCollision(MazaalHeadEntity* this);
void MazaalHead_OnGrabbed(MazaalHeadEntity* this);
void sub_08033F1C(MazaalHeadEntity* this);
void sub_08034A84(MazaalHeadEntity* this);
void sub_08034AC4(MazaalHeadEntity* this);
void sub_08034BC8(MazaalHeadEntity* this);
void sub_08033F3C(MazaalHeadEntity* this);
void sub_08033FFC(MazaalHeadEntity* this);
void sub_0803414C(MazaalHeadEntity* this);
void sub_080341B8(MazaalHeadEntity* this);
void sub_080341D0(MazaalHeadEntity* this);
void sub_080341F4(MazaalHeadEntity* this);
void sub_08034210(MazaalHeadEntity* this);
void sub_08034240(MazaalHeadEntity* this);
void sub_08034274(MazaalHeadEntity* this);
void sub_080342A0(MazaalHeadEntity* this);
void sub_080342B4(MazaalHeadEntity* this);
void sub_080342C8(MazaalHeadEntity* this);
void sub_08034348(MazaalHeadEntity* this);
void sub_0803438C(MazaalHeadEntity* this);
void sub_08034578(MazaalHeadEntity* this);
void sub_080344E0(MazaalHeadEntity* this);
void sub_0803451C(MazaalHeadEntity* this);
void sub_08034558(MazaalHeadEntity* this);
void sub_080346A0(MazaalHeadEntity* this);
void sub_080345D0(MazaalHeadEntity* this);
void sub_080346C8(MazaalHeadEntity* this);
void sub_08034AEC(MazaalHeadEntity* this);
void sub_08034B0C(MazaalHeadEntity* this);
void sub_08034B38(MazaalHeadEntity* this);
void sub_08034BA0(MazaalHeadEntity* this);

void (*const MazaalHead_Functions[])(MazaalHeadEntity*) = {
    MazaalHead_OnTick,
    MazaalHead_OnCollision,
    (void (*)(MazaalHeadEntity*))GenericKnockback,
    (void (*)(MazaalHeadEntity*))GenericDeath,
    (void (*)(MazaalHeadEntity*))GenericConfused,
    MazaalHead_OnGrabbed,
};
void (*const gUnk_080CECA0[])(MazaalHeadEntity*) = {
    sub_08033F1C, sub_08034A84, sub_08034AC4, sub_08034AC4, sub_08034BC8,
};
void (*const gUnk_080CECB4[])(MazaalHeadEntity*) = {
    sub_08033F3C, sub_08033FFC, sub_0803414C, sub_080341B8, sub_080341D0, sub_080341F4, sub_08034210,
    sub_08034240, sub_08034274, sub_080342A0, sub_080342B4, sub_080342C8, sub_08034348, sub_0803438C,
};
const u8 gUnk_080CECEC[] = { 0xff, 0xfe, 0xff, 0x00, 0x01, 0x02, 0x01, 0x00 };
const s8 gUnk_080CECF4[] = { -2, 0x01, -8, -4, -0x14, 0x08, 0x0e, -0x10, -6, 0x0c, 0x12, -2, 0x00, 0x00 };
const u8 gUnk_080CED02[] = { FX_GIANT_EXPLOSION, FX_GIANT_EXPLOSION, FX_GIANT_EXPLOSION2, FX_GIANT_EXPLOSION3 };
const u8 gUnk_080CED06[] = { 0xff, 0x00, 0x01, 0x00, 0x00, 0x00 };
void (*const gUnk_080CED0C[])(MazaalHeadEntity*) = {
    sub_08034578, sub_080344E0, sub_0803451C, sub_080344E0, sub_08034558, sub_080344E0, sub_0803451C, sub_080346A0,
};
void (*const gUnk_080CED2C[])(MazaalHeadEntity*) = {
    sub_08034578, sub_080345D0, sub_0803451C, sub_080346A0, sub_08034558, sub_080345D0, sub_08034558, sub_080346A0,
};
void (*const gUnk_080CED4C[])(MazaalHeadEntity*) = {
    sub_08034578, sub_080345D0, sub_080346C8, sub_080346A0, sub_08034558, sub_080346C8, sub_080346A0, sub_080346C8,
};
const s8 gUnk_080CED6C[] = { -0xa, -0xb, -0xc, -0xb, -0xa, -0x9, -0x8, -0x9 };
void (*const gUnk_080CED74[])(MazaalHeadEntity*) = {
    sub_08034AEC,
    sub_08034B0C,
    sub_08034B38,
    sub_08034BA0,
};
const s8 gUnk_080CED84[] = { 0x0d, 0x10, -0xd, 0x10 };
const Transition gUnk_080CED88 = { 1, 0, 0, 0x88, 0xf8, 0, AREA_INNER_MAZAAL, ROOM_INNER_MAZAAL_PHASE_1, 1, 0, 0, 0 };
const Transition gUnk_080CED9C = { 1, 0, 0, 0x88, 0xf8, 0, AREA_INNER_MAZAAL, ROOM_INNER_MAZAAL_MAIN, 1, 0, 0, 0 };

void MazaalHead(MazaalHeadEntity* this) {
    MazaalHead_Functions[GetNextFunction(super)](this);
}

void MazaalHead_OnTick(MazaalHeadEntity* this) {
    gUnk_080CECA0[super->type](this);
}

void MazaalHead_OnCollision(MazaalHeadEntity* this) {
    EnemyFunctionHandlerAfterCollision(super, MazaalHead_Functions);
}

void MazaalHead_OnGrabbed(MazaalHeadEntity* this) {
}

void sub_08033F1C(MazaalHeadEntity* this) {
    gUnk_080CECB4[super->action](this);
    sub_0803499C(this);
}

void sub_08033F3C(MazaalHeadEntity* this) {
    Entity* pEVar1;
    Entity* pEVar2;

    if (gEntCount < 0x43) {
        if (gRoomTransition.field_0x38 != 0) {
            sub_08034420(this);
            this->unk_81 = 1;
        } else {
            super->action = 1;
        }
        super->spriteSettings.draw = 1;
        this->unk_6d |= 1;
        this->unk_80 = 0;
        InitializeAnimation(super, 0);
        pEVar1 = CreateEnemy(MAZAAL_HEAD, 1);
#ifdef PC_PORT
        /* #140-class: GBA left this create unchecked; a full entity pool returns
         * NULL and the write below hits the read-only BIOS region at address 0
         * harmlessly on GBA but SIGSEGV at address 0 on PC. Guard the write. */
        if (pEVar1 != NULL)
#endif
        {
            pEVar1->parent = super;
        }
        pEVar2 = CreateObject(MAZAAL_OBJECT, 0, 0);
#ifdef PC_PORT
        /* #140-class: unchecked create; guard the dereference below (a NULL pool
         * slot writes harmlessly to BIOS on GBA but SIGSEGV at address 0 on PC). */
        if (pEVar2 != NULL)
#endif
        {
            pEVar2->parent = super;
        }
        if (gRoomTransition.field_0x39 == 0) {
            super->action = 0xd;
            super->subAction = 0;
            super->timer = 252;
            COLLISION_OFF(super);
        } else {
            pEVar2 = CreateEnemy(MAZAAL_BRACELET, 0);
#ifdef PC_PORT
            /* #140-class: unchecked create; guard the writes below. */
            if (pEVar2 != NULL)
#endif
            {
                pEVar2->parent = super;
                MH78_SET((MazaalHeadEntity*)pEVar2, this);
                MH74_SET(this, pEVar2);
            }
            pEVar2 = CreateEnemy(MAZAAL_BRACELET, 1);
#ifdef PC_PORT
            /* #140-class: unchecked create; guard the writes below. */
            if (pEVar2 != NULL)
#endif
            {
                pEVar2->parent = super;
                MH78_SET((MazaalHeadEntity*)pEVar2, pEVar1);
                MH78_SET(this, pEVar2);
            }
            pEVar2 = CreateEnemy(MAZAAL_HEAD, 4);
#ifdef PC_PORT
            /* #140-class: GBA inlined this create+deref; capture into a temp so PC
             * can guard the NULL pool case (harmless BIOS write on GBA). */
            if (pEVar2 != NULL)
#endif
            {
                pEVar2->parent = super;
            }
        }
    }
}

void sub_08033FFC(MazaalHeadEntity* this) {
    u16 temp;
    MazaalHeadEntity* entity;

    switch (super->subAction) {
        case 0:
        case 2:
        case 5:
            break;
        case 1:
            super->subAction = 2;
            super->timer = 30;
            /* #162: links resolve through EntityRef slots now; a NULL means the
               bracelet (or its arm piece) never spawned (full pool) — GBA wrote
               harmlessly to BIOS here, PC must skip. */
            entity = MH74(this);
            if (entity != NULL) {
                entity = MH74(entity);
            }
            if (entity != NULL) {
                entity->base.subAction = 1;
            }
            entity = MH78(this);
            if (entity != NULL) {
                entity = MH74(entity);
            }
            if (entity != NULL) {
                entity->base.subAction = 1;
            }
            UnloadOBJPalette(super);
            break;
        case 3:
            if (--super->timer == 0) {
                super->subAction = 4;
            }
            break;
        case 4:
            super->z.WORD -= Q_16_16(0.5);
            if (super->z.HALF.HI == -10) {
                super->subAction = 5;
                this->unk_81 = 1;
            }
            break;
        case 6:
            super->subAction = 7;
            super->timer = 0;
            entity = MH74(this);
            if (entity != NULL) {
                entity->base.subAction = 1;
                entity = MH74(entity);
            }
            if (entity != NULL) {
                entity->base.subAction = 4;
            }
            entity = MH78(this);
            if (entity != NULL) {
                entity->base.subAction = 1;
                entity = MH74(entity);
            }
            if (entity != NULL) {
                entity->base.subAction = 4;
            }
            gScreen.controls.layerFXControl = 0xf40;
            gScreen.controls.alphaBlend = 0x1000;
            break;
        case 7:
            temp = ++super->timer >> 1;
            gScreen.controls.alphaBlend = (temp) | (0x10 - (temp)) * 0x100;
            if (super->timer > 31) {
                super->subAction = 8;
                entity = MH74(this);
                if (entity != NULL) {
                    entity->base.subAction = 3;
                    entity = MH74(entity);
                }
                if (entity != NULL) {
                    entity->base.subAction = 6;
                }
                entity = MH78(this);
                if (entity != NULL) {
                    entity->base.subAction = 3;
                    entity = MH74(entity);
                }
                if (entity != NULL) {
                    entity->base.subAction = 6;
                }
                super->spriteRendering.alphaBlend = 0;
                gScreen.controls.layerFXControl = 0;
            }
            break;
        default:
            if (MH74(this) != NULL && (MH74(this)->base.flags & ENT_COLLIDE) != 0) {
                gRoomControls.camera_target = &gPlayerEntity.base;
                sub_08034420(this);
                gPlayerState.controlMode = CONTROL_1;
                ResetPlayerAnimationAndAction();
                SoundReq(BGM_BOSS_THEME);
            }
    }
}

void sub_0803414C(MazaalHeadEntity* this) {
    u32 x, y;

    x = gRoomControls.origin_x + 0xb8;
    y = gRoomControls.origin_y + 0x48;

    if (super->x.HALF.HI - x + 1 < 3 && super->y.HALF.HI - y + 1 < 3) {
        if (sub_080349D8(this)) {
            if (super->subAction != 0) {
                super->subAction = 0;
                sub_0803442C(this, 0x1e);
            } else {
                sub_0803442C(this, 0x5a);
            }
        }
    } else {
        super->direction = CalculateDirectionTo(super->x.HALF.HI, super->y.HALF.HI, x, y);
        LinearMoveUpdate(super);
    }
}

void sub_080341B8(MazaalHeadEntity* this) {
    if (sub_080349D8(this)) {
        sub_0803442C(this, 0x2d);
    }
}

void sub_080341D0(MazaalHeadEntity* this) {
    if (((this->unk_80 & 3) != 3) && (--super->timer == 0)) {
        sub_0803443C(this);
    }
}

void sub_080341F4(MazaalHeadEntity* this) {
    sub_080347B4(this);
    if (sub_080349D8(this)) {
        sub_08034420(this);
    }
}

void sub_08034210(MazaalHeadEntity* this) {
    sub_080347B4(this);
    if ((this->unk_7d & 0x80) != 0 && sub_080349D8(this)) {
        sub_08034420(this);
    } else {
        sub_08034830(this);
    }
}

void sub_08034240(MazaalHeadEntity* this) {
    if ((this->unk_7e != 0) && (--this->unk_7e < 0x96)) {
        sub_0803473C(this);
    }
    if (sub_080349D8(this)) {
        sub_08034420(this);
    }
}

void sub_08034274(MazaalHeadEntity* this) {
    if (sub_080349D8(this)) {
        sub_08034420(this);
    } else {
        sub_0803473C(this);
        if (sub_08034A10(this) != 0) {
            sub_080347FC(this);
        }
    }
}

void sub_080342A0(MazaalHeadEntity* this) {
    if (sub_080349D8(this)) {
        sub_08034420(this);
    }
}

void sub_080342B4(MazaalHeadEntity* this) {
    if (this->unk_7e == 0xff) {
        sub_08034420(this);
    }
}

void sub_080342C8(MazaalHeadEntity* this) {
    if (super->timer != 0) {
        if (--super->timer == 0) {
            SoundReq(SFX_115);
        }
        super->spriteOffsetX = gUnk_080CECEC[super->timer >> 1 & 7];
    } else {
        super->spriteOffsetX = 0;
        if (GravityUpdate(super, Q_8_8(32.0)) == 0) {
            super->action = 0xc;
            this->unk_7e = 0x708;
            this->unk_7d = 0;
            this->unk_81 = 0;
            InitScreenShake(30, 0);
            SoundReq(SFX_1A1);
        }
    }
}

void sub_08034348(MazaalHeadEntity* this) {
    if (this->unk_7e != 0) {
        this->unk_7e--;
    } else {
        if (super->z.HALF.HI > -10) {
            super->z.HALF.HI--;
        } else {
            if (sub_080349D8(this)) {
                this->unk_81 = 1;
                sub_08034420(this);
            }
        }
    }
}

void sub_0803438C(MazaalHeadEntity* this) {
    Entity* pEVar3;
    const s8* pVar;

    if (super->timer == 0) {
        super->health = 0;
    } else {
        super->timer--;
        if (super->timer > 192) {
            super->spriteOffsetX = gUnk_080CED06[super->timer & 3];
        } else {
            if ((super->timer & 0x1f) == 0) {
                pEVar3 = CreateFx(super, gUnk_080CED02[Random() & 3], 0);
                if (pEVar3 != NULL) {
                    pVar = &gUnk_080CECF4[super->timer >> 4];
                    pEVar3->x.HALF.HI = *pVar + pEVar3->x.HALF.HI;
                    pVar++;
                    pEVar3->y.HALF.HI = *pVar + pEVar3->y.HALF.HI;
                    pEVar3->spritePriority.b0 = 3;
                }
            }
        }
    }
}

void sub_08034420(MazaalHeadEntity* this) {
    super->action = 2;
    this->unk_7d = 3;
}

void sub_0803442C(MazaalHeadEntity* this, u32 unk) {
    super->action = 4;
    super->timer = unk;
    this->unk_7d = 3;
}

void sub_0803443C(MazaalHeadEntity* this) {
    this->unk_7d = 0;
    if (gRoomTransition.field_0x39 < 0x1f) {
        sub_080344BC(this);
    } else {
        if (gRoomTransition.field_0x39 < 0x3d) {
            sub_08034498(this);
        } else {
            sub_08034474(this);
        }
    }
}

void sub_08034474(MazaalHeadEntity* this) {
    gUnk_080CED0C[this->unk_7c](this);
    this->unk_7c = (this->unk_7c + 1) & 7;
}

void sub_08034498(MazaalHeadEntity* this) {
    gUnk_080CED2C[this->unk_7c](this);
    this->unk_7c = (this->unk_7c + 1) & 7;
}

void sub_080344BC(MazaalHeadEntity* this) {
    gUnk_080CED4C[this->unk_7c](this);
    this->unk_7c = (this->unk_7c + 1) & 7;
}

void sub_080344E0(MazaalHeadEntity* this) {
    if ((this->unk_80 & 5) != 0) {
        sub_080345A0(this);
    } else {
        if ((this->unk_80 & 10) != 0 || (Random() & 1) != 0) {
            sub_080345B8(this);
        } else {
            sub_080345A0(this);
        }
    }
}

void sub_0803451C(MazaalHeadEntity* this) {
    if ((this->unk_80 & 5) != 0) {
        sub_08034618(this);
    } else if ((this->unk_80 & 10) != 0) {
        sub_08034638(this);
    } else if ((Random() & 1) != 0) {
        sub_08034618(this);
    } else {
        sub_08034638(this);
    }
}

void sub_08034558(MazaalHeadEntity* this) {
    if ((Random() & 1) != 0) {
        sub_08034658(this);
    } else {
        sub_0803467C(this);
    }
}

void sub_08034578(MazaalHeadEntity* this) {
    super->action = 3;
    this->unk_7d = 0;
    sub_080348A4(this, MH74(this), 0);
    sub_080348A4(this, MH78(this), 0);
}

void sub_080345A0(MazaalHeadEntity* this) {
    super->action = 5;
    this->unk_7d = 1;
    sub_080348A4(this, MH78(this), 1);
}

void sub_080345B8(MazaalHeadEntity* this) {
    super->action = 5;
    this->unk_7d = 2;
    sub_080348A4(this, MH74(this), 1);
}

void sub_080345D0(MazaalHeadEntity* this) {
    super->action = 6;
    if ((Random() & 1) != 0) {
        if (sub_080348A4(this, MH74(this), 2) == 0) {
            sub_080348A4(this, MH78(this), 2);
        }
    } else {
        if (sub_080348A4(this, MH78(this), 2) == 0) {
            sub_080348A4(this, MH74(this), 2);
        }
    }
}

void sub_08034618(MazaalHeadEntity* this) {
    super->action = 7;
    this->unk_7d = 1;
    this->unk_7e = 0xb4;
    sub_080348A4(this, MH78(this), 3);
}

void sub_08034638(MazaalHeadEntity* this) {
    super->action = 7;
    this->unk_7d = 2;
    this->unk_7e = 0xb4;
    sub_080348A4(this, MH74(this), 3);
}

void sub_08034658(MazaalHeadEntity* this) {
    super->action = 8;
    this->unk_7d = 0;
    if (sub_080348A4(this, MH78(this), 4) == 0) {
        this->unk_7d |= 0x40;
    }
}

void sub_0803467C(MazaalHeadEntity* this) {
    super->action = 8;
    this->unk_7d = 0;
    if (sub_080348A4(this, MH74(this), 4) == 0) {
        this->unk_7d |= 0x40;
    }
}

void sub_080346A0(MazaalHeadEntity* this) {
    super->action = 9;
    this->unk_7d = 0;
    sub_080348A4(this, MH74(this), 6);
    sub_080348A4(this, MH78(this), 6);
}

void sub_080346C8(MazaalHeadEntity* this) {
    MazaalHeadEntity* hand;

    super->action = 10;
    this->unk_7e = 0;
    if ((Random() & 1) != 0) {
        if (sub_080348A4(this, MH74(this), 8) != 0) {
            hand = MH78(this);
            sub_080348A4(this, hand, 7);
        } else {
            sub_080348A4(this, MH74(this), 7);
            sub_080348A4(this, MH78(this), 8);
        }
    } else {
        if (sub_080348A4(this, MH78(this), 8) != 0) {
            hand = MH74(this);
            sub_080348A4(this, hand, 7);
        } else {
            sub_080348A4(this, MH74(this), 8);
            sub_080348A4(this, MH78(this), 7);
        }
    }
}

void sub_0803473C(MazaalHeadEntity* this) {
    u32 playerX;
    u32 roomX;

    if ((this->unk_7d & 0x10) != 0) {
        if (MH74(this) == NULL || 0x28 < MH74(this)->base.action) {
            return;
        }
        playerX = gPlayerEntity.base.x.HALF.HI - 0x60;
    } else {
        if (MH78(this) == NULL || 0x28 < MH78(this)->base.action) {
            return;
        }
        playerX = gPlayerEntity.base.x.HALF.HI + 0x60;
    }
    roomX = gRoomControls.origin_x;
    if (playerX - 4 > super->x.HALF.HI) {
        if (roomX + 0xe0 < super->x.HALF.HI) {
            return;
        }
        super->direction = DirectionEast;
    } else {
        if (playerX + 4 >= super->x.HALF.HI) {
            return;
        }
        if (roomX + 0x90 > super->x.HALF.HI) {
            return;
        }
        super->direction = DirectionWest;
    }
    LinearMoveUpdate(super);
}

void sub_080347B4(MazaalHeadEntity* this) {
    u32 playerX = gPlayerEntity.base.x.HALF.HI;
    u32 roomX = gRoomControls.origin_x;
    if (playerX - 4 > super->x.HALF.HI) {
        if (roomX + 0xe0 < super->x.HALF.HI) {
            return;
        }
        super->direction = DirectionEast;
    } else {
        if (playerX + 4 >= super->x.HALF.HI) {
            return;
        }
        if (roomX + 0x90 > super->x.HALF.HI) {
            return;
        }
        super->direction = DirectionWest;
    }
    LinearMoveUpdate(super);
}

void sub_080347FC(MazaalHeadEntity* this) {
    if ((this->unk_7d & 0x10) != 0) {
        sub_080348A4(this, MH78(this), 5);
    } else {
        sub_080348A4(this, MH74(this), 5);
    }
    this->unk_7d |= 0x20;
}

void sub_08034830(MazaalHeadEntity* this) {
    if (sub_080349D8(this)) {
        this->unk_7d = 0x80;
        sub_080345D0(this);
    } else {
        if (sub_08034A10(this) != 0) {
            if ((this->unk_80 & 0xf) != 0) {
                if (sub_080349D8(this)) {
                    sub_08034420(this);
                }
            } else {
                this->unk_7d |= 0x20;
                if ((this->unk_7d & 0x10) != 0) {
                    sub_080348A4(this, MH78(this), 2);
                } else {
                    sub_080348A4(this, MH74(this), 2);
                }
            }
        }
    }
}

u32 sub_080348A4(MazaalHeadEntity* this, MazaalHeadEntity* hand_, u32 unk) {
    u32 bVar1;

#ifdef PC_PORT
    /* #162/#140-class: link may be unset (full pool at spawn). GBA read BIOS
     * bytes here harmlessly; treat as "arm unavailable". */
    if (hand_ == NULL) {
        return 0;
    }
#endif

    if (hand_->base.type == 0) {
        bVar1 = 5;
        this->unk_7d |= 0x10;
    } else {
        bVar1 = 10;
    }

    if (this->unk_80 & bVar1) {
        this->unk_7d |= (hand_->base.type == 0) ? 1 : 2;
        return 0;
    } else {
        switch (unk) {
            case 0:
                hand_->base.action = 3;
                break;
            case 1:
                hand_->base.action = 4;
                hand_->unk_7c = 3;
                break;
            case 2:
            case 4:
                hand_->base.action = 4;
                hand_->unk_7c = 1;
                break;
            case 3:
                hand_->base.action = 0xf;
                hand_->unk_7e = 0xb4;
                break;
            case 5:
                hand_->base.action = 0xf;
                hand_->unk_7e = 1;
                break;
            case 6:
                hand_->base.action = 0x1e;
                break;
            case 7:
                hand_->base.action = 0x1b;
                hand_->base.flags = hand_->base.flags & ~ENT_COLLIDE;
                InitializeAnimation(&hand_->base, 5);
                break;
            case 8:
                bVar1 = hand_->base.type + 2;
                hand_ = (MazaalHeadEntity*)CreateEnemy(MAZAAL_HEAD, bVar1);
                if (hand_ != NULL) {
                    hand_->base.parent = super;
                }
                break;
            default:
                break;
        }
        return 1;
    }
}

void sub_0803499C(MazaalHeadEntity* this) {
    if (((this->unk_80 & 0xc) != 0xc) && (this->unk_81 != 0)) {
        super->z.HALF.HI = gUnk_080CED6C[(++super->subtimer >> 4) & 7];
    }
}

bool32 sub_080349D8(MazaalHeadEntity* this) {
    // super feels like a switch, but I couldn't get it to match as one
    if ((this->unk_7d & 3) == 3) {
        return TRUE;
    } else {
        if ((this->unk_7d & 3) == 1) {
            if (this->unk_80 & 10)
                return TRUE;
        } else {
            if ((this->unk_7d & 3) == 2) {
                if (this->unk_80 & 5)
                    return TRUE;
            }
        }
        return FALSE;
    }
}

u32 sub_08034A10(MazaalHeadEntity* this) {
    if ((this->unk_7d & 0x60) != 0x40) {
        if ((this->unk_7d & 0x10) != 0) {
            if ((this->unk_80 & 5) != 0) {
                this->unk_7d |= 0x40;
            }
            if (((this->unk_80 & 10) != 0) && ((this->unk_7d & 3) == 1)) {
                return 1;
            }
        } else {
            if ((this->unk_80 & 10) != 0) {
                this->unk_7d |= 0x40;
            }
            if ((this->unk_80 & 5) == 0) {
                return 0;
            }
            if ((this->unk_7d & 3) == 2) {
                return 1;
            }
        }
    } else {
        return 1;
    }
    return 0;
}

void sub_08034A84(MazaalHeadEntity* this) {
#ifdef PC_PORT
    if (super->parent == NULL)
        return; /* #136 family */
#endif
    if (super->parent->next == NULL) {
        DeleteEntity(super);
    } else {
        if (super->action == 0) {
            super->action = 1;
            super->spriteSettings.draw = 1;
            super->spriteSettings.flipX = 1;
            InitializeAnimation(super, 0);
        }
        CopyPositionAndSpriteOffset(super->parent, super);
    }
}

void sub_08034AC4(MazaalHeadEntity* this) {
    PositionRelative(super->parent, super, 0, Q_16_16(1.0));
    gUnk_080CED74[super->action](this);
}

void sub_08034AEC(MazaalHeadEntity* this) {
    super->action = 1;
    super->timer = 30;
    InitializeAnimation(super, super->type + 1);
    SoundReq(SFX_151);
}

void sub_08034B0C(MazaalHeadEntity* this) {
    if (super->timer != 0) {
        super->timer--;
    } else {
        GetNextFrame(super);
        if ((super->frame & ANIM_DONE) == 0) {
            return;
        }
        super->action = 2;
        super->timer = 80;
    }
}

void sub_08034B38(MazaalHeadEntity* this) {
    Entity* target;
    const s8* ptr;

    if (--super->timer == 0) {
        super->action = 3;
        InitializeAnimation(super, super->type + 3);
    } else {
        target = CreateProjectile(MAZAAL_ENERGY_BEAM);
        if (target != NULL) {
            target->type = super->type - 2;
            if (target->type == 0) {
                target->direction = 0xa8 - super->timer;
            } else {
                target->direction = super->timer + 0x58;
            }
            ptr = &gUnk_080CED84[target->type * 2];
            PositionRelative(super, target, Q_16_16(ptr[0]), Q_16_16(ptr[1]));
        }
    }
}

void sub_08034BA0(MazaalHeadEntity* this) {
    GetNextFrame(super);
    if ((super->frame & ANIM_DONE) != 0) {
        ((MazaalHeadEntity*)super->parent)->unk_7e = 0xff;
        DeleteEntity(super);
    }
}

void sub_08034BC8(MazaalHeadEntity* this) {
    if (super->action == 0) {
        super->action = 1;
        InitializeAnimation(super, 1);
    }
    sub_0806FA90(super->parent, super, 0, 1);
    sub_0800445C(super);
    if (super->z.HALF.HI == 0) {
        sub_08034C00(this);
    }
}

void sub_08034C00(MazaalHeadEntity* this) {
    if (((gPlayerState.flags & PL_MINISH) != 0) &&
        CheckPlayerInRegion(super->x.HALF.HI - gRoomControls.origin_x, super->y.HALF.HI - gRoomControls.origin_y + 0xd,
                            3, 3) != 0) {
        if (gRoomTransition.field_0x39 >= 0x3d) {
            DoExitTransition(&gUnk_080CED88);
        } else {
            DoExitTransition(&gUnk_080CED9C);
        }
    }
}
