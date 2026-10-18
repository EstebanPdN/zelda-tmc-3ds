#include "asm.h"
#include "global.h"
#include "entity.h"

#include "map.h"
#include "player.h"
#include "room.h"
#include "enemy.h"

extern Entity* (*const gUnk_080D3BE8[])(void);
Entity* gEnemyTarget;

Entity* sub_08049DF4(u32 arg0) {
    if (gEnemyTarget != NULL) {
        return gEnemyTarget;
    }
    return gUnk_080D3BE8[arg0]();
}

Entity* sub_08049E18(void) {
    if ((gPlayerState.killed == 0) &&
        !(gPlayerState.flags &
          (PL_BUSY | PL_DROWNING | PL_CAPTURED | PL_USE_PORTAL | PL_HIDDEN | PL_DISABLE_ITEMS | PL_FALLING | PL_FROZEN |
           PL_IN_MINECART | PL_PIT_IS_EXIT | PL_MOLDWORM_CAPTURED | PL_IN_HOLE | PL_CONVEYOR_PUSHED | PL_CLIMBING))) {
        gEnemyTarget = &gPlayerEntity.base;
        return &gPlayerEntity.base;
    }
    return NULL;
}

Entity* sub_08049E4C(void) {
    if ((gPlayerState.killed == 0) &&
        !(gPlayerState.flags & (PL_BUSY | PL_DROWNING | PL_CAPTURED | PL_USE_PORTAL | PL_HIDDEN | PL_MINISH |
                                PL_DISABLE_ITEMS | PL_FALLING | PL_FROZEN | PL_IN_MINECART | PL_PIT_IS_EXIT |
                                PL_MOLDWORM_CAPTURED | PL_IN_HOLE | PL_CONVEYOR_PUSHED | PL_CLIMBING))) {
        gEnemyTarget = &gPlayerEntity.base;
        return &gPlayerEntity.base;
    }
    return NULL;
}

Entity* sub_08049E80(void) {
    if ((gPlayerState.killed != 0) || !(gPlayerState.flags & PL_MINISH)) {
        return NULL;
    }
    gEnemyTarget = &gPlayerEntity.base;
    return &gPlayerEntity.base;
}

Entity* sub_08049EB0(void) {
    if ((gPlayerState.killed == 0) &&
        !(gPlayerState.flags & (PL_MOLDWORM_CAPTURED | PL_DISABLE_ITEMS | PL_MINISH | PL_CAPTURED))) {
        gEnemyTarget = &gPlayerEntity.base;
        return &gPlayerEntity.base;
    }
    return NULL;
}

u32 sub_08049EE4(Entity* ent) {
    Enemy* enemy = (Enemy*)ent;
    u16 tempLO = enemy->homeX + 4 * enemy->rangeX;
    u16 tempHI = enemy->homeY + 4 * enemy->rangeY;

    return CalculateDirectionTo(ent->x.HALF.HI, ent->y.HALF.HI, tempLO, tempHI);
}

bool32 sub_08049F1C(Entity* entA, Entity* entB, s32 maxDist) {
    if ((entA->collisionLayer & entB->collisionLayer) != 0) {
        s32 xDiff = entB->x.HALF.HI - entA->x.HALF.HI;
        s32 yDiff = entB->y.HALF.HI - entA->y.HALF.HI;
        s32 maxDistSq = maxDist * maxDist;
        s32 distSq = xDiff * xDiff + yDiff * yDiff;

        if (maxDistSq >= distSq) {
            return TRUE;
        }
    }
    return FALSE;
}

bool32 PlayerInRange(Entity* ent, u32 arg1, s32 maxDist) {
    Entity* tempEnt = sub_08049DF4(arg1);
    if (tempEnt == NULL) {
        return FALSE;
    } else {
        return sub_08049F1C(ent, tempEnt, maxDist);
    }
}

u32 sub_08049F84(Entity* ent, s32 arg2) {
    Entity* target = sub_08049DF4(arg2);

    if (target == NULL) {
        return 0xFF;
    } else {
        return GetFacingDirection(ent, target);
    }
}

bool32 sub_08049FA0(Entity* ent) {
    Enemy* enemy = (Enemy*)ent;
    u32 temp = 8 * enemy->rangeX;

    if (temp >= ent->x.HALF.HI - (u16)enemy->homeX) {
        temp = 8 * enemy->rangeY;
        if (temp >= ent->y.HALF.HI - (u16)enemy->homeY) {
            return TRUE;
        }
    }
    return FALSE;
}

bool32 sub_08049FDC(Entity* ent, u32 arg1) {
    u32 temp;
    Enemy* enemy = (Enemy*)ent;
    Entity* tempEnt = sub_08049DF4(arg1);

    if (tempEnt != NULL) {
        temp = 8 * enemy->rangeX;
        if (temp >= tempEnt->x.HALF.HI - (u16)enemy->homeX) {
            temp = 8 * enemy->rangeY;
            if (temp >= tempEnt->y.HALF.HI - (u16)enemy->homeY) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

u32 sub_0804A024(Entity* ent, u32 arg1, u32 arg2) {
    Entity* tempEnt = sub_08049DF4(arg1);
    if (tempEnt == NULL) {
        return 0xFF;
    } else {
        return sub_0804A044(ent, tempEnt, arg2);
    }
}

u32 sub_0804A168(Entity*, Entity*, MapLayer* mapLayer);
u32 sub_0804A318(Entity*, Entity*, MapLayer* mapLayer);

u32 sub_0804A044(Entity* entA, Entity* entB, u32 arg2) {
    MapLayer* mapLayer;
    s32 ret;
    s32 yDiff;
    s32 xDiff;
    s32 flags;

    if ((entB->collisionLayer & entA->collisionLayer) != 0) {
        flags = 0;
        xDiff = entB->x.HALF.HI + entB->hitbox->offset_x - entA->x.HALF.HI - entA->hitbox->offset_x;

        if (arg2 >= xDiff + (arg2 >> 1)) {
            flags |= 1;
        }
        if (arg2 << 1 >= xDiff + arg2) {
            flags |= 2;
        }

        yDiff = entB->y.HALF.HI + entB->hitbox->offset_y - entA->y.HALF.HI - entA->hitbox->offset_y;
        if (arg2 >= yDiff + (arg2 >> 1)) {
            flags |= 4;
        }
        if (arg2 << 1 >= yDiff + arg2) {
            flags |= 8;
        }

        //! @bug flags & 5 can never equal 0xA
        if (flags && ((flags & 5) != 0xA)) {
            mapLayer = GetLayerByIndex(entA->collisionLayer);
            if (xDiff < 0) {
                xDiff = -xDiff;
            }
            if (yDiff < 0) {
                yDiff = -yDiff;
            }
            if (xDiff < yDiff) {
                if (flags & 1) {
                    ret = sub_0804A168(entA, entB, mapLayer);
                    if (ret != 0xFF) {
                        return ret;
                    }
                }
                if (!(flags & 4)) {
                    return 0xFF;
                }
                ret = sub_0804A318(entA, entB, mapLayer);
            } else {
                if (flags & 4) {
                    ret = sub_0804A318(entA, entB, mapLayer);
                    if (ret != 0xFF) {
                        return ret;
                    }
                }
                if (!(flags & 1)) {
                    return 0xFF;
                }
                ret = sub_0804A168(entA, entB, mapLayer);
            }
            if (ret != 0xFF) {
                return ret;
            }
        }
    }
    return 0xFF;
}

bool32 sub_0804A4BC(u8* from, u8* to, s32 step, u32 bitmask);

u32 sub_0804A168(Entity* entA, Entity* entB, MapLayer* mapLayer) {
    u32 uVar2;
    u32 uVar3;
    u32 tile1;
    u32 tile2;

    if (entB->y.HALF.HI > entA->y.HALF.HI) {
        uVar2 = entA->x.HALF.HI - 4;
        uVar3 = ((uVar2 & 0xF) < 8) ? 10 : 5;
        tile1 = TILE(uVar2, entA->y.HALF.HI + 10);
        tile2 = TILE(uVar2, entB->y.HALF.HI);

        if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], 0x40, uVar3)) {
            uVar2 = entA->x.HALF.HI + 4;
            uVar3 ^= 0xF;
            tile1 = TILE(uVar2, entA->y.HALF.HI + 10);
            tile2 = TILE(uVar2, entB->y.HALF.HI);

            if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], 0x40, uVar3)) {
                return 0x10;
            }
        }
    } else {
        uVar2 = entA->x.HALF.HI - 4;
        uVar3 = ((uVar2 & 0xF) < 8) ? 10 : 5;
        tile1 = TILE(uVar2, entA->y.HALF.HI - 10);
        tile2 = TILE(uVar2, entB->y.HALF.HI);

        if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], -0x40, uVar3)) {
            uVar2 = entA->x.HALF.HI + 4;
            uVar3 ^= 0xF;
            tile1 = TILE(uVar2, entA->y.HALF.HI - 10);
            tile2 = TILE(uVar2, entB->y.HALF.HI);

            if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], -0x40, uVar3)) {
                return 0;
            }
        }
    }
    return 0xFF;
}

u32 sub_0804A318(Entity* entA, Entity* entB, MapLayer* mapLayer) {
    u32 uVar2;
    u32 uVar3;
    u32 tile1;
    u32 tile2;

    if (entB->x.HALF.HI > entA->x.HALF.HI) {
        uVar2 = entA->y.HALF.HI - 4;
        uVar3 = ((uVar2 & 0xF) < 8) ? 0xC : 3;
        tile1 = TILE(entA->x.HALF.HI + 10, uVar2);
        tile2 = TILE(entB->x.HALF.HI, uVar2);

        if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], 1, uVar3)) {
            uVar2 = entA->y.HALF.HI + 4;
            uVar3 ^= 0xF;
            tile1 = TILE(entA->x.HALF.HI + 10, uVar2);
            tile2 = TILE(entB->x.HALF.HI, uVar2);

            if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], 1, uVar3)) {
                return 8;
            }
        }
    } else {
        uVar2 = entA->y.HALF.HI - 4;
        uVar3 = ((uVar2 & 0xF) < 8) ? 0xC : 3;
        tile1 = TILE(entA->x.HALF.HI - 10, uVar2);
        tile2 = TILE(entB->x.HALF.HI, uVar2);

        if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], -1, uVar3)) {
            uVar2 = entA->y.HALF.HI + 4;
            uVar3 ^= 0xF;
            tile1 = TILE(entA->x.HALF.HI - 10, uVar2);
            tile2 = TILE(entB->x.HALF.HI, uVar2);
            if (sub_0804A4BC(&mapLayer->collisionData[tile1], &mapLayer->collisionData[tile2], -1, uVar3)) {
                return 0x18;
            }
        }
    }
    return 0xFF;
}

bool32 sub_0804A4BC(u8* from, u8* to, s32 step, u32 bitmask) {
    while (from != to) {
        u8 r0 = *from;

        if (r0 != 0) {
            if (r0 > 0xF) {
                return FALSE;
            }

            r0 &= bitmask;
            if (r0 != 0) {
                return FALSE;
            }
        }
        from += step;
    }

    return TRUE;
}
