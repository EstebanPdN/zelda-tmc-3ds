#include "global.h"
#include "object.h"
#include "player.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    Entity base;
    uint8_t padding[0x18];
} TestPullableMushroom;

extern bool32 sub_0808B21C(void* mushroom, u32 gustPath);
extern void sub_0808B05C(void* mushroom);

PlayerEntity gPlayerEntity;

static Entity* sCreateResults[2];
static int sCreateCalls;
static Entity* sDeleted;
static int sDeleteCalls;
static int sFailures;

Entity* CreateObjectWithParent(Entity* parent, Object subtype, u32 form, u32 type2) {
    Entity* result = sCreateCalls < 2 ? sCreateResults[sCreateCalls] : NULL;
    (void)subtype;
    (void)form;
    (void)type2;
    sCreateCalls++;
    if (result != NULL) {
        result->parent = parent;
    }
    return result;
}

void DeleteEntity(Entity* entity) {
    sDeleted = entity;
    sDeleteCalls++;
}

void InitializeAnimation(Entity* entity, u32 animation) {
    entity->animIndex = (u16)animation;
}

#define CHECK(condition, message)                     \
    do {                                              \
        if (!(condition)) {                           \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                              \
        }                                             \
    } while (0)

static void ResetMocks(Entity* first, Entity* second) {
    sCreateResults[0] = first;
    sCreateResults[1] = second;
    sCreateCalls = 0;
    sDeleted = NULL;
    sDeleteCalls = 0;
}

static void ResetHost(TestPullableMushroom* host, Entity* oldChild, Entity* oldParent) {
    memset(host, 0, sizeof(*host));
    host->base.child = oldChild;
    host->base.parent = oldParent;
    host->base.spritePriority.b0 = 3;
    host->base.animationState = 3;
    host->base.direction = 24;
    host->base.spriteSettings.flipX = 1;
}

int main(void) {
    TestPullableMushroom host;
    Entity child;
    Entity affine;
    Entity oldChild;
    Entity oldParent;
    Entity failedChild;

    memset(&child, 0, sizeof(child));
    memset(&affine, 0, sizeof(affine));
    memset(&oldChild, 0, sizeof(oldChild));
    memset(&oldParent, 0, sizeof(oldParent));
    memset(&failedChild, 0, sizeof(failedChild));

    ResetHost(&host, &oldChild, &oldParent);
    ResetMocks(NULL, &affine);
    CHECK(!sub_0808B21C(&host, 0), "first allocation failure is reported");
    CHECK(sCreateCalls == 1, "second allocation is not attempted without the first");
    CHECK(sDeleteCalls == 0, "nothing is deleted when the first allocation fails");
    CHECK(host.base.child == &oldChild && host.base.parent == &oldParent,
          "first failure does not publish partial pointers");
    CHECK(host.base.spritePriority.b0 == 3, "first failure preserves sprite priority");

    memset(&child, 0, sizeof(child));
    ResetHost(&host, &oldChild, &oldParent);
    ResetMocks(&child, NULL);
    CHECK(!sub_0808B21C(&host, 0), "second allocation failure is reported");
    CHECK(sCreateCalls == 2, "both allocations are attempted in order");
    CHECK(sDeleteCalls == 1 && sDeleted == &child, "first entity is rolled back on second failure");
    CHECK(host.base.child == &oldChild && host.base.parent == &oldParent,
          "second failure does not publish partial pointers");
    CHECK(host.base.spritePriority.b0 == 3, "second failure preserves sprite priority");

    memset(&child, 0, sizeof(child));
    memset(&affine, 0, sizeof(affine));
    ResetHost(&host, &oldChild, &oldParent);
    ResetMocks(&child, &affine);
    CHECK(sub_0808B21C(&host, 1), "complete allocation succeeds");
    CHECK(sCreateCalls == 2 && sDeleteCalls == 0, "successful pair needs no rollback");
    CHECK(host.base.child == &child && host.base.parent == &affine, "pair is published atomically");
    CHECK(host.base.spritePriority.b0 == 6, "successful pair commits stretched priority");
    CHECK(child.parent == &host.base && child.type2 == 1, "cap child links to its mushroom and gust path");
    CHECK(child.animationState == 3 && child.direction == 24 && child.spriteSettings.flipX == 1,
          "cap child inherits orientation");
    CHECK(affine.animationState == 3 && affine.child == &child, "affine helper links to the cap child");

    /* Exercise the real gust-setup caller as well as the transaction itself:
     * a partial first attempt must remain in setup (subAction 0), publish no
     * child, and a later successful retry must advance exactly once. */
    memset(&gPlayerEntity, 0, sizeof(gPlayerEntity));
    gPlayerEntity.base.animationState = 2;
    ResetHost(&host, &oldChild, &oldParent);
    host.base.type = 0;
    host.base.subAction = 0;
    host.base.flags |= ENT_COLLIDE;
    ResetMocks(&failedChild, NULL);
    sub_0808B05C(&host);
    CHECK(host.base.subAction == 0, "partial gust allocation does not advance its action");
    CHECK(host.base.child == &oldChild && host.base.parent == &oldParent,
          "partial gust allocation publishes no duplicate child");
    CHECK(sDeleteCalls == 1 && sDeleted == &failedChild, "partial gust child is rolled back before retry");

    memset(&child, 0, sizeof(child));
    memset(&affine, 0, sizeof(affine));
    ResetMocks(&child, &affine);
    sub_0808B05C(&host);
    CHECK(host.base.subAction == 1, "successful gust retry advances setup exactly once");
    CHECK(host.base.child == &child && host.base.parent == &affine,
          "successful gust retry publishes only its complete pair");
    CHECK(sDeleteCalls == 0, "successful retry has no stale rollback");

    if (sFailures != 0) {
        return 1;
    }
    printf("pullable_mushroom_allocation_test: ALL PASS\n");
    return 0;
}
