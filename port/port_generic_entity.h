/*
 * port_generic_entity.h — 64-bit safe GenericEntity field accessors.
 *
 * Problem:
 *   On GBA (32-bit pointers), all entity subtypes have identical byte layouts
 *   for the "extra fields" area (offsets 0x68-0x87 in GBA terms). Code freely
 *   casts between Enemy*, PlayerEntity*, GenericEntity* and accesses fields by
 *   GBA offset names (field_0x68, field_0x80, cutsceneBeh, etc.).
 *
 *   On 64-bit, entity subtypes with pointer fields in the extra area have
 *   DIFFERENT layouts because pointers grow from 4 to 8 bytes:
 *     - Enemy:  Entity* child at extra+0 → shifts all fields after it by +4
 *     - Player: Entity* pulledJarEntity at extra+8, Entity* carriedEntity at
 *               extra+12 → shifts fields after carriedEntity by +8
 *
 *   GenericEntity (no pointers in extra area) keeps the original layout.
 *   Casting Enemy/Player to GenericEntity and reading fields yields WRONG data.
 *
 * Solution:
 *   These macros compute the correct byte address for a given GenericEntity
 *   field name on any entity, adjusting for pointer-size expansion.
 *
 * Usage:
 *   // Instead of: ((GenericEntity*)entity)->field_0x80.HWORD = 0;
 *   // Use:        GE_FIELD(entity, field_0x80)->HWORD = 0;
 */
#ifndef PORT_GENERIC_ENTITY_H
#define PORT_GENERIC_ENTITY_H

#include "entity.h"

#ifdef PC_PORT

/*
 * Compute the byte-offset shift for a GenericEntity extra field when
 * the underlying entity is an Enemy or PlayerEntity.
 *
 * ge_extra_rel = offset of the field within GenericEntity's extra area
 *              = offsetof(GenericEntity, field) - sizeof(Entity)
 *
 * Shift table (64-bit only, sizeof(void*)==8):
 *
 *   Enemy (kind=3): Entity* child at extra+0 adds 4 bytes.
 *     extra_rel [0,3]:   shift = 0  (within child low bytes)
 *     extra_rel [4,27]:  shift = +4 (after child)
 *     extra_rel [28,31]: padding in GE (no named field)
 *     extra_rel [32+]:   shift = 0  (GE padding absorbs the +4)
 *
 *   Player (kind=1): Two Entity* pointers at GBA extra+8 and +12.
 *     extra_rel [0,11]:  shift = 0  (before/within first pointer)
 *     extra_rel [12,15]: shift = +4 (after first pointer expanded)
 *     extra_rel [16,27]: shift = +8 (after both pointers expanded)
 *     extra_rel [28,31]: padding in GE (no named field)
 *     extra_rel [32+]:   shift = +4 (GE padding absorbs 4 of the +8)
 */
static inline int Port_GEFieldShift(const Entity* e, size_t ge_extra_rel) {
    if (sizeof(void*) <= 4)
        return 0; /* 32-bit pointers (N64 target): GBA layouts hold, no shift */

    switch (e->kind) {
    case 3: /* ENEMY */
        if (ge_extra_rel >= 4 && ge_extra_rel < 28)
            return 4;
        return 0;

    case 1: /* PLAYER */
        if (ge_extra_rel >= 32)
            return 4;
        if (ge_extra_rel >= 16)
            return 8;
        if (ge_extra_rel >= 12)
            return 4;
        return 0;

    default:
        return 0;
    }
}

/*
 * Return a pointer (u8*) to the correct byte in `ent` for a given
 * GenericEntity field, accounting for pointer-size expansion.
 */
static inline u8* Port_GEFieldAddr(Entity* e, size_t ge_field_offset) {
    size_t ge_extra_rel = ge_field_offset - sizeof(Entity);
    return (u8*)e + ge_field_offset + Port_GEFieldShift(e, ge_extra_rel);
}

/*
 * Accessor macros for individual GenericEntity fields.
 * Each returns a pointer to the correct type (SplitHWord*, SplitWord*, etc.)
 * that can be dereferenced to read/write the field.
 *
 * Usage:  GE_FIELD(entity, field_0x80)->HWORD = 0;
 */
#define GE_FIELD_HWORD(ent, fname) \
    ((union SplitHWord*)Port_GEFieldAddr((Entity*)(ent), offsetof(GenericEntity, fname)))

#define GE_FIELD_WORD(ent, fname) \
    ((union SplitWord*)Port_GEFieldAddr((Entity*)(ent), offsetof(GenericEntity, fname)))

/* Generic accessor — casts the pointer to the field's actual type.
 * Works for any GenericEntity field thanks to __typeof__. */
#define GE_FIELD(ent, fname) \
    ((__typeof__(((GenericEntity*)0)->fname)*)Port_GEFieldAddr((Entity*)(ent), offsetof(GenericEntity, fname)))

#else /* !PC_PORT — on GBA, GenericEntity cast works directly */

#define GE_FIELD(ent, fname) (&((GenericEntity*)(ent))->fname)

#endif /* PC_PORT */

#endif /* PORT_GENERIC_ENTITY_H */
