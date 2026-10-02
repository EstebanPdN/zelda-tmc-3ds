/*
 * port_animation.c — Animation system for the PC port.
 *
 * Ported from ARM Thumb ASM (code_08003FC4.s @ 0x08004260–0x08004358).
 *
 * Animation data layout (4 bytes per frame):
 *   [0] frameIndex
 *   [1] frameDuration
 *   [2] frameSpriteSettings
 *   [3] frame — bit 7 = loop flag; if set, next byte × 4 = backwards offset
 *
 * gSpritePtrs[spriteIndex]:
 *   .animations → pointer to array of u32 (GBA ROM addrs → resolved to native ptrs)
 *   .frames     → SpriteFrame array {numTiles, unk_1, firstTileIndex}
 *   .ptr        → base pointer for tile data
 */

#include "entity.h"
#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include "port_rom.h"
#include "structures.h"
#include <stdio.h>
#include <string.h>


static int AnimRangeHasBytes(const void* ptr, size_t count) {
    uintptr_t start = (uintptr_t)gRomData;
    uintptr_t end = start + (uintptr_t)gRomSize;
    uintptr_t p = (uintptr_t)ptr;
    if (ptr == NULL) {
        return 0;
    }
    if (gRomData != NULL && p >= start && p <= end) {
        return count <= (size_t)(end - p);
    }
    /* If ptr lands inside a loaded asset buffer, that buffer's bounds are
     * authoritative: enforce the count-aware asset check and do NOT fall back
     * to the count-blind host-pointer check. The fallback ignores `count`, so
     * it would pass a near-end pointer and let FrameZero read off the end of a
     * short animation — e.g. the JP file-select objects' 15-byte anim, where a
     * 4-byte frame read at offset 12 overruns by one byte (heap-buffer-overflow,
     * port_animation.c:99). Only truly-external pointers (not in any asset) use
     * the best-effort host check. */
    if (Port_IsLoadedAssetBytes(ptr, 1)) {
        return Port_IsLoadedAssetBytes(ptr, (u32)count);
    }
    return Port_IsValidHostPtr(ptr);
}


/* ------------------------------------------------------------------ */
/* sub_080042D0 — Update GFX slot with new frame tile data             */
/* ------------------------------------------------------------------ */
static void UpdateSpriteGfxSlot(Entity* entity, u8 frameIdx, u16 spriteIdx) {
    if (frameIdx == 0xFF)
        return;

    const SpritePtr* spr = Port_GetSpritePtr(spriteIdx);
    if (!spr)
        return;
    SpriteFrame* frames = spr->frames;
    if (!frames)
        return;

    SpriteFrame* f = &frames[frameIdx];
    if (f->numTiles == 0)
        return;

    u8 slotIdx = entity->spriteAnimation[0];
    if (slotIdx >= MAX_GFX_SLOTS)
        return;
    GfxSlot* slot = &gGFXSlots.slots[slotIdx];

    /* status is the low 4 bits */
    if (slot->status < GFX_SLOT_GFX) /* < 5: slot not ready */
        return;

    u16 oldTileCount = slot->paletteIndex;
    slot->paletteIndex = f->numTiles;
    int countDiff = (int)oldTileCount - (int)f->numTiles;

    const void* newSrc = (const u8*)spr->ptr + (u32)f->firstTileIndex * 32;
    const void* oldSrc = slot->palettePointer;
    slot->palettePointer = newSrc;

    intptr_t srcDiff = (intptr_t)oldSrc - (intptr_t)newSrc;
    if (countDiff != 0 || srcDiff != 0) {
        /* Mark vramStatus = GFX_VRAM_3 (needs upload to VRAM) */
        slot->vramStatus = GFX_VRAM_3;
    }
}

/* ------------------------------------------------------------------ */
/* FrameZero — load current frame data from animPtr and advance        */
/* ------------------------------------------------------------------ */
static void FrameZero(Entity* entity) {
    entity->lastFrameIndex = entity->frameIndex;

    u8* p = (u8*)entity->animPtr;
    if (!p)
        return; /* Safety: no animation data */
    if (!AnimRangeHasBytes(p, 4)) {
        fprintf(stderr,
                "FrameZero: sprite=%u anim=%u action=%u subAction=%u animPtr=%p outside/overruns ROM\n",
                entity->spriteIndex, entity->animIndex, entity->action, entity->subAction, (void*)p);
        return;
    }
    entity->frameIndex = p[0];
    entity->frameDuration = p[1];
    entity->frameSpriteSettings = p[2];
    entity->frame = p[3];

    p += 4;
    /* Check loop flag: bit 7 of frame byte */
    if (entity->frame & 0x80) {
        if (!AnimRangeHasBytes(p, 1)) {
            fprintf(stderr, "FrameZero: sprite=%u anim=%u loop byte out of ROM at %p\n", entity->spriteIndex,
                    entity->animIndex, (void*)p);
            return;
        }
        u8 loopBack = p[0]; /* next byte = backwards distance in 4-byte frames */
        p -= (u32)loopBack * 4;
    }
    entity->animPtr = p;
}

/* ------------------------------------------------------------------ */
/* InitializeAnimation — set up animation from sprite data             */
/* ------------------------------------------------------------------ */
void InitializeAnimation(Entity* entity, u32 animIndex) {
    entity->animIndex = (u8)animIndex;

    u16 spriteIdx = (u16)entity->spriteIndex;

    /* Bounds check for sprite table entries */
    if (spriteIdx >= 512) {
        fprintf(stderr, "InitializeAnimation: spriteIndex %u out of range!\n", spriteIdx);
        return;
    }
    const SpritePtr* spr = Port_GetSpritePtr(spriteIdx);
    if (!spr)
        return;

    entity->animPtr = (u8*)Port_GetSpriteAnimationData(spriteIdx, animIndex);
    if (!entity->animPtr) {
        return;
    }

    FrameZero(entity);
}

/* ------------------------------------------------------------------ */
/* UpdateAnimationVariableFrames — advance animation by N ticks        */
/* ------------------------------------------------------------------ */
void UpdateAnimationVariableFrames(Entity* entity, u32 amount) {
    if (!entity->animPtr)
        return; /* Safety: no animation data */
    int remaining = (int)entity->frameDuration - (int)amount;

    if (remaining > 0) {
        entity->frameDuration = (u8)remaining;
        return;
    }

    if (remaining == 0) {
        FrameZero(entity);
        return;
    }

    /* FrameNeg: skip through frames until remaining > 0 */
    u8* p = (u8*)entity->animPtr;
    int maxIter = 256; /* safety limit to prevent infinite loops on corrupt data */
    for (;;) {
        if (!AnimRangeHasBytes(p, 4)) {
            fprintf(stderr, "UpdateAnimationVariableFrames: anim ptr %p outside/overruns ROM\n", (void*)p);
            return;
        }
        if (--maxIter <= 0) {
            fprintf(stderr, "UpdateAnimationVariableFrames: infinite loop detected, sprite %u anim %u\n",
                    entity->spriteIndex, entity->animIndex);
            return;
        }
        remaining += p[1]; /* add this frame's duration */
        if (remaining > 0) {
            /* Found the right frame — process it, then fix up duration */
            entity->animPtr = p;
            FrameZero(entity);
            entity->frameDuration = (u8)remaining;
            return;
        }
        /* Check loop flag on current frame before advancing */
        u8 frameByte = p[3];
        p += 4;
        if (frameByte & 0x80) {
            if (!AnimRangeHasBytes(p, 1)) {
                fprintf(stderr, "UpdateAnimationVariableFrames: loop byte out of ROM at %p\n", (void*)p);
                return;
            }
            u8 loopBack = p[0];
            p -= (u32)loopBack * 4;
        }
    }
}

/* ------------------------------------------------------------------ */
/* GetNextFrame — advance animation by 1 tick                          */
/* ------------------------------------------------------------------ */
void GetNextFrame(Entity* entity) {
    UpdateAnimationVariableFrames(entity, 1);
}

/* ------------------------------------------------------------------ */
/* InitAnimationForceUpdate — init + force GFX slot update             */
/* ------------------------------------------------------------------ */
void InitAnimationForceUpdate(Entity* entity, u32 animIndex) {
    InitializeAnimation(entity, animIndex);
    entity->lastFrameIndex = 0xFF;
    /* Fall through to sprite GFX update (same as tail of UpdateAnimationSingleFrame) */
    u8 fi = entity->frameIndex;
    u8 lfi = entity->lastFrameIndex;
    entity->lastFrameIndex = fi;
    if (fi != lfi) {
        UpdateSpriteGfxSlot(entity, fi, (u16)entity->spriteIndex);
    }
}

/* ------------------------------------------------------------------ */
/* UpdateAnimationSingleFrame — advance by 1 tick + GFX slot update    */
/* ------------------------------------------------------------------ */
void UpdateAnimationSingleFrame(Entity* entity) {
#ifdef PC_PORT
    extern unsigned long Port_UASF_Calls;
    Port_UASF_Calls++;
    {
        extern int Port_FselAnimTrace;
        if (Port_FselAnimTrace) {
            u8 fd0 = entity->frameDuration, fi0 = entity->frameIndex, ai0 = entity->animIndex;
            const void* ap0 = entity->animPtr;
            UpdateAnimationVariableFrames(entity, 1);
            fprintf(stderr,
                    "[fselanim] sprite=%u anim=%u fi:%u->%u dur:%u->%u animPtr=%p%s\n",
                    entity->spriteIndex, ai0, fi0, entity->frameIndex, fd0,
                    entity->frameDuration, ap0,
                    (ap0 != entity->animPtr) ? " (animPtr-moved)" : "");
            u8 fi = entity->frameIndex;
            u8 lfi = entity->lastFrameIndex;
            entity->lastFrameIndex = fi;
            if (fi != lfi)
                UpdateSpriteGfxSlot(entity, fi, (u16)entity->spriteIndex);
            return;
        }
    }
#endif
    UpdateAnimationVariableFrames(entity, 1);

    u8 fi = entity->frameIndex;
    u8 lfi = entity->lastFrameIndex;
    entity->lastFrameIndex = fi;
    if (fi != lfi) {
        UpdateSpriteGfxSlot(entity, fi, (u16)entity->spriteIndex);
    }
}

#ifdef PC_PORT
int Port_FselAnimTrace = 0;
unsigned long Port_UASF_Calls = 0;
#endif

/* ------------------------------------------------------------------ */
/* sub_080042BA — advance by N ticks + GFX slot update                 */
/* ------------------------------------------------------------------ */
void sub_080042BA(Entity* entity, u32 amount) {
    UpdateAnimationVariableFrames(entity, amount);

    u8 fi = entity->frameIndex;
    u8 lfi = entity->lastFrameIndex;
    entity->lastFrameIndex = fi;
    if (fi != lfi) {
        UpdateSpriteGfxSlot(entity, fi, (u16)entity->spriteIndex);
    }
}

/* ------------------------------------------------------------------ */
/* sub_080042D0 — raw GFX slot update for given frame + sprite         */
/* ------------------------------------------------------------------ */
void sub_080042D0(Entity* entity, u32 frameIndex, u16 spriteIndex) {
    UpdateSpriteGfxSlot(entity, (u8)frameIndex, spriteIndex);
}
