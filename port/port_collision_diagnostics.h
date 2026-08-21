#ifndef PORT_COLLISION_DIAGNOSTICS_H
#define PORT_COLLISION_DIAGNOSTICS_H

#include "entity.h"

/*
 * A pointer-free snapshot of the most recent collision that actually changed
 * Link's health.  Quick dumps are normally requested several frames after the
 * visible hit, when the source entity may already have deleted itself.  Keep
 * only scalar values here so collecting or printing the diagnostic never
 * dereferences a stale entity.
 */
typedef struct {
    u32 sequence;
    u16 gameTick;
    u16 reservedTick;
    u8 valid;
    u8 area;
    u8 room;
    u8 effectiveDamage;
    u8 healthBefore;
    u8 sourceKind;
    u8 sourceId;
    u8 sourceType;
    u8 sourceType2;
    u8 sourceAction;
    u8 sourceSubAction;
    u8 sourceHitType;
    u8 sourceHurtType;
    u8 sourceCollisionLayer;
    u8 sourceCollisionFlags;
    u8 sourceCollisionMask;
    u8 sourceFlags;
    u8 sourceDraw;
    u8 sourceFrameIndex;
    u8 sourceHasHitbox;
    u8 sourceAffineIndex;
    u8 sourceAffineMode;
    u8 playerHasHitbox;
    s8 sourceSpriteOffsetX;
    s8 sourceSpriteOffsetY;
    s8 sourceHitboxOffsetX;
    s8 sourceHitboxOffsetY;
    s8 playerHitboxOffsetX;
    s8 playerHitboxOffsetY;
    u8 sourceHitboxWidth;
    u8 sourceHitboxHeight;
    u8 playerHitboxWidth;
    u8 playerHitboxHeight;
    s16 sourceX;
    s16 sourceY;
    s16 sourceZ;
    s16 playerX;
    s16 playerY;
    s16 playerZ;
    s32 healthAfter;
} PortPlayerDamageDiagnostic;

void Port_Collision_RecordPlayerDamage(const Entity* player, const Entity* source, u32 effectiveDamage,
                                       u32 healthBefore, s32 healthAfter);
void Port_Collision_GetLastPlayerDamage(PortPlayerDamageDiagnostic* out);

#endif /* PORT_COLLISION_DIAGNOSTICS_H */
