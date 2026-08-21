#include "collision.h"
#include "entity.h"
#include "hitbox.h"
#include "projectile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void ArrowProjectile_Init(Entity*);
extern void sub_080A94C0(Entity*, u32);
extern const Hitbox gUnk_08129A18;
extern const Hitbox gUnk_08129A20;

static int sFailures;
static unsigned int sAnimationCalls;
static u32 sLastAnimation;

/* sub_080A94C0 is the production direction/geometry selector.  Animation
 * decoding is unrelated to collision, so keep only its observable argument. */
void InitializeAnimation(Entity* entity, u32 animation) {
    (void)entity;
    sAnimationCalls++;
    sLastAnimation = animation;
}

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                             \
        }                                            \
    } while (0)

static s32 Abs32(s32 value) {
    return value < 0 ? -value : value;
}

/* Independent, readable form of retail IsColliding for ordinary 2D hitboxes.
 * Width and height are half-extents and the boundary pixel is inclusive. */
static bool32 Retail2DOracle(const Entity* first, const Entity* second) {
    const s32 firstX = first->x.HALF.HI + first->hitbox->offset_x;
    const s32 firstY = first->y.HALF.HI + first->hitbox->offset_y;
    const s32 secondX = second->x.HALF.HI + second->hitbox->offset_x;
    const s32 secondY = second->y.HALF.HI + second->hitbox->offset_y;
    const s32 maxX = first->hitbox->width + second->hitbox->width;
    const s32 maxY = first->hitbox->height + second->hitbox->height;

    return (first->collisionLayer & second->collisionLayer) != 0 &&
           Abs32(firstX - secondX) <= maxX && Abs32(firstY - secondY) <= maxY &&
           Abs32(first->z.HALF.HI - second->z.HALF.HI) <= 10;
}

static void PlacePlayerCenter(Entity* player, const Entity* arrow, s32 deltaX, s32 deltaY) {
    player->x.HALF.HI = arrow->x.HALF.HI + deltaX - player->hitbox->offset_x;
    player->y.HALF.HI = arrow->y.HALF.HI + deltaY - player->hitbox->offset_y;
}

static void TestRetailDirectionTable(void) {
    static const u8 expectedFlipX[4] = { 0, 1, 0, 0 };
    static const u8 expectedFlipY[4] = { 0, 0, 1, 0 };
    static const u8 expectedAnimation[4] = { 1, 0, 1, 0 };
    static const u8 expectedWidth[4] = { 4, 6, 4, 6 };
    static const u8 expectedHeight[4] = { 6, 4, 6, 4 };
    static const Hitbox horizontal = { 0, 0, { 4, 0, 0, 0 }, 6, 4 };
    static const Hitbox vertical = { 0, 0, { 0, 0, 0, 4 }, 4, 6 };

    CHECK(sizeof(Hitbox) == 8, "Hitbox keeps the retail eight-byte layout");
    CHECK(memcmp(&gUnk_08129A18, &horizontal, sizeof(horizontal)) == 0,
          "horizontal arrow hitbox is the retail 6x4 half-extent");
    CHECK(memcmp(&gUnk_08129A20, &vertical, sizeof(vertical)) == 0,
          "vertical arrow hitbox is the retail 4x6 half-extent");
    CHECK(gPlayerHitbox.offset_x == 0 && gPlayerHitbox.offset_y == -3 &&
              gPlayerHitbox.width == 6 && gPlayerHitbox.height == 6,
          "Link uses the retail centered 6x6 collision half-extent");

    for (u32 direction = 0; direction < 4; ++direction) {
        Entity arrow;
        memset(&arrow, 0, sizeof(arrow));
        arrow.type = direction;
        arrow.flags = ENT_COLLIDE;
        sLastAnimation = 0xFFFFFFFFu;
        ArrowProjectile_Init(&arrow);

        CHECK(arrow.action == 1 && arrow.timer == 106 && arrow.subtimer == 0,
              "arrow initialization preserves the retail wait/lifetime state");
        CHECK((arrow.flags & ENT_COLLIDE) == 0,
              "an arrow cannot collide while it is still attached to the Bow Moblin");
        CHECK(arrow.spriteSettings.draw == 1, "arrow initialization enables its sprite");
        CHECK(arrow.spriteSettings.flipX == expectedFlipX[direction],
              "direction selects the retail horizontal flip");
        CHECK(arrow.spriteSettings.flipY == expectedFlipY[direction],
              "direction selects the retail vertical flip");
        CHECK(arrow.animIndex == expectedAnimation[direction] &&
                  sLastAnimation == expectedAnimation[direction],
              "direction selects the matching retail animation");
        CHECK(arrow.hitbox != NULL && arrow.hitbox->offset_x == 0 && arrow.hitbox->offset_y == 0 &&
                  arrow.hitbox->width == expectedWidth[direction] &&
                  arrow.hitbox->height == expectedHeight[direction],
              "direction selects only the narrow retail arrow geometry");
    }
}

static void TestEveryCollisionBoundary(void) {
    Entity arrow;
    Entity player;

    memset(&arrow, 0, sizeof(arrow));
    memset(&player, 0, sizeof(player));
    arrow.x.HALF.HI = 1000;
    arrow.y.HALF.HI = 2000;
    arrow.collisionLayer = 2;
    player.collisionLayer = 2;
    player.hitbox = (Hitbox*)&gPlayerHitbox;

    for (u32 direction = 0; direction < 4; ++direction) {
        sub_080A94C0(&arrow, direction);
        for (s32 deltaY = -20; deltaY <= 20; ++deltaY) {
            for (s32 deltaX = -20; deltaX <= 20; ++deltaX) {
                PlacePlayerCenter(&player, &arrow, deltaX, deltaY);
                if (!!IsColliding(&arrow, &player) != !!Retail2DOracle(&arrow, &player)) {
                    fprintf(stderr, "FAIL: direction=%lu collision mismatch at center delta (%ld,%ld)\n",
                            (unsigned long)direction, (long)deltaX, (long)deltaY);
                    sFailures++;
                    return;
                }
            }
        }

        /* Even the widest direction stops 13 pixels from Link's collision
         * center.  This is a direct guard against the reported giant box. */
        PlacePlayerCenter(&player, &arrow, 13, 0);
        CHECK(!IsColliding(&arrow, &player), "retail arrow never reaches Link 13 center-pixels away on X");
        PlacePlayerCenter(&player, &arrow, -13, 0);
        CHECK(!IsColliding(&arrow, &player), "retail arrow X boundary is symmetric");
        PlacePlayerCenter(&player, &arrow, 0, 13);
        CHECK(!IsColliding(&arrow, &player), "retail arrow never reaches Link 13 center-pixels away on Y");
        PlacePlayerCenter(&player, &arrow, 0, -13);
        CHECK(!IsColliding(&arrow, &player), "retail arrow Y boundary is symmetric");

        PlacePlayerCenter(&player, &arrow, 24, 0);
        CHECK(!IsColliding(&arrow, &player), "visible sprite displacement cannot enlarge collision geometry");
    }
}

static void TestLayerDepthAndOversizeSensitivity(void) {
    Entity arrow;
    Entity player;
    Hitbox oversized;

    memset(&arrow, 0, sizeof(arrow));
    memset(&player, 0, sizeof(player));
    arrow.x.HALF.HI = 1000;
    arrow.y.HALF.HI = 2000;
    arrow.collisionLayer = 2;
    player.collisionLayer = 2;
    player.hitbox = (Hitbox*)&gPlayerHitbox;
    sub_080A94C0(&arrow, 1);
    PlacePlayerCenter(&player, &arrow, 0, 0);
    CHECK(IsColliding(&arrow, &player), "coincident arrow and Link centers collide");

    player.collisionLayer = 1;
    CHECK(!IsColliding(&arrow, &player), "different collision layers cannot collide");
    player.collisionLayer = 2;
    player.z.HALF.HI = 11;
    CHECK(!IsColliding(&arrow, &player), "ordinary hitboxes have only the retail ten-pixel combined depth");
    player.z.HALF.HI = 0;

    /* Negative control: prove this harness would detect an actually enlarged
     * projectile at the same distance rejected by the production hitbox. */
    oversized = *arrow.hitbox;
    oversized.width = 32;
    arrow.hitbox = &oversized;
    PlacePlayerCenter(&player, &arrow, 24, 0);
    CHECK(IsColliding(&arrow, &player), "negative control detects a deliberately oversized arrow hitbox");
}

int main(void) {
    TestRetailDirectionTable();
    TestEveryCollisionBoundary();
    TestLayerDepthAndOversizeSensitivity();

    CHECK(sAnimationCalls == 9, "production direction selector was exercised for every test family");
    if (sFailures != 0) return 1;
    puts("port_arrow_projectile_hitbox_test: ALL PASS");
    return 0;
}
