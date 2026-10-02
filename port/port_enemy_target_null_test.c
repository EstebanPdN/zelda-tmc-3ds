#include "entity.h"

#include <stdio.h>
#include <string.h>

extern u32 sub_0800132C(Entity* entity, Entity* target);

static int sFacingCalls;
static int sFailures;

u32 GetFacingDirection(Entity* entity, Entity* target) {
    (void)entity;
    (void)target;
    sFacingCalls++;
    return 7;
}

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                             \
        }                                            \
    } while (0)

int main(void) {
    Entity enemy;
    Entity target;

    memset(&enemy, 0, sizeof(enemy));
    memset(&target, 0, sizeof(target));
    enemy.collisionLayer = 1;
    target.collisionLayer = 1;
    enemy.x.HALF.HI = 32;
    target.x.HALF.HI = 64;

    /* Exact failure shape from crash_dump_00000028: Leever_Move passes a
     * valid enemy with gEnemyTarget == NULL during a transition. */
    CHECK(sub_0800132C(&enemy, NULL) == 0xFF, "missing player target returns no direction");
    CHECK(sFacingCalls == 0, "missing target never reaches facing calculation");

    CHECK(sub_0800132C(NULL, &target) == 0xFF, "missing enemy is also rejected safely");
    CHECK(sFacingCalls == 0, "missing enemy never reaches facing calculation");

    CHECK(sub_0800132C(&enemy, &target) == 7, "normal separated entities still calculate facing");
    CHECK(sFacingCalls == 1, "normal path calls facing calculation exactly once");

    if (sFailures != 0) return 1;
    puts("port_enemy_target_null_test: ALL PASS");
    return 0;
}
