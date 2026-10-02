/* Production death handler with controlled allocation failure, based on
 * Project Picori 29928eec4c70 (GPL-3.0), with success/retry coverage. */
#include "enemy.h"
#include "object/deathFx.h"
#include "room.h"
#include "save.h"
#include <stdio.h>
#include <string.h>

SaveFile gSave;
RoomTransition gRoomTransition;
static DeathFxObject sFx;
static int sAvailable, sDeleted, sAllocations, sRespawnDisabled;
static u32 sPriority;
extern void EnemyCreateDeathFX(Enemy*, u32, u32);

Entity* CreateObject(u32 id, u32 type, u32 type2) {
    (void)id; (void)type; (void)type2;
    ++sAllocations;
    return sAvailable ? &sFx.base : NULL;
}
void EnemyDisableRespawn(Enemy* enemy) { (void)enemy; ++sRespawnDisabled; }
void SetEntityPriority(Entity* entity, u32 priority) { (void)entity; sPriority = priority; }
void PositionRelative(Entity* origin, Entity* entity, s32 x, s32 y) {
    (void)origin; (void)entity; (void)x; (void)y;
}
void CopyPosition(Entity* origin, Entity* entity) {
    entity->x = origin->x; entity->y = origin->y;
}
void DeleteThisEntity(void) { ++sDeleted; }
void DeleteEntity(Entity* entity) { (void)entity; ++sDeleted; }
void sub_0807CD9C(void) {}
void SoundReq(u32 sound) { (void)sound; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char** argv) {
    /* Optional case selector lets each old null-dereference fail separately. */
    int first = argc > 1 ? argv[1][0] - '0' : 0;
    int last = argc > 1 ? first + 1 : 5;
    for (int branch = first; branch < last; ++branch) {
        for (int success = 0; success <= 1; ++success) {
            Enemy enemy = {0};
            memset(&gSave, 0, sizeof(gSave));
            memset(&sFx, 0, sizeof(sFx));
            sAvailable = success;
            sDeleted = sAllocations = sRespawnDisabled = 0;
            enemy.base.x.HALF.HI = 40;
            enemy.base.y.HALF.HI = 50;
            if (branch == 0) enemy.enemyFlags = EM_FLAG_NO_DEATH_FX;
            if (branch >= 1 && branch <= 3) {
                enemy.base.contactFlags = 0x13;
                enemy.base.gustJarFlags = branch;
            }
            EnemyCreateDeathFX(&enemy, 7, 9);
            CHECK(sAllocations == 1 && sRespawnDisabled == 1);
            CHECK(gSave.enemies_killed == 1);
            CHECK(sPriority == 3);
            CHECK(sDeleted == (branch < 4));
            if (success) {
                CHECK(sFx.parentId == 7 && sFx.item == 9);
                CHECK(sFx.base.x.HALF.HI == 40 && sFx.base.y.HALF.HI == 50);
                CHECK(sFx.base.child == &enemy.base);
                CHECK(sFx.unk6c == (branch == 0 ? 8 : branch == 1 ? 4 : branch == 2 ? 2 : 0));
                CHECK(sFx.base.parent == (branch >= 1 && branch <= 3 ? NULL : &enemy.base));
            }
            if (branch == 4) {
                EnemyCreateDeathFX(&enemy, 7, 9);
                CHECK(sAllocations == 1 && gSave.enemies_killed == 1);
                enemy.base.timer = 0;
                EnemyCreateDeathFX(&enemy, 7, 9);
                CHECK(sDeleted == 1);
            }
        }
    }
    Enemy boss = {0};
    boss.enemyFlags = EM_FLAG_BOSS;
    sAvailable = sDeleted = sAllocations = 0;
    EnemyCreateDeathFX(&boss, 7, 9);
    CHECK(sDeleted == 0 && !(boss.enemyFlags & EM_FLAG_BOSS_KILLED));
    sAvailable = 1;
    EnemyCreateDeathFX(&boss, 7, 9);
    CHECK(sDeleted == 1 && (boss.enemyFlags & EM_FLAG_BOSS_KILLED));
    EnemyCreateDeathFX(&boss, 7, 9);
    CHECK(sAllocations == 2 && sDeleted == 1);
    puts("enemy_death_fx_test: allocation failure/success/cleanup/boss retry PASS");
    return 0;
}
