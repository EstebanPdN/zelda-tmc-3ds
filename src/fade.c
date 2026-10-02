#include "common.h"
#include "functions.h"
#include "global.h"
#include "screen.h"
#include "structures.h"

#ifdef PC_PORT
#include "port_gba_mem.h"
#include "port_rom.h"
#include <stdio.h>
#endif

static u32 sub_080501C0(FadeControl* ctl);
static u32 sub_08050230(FadeControl* ctl);
static u32 sub_080502A4(FadeControl* ctl);

extern u32 gUsedPalettes;
extern u16 gPaletteBuffer[];
extern u16 gUnk_080FC3C4[];

// function pointer to overlay (0x03005e98) in ram calls rom function MakeFadeBuff256
extern u32 ram_MakeFadeBuff256;
typedef void (*fptrMakeFadeBuff256)(u8*, u8*, u16, u8);

#ifdef PC_PORT
/**
 * C reimplementation of arm_MakeFadeBuff256 (ROM 0x080B2124).
 * Applies brightness+fade to 16 palette entries.
 *
 * @param src      Pointer to 16 source palette entries (gPaletteBuffer)
 * @param dest     Pointer to 16 destination entries (PAL_RAM, a GBA address on GBA but resolved here)
 * @param intensity Fade intensity (ptrUnk->unk2)
 * @param color    Fade color/mode (ptrUnk->unk1)
 */

/* Compile-time fade lookup table offsets (from data_08000F54.s).
 * 3 brightness levels, each with {R_offset, G_offset, B_offset} into gRomData.
 * The 9-table block (3 x RGB, 0x40 bytes each) sits at 0xF84 on USA/JP but
 * 0xFCC on EU (+0x48 code shift; verified byte-identical content in all three
 * retail ROMs). Reading the USA offsets from an EU ROM pushed every palette
 * row through garbage LUTs — the "garbled EU title BG" M6 bug, which really
 * corrupted EVERY faded palette on EU. */
#define NUM_FADE_BRIGHTNESS 3
static const u32 sFadeTableOffsets[NUM_FADE_BRIGHTNESS][3] = {
    { 0x0F84, 0x0FC4, 0x1004 }, /* brightness 0 */
    { 0x1044, 0x1084, 0x10C4 }, /* brightness 1 */
    { 0x1104, 0x1144, 0x1184 }, /* brightness 2 */
};

#ifdef TMC_N64
/* 16-bit cart reads (lhu) are broken on N64's strict PI bus; read the containing
 * 32-bit-aligned word (lw is fine) and extract the halfword (big-endian: the
 * lower address is the high half). Returns the same value the GBA's lhu would. */
static inline u16 n64_cart_u16(const void* p) {
    uintptr_t a = (uintptr_t)p;
    u32 w = *(const volatile u32*)(void*)(a & ~(uintptr_t)3);
    return (a & 2u) ? (u16)(w & 0xFFFFu) : (u16)(w >> 16);
}
#endif

static void Port_MakeFadeBuff256(u8* src, u8* dest, u16 intensity, u8 color) {
    u32 bias = (u32)intensity * (u32)color;
    u32 factor = 0x400 - (u32)intensity * 4;

    /* Brightness preference from EWRAM offset 6 */
    u8 brightness = gEwram[6];
    if (brightness >= NUM_FADE_BRIGHTNESS)
        brightness = 0;

    /* Resolve fade lookup tables directly from known ROM offsets
     * (avoids chasing GBA ROM pointers in the pointer table at 0xF54).
     * EU's block sits +0x48 later (see sFadeTableOffsets comment). */
    u32 region_shift = 0;
#if defined(MULTI_REGION) || defined(EU)
    if (REGION_IS_EU)
        region_shift = 0x48;
#endif
    u16* tableR = (u16*)(gRomData + sFadeTableOffsets[brightness][0] + region_shift);
    u16* tableG = (u16*)(gRomData + sFadeTableOffsets[brightness][1] + region_shift);
    u16* tableB = (u16*)(gRomData + sFadeTableOffsets[brightness][2] + region_shift);

    u16* srcPtr = (u16*)src;
    /* dest is a GBA palette RAM address — resolve it */
    u16* dstPtr = (u16*)port_resolve_write_addr((uintptr_t)dest);

    if (!dstPtr)
        return; // Safety check

#ifdef TMC_N64
    /* The fade tables live in the cart; the tableR[r>>1] loads below are 16-bit
     * cart reads (lhu) that return garbage on N64's strict PI bus -> wrong faded
     * colors every frame (the ares "wrong colours" bug). Cache the 3 tables
     * (32 u16 each) in RAM via 32-bit-safe reads, re-copied only when brightness
     * (hence the table set) changes, then index the RAM copies. */
    static u16 sCacheR[32], sCacheG[32], sCacheB[32];
    static int sCacheBrightness = -1;
    if ((int)brightness != sCacheBrightness) {
        for (int k = 0; k < 32; k++) {
            sCacheR[k] = n64_cart_u16(&tableR[k]);
            sCacheG[k] = n64_cart_u16(&tableG[k]);
            sCacheB[k] = n64_cart_u16(&tableB[k]);
        }
        sCacheBrightness = (int)brightness;
    }
    tableR = sCacheR;
    tableG = sCacheG;
    tableB = sCacheB;
#endif

    for (int i = 0; i < 16; i++) {
        u16 col = srcPtr[i];
        u32 shifted = (u32)col << 1;

        u32 r = shifted & 0x3E;
        u32 g = (shifted >> 5) & 0x3E;
        u32 b = (shifted >> 10) & 0x3E;

        r = ((factor * r + bias) >> 10) & 0x3E;
        g = ((factor * g + bias) >> 10) & 0x3E;
        b = ((factor * b + bias) >> 10) & 0x3E;

        /* Tables are indexed by byte offset (each entry is u16 = 2 bytes),
           so r/2 gives the array index. ARM uses ldrh [base, r] with r as byte offset. */
        dstPtr[i] = tableR[r >> 1] | tableG[g >> 1] | tableB[b >> 1];
    }
}
#endif

void SetBrightness(u32 brightness) {
    gSaveHeader->brightness = brightness;
    gUsedPalettes = 0xffffffff;
}

void FadeVBlank(void) {
    fptrMakeFadeBuff256 func;
    u32 usedPalettesTmp, palIdx;

    struct_020354C0* ptrUnk = gUnk_020354C0;
    usedPalettesTmp = gUsedPalettes;
    gUsedPalettes = 0;
    palIdx = 0;

    while (usedPalettesTmp != 0) {
        if ((usedPalettesTmp & 1) == 1) {
#ifdef PC_PORT
            Port_MakeFadeBuff256(&((u8*)gPaletteBuffer)[palIdx], &PAL_RAM[palIdx], ptrUnk->unk2, ptrUnk->unk1);
#else
            func = (fptrMakeFadeBuff256)&ram_MakeFadeBuff256;
            func(&((u8*)gPaletteBuffer)[palIdx], &PAL_RAM[palIdx], ptrUnk->unk2, ptrUnk->unk1);
#endif
        }
        palIdx += 0x20;

        ptrUnk++;
        usedPalettesTmp >>= 1;
    }
}

void InitFade(void) {
    MemClear(&gFadeControl, sizeof(gFadeControl));
    MemClear(&gUnk_020354C0, sizeof(gUnk_020354C0));
    gFadeControl.mask = 0xffffffff;
}

void ResetFadeMask(void) {
    MemClear(&gUnk_020354C0, sizeof(gUnk_020354C0));
    gFadeControl.mask = 0xFFFFFFFF;
}

static void sub_08050024(void) {
    sub_0801E104();
    SetFade(FADE_IN_OUT | FADE_INSTANT, 256);
}

void SetFadeProgress(u32 arg0) {
    if ((gFadeControl.type & FADE_IN_OUT) != 0) {
        gFadeControl.sustain = arg0;
    } else {
        gFadeControl.progress = arg0;
    }
}

void SetFade(u32 type, u32 speed) {
#ifdef PC_PORT
    {
        extern void Port_LogFadeCall(const char* fn, u32 arg1, u32 arg2, u32 priorType, u32 priorActive, u32 priorProg);
        Port_LogFadeCall("SetFade", type, speed, gFadeControl.type, gFadeControl.active, gFadeControl.progress);
    }
#endif
    gFadeControl.speed = speed;
    gFadeControl.type = type;
    gFadeControl.active = 1;
    gFadeControl.progress = 0x100;
    gFadeControl.sustain = 0;
    if (gFadeControl.type & FADE_BLACK_WHITE) {
        gFadeControl.color = 0xf8;
    } else {
        gFadeControl.color = 0;
    }
    if (type & FADE_MOSAIC) {
        gOAMControls.spritesOffset = 1;
        gScreen.bg1.control |= BGCNT_MOSAIC;
        gScreen.bg2.control |= BGCNT_MOSAIC;
        gScreen.bg3.control |= BGCNT_MOSAIC;
    }
    if (type & FADE_IRIS) {
        sub_0801E1B8(gFadeControl.win_inside_cnt, gFadeControl.win_outside_cnt);
        sub_0801E1EC(gFadeControl.iris_x, gFadeControl.iris_y, gFadeControl.iris_size);
        if ((type & FADE_IN_OUT) == 0) {
            gFadeControl.type &= ~FADE_INSTANT;
            ResetFadeMask();
            gUsedPalettes = 0xffffffff;
        }
    }
}

void SetFadeInverted(u32 speed) {
#ifdef PC_PORT
    {
        extern void Port_LogFadeCall(const char* fn, u32 arg1, u32 arg2, u32 priorType, u32 priorActive, u32 priorProg);
        Port_LogFadeCall("SetFadeInverted", speed, 0, gFadeControl.type, gFadeControl.active, gFadeControl.progress);
    }
#endif
    gFadeControl.speed = speed;
    gFadeControl.type ^= FADE_IN_OUT;
    gFadeControl.active = 1;
    gFadeControl.progress = 256;
}

void SetFadeIris(u32 x, u32 y, u32 type, u32 speed) {
    if ((type & FADE_IN_OUT) != 0) {
        gFadeControl.iris_size = 0x96;
    } else {
        gFadeControl.iris_size = 0;
    }
    gFadeControl.iris_x = x;
    gFadeControl.iris_y = y;
    gFadeControl.win_inside_cnt = 0x3f3f;
    gFadeControl.win_outside_cnt = 0;
    SetFade(type, speed);
}

void FadeMain(void) {
    FadeControl* ctl = &gFadeControl;
    u32 flags = ctl->type & 0x1C;
    u32 active = 0;
    u32 bit;

    if (ctl->active) {
        ctl->progress -= ctl->speed;
        if ((s16)ctl->progress <= (s16)ctl->sustain)
            ctl->progress = ctl->sustain;
        while (flags) {
            bit = (~flags + 1) & flags;
            flags ^= bit;
            switch (bit) {
                case 4:
                    active |= sub_080501C0(ctl);
                    break;
                case 8:
                    active |= sub_08050230(ctl);
                    break;
                case 16:
                    active |= sub_080502A4(ctl);
                    break;
            }
        }
        ctl->active = active;
    }
}

static u32 sub_080501C0(FadeControl* ctl) {
    u32 v1;
    u32 v2;
    struct_020354C0* v3;
    u32 i;

    if (ctl->type & FADE_IN_OUT) {
        v1 = 256 - (s16)ctl->progress;
    } else {
        v1 = (s16)ctl->progress;
    }
    v2 = gFadeControl.mask;
    v3 = gUnk_020354C0;
    for (i = 0; i < 0x20; ++i, ++v3) {
        if (v2 & 1) {
            v3->unk0 = 1;
            v3->unk2 = v1;
        } else {
            v3->unk0 = 0;
            v3->unk2 = 0;
        }
        v3->unk1 = ctl->color;
        v2 >>= 1;
    }
    gUsedPalettes = 0xffffffff;

    return !!((s16)ctl->sustain ^ (s16)ctl->progress);
}

const u16 gMosaicSizes[] = {
    0,      0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777,
    0x8888, 0x9999, 0xaaaa, 0xbbbb, 0xcccc, 0xdddd, 0xeeee, 0xffff,
};

static u32 sub_08050230(FadeControl* ctl) {
    u32 type = ctl->type;
    u32 idx = ((s16)ctl->progress >> 4) & 0xF;
    if (type & 1)
        idx = 0xF - idx;
    gScreen.controls.mosaicSize = gMosaicSizes[idx];
    if (ctl->progress != 0)
        return 1;

    // fade is finished
    gOAMControls.spritesOffset = 0;
    if ((type & FADE_IN_OUT) == 0) {
        // reset registers if fading in
        gScreen.bg0.control &= ~BGCNT_MOSAIC;
        gScreen.bg1.control &= ~BGCNT_MOSAIC;
        gScreen.bg2.control &= ~BGCNT_MOSAIC;
        gScreen.bg3.control &= ~BGCNT_MOSAIC;
    }
    return 0;
}

static u32 sub_080502A4(FadeControl* ctl) {
    if (ctl->type & FADE_IN_OUT) {
        s32 delta = (u16)gFadeControl.iris_size - gFadeControl.speed;
        gFadeControl.iris_size -= gFadeControl.speed;
        if (delta << 16 <= 0)
            gFadeControl.iris_size = 0;
        sub_0801E1EC(gFadeControl.iris_x, gFadeControl.iris_y, gFadeControl.iris_size);
        if (!gFadeControl.iris_size)
            return 0;
    } else {
        gFadeControl.iris_size += gFadeControl.speed;
        sub_0801E1EC(gFadeControl.iris_x, gFadeControl.iris_y, gFadeControl.iris_size);
        if (gFadeControl.iris_size > 150) {
            sub_0801E104();
            return 0;
        }
    }
    return 1;
}
