/**
 * @file darkNutSwordSlash.c
 * @ingroup Projectiles
 *
 * @brief Dark Nut Sword Slash Projectile
 */
#include "entity.h"
#include "physics.h"
#include "player.h"

extern const u8 DarkNutSwordSlash_hitTypes[];
extern void (*const DarkNutSwordSlash_UpdatesForType[])(Entity*);
extern Hitbox const* const* const gUnk_081293E0[];
extern const Hitbox* const gUnk_081293F0[];
extern const Hitbox* const gUnk_08129400[];
extern const Hitbox* const gUnk_08129410[];
extern const Hitbox* const gUnk_08129420[];

void DarkNutSwordSlash_Init(Entity*);
void DarkNutSwordSlash_OnTick(Entity*);

void DarkNutSwordSlash(Entity* this) {
    /* #97 root-cause fix: the Darknut can both spawn a sword-slash child
     * AND null its parent pointer in the same frame — sub_08021038 spawns
     * a slash when `super->frame != 0`, and the same function calls
     * sub_08021588 (which clears `child->parent`) when `super->frame &
     * ANIM_DONE`. With ANIM_DONE = 0x80 those conditions overlap, so the
     * slash's very first tick can see parent = NULL.
     *
     * On GBA the NULL deref reads from the BIOS at 0x00000000 — garbage
     * but harmless — so the original game "worked". On PC it's a hard
     * segfault. Bail out before any parent-relative read and let the next
     * tick delete the orphaned slash via the existing null guard. */
    if (this->parent == NULL) {
        DeleteThisEntity();
        return;
    }
    if (this->action == 0) {
        this->action = 1;
        DarkNutSwordSlash_Init(this);
        if (this->type == 3) {
            InitAnimationForceUpdate(this, this->parent->animationState + 0x18);
        }
    }
    if (this->parent->health == 0) {
        DeleteThisEntity();
    }
    if (((this->contactFlags & CONTACT_NOW) != 0) && (this->contactedEntity == &gPlayerEntity.base)) {
        this->iframes = -0x2d;
    }
    this->contactFlags = 0;
    CopyPositionAndSpriteOffset(this->parent, this);
    DarkNutSwordSlash_OnTick(this);
    if (this->type == 3) {
        this->y.HALF.HI++;
        UpdateAnimationSingleFrame(this);
    }
}

void DarkNutSwordSlash_Init(Entity* this) {
    /* Caller (DarkNutSwordSlash) guarantees this->parent != NULL. */
    this->hitType = DarkNutSwordSlash_hitTypes[this->parent->type + this->type * 4];
}

void DarkNutSwordSlash_OnTick(Entity* this) {
    DarkNutSwordSlash_UpdatesForType[this->type](this);
}

/* #97 fix: every UpdateTypeN here indexes Hitbox-pointer tables by
 * `parent->animationState` (table size 4: directions 0..3) and
 * UpdateType0 additionally indexes by `(parent->frame & 0xf) - 1`
 * (table size 6). The original GBA values are usually in range, but
 * during animation transitions / state resets the Darknut briefly has
 * frame & 0xf == 0 (so `- 1` underflows to -1) or animationState ==
 * 0xff (sub_08020D70 sets it to -1 on init before sub_08021218
 * replaces it). With 8-byte pointers on PC the read lands far enough
 * past the table to dereference random .rodata as a Hitbox*, and on
 * Windows that maps to 0xc0000005 access-violation crashes during the
 * Castor Wilds Darknut fight. Clamp into the valid range — keeps the
 * last valid hitbox active until the next frame restores a healthy
 * value (same pattern as the #91 cat-attack fix). */
static inline int DarkNut_ClampAnim(int s) {
    return s < 0 ? 0 : (s > 3 ? 3 : s);
}

void DarkNutSwordSlash_UpdateType0(Entity* this) {
    Entity* parent = this->parent;
    int frame_idx = (int)(parent->frame & 0xf) - 1;
    if (frame_idx < 0)  frame_idx = 0;
    if (frame_idx > 5) frame_idx = 5;
    this->hitbox = (Hitbox*)gUnk_081293E0[DarkNut_ClampAnim((s8)parent->animationState)][frame_idx];
}

void DarkNutSwordSlash_UpdateType2(Entity* this) {
    Entity* parent = this->parent;
    this->hitbox = (Hitbox*)gUnk_081293F0[DarkNut_ClampAnim((s8)parent->animationState)];
}

void DarkNutSwordSlash_UpdateType34(Entity* this) {
    int anim = DarkNut_ClampAnim((s8)this->parent->animationState);
    if ((this->parent->frame & 0x10) != 0) {
        this->hitbox = (Hitbox*)gUnk_08129410[anim];
    } else {
        this->hitbox = (Hitbox*)gUnk_08129400[anim];
    }
}

void DarkNutSwordSlash_UpdateType1(Entity* this) {
    Entity* parent = this->parent;
    this->hitbox = (Hitbox*)gUnk_08129420[DarkNut_ClampAnim((s8)parent->animationState)];
}

const u8 DarkNutSwordSlash_hitTypes[] = {
    76, 76, 78, 77, 83, 83, 83, 83, 79, 79, 76, 80, 84, 84, 84, 85, 79, 79, 76, 80,
};
void (*const DarkNutSwordSlash_UpdatesForType[])(Entity*) = {
    DarkNutSwordSlash_UpdateType0,  DarkNutSwordSlash_UpdateType1,  DarkNutSwordSlash_UpdateType2,
    DarkNutSwordSlash_UpdateType34, DarkNutSwordSlash_UpdateType34,
};

extern const Hitbox* const gUnk_08129460[];
extern const Hitbox* const gUnk_081294A8[];
extern const Hitbox* const gUnk_081294F0[];
extern const Hitbox* const gUnk_08129538[];
Hitbox const* const* const gUnk_081293E0[] = {
    gUnk_08129460,
    gUnk_081294A8,
    gUnk_081294F0,
    gUnk_08129538,
};
extern const Hitbox gUnk_08129550;
extern const Hitbox gUnk_08129558;
extern const Hitbox gUnk_08129560;
extern const Hitbox gUnk_08129568;
const Hitbox* const gUnk_081293F0[] = {
    &gUnk_08129550,
    &gUnk_08129558,
    &gUnk_08129560,
    &gUnk_08129568,
};
extern const Hitbox gUnk_08129590;
extern const Hitbox gUnk_081295A0;
extern const Hitbox gUnk_081295B0;
extern const Hitbox gUnk_081295C0;
const Hitbox* const gUnk_08129400[] = {
    &gUnk_08129590,
    &gUnk_081295A0,
    &gUnk_081295B0,
    &gUnk_081295C0,
};
extern const Hitbox gUnk_08129598;
extern const Hitbox gUnk_081295A8;
extern const Hitbox gUnk_081295B8;
extern const Hitbox gUnk_081295C8;
const Hitbox* const gUnk_08129410[] = {
    &gUnk_08129598,
    &gUnk_081295A8,
    &gUnk_081295B8,
    &gUnk_081295C8,
};
extern const Hitbox gUnk_08129570;
extern const Hitbox gUnk_08129578;
extern const Hitbox gUnk_08129580;
extern const Hitbox gUnk_08129588;
const Hitbox* const gUnk_08129420[] = {
    &gUnk_08129570,
    &gUnk_08129578,
    &gUnk_08129580,
    &gUnk_08129588,
};

const Hitbox gUnk_08129430 = { -5, -32, { 0, 0, 0, 0 }, 8, 10 };
const Hitbox gUnk_08129438 = { 2, -37, { 0, 0, 0, 0 }, 8, 10 };
const Hitbox gUnk_08129440 = { 19, -33, { 0, 0, 0, 0 }, 10, 11 };
const Hitbox gUnk_08129448 = { 28, -14, { 0, 0, 0, 0 }, 16, 6 };
const Hitbox gUnk_08129450 = { 34, -18, { 0, 0, 0, 0 }, 8, 4 };
const Hitbox gUnk_08129458 = { 25, 15, { 0, 0, 0, 0 }, 4, 4 };

const Hitbox* const gUnk_08129460[] = {
    &gUnk_08129430, &gUnk_08129438, &gUnk_08129440, &gUnk_08129448, &gUnk_08129450, &gUnk_08129458,
};

const Hitbox gUnk_08129478 = { 21, -6, { 0, 0, 0, 0 }, 12, 8 };
const Hitbox gUnk_08129480 = { 21, -20, { 0, 0, 0, 0 }, 16, 8 };
const Hitbox gUnk_08129488 = { 15, -31, { 0, 0, 0, 0 }, 8, 10 };
const Hitbox gUnk_08129490 = { 3, -39, { 0, 0, 0, 0 }, 8, 12 };
const Hitbox gUnk_08129498 = { 7, -38, { 0, 0, 0, 0 }, 7, 8 };
const Hitbox gUnk_081294A0 = { 1, -29, { 0, 0, 0, 0 }, 4, 5 };

const Hitbox* const gUnk_081294A8[] = {
    &gUnk_08129478, &gUnk_08129480, &gUnk_08129488, &gUnk_08129490, &gUnk_08129498, &gUnk_081294A0,
};

const Hitbox gUnk_081294C0 = { 7, 7, { 0, 0, 0, 0 }, 8, 11 };
const Hitbox gUnk_081294C8 = { -6, 10, { 0, 0, 0, 0 }, 8, 12 };
const Hitbox gUnk_081294D0 = { -22, 2, { 0, 0, 0, 0 }, 8, 8 };
const Hitbox gUnk_081294D8 = { -27, -11, { 0, 0, 0, 0 }, 12, 8 };
const Hitbox gUnk_081294E0 = { -28, -9, { 0, 0, 0, 0 }, 15, 6 };
const Hitbox gUnk_081294E8 = { -21, -12, { 0, 0, 0, 0 }, 6, 2 };

const Hitbox* const gUnk_081294F0[] = {
    &gUnk_081294C0, &gUnk_081294C8, &gUnk_081294D0, &gUnk_081294D8, &gUnk_081294E0, &gUnk_081294E8,
};

const Hitbox gUnk_08129508 = { -21, -6, { 0, 0, 0, 0 }, 12, 8 };
const Hitbox gUnk_08129510 = { -21, -20, { 0, 0, 0, 0 }, 16, 8 };
const Hitbox gUnk_08129518 = { -15, -31, { 0, 0, 0, 0 }, 8, 10 };
const Hitbox gUnk_08129520 = { -3, -39, { 0, 0, 0, 0 }, 8, 12 };
const Hitbox gUnk_08129528 = { -7, -38, { 0, 0, 0, 0 }, 7, 8 };
const Hitbox gUnk_08129530 = { -1, -29, { 0, 0, 0, 0 }, 4, 5 };

const Hitbox* const gUnk_08129538[] = {
    &gUnk_08129508, &gUnk_08129510, &gUnk_08129518, &gUnk_08129520, &gUnk_08129528, &gUnk_08129530,
};

const Hitbox gUnk_08129550 = { 3, -30, { 0, 0, 0, 0 }, 7, 10 };
const Hitbox gUnk_08129558 = { 28, -12, { 0, 0, 0, 0 }, 14, 7 };
const Hitbox gUnk_08129560 = { -2, 14, { 0, 0, 0, 0 }, 12, 7 };
const Hitbox gUnk_08129568 = { -28, -12, { 0, 0, 0, 0 }, 14, 7 };
const Hitbox gUnk_08129570 = { 1, -29, { 0, 0, 0, 0 }, 8, 8 };
const Hitbox gUnk_08129578 = { 19, -8, { 0, 0, 0, 0 }, 8, 8 };
const Hitbox gUnk_08129580 = { 0, 10, { 0, 0, 0, 0 }, 8, 8 };
const Hitbox gUnk_08129588 = { -19, -8, { 0, 0, 0, 0 }, 8, 8 };
const Hitbox gUnk_08129590 = { 4, -37, { 0, 0, 0, 0 }, 7, 10 };
const Hitbox gUnk_08129598 = { 4, -35, { 0, 0, 0, 0 }, 2, 8 };
const Hitbox gUnk_081295A0 = { 32, -14, { 0, 0, 0, 0 }, 14, 6 };
const Hitbox gUnk_081295A8 = { 30, -14, { 0, 0, 0, 0 }, 12, 2 };
const Hitbox gUnk_081295B0 = { -1, 22, { 0, 0, 0, 0 }, 7, 12 };
const Hitbox gUnk_081295B8 = { -1, 20, { 0, 0, 0, 0 }, 2, 10 };
const Hitbox gUnk_081295C0 = { -32, -14, { 0, 0, 0, 0 }, 14, 6 };
const Hitbox gUnk_081295C8 = { -30, -14, { 0, 0, 0, 0 }, 12, 2 };
