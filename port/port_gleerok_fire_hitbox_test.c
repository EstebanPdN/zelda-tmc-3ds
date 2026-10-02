#include "collision.h"
#include "hitbox.h"
#include "main.h"
#include "object.h"
#include "player.h"
#include "port_collision_diagnostics.h"
#include "room.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep this test-side view byte-for-byte equivalent to the implementation's
 * private GleerokParticleEntity.  Using the real action functions catches
 * changes to the scale accumulator, u8 hitbox stores, and sprite offsets. */
typedef struct {
    Entity base;
    u8 filler[0xC];
    u32 unk74;
    u32 unk78;
    u32 unk7c;
    union SplitWord unk80;
    union SplitWord unk84;
} TestGleerokParticle;

extern void GleerokParticle_Action2(TestGleerokParticle*);
extern void GleerokParticle_Action3(TestGleerokParticle*);
extern void GleerokParticle_Action4(TestGleerokParticle*);
extern s32 CalculateDamage(Entity*, Entity*);

Main gMain;
RoomControls gRoomControls;
PlayerEntity gPlayerEntity;
SaveFile gSave;

static int sFailures;
static int sAffineCalls;
static u32 sLastAffineX;
static u32 sLastAffineY;
static s32 sHealth;

void SoundReqClipped(Entity* entity, u32 sound) {
    (void)entity;
    (void)sound;
}

s32 ModHealth(s32 delta) {
    sHealth += delta;
    return sHealth;
}

bool32 SetAffineInfo(Entity* entity, u32 xScale, u32 yScale, u32 rotation) {
    (void)entity;
    (void)rotation;
    sAffineCalls++;
    sLastAffineX = xScale;
    sLastAffineY = yScale;
    return TRUE;
}

void DeleteThisEntity(void) {
    /* The test gives the flame controller its retail keep-alive bit, so this
     * branch must not be reached.  Count it as a hard regression if it is. */
    sFailures++;
}

void sub_0806FCF4(Entity* entity, s32 inverseScale, s32 baseHalfSize, s32 anchor) {
    s32 offset = 0;
    if (inverseScale < 0) inverseScale = -inverseScale;
    if (baseHalfSize != 0) {
        const s32 scaledHalfSize = (s32)(((u32)baseHalfSize * (0x10000u / (u32)inverseScale)) >> 8);
        offset = baseHalfSize - scaledHalfSize;
    }
    if (anchor == 0 || anchor == 3) offset = -offset;
    if (anchor == 0 || anchor == 2) {
        entity->spriteOffsetY = (s8)offset;
    } else {
        entity->spriteOffsetX = (u8)(s8)offset;
    }
}

#define CHECK(condition, message)                    \
    do {                                             \
        if (!(condition)) {                          \
            fprintf(stderr, "FAIL: %s\n", message); \
            sFailures++;                             \
        }                                            \
    } while (0)

static u32 AbsScale(s16 scale) {
    return scale < 0 ? (u32)(-(s32)scale) : (u32)scale;
}

static u8 RetailHalfExtent(u32 inverseScale) {
    return (u8)((0x10000u / inverseScale) >> 3);
}

static s8 RetailAnchorOffset(u32 inverseScale, u8 anchor) {
    s32 value = 32 - (s32)((32u * (0x10000u / inverseScale)) >> 8);
    if (anchor == 0 || anchor == 3) value = -value;
    return (s8)value;
}

static void InitAction2(TestGleerokParticle* flame, Hitbox* hitbox, GenericEntity* controller, u8 quadrant) {
    static const u8 xAnchors[4] = { 1, 3, 1, 3 };
    static const u8 yAnchors[4] = { 2, 2, 0, 0 };
    static const s8 xPositions[4] = { -32, 31, -32, 31 };
    static const s8 yPositions[4] = { -40, -40, 23, 23 };
    static const u8 signs[4] = { 0, 1, 2, 3 };

    memset(flame, 0, sizeof(*flame));
    memset(hitbox, 0, sizeof(*hitbox));
    flame->base.kind = OBJECT;
    flame->base.id = GLEEROK_PARTICLE;
    flame->base.type = 1;
    flame->base.type2 = quadrant;
    flame->base.action = 2;
    flame->base.speed = 0x7000;
    flame->base.timer = xAnchors[quadrant];
    flame->base.subtimer = yAnchors[quadrant];
    flame->base.x.HALF.HI = 1000 + xPositions[quadrant];
    flame->base.y.HALF.HI = 1000 + yPositions[quadrant];
    flame->base.collisionLayer = 2;
    flame->base.hitbox = hitbox;
    flame->base.parent = &controller->base;
    flame->unk7c = 600;
    flame->unk80.HALF.HI = (signs[quadrant] & 1) ? -0x100 : 0x100;
    flame->unk84.HALF.HI = (signs[quadrant] & 2) ? -0x100 : 0x100;
    hitbox->width = 32;
    hitbox->height = 32;
}

static void CheckLiveGeometry(const TestGleerokParticle* flame, const Hitbox* hitbox) {
    const u32 xScale = AbsScale(flame->unk80.HALF.HI);
    const u32 yScale = AbsScale(flame->unk84.HALF.HI);
    CHECK(xScale != 0 && yScale != 0, "retail Gleerok flame sequence never reaches zero inverse scale");
    if (xScale == 0 || yScale == 0) return;

    CHECK(hitbox->width == RetailHalfExtent(xScale), "hitbox width follows the rendered affine half-extent");
    CHECK(hitbox->height == RetailHalfExtent(yScale), "hitbox height follows the rendered affine half-extent");
    CHECK((s8)flame->base.spriteOffsetX == RetailAnchorOffset(xScale, flame->base.timer),
          "hitbox X center follows the affine quadrant anchor");
    CHECK(flame->base.spriteOffsetY == RetailAnchorOffset(yScale, flame->base.subtimer),
          "hitbox Y center follows the affine quadrant anchor");
    CHECK(hitbox->offset_x == (s8)flame->base.spriteOffsetX,
          "collision X offset is the same offset used to render the affine sprite");
    CHECK(hitbox->offset_y == flame->base.spriteOffsetY,
          "collision Y offset is the same offset used to render the affine sprite");
    CHECK(hitbox->width >= 32 && hitbox->width <= 48, "valid flame width stays within retail 32..48 range");
    if (hitbox->height < 31 || hitbox->height > 48) {
        fprintf(stderr, "FAIL: height=%u yScale=%u action=%u timer=%lu\n", hitbox->height, yScale,
                flame->base.action, (unsigned long)flame->unk7c);
        sFailures++;
    }
}

static void TestFullRetailSequence(void) {
    for (u8 quadrant = 0; quadrant < 4; ++quadrant) {
        TestGleerokParticle flame;
        Hitbox hitbox;
        GenericEntity controller;
        u32 action2Frames = 0;
        u32 action3Frames = 0;
        u32 action4Frames = 0;
        u32 minimumScale = 0x100;

        memset(&controller, 0, sizeof(controller));
        controller.field_0x78.HALF.HI = 0x80;
        InitAction2(&flame, &hitbox, &controller, quadrant);

        while (flame.base.action == 2 && action2Frames < 700) {
            GleerokParticle_Action2(&flame);
            action2Frames++;
            const u32 scale = AbsScale(flame.unk80.HALF.HI);
            if (scale < minimumScale) minimumScale = scale;
            CheckLiveGeometry(&flame, &hitbox);
        }
        CHECK(action2Frames == 601, "Action2 has the retail 601-frame expansion duration");
        CHECK(flame.base.action == 3, "flame enters its hold action after expansion");
        CHECK(minimumScale == 168 || minimumScale == 169,
              "fractional affine accumulator bottoms out at the retail ~0xA9 scale");

        while (flame.base.action == 3 && action3Frames < 140) {
            GleerokParticle_Action3(&flame);
            action3Frames++;
            CheckLiveGeometry(&flame, &hitbox);
        }
        CHECK(action3Frames == 121, "Action3 has the retail 121-frame hold duration");
        CHECK(flame.base.action == 4, "flame enters its contraction action after the hold");

        while (AbsScale(flame.unk80.HALF.HI) != 0x100 && action4Frames < 120) {
            GleerokParticle_Action4(&flame);
            action4Frames++;
            CheckLiveGeometry(&flame, &hitbox);
        }
        CHECK(AbsScale(flame.unk80.HALF.HI) == 0x100, "Action4 returns exactly to 1:1 inverse scale");
        CHECK(action4Frames >= 87 && action4Frames <= 88, "Action4 contraction covers the retail scale range");
        for (int terminalFrame = 0; terminalFrame < 3; ++terminalFrame) {
            GleerokParticle_Action4(&flame);
            CheckLiveGeometry(&flame, &hitbox);
        }
        {
            /* Signed Q16.16 floors negative fractions toward -infinity.  The
             * two mixed-sign quadrants therefore finish Y one integer either
             * side of 0x100.  Retail ARM does the same; both extents remain
             * bounded (32 for 0xFF, 31 for 0x101). */
            static const u16 terminalYScales[4] = { 0x100, 0xFF, 0x101, 0x100 };
            const u8 expectedHeight = RetailHalfExtent(terminalYScales[quadrant]);
            if (hitbox.width != 32 || hitbox.height != expectedHeight ||
                AbsScale(flame.unk84.HALF.HI) != terminalYScales[quadrant]) {
                fprintf(stderr, "FAIL: terminal quadrant=%u scale=%u/%u hitbox=%u/%u offsets=%d/%d\n", quadrant,
                        AbsScale(flame.unk80.HALF.HI), AbsScale(flame.unk84.HALF.HI), hitbox.width, hitbox.height,
                        hitbox.offset_x, hitbox.offset_y);
                sFailures++;
            }
        }
    }
}

static bool OracleCollides(const Entity* first, const Entity* second) {
    const s32 firstX = first->x.HALF.HI + first->hitbox->offset_x;
    const s32 firstY = first->y.HALF.HI + first->hitbox->offset_y;
    const s32 secondX = second->x.HALF.HI + second->hitbox->offset_x;
    const s32 secondY = second->y.HALF.HI + second->hitbox->offset_y;
    const s32 maxX = first->hitbox->width + second->hitbox->width;
    const s32 maxY = first->hitbox->height + second->hitbox->height;
    return abs(firstX - secondX) <= maxX && abs(firstY - secondY) <= maxY;
}

static void TestEveryReachableScaleAndCollisionOffset(void) {
    static const u8 xAnchors[4] = { 1, 3, 1, 3 };
    static const u8 yAnchors[4] = { 2, 2, 0, 0 };
    static const s8 xPositions[4] = { -32, 31, -32, 31 };
    static const s8 yPositions[4] = { -40, -40, 23, 23 };
    Hitbox playerHitbox = { 0, -3, { 5, 3, 3, 5 }, 6, 6 };
    Entity player;
    Entity flame;
    Hitbox flameHitbox;

    memset(&player, 0, sizeof(player));
    memset(&flame, 0, sizeof(flame));
    player.hitbox = &playerHitbox;
    flame.hitbox = &flameHitbox;
    player.collisionLayer = 2;
    flame.collisionLayer = 2;
    flame.x.HALF.HI = 1000;
    flame.y.HALF.HI = 1000;

    /* The complete live sequence observed above spans inverse scales 168..256.
     * Sweep every integer scale, all four affine anchors, and every player
     * center in a 129x129 square.  This includes every collision boundary and
     * a margin outside the largest (48+6) half-extent. */
    for (u32 scale = 168; scale <= 0x100; ++scale) {
        flameHitbox.width = RetailHalfExtent(scale);
        flameHitbox.height = RetailHalfExtent(scale);
        CHECK(flameHitbox.width >= 32 && flameHitbox.width <= 48,
              "all reachable integer scales produce a bounded u8 width");
        for (u8 quadrant = 0; quadrant < 4; ++quadrant) {
            flameHitbox.offset_x = RetailAnchorOffset(scale, xAnchors[quadrant]);
            flameHitbox.offset_y = RetailAnchorOffset(scale, yAnchors[quadrant]);
            for (s32 dy = -64; dy <= 64; ++dy) {
                for (s32 dx = -64; dx <= 64; ++dx) {
                    player.x.HALF.HI = flame.x.HALF.HI + dx;
                    player.y.HALF.HI = flame.y.HALF.HI + dy;
                    if (!!IsColliding(&flame, &player) != OracleCollides(&flame, &player)) {
                        fprintf(stderr, "FAIL: collision mismatch scale=%u quadrant=%u dx=%d dy=%d\n",
                                scale, quadrant, dx, dy);
                        sFailures++;
                        return;
                    }
                }
            }

            player.y.HALF.HI = flame.y.HALF.HI;
            for (s32 farX = 96; farX <= 192; farX += 48) {
                player.x.HALF.HI = flame.x.HALF.HI + farX;
                CHECK(!IsColliding(&flame, &player), "reachable flame hitbox never collides 96+ pixels away");
                player.x.HALF.HI = flame.x.HALF.HI - farX;
                CHECK(!IsColliding(&flame, &player), "reachable flame hitbox is bounded on both X sides");
            }
        }

        /* The four quadrant hitboxes together stay within the rendered
         * attack's exact maximum rectangle around their shared parent. */
        s32 unionLeft = 0x7FFFFFFF;
        s32 unionRight = -0x7FFFFFFF;
        s32 unionTop = 0x7FFFFFFF;
        s32 unionBottom = -0x7FFFFFFF;
        for (u8 quadrant = 0; quadrant < 4; ++quadrant) {
            const s32 offsetX = RetailAnchorOffset(scale, xAnchors[quadrant]);
            const s32 offsetY = RetailAnchorOffset(scale, yAnchors[quadrant]);
            const s32 extent = RetailHalfExtent(scale);
            const s32 left = xPositions[quadrant] + offsetX - extent;
            const s32 right = xPositions[quadrant] + offsetX + extent;
            const s32 top = yPositions[quadrant] + offsetY - extent;
            const s32 bottom = yPositions[quadrant] + offsetY + extent;
            if (left < unionLeft) unionLeft = left;
            if (right > unionRight) unionRight = right;
            if (top < unionTop) unionTop = top;
            if (bottom > unionBottom) unionBottom = bottom;
        }
        CHECK(unionLeft >= -96 && unionRight <= 95 && unionTop >= -104 && unionBottom <= 87,
              "all four flame hitboxes remain inside the rendered attack rectangle");
    }
}

static void TestDamageDiagnostic(void) {
    Entity source;
    Hitbox sourceHitbox = { -7, 9, { 5, 3, 3, 5 }, 48, 41 };
    PortPlayerDamageDiagnostic diagnostic;

    memset(&gMain, 0, sizeof(gMain));
    memset(&gRoomControls, 0, sizeof(gRoomControls));
    memset(&gPlayerEntity, 0, sizeof(gPlayerEntity));
    memset(&gSave, 0, sizeof(gSave));
    memset(&source, 0, sizeof(source));
    gMain.ticks = 54321;
    gRoomControls.area = 0x51;
    gRoomControls.room = 0;
    gPlayerEntity.base.kind = PLAYER;
    gPlayerEntity.base.health = 20;
    gPlayerEntity.base.x.HALF.HI = 811;
    gPlayerEntity.base.y.HALF.HI = 7239;
    {
        static Hitbox playerHitbox = { 0, -3, { 5, 3, 3, 5 }, 6, 6 };
        gPlayerEntity.base.hitbox = &playerHitbox;
    }
    source.kind = OBJECT;
    source.id = GLEEROK_PARTICLE;
    source.type = 1;
    source.type2 = 3;
    source.action = 2;
    source.hitType = 0x7A;
    source.hurtType = 0x48;
    source.damage = 4;
    source.x.HALF.HI = 900;
    source.y.HALF.HI = 7200;
    source.hitbox = &sourceHitbox;
    sHealth = 20;

    CHECK(CalculateDamage(&gPlayerEntity.base, &source) == 16, "test collision applies its four points of damage");
    memset(&diagnostic, 0, sizeof(diagnostic));
    Port_Collision_GetLastPlayerDamage(&diagnostic);
    CHECK(diagnostic.valid && diagnostic.gameTick == 54321, "diagnostic records the exact game tick");
    CHECK(diagnostic.area == 0x51 && diagnostic.room == 0, "diagnostic records the damage room");
    CHECK(diagnostic.effectiveDamage == 4 && diagnostic.healthBefore == 20 && diagnostic.healthAfter == 16,
          "diagnostic records effective damage and health transition");
    CHECK(diagnostic.sourceKind == OBJECT && diagnostic.sourceId == GLEEROK_PARTICLE &&
              diagnostic.sourceType == 1 && diagnostic.sourceType2 == 3 && diagnostic.sourceAction == 2,
          "diagnostic identifies a Gleerok flame quadrant without retaining its pointer");
    CHECK(diagnostic.sourceHasHitbox && diagnostic.sourceHitboxOffsetX == -7 &&
              diagnostic.sourceHitboxOffsetY == 9 && diagnostic.sourceHitboxWidth == 48 &&
              diagnostic.sourceHitboxHeight == 41,
          "diagnostic copies the source hitbox while it is alive");
    CHECK(diagnostic.playerHasHitbox && diagnostic.playerHitboxOffsetY == -3 &&
              diagnostic.playerHitboxWidth == 6 && diagnostic.playerHitboxHeight == 6,
          "diagnostic includes Link's collision geometry for distance reconstruction");

    /* A malformed hitbox pointer must never make optional diagnostics crash. */
    source.hitbox = (Hitbox*)(uintptr_t)0x08000000u;
    Port_Collision_RecordPlayerDamage(&gPlayerEntity.base, &source, 1, 16, 15);
    Port_Collision_GetLastPlayerDamage(&diagnostic);
    CHECK(!diagnostic.sourceHasHitbox, "diagnostic rejects a leaked raw GBA hitbox pointer safely");
}

int main(void) {
    TestFullRetailSequence();
    TestEveryReachableScaleAndCollisionOffset();
    TestDamageDiagnostic();

    CHECK(sAffineCalls != 0 && sLastAffineX != 0 && sLastAffineY != 0,
          "real action functions exercised the affine renderer interface");
    if (sFailures != 0) return 1;
    puts("port_gleerok_fire_hitbox_test: ALL PASS");
    return 0;
}
