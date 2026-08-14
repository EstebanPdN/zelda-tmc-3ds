/* C reimplementation of text rendering routines from asm/src/code_08001A7C.s */

#include "global.h"
#include "port_rom.h"
#ifdef TMC_3DS
#include "port_russian_3ds.h"
#endif

// 0x08002724: Unpack 4bpp font data (64 bytes) into per-pixel bytes (128 bytes)
void UnpackTextNibbles(void* src_ptr, u8* dest) {
    const u8* src = (const u8*)src_ptr;
    u8* const destStart = dest;
#ifdef PC_PORT
    if (src_ptr == NULL) {
        fprintf(stderr, "[TEXT] UnpackTextNibbles: src_ptr is NULL!\n");
        return;
    }
    {
        uintptr_t srcAddr = (uintptr_t)src;
        uintptr_t romStart = (uintptr_t)gRomData;
        uintptr_t romEnd = romStart + (uintptr_t)gRomSize;
        if (!(srcAddr >= romStart && srcAddr < romEnd)
#ifdef TMC_3DS
            && !Port3DS_IsRussianFontGlyph(src_ptr)
            && !Port3DS_IsRussianBannerFontGlyph(src_ptr)
#endif
        ) {
            fprintf(stderr, "[TEXT] UnpackTextNibbles: src=%p outside ROM [%p..%p)\n", (void*)src, (void*)gRomData,
                    (void*)(gRomData + gRomSize));
        }
    }
#endif
    for (int i = 0; i < 16; i++) {
        u8 b0 = src[0];
        u8 b1 = src[1];
        u8 b2 = src[2];
        u8 b3 = src[3];
        dest[0] = b0 & 0xF;
        dest[1] = b0 >> 4;
        dest[2] = b1 & 0xF;
        dest[3] = b1 >> 4;
        dest[4] = b2 & 0xF;
        dest[5] = b2 >> 4;
        dest[6] = b3 & 0xF;
        dest[7] = b3 >> 4;
        src += 4;
        dest += 8;
    }
#ifdef TMC_3DS
    /* Russian glyph byte 0 stores an invisible width marker in its first
     * 8 nibbles. sub_0805F7A0 reads that marker before this function; blank
     * the decoded first row so the marker itself is never drawn. */
    if (Port3DS_IsRussianBannerFontGlyph(src_ptr)) {
        /* Bank 8 uses transparent-merge rendering where palette value 0 is
         * transparent; its row 0 exists only to carry glyph metrics. */
        for (int i = 0; i < 8; ++i) destStart[i] = 0x00;
    } else if (Port3DS_IsRussianFontGlyph(src_ptr)) {
        for (int i = 0; i < 8; ++i) destStart[i] = 0x0F;
    }
#endif
}

// 0x080026C4: Render one glyph column into a 4bpp tile buffer
void sub_080026C4(u8* src, u8* dest, u8* colorLUT, u32 col) {
    u32 tileOffset = (col >> 3) << 6;
    dest += tileOffset;

    u8 mask = 0xF0;
    u32 byteOffset = (col >> 1) & 3;

    if (col & 1) {
        mask = 0x0F;
        colorLUT += 0x10;
    }

    dest += byteOffset;

    for (int i = 0; i < 16; i++) {
        u8 pixel = src[0];
        u8 color = colorLUT[pixel];
        u8 existing = dest[0];
        existing &= mask;
        existing |= color;
        dest[0] = existing;
        dest += 4;
        src += 8;
    }
}

// 0x080026F2: Same as sub_080026C4 but skips transparent pixels
void sub_080026F2(u8* src, void* dest_ptr, u8* colorLUT, u32 col) {
    u8* dest = (u8*)dest_ptr;
    u32 tileOffset = (col >> 3) << 6;
    dest += tileOffset;

    u8 mask = 0xF0;
    u32 byteOffset = (col >> 1) & 3;

    if (col & 1) {
        mask = 0x0F;
        colorLUT += 0x10;
    }

    dest += byteOffset;

    for (int i = 0; i < 16; i++) {
        u8 pixel = src[0];
        u8 color = colorLUT[pixel];
        if (color != 0) {
            u8 existing = dest[0];
            existing &= mask;
            existing |= color;
            dest[0] = existing;
        }
        dest += 4;
        src += 8;
    }
}
