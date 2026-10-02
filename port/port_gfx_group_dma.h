#ifndef PORT_GFX_GROUP_DMA_H
#define PORT_GFX_GROUP_DMA_H

#include <string.h>
#include "gba/types.h"

typedef enum {
    PORT_GFX_GROUP_DMA_NOT_EWRAM = 0,
    PORT_GFX_GROUP_DMA_COPIED = 1,
    PORT_GFX_GROUP_DMA_INVALID = 2,
} PortGfxGroupDmaResult;

void* Port_ResolveEwramPtr(u32 gbaAddress);

/* Raw-ROM gfx groups store GBA destination addresses.  Known map buffers are
 * standalone native globals in the port, so a normal DMA address translation
 * would write the flat EWRAM mirror instead.  Resolve EWRAM here and copy the
 * same whole 16-bit units as GBA DMA. */
static inline PortGfxGroupDmaResult Port_CopyGfxGroupDmaToEwram(const void* src, u32 gbaDest, u32 declaredBytes) {
    const u32 ewramStart = 0x02000000u;
    const u32 ewramEnd = 0x02040000u;
    u32 transferBytes;
    void* resolvedDest;

    if (gbaDest < ewramStart || gbaDest >= ewramEnd) {
        return PORT_GFX_GROUP_DMA_NOT_EWRAM;
    }

    /* LoadGfxGroup programs 16-bit DMA with declaredBytes >> 1 units. */
    transferBytes = declaredBytes & ~1u;
    if (transferBytes > ewramEnd - gbaDest || (transferBytes != 0 && src == NULL)) {
        return PORT_GFX_GROUP_DMA_INVALID;
    }
    if (transferBytes == 0) {
        return PORT_GFX_GROUP_DMA_COPIED;
    }

    resolvedDest = Port_ResolveEwramPtr(gbaDest);
    if (resolvedDest == NULL || (uintptr_t)resolvedDest > UINTPTR_MAX - (transferBytes - 1)) {
        return PORT_GFX_GROUP_DMA_INVALID;
    }

    /* Port_ResolveEwramPtr is intentionally piecewise: selected GBA globals
     * are native aliases while all other addresses use the flat EWRAM mirror.
     * Validate every byte in the requested span before writing. Checking only
     * the final byte would miss an internal discontinuity that later rejoins
     * the expected address, and crossing MapLayer's 4-byte GBA pointer into
     * its wider native pointer padding must fail without a partial copy. Gfx
     * groups load outside the frame loop, so this bounded validation does not
     * add per-frame work. */
    for (u32 offset = 0; offset < transferBytes; ++offset) {
        const void* resolvedByte = Port_ResolveEwramPtr(gbaDest + offset);
        if ((uintptr_t)resolvedByte != (uintptr_t)resolvedDest + offset) {
            return PORT_GFX_GROUP_DMA_INVALID;
        }
    }
    memcpy(resolvedDest, src, transferBytes);
    return PORT_GFX_GROUP_DMA_COPIED;
}

#endif
