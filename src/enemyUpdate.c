#include "entity.h"
#include "enemy.h"
#include "asm.h"
#include "effects.h"

extern void (*const gEnemyFunctions[])(Entity*);
extern void Knockback1(Entity*);
extern void Knockback2(Entity*);
extern void EnemyDetachFX(Entity*);
extern void UpdateAnimationVariableFrames(Entity*, u32);
extern void CreatePitFallFx(Entity*);

s32 sub_080012DC(Entity* entity) {
    u32 hazard;
    if (entity->gustJarState & 4)
        return 0;

    hazard = GetTileHazardType(entity);

    if (hazard == 4)
        return 0;

    if (hazard == 0) {
        if (entity->gustJarState & 1)
            entity->flags |= 0x80;
        return 0;
    }

    if (hazard == 1)
        return 1;

    entity->timer = 1;
    entity->gustJarState |= 1;
    return (s32)hazard;
}

typedef void (*HazardFn)(Entity*);
static void HazardNull(Entity* e) {
    (void)e;
}

void sub_08001214(Entity*);
#ifdef PC_PORT
void (*const gUnk_080012C8[])(Entity*) = {
    HazardNull,
    sub_08001214,
    CreateDrownFx,
    CreateLavaDrownFx,
    CreateSwampDrownFx,
};
#else
extern void (*const gUnk_080012C8[])(Entity*);
#endif

void sub_08001290(Entity* entity, u32 hazardType) {
    if (hazardType != 0) {
        gUnk_080012C8[hazardType](entity);
    }
}

void sub_08001214(Entity* entity) {
    if (!(entity->gustJarState & 1)) {
        u8 t = 1;
        entity->gustJarState = 1;
        if (entity->frameSpriteSettings & 0x80)
            t = 0x20;
        entity->timer = t;
    }
    entity->timer--;
    if (entity->timer == 0) {
        CreatePitFallFx(entity);
        return;
    }
    UpdateAnimationVariableFrames(entity, 4);
}

void EnemyFunctionHandler(Entity* entity, EntityActionArray actions) {
    s32 hazard = sub_080012DC(entity);
    void (*fn)(Entity*);

    if (hazard != 0) {
        fn = gUnk_080012C8[hazard];
    } else {
        u32 idx = GetNextFunction(entity);
        fn = actions[idx];
    }
    fn(entity);
}

void GenericConfused(Entity* entity) {
    entity->confusedTime--;
    if (entity->confusedTime < 0x3C) {
        u8 phase = entity->confusedTime & 3;
        entity->spriteOffsetX = (phase == 1 ? 1 : (phase == 3 ? -1 : 0));
        if (entity->confusedTime == 0) {
            Entity* fx = ((Enemy*)entity)->child;
            if (fx != NULL && fx->kind == OBJECT && fx->id == 0xF && fx->type == 0x1C) {
                EnemyDetachFX(entity);
            }
        }
    }
    GravityUpdate(entity, 0x1800);
}

void GenericKnockback(Entity* entity) {
    Knockback1(entity);
}

void GenericKnockback2(Entity* entity) {
    Knockback2(entity);
}

void sub_08001318(Entity* entity) {
    if ((s16)entity->z.HALF.HI < 0)
        entity->direction = 0xFF;
    Knockback1(entity);
}

extern u32 GetFacingDirection(Entity*, Entity*);

u32 sub_0800132C(Entity* entity, Entity* target) {
    s32 dx, dy;
    if (!(entity->collisionLayer & target->collisionLayer))
        return 0xFF;
    dx = entity->x.HALF.HI - target->x.HALF.HI + 8;
    if ((u32)dx >= 0x11)
        return GetFacingDirection(entity, target);
    dy = entity->y.HALF.HI - target->y.HALF.HI + 8;
    if ((u32)dy < 0x11)
        return 0xFF;
    return GetFacingDirection(entity, target);
}

void EnemyUpdate(Entity* entity) {
    if (entity->action == 0) {
        if (!EnemyInit((Enemy*)entity)) {
            DeleteThisEntity();
            return;
        }
    } else {
        if (EntityDisabled(entity)) {
            goto draw;
        }
        sub_080028E0(entity);
    }

    {
        Enemy* enemy = (Enemy*)entity;
        u8 eflags = enemy->enemyFlags;
        if (!(eflags & EM_FLAG_SUPPORT)) {
            gEnemyFunctions[entity->id](entity);
            entity->contactFlags &= ~CONTACT_NOW;
        }
    }

draw:
    DrawEntity(entity);
}
