/**
 * @file sanctuaryStoneTablet.c
 * @ingroup Objects
 *
 * @brief Sanctuary Stone Tablet object
 */
#include "effects.h"
#include "entity.h"
#include "flags.h"
#include "hitbox.h"

typedef struct {
    Entity base;
    u8 filler[0x1E];
#if defined(PC_PORT) && __SIZEOF_POINTER__ == 8 /* 64-bit-ptr ports only; 32-bit ports (N64/MIPS) keep GBA 0x68 */
    /* #130 Four Sword Sanctuary softlock. On GBA `objFlags` sits at 0x86, which
     * is exactly GenericEntity.field_0x86 — the word sub_0807F8E8 writes the
     * activation flag (0x8004) into when the script spawns this plaque. The
     * Entity base grew 0x68->0x90 on PC and GenericEntity's tail pointer pushes
     * field_0x86 to 0xB2, but a raw 0x1E filler only reaches 0xAE, so objFlags
     * and field_0x86 stopped aliasing. The plaque then read/wrote the wrong
     * word: beaming it ran SetFlag(0) (a no-op) instead of SetRoomFlag(4), so
     * the ceremony's `CheckRoomFlag 4` wait after the Four Sword is restored
     * never cleared -> softlock. 4 bytes of padding restores the alias. */
    u8 filler_pc[4];
#endif
    u16 objFlags;
} SanctuaryStoneTabletEntity;

/* objFlags MUST alias GenericEntity.field_0x86 (what sub_0807F8E8 writes). */
PORT_STATIC_ASSERT_OFFSET(SanctuaryStoneTabletEntity, objFlags, 0x86, 0xB2,
                          "SanctuaryStoneTablet objFlags must alias field_0x86");

void SanctuaryStoneTablet_Init(SanctuaryStoneTabletEntity*);
void SanctuaryStoneTablet_Action1(SanctuaryStoneTabletEntity*);

void SanctuaryStoneTablet(Entity* this) {
    static void (*const SanctuaryStoneTablet_Actions[])(SanctuaryStoneTabletEntity*) = {
        SanctuaryStoneTablet_Init,
        SanctuaryStoneTablet_Action1,
    };

    SanctuaryStoneTablet_Actions[this->action]((SanctuaryStoneTabletEntity*)this);
}

void SanctuaryStoneTablet_Init(SanctuaryStoneTabletEntity* this) {
    if (CheckFlags(this->objFlags)) {
        DeleteThisEntity();
    }

    super->action = 1;
    COLLISION_ON(super);
    super->collisionFlags = 7;
    super->hurtType = 0x48;
    super->hitType = 1;
    super->collisionMask = 2;
    super->hitbox = (Hitbox*)&gHitbox_0;
}

void SanctuaryStoneTablet_Action1(SanctuaryStoneTabletEntity* this) {
    Entity* fxEnt;

    if (super->contactFlags == (CONTACT_NOW | 0x21)) {
        fxEnt = CreateFx(super, FX_MAGIC_STORM, 0);
        if (fxEnt != NULL) {
            fxEnt->spritePriority.b0 = 3;
            fxEnt->spriteOffsetY = -5;
        }
        SetFlag(this->objFlags);
        DeleteThisEntity();
    }
}
