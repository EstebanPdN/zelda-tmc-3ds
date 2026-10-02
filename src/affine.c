#include "global.h"
#include "main.h"
#include "room.h"
#include "screen.h"
#include "structures.h"

#include <string.h>

#ifdef PC_PORT
#include "port_rom.h"
extern u32 gFrameObjLists[50016];
#else
extern u32 gFrameObjLists[];
#endif

extern void ram_DrawEntities(void);
extern void ram_sub_080ADA04(OAMCommand*, void*);
extern void ram_DrawDirect(OAMCommand*, u32, u32);

void* sub_080AD8F0(u32 sprite, u32 frame) {
#ifdef PC_PORT
    const size_t frameObjSize = sizeof(gFrameObjLists);
    const u8* base = (const u8*)gFrameObjLists;
    size_t frameEntryOffset;
    u32 frameTableOffset;
    u32 frameDataOffset;

    if ((size_t)sprite >= (frameObjSize / sizeof(u32))) {
        return NULL;
    }

    frameTableOffset = gFrameObjLists[sprite];
    if ((size_t)frameTableOffset > frameObjSize - sizeof(u32)) {
        return NULL;
    }

    frameEntryOffset = (size_t)frameTableOffset + (size_t)frame * sizeof(u32);
    if (frameEntryOffset > frameObjSize - sizeof(u32)) {
        return NULL;
    }

    frameDataOffset = Port_ReadU32(base + frameEntryOffset);
    if ((size_t)frameDataOffset >= frameObjSize) {
        return NULL;
    }

    return (void*)(base + frameDataOffset);
#else
    u32* temp = &gFrameObjLists[0];
    u32 x = gFrameObjLists[sprite];
    temp = (u32*)((uintptr_t)(((u32*)((uintptr_t)temp + x))[frame]) + (uintptr_t)temp);

    return temp;
#endif
}

void FlushSprites(void) {
    gOAMControls.updated = 0;
}

void CopyOAM(void) {
    u16* d;
    s32 rem;

    if (gMain.pad == 0) {
        gOAMControls.unk[0x20].unk0 = 0;
        gOAMControls.unk[0x48].unk4 = 0;
        gOAMControls.unk[0x71].unk0 = 0;
        gOAMControls.unk[0x99].unk4 = 0;
    } else {
        gMain.pad--;
    }

    rem = 0x80 - gOAMControls.updated;
    if (rem > 0) {
        d = (u16*)&gOAMControls.oam[gOAMControls.updated];
        for (; rem != 0; rem--) {
            *d = 0x2A0;
            d = (u16*)((u8*)d + 8);
        }
    }
    if (gOAMControls.unk[0].unk7) {
        gOAMControls.unk[0].unk7 = 0;
#ifdef PC_PORT
        struct ObjAffineSrcData affineSrc[32];
        u32 i;

        for (i = 0; i < ARRAY_COUNT(affineSrc); i++) {
            memcpy(&affineSrc[i], &gOAMControls.unk[i].unk0, sizeof(affineSrc[i]));
        }
        ObjAffineSet(affineSrc, &gOAMControls.oam[0].affineParam, 32, 8);
#else
        ObjAffineSet((struct ObjAffineSrcData*)gOAMControls.unk, &gOAMControls.oam[0].affineParam, 32, 8);
#endif
    }
    gOAMControls.field_0x0 = 1;
}

void DrawEntities(void) {
    void (*fn)(void);

    gOAMControls._0[6] = gRoomTransition.field2f ? 15 : 0;
    gOAMControls._4 = gRoomControls.aff_x + gRoomControls.scroll_x;
    gOAMControls._6 = gRoomControls.aff_y + gRoomControls.scroll_y;
    gOAMControls.field_0x1++;

    fn = &ram_DrawEntities;
    fn();
}

// TODO second parameter is a frame obj entry from gFrameObjLists
void sub_080ADA04(OAMCommand* cmd, void* dst) {
    void (*fn)(OAMCommand*, void*) = ram_sub_080ADA04;
    fn(cmd, dst);
}

void DrawDirect(u32 spriteIndex, u32 frameIndex) {
    void (*fn)(OAMCommand*, u32, u32) = ram_DrawDirect;
    fn(&gOamCmd, spriteIndex, frameIndex);
}
