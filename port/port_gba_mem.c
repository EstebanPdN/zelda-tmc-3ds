#include "gba/io_reg.h"
#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

u8 gIoMem[0x400];
u8 gEwram[0x40000];
u8 gIwram[0x8000];
u16 gBgPltt[256];
u16 gObjPltt[256];
u16 gOamMem[0x400 / 2];
u8 gVram[0x18000];

void gba_write8(uint32_t addr, uint8_t v) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        gEwram[addr - 0x02000000u] = v;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03008000u) {
        gIwram[addr - 0x03000000u] = v;
        return;
    }
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06018000u) {
        gVram[addr - 0x06000000u] = v;
        return;
    }

    printf("gba_write8: unimplemented for address 0x%08X\n", addr);
}

u8 gba_read8(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr < 0x03008000u)
        return gIwram[addr - 0x03000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return gVram[addr - 0x06000000u];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
#ifndef TMC_3DS
        Port_LogRomAccess(addr, "gba_read8");
#endif
        return gRomData[addr - 0x08000000u];
    }

    printf("gba_read8: unimplemented for address 0x%08X\n", addr);
    return 0;
}

void gba_write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FFu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        gEwram[addr - 0x02000000u] = v & 0xFF;
        gEwram[addr - 0x02000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFFu) {
        gIwram[addr - 0x03000000u] = v & 0xFF;
        gIwram[addr - 0x03000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v;
        return;
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v;
        return;
    }
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        return;
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v;
        return;
    }
    printf("gba_write16: unimplemented for address 0x%08X\n", addr);
}

u16 gba_read16(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FFu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8);
    if (addr >= 0x05000000u && addr < 0x050001FFu)
        return gBgPltt[(addr - 0x05000000u) >> 1];
    if (addr >= 0x05000200u && addr < 0x050003FFu)
        return gObjPltt[(addr - 0x05000200u) >> 1];
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8);
    }
    if (addr >= 0x07000000u && addr < 0x070003FFu)
        return gOamMem[(addr - 0x07000000u) >> 1];
    if (addr >= 0x02000000u && addr < 0x0203FFFFu)
        return gEwram[addr - 0x02000000u] | (gEwram[addr - 0x02000000u + 1] << 8);
    if (addr >= 0x03000000u && addr < 0x03007FFFu)
        return gIwram[addr - 0x03000000u] | (gIwram[addr - 0x03000000u + 1] << 8);
    if (gRomData && addr >= 0x08000000u) {
#ifndef TMC_3DS
        Port_LogRomAccess(addr, "gba_read16");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 1 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8);
        }
    }

    printf("gba_read16: unimplemented for address 0x%08X\n", addr);
    return 0;
}

void gba_write32(uint32_t addr, uint32_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FDu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        gIoMem[addr - 0x04000000u + 2] = (v >> 16) & 0xFF;
        gIoMem[addr - 0x04000000u + 3] = (v >> 24) & 0xFF;
        return;
    }
    // BG palette (0x05000000 - 0x050001FF)
    if (addr >= 0x05000000u && addr < 0x050001FEu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v & 0xFFFF;
        gBgPltt[((addr - 0x05000000u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    // BG/OBJ palette boundary straddle
    if (addr == 0x050001FEu) {
        gBgPltt[0xFF] = v & 0xFFFF;
        gObjPltt[0] = (v >> 16) & 0xFFFF;
        return;
    }
    // OBJ palette (0x05000200 - 0x050003FF)
    if (addr >= 0x05000200u && addr < 0x050003FEu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v & 0xFFFF;
        gObjPltt[((addr - 0x05000200u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        gVram[off + 2] = (v >> 16) & 0xFF;
        gVram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v & 0xFFFF;
        gOamMem[(addr - 0x07000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        gEwram[off] = v & 0xFF;
        gEwram[off + 1] = (v >> 8) & 0xFF;
        gEwram[off + 2] = (v >> 16) & 0xFF;
        gEwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        gIwram[off] = v & 0xFF;
        gIwram[off + 1] = (v >> 8) & 0xFF;
        gIwram[off + 2] = (v >> 16) & 0xFF;
        gIwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    printf("gba_write32: unimplemented for address 0x%08X\n", addr);
}

u32 gba_read32(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FDu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8) |
               (gIoMem[addr - 0x04000000u + 2] << 16) | (gIoMem[addr - 0x04000000u + 3] << 24);
    if (addr >= 0x05000000u && addr < 0x050001FEu)
        return gBgPltt[(addr - 0x05000000u) >> 1] | (gBgPltt[((addr - 0x05000000u) >> 1) + 1] << 16);
    if (addr >= 0x05000200u && addr < 0x050003FEu)
        return gObjPltt[(addr - 0x05000200u) >> 1] | (gObjPltt[((addr - 0x05000200u) >> 1) + 1] << 16);
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8) | (gVram[off + 2] << 16) | (gVram[off + 3] << 24);
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu)
        return gOamMem[(addr - 0x07000000u) >> 1] | (gOamMem[(addr - 0x07000000u + 2) >> 1] << 16);
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8) | (gEwram[off + 2] << 16) | (gEwram[off + 3] << 24);
    }
    if (addr >= 0x03000000u && addr < 0x03007FFDu) {
        u32 off = addr - 0x03000000u;
        return gIwram[off] | (gIwram[off + 1] << 8) | (gIwram[off + 2] << 16) | (gIwram[off + 3] << 24);
    }
    if (gRomData && addr >= 0x08000000u) {
#ifndef TMC_3DS
        Port_LogRomAccess(addr, "gba_read32");
#endif
        u32 off = addr - 0x08000000u;
        if (off + 3 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8) | (gRomData[off + 2] << 16) | (gRomData[off + 3] << 24);
        }
    }

    printf("gba_read32: unimplemented for address 0x%08X\n", addr);
    return 0;
}

void* port_resolve_addr(uintptr_t val)
{
    /* Native pointers into the loaded ROM are already resolved. On 3DS the
     * ROM buffer is deliberately allocated outside the numeric GBA window,
     * which makes this test unambiguous. */
    if (Port_IsLoadedAssetBytes((const void*)val, 1)) return (void*)val;

    /* GBA-range values are address-mapped through gba_TryMemPtr on
     * every platform. The previous Windows-only short-circuit used
     * VirtualQuery to "trust" the raw address if it happened to be a
     * committed VM page on the host — but on Wine (and native Windows
     * with the wrong ASLR alignment) a real GBA EWRAM address like
     * gMapDataBottomSpecial at 0x02006B00 IS a committed page (the
     * system DLLs map plenty of pages in the low address space) so
     * the short-circuit returned the GBA address verbatim instead of
     * remapping to gEwram[]. The pause-menu map screen then read its
     * tilemap from random host memory — symptom #44 (grey blocks at
     * top of map + bouncing player marker), Windows-only on the bug
     * tracker. Linux's path was already correct, so unify on it.
     *
     * On 3DS, the application stack reservation can numerically cover GBA
     * ROM addresses. This resolver is specifically for GBA-address values;
     * treating every mapped address in that overlap as native made valid ROM
     * graphics reads point into unrelated process memory. Native ROM-backed
     * sources bypass this resolver through Port_IsLoadedAssetBytes, while
     * the script paths recognize their known native pointers directly. */
    if (val >= 0x02000000u && val < 0x0A000000u) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) {
            return p;
        }
    }
    return (void*)val;
}

void* port_resolve_write_addr(uintptr_t val) {
    /* 0x08000000 and above is read-only cartridge space on GBA. A write
     * destination in that numeric range is therefore a native host pointer,
     * most commonly a 3DS stack or heap object. */
    if (val >= 0x02000000u && val < 0x08000000u) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p) return p;
    }
    return (void*)val;
}

const void* port_resolve_copy_src(const void* src, u32 size) {
    if (Port_IsLoadedAssetBytes(src, size)) return src;
#ifdef TMC_3DS
    /* Local scalars and temporary structs can occupy the same numeric range
     * as GBA ROM. This check is intentionally limited to copy sources; raw
     * GBA addresses passed to the general resolver must remain GBA addresses. */
    if ((uintptr_t)src >= 0x08000000u && (uintptr_t)src < 0x0A000000u) {
        extern int Platform3DS_IsActiveStackAddress(uintptr_t value);
        if (Platform3DS_IsActiveStackAddress((uintptr_t)src)) return src;
    }
#endif
    return port_resolve_addr((uintptr_t)src);
}
