#include <stdio.h>
#include <string.h>

#include "definitions.h"
#include "entity.h"
#include "hitbox.h"
#include "npc.h"

static int sAllowLoad;
static unsigned sPaletteLoads;

const NPCDefinition gNPCDefinitions[0x33] = {
    [GORON] = {
        .bitfield = { .type = 1, .hitbox = 3, .gfx = 1, .gfx_type = 1 },
        .data.sprite = { .paletteIndex = 191, .spriteIndex = SPRITE_GORON, .spritePriority = 1, .draw = 1 },
    },
};
const NPCDefinition gNPCDefinitions_eu[0x33] = {
    [GORON] = {
        .bitfield = { .type = 1, .hitbox = 3, .gfx = 1, .gfx_type = 1 },
        .data.sprite = { .paletteIndex = 191, .spriteIndex = SPRITE_GORON, .spritePriority = 1, .draw = 1 },
    },
};

const Hitbox gHitbox_2 = { 0 };
const Hitbox gHitbox_3 = { 0 };
const Hitbox gHitbox_30 = { 0 };
const Hitbox gHitbox_31 = { 0 };

bool32 LoadSwapGFX(Entity* entity, u32 count, u32 slotIndex) {
    (void)count;
    (void)slotIndex;
    if (sAllowLoad) {
        entity->spriteVramOffset = 0x150;
        entity->spriteAnimation[0] = 1;
        return TRUE;
    }
    return FALSE;
}

bool32 LoadFixedGFX(Entity* entity, u32 gfx) {
    (void)entity;
    (void)gfx;
    return FALSE;
}

u32 LoadObjPalette(Entity* entity, u32 palette) {
    (void)entity;
    (void)palette;
    sPaletteLoads++;
    return 0;
}

void UpdateSpriteForCollisionLayer(Entity* entity) {
    (void)entity;
}

int gActiveRegion = TMC_REGION_USA;

int main(void) {
    Entity goron;
    memset(&goron, 0, sizeof(goron));
    goron.id = GORON;
    goron.spriteIndex = 0x1FF;
    goron.animIndex = 0x44;
    goron.frameIndex = 0x55;

    sAllowLoad = 0;
    NPCInit(&goron);
    if ((goron.flags & ENT_DID_INIT) != 0 || goron.spriteIndex != 0x1FFu ||
        goron.animIndex != 0x44u || goron.frameIndex != 0x55u || sPaletteLoads != 0u) {
        fputs("FAIL: failed Goron graphics load exposed a partially initialized NPC\n", stderr);
        return 1;
    }

    sAllowLoad = 1;
    NPCInit(&goron);
    if ((goron.flags & ENT_DID_INIT) == 0 || goron.spriteIndex != SPRITE_GORON ||
        goron.spriteVramOffset != 0x150u || goron.spriteAnimation[0] != 1u ||
        goron.animIndex != 0xFFu || goron.frameIndex != 0xFFu || sPaletteLoads != 1u) {
        fputs("FAIL: Goron did not initialize cleanly after graphics capacity became available\n", stderr);
        return 1;
    }

    puts("port_npc_gfx_retry_test: ALL PASS");
    return 0;
}
