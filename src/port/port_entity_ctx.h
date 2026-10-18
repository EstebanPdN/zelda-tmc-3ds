/*
 * port_entity_ctx.h — 64-bit safe storage for entity ScriptExecutionContext pointers.
 *
 * On GBA (32-bit), 4-byte pointers are crammed into the entity's cutsceneBeh + field_0x86 fields.
 * On 64-bit PC, 8-byte pointers don't fit there. We use a side table instead.
 */
#ifndef PORT_ENTITY_CTX_H
#define PORT_ENTITY_CTX_H

#include "entity.h"
#include "player.h"
#include "script.h"

#ifdef PC_PORT

#define PC_MAX_ENTITY_SLOTS 80 /* 1 player + 7 aux + 72 regular */

extern ScriptExecutionContext* gEntityScriptCtxTable[PC_MAX_ENTITY_SLOTS];

/* Map entity pointer → slot index. Returns -1 if not found. */
static inline int Port_EntitySlot(const Entity* ent) {
    if (ent == &gPlayerEntity.base)
        return 0;
    {
        int i;
        for (i = 0; i < MAX_AUX_PLAYER_ENTITIES; i++)
            if (ent == &gAuxPlayerEntities[i].base)
                return 1 + i;
    }
    {
        int i;
        for (i = 0; i < MAX_ENTITIES; i++)
            if (ent == &gEntities[i].base)
                return 8 + i;
    }
    return -1;
}

/* Map slot index → entity pointer. Inverse of Port_EntitySlot; slot < 0 → NULL.
 * Used by the EntityRef macros (entity.h) so over-slot enemy structs can store a
 * 4-byte slot instead of an 8-byte Entity* (see MoldormEntity/GibdoEntity/etc.). */
static inline Entity* Port_EntityFromSlot(int slot) {
    if (slot < 0)
        return NULL;
    if (slot == 0)
        return &gPlayerEntity.base;
    if (slot < 1 + MAX_AUX_PLAYER_ENTITIES)
        return &gAuxPlayerEntities[slot - 1].base;
    if (slot < 1 + MAX_AUX_PLAYER_ENTITIES + MAX_ENTITIES)
        return &gEntities[slot - (1 + MAX_AUX_PLAYER_ENTITIES)].base;
    return NULL;
}

static inline ScriptExecutionContext* Port_GetEntityScriptCtx(Entity* ent) {
    int slot = Port_EntitySlot(ent);
    return (slot >= 0) ? gEntityScriptCtxTable[slot] : NULL;
}

static inline void Port_SetEntityScriptCtx(Entity* ent, ScriptExecutionContext* ctx) {
    int slot = Port_EntitySlot(ent);
    if (slot >= 0) {
        gEntityScriptCtxTable[slot] = ctx;
        /* Also write to the GenericEntity's scriptContext struct field so that
         * entity-specific structs (BedCover, EzloCap, Stockwell, etc.) which read
         * their own unk_84/context field directly still see the correct value.
         * SKIP for gPlayerEntity (slot 0): PlayerEntity is smaller than GenericEntity
         * and the 8-byte write would overflow past the end of the struct. */
        if (slot > 0) {
            ((GenericEntity*)ent)->scriptContext = ctx;
        }
    }
}

#endif /* PC_PORT */
#endif /* PORT_ENTITY_CTX_H */
