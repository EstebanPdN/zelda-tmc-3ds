/* Debug memory-watch list (PC port).
 *
 * A small Cheat-Engine-style watch list: the user adds GBA addresses (with a
 * u8/u16/u32 width) and the F8 menu shows each one's live value every frame.
 * Backed by a fixed array of at most PORT_MEMWATCH_MAX entries; session-only
 * (deliberately not persisted to config.json — addresses are transient debug
 * state, and a stale address from a previous build would be misleading).
 *
 * Reads go through MemWatch_BytePtr, a fault-safe resolver that mirrors the
 * known GBA memory ranges (EWRAM / IWRAM / I/O / palette / VRAM / OAM / ROM)
 * but, unlike gba_MemPtr, NEVER aborts on an unmapped address and — unlike
 * gba_TryMemPtr — does NOT log ROM accesses (a watch re-read every frame would
 * otherwise flood the ROM-access log). An unmapped address simply reports as
 * "not readable" so the UI can render "<unmapped>" instead of crashing. */

#include "port_gba_mem.h"      /* gba memory arrays + u8/u16/u32 (via port_types.h) */
#include "port_debug_actions.h" /* our own declarations, for signature matching */

#define PORT_MEMWATCH_MAX 32

typedef struct {
    u32 addr;
    int width; /* 0 = u8, 1 = u16, 2 = u32 */
} MemWatch;

static MemWatch sWatch[PORT_MEMWATCH_MAX];
static int      sWatchCount = 0;

/* Resolve a single GBA byte address to a native pointer, or NULL if the
 * address falls outside every mapped range. Ranges and bases match
 * gba_TryMemPtr (port_gba_mem.h); kept local so this stays abort-free and
 * log-free for per-frame polling. */
static const u8* MemWatch_BytePtr(u32 a) {
    if (a >= 0x02000000u && a < 0x02000000u + sizeof(gEwram)) return &gEwram[a - 0x02000000u];
    if (a >= 0x03000000u && a < 0x03000000u + sizeof(gIwram)) return &gIwram[a - 0x03000000u];
    if (a >= 0x04000000u && a < 0x04000000u + sizeof(gIoMem)) return &gIoMem[a - 0x04000000u];
    if (a >= 0x05000000u && a < 0x05000200u) return (const u8*)gBgPltt + (a - 0x05000000u);
    if (a >= 0x05000200u && a < 0x05000400u) return (const u8*)gObjPltt + (a - 0x05000200u);
    if (a >= 0x06000000u && a < 0x06000000u + sizeof(gVram)) return &gVram[a - 0x06000000u];
    if (a >= 0x07000000u && a < 0x07000400u) return (const u8*)gOamMem + (a - 0x07000000u);
    if (gRomData && a >= 0x08000000u && a < 0x08000000u + gRomSize) return &gRomData[a - 0x08000000u];
    return NULL;
}

int Port_DebugQuery_MemRead(unsigned int addr, int width, unsigned int* outValue) {
    const int nbytes = (width == 2) ? 4 : (width == 1) ? 2 : 1;
    unsigned int v = 0;
    int i;
    for (i = 0; i < nbytes; i++) {
        const u8* p = MemWatch_BytePtr((u32)addr + (u32)i);
        if (p == NULL) {
            if (outValue) *outValue = 0;
            return 0; /* any byte unmapped -> the whole read is invalid */
        }
        v |= (unsigned int)(*p) << (8 * i); /* GBA is little-endian */
    }
    if (outValue) *outValue = v;
    return 1;
}

int Port_DebugQuery_MemWatchCount(void) {
    return sWatchCount;
}

int Port_DebugAction_MemWatchAdd(unsigned int addr, int width) {
    int i;
    if (width < 0 || width > 2) width = 0;
    /* De-dup exact (addr, width) pairs so repeated "Add" presses are no-ops. */
    for (i = 0; i < sWatchCount; i++) {
        if (sWatch[i].addr == addr && sWatch[i].width == width) return i;
    }
    if (sWatchCount >= PORT_MEMWATCH_MAX) return -1;
    sWatch[sWatchCount].addr = addr;
    sWatch[sWatchCount].width = width;
    return sWatchCount++;
}

void Port_DebugAction_MemWatchRemove(int index) {
    int i;
    if (index < 0 || index >= sWatchCount) return;
    for (i = index; i < sWatchCount - 1; i++) {
        sWatch[i] = sWatch[i + 1];
    }
    sWatchCount--;
}

void Port_DebugAction_MemWatchClear(void) {
    sWatchCount = 0;
}

unsigned int Port_DebugQuery_MemWatchAddr(int index) {
    if (index < 0 || index >= sWatchCount) return 0;
    return sWatch[index].addr;
}

int Port_DebugQuery_MemWatchWidth(int index) {
    if (index < 0 || index >= sWatchCount) return 0;
    return sWatch[index].width;
}

const char* Port_DebugQuery_MemWidthName(int width) {
    switch (width) {
        case 1:  return "u16";
        case 2:  return "u32";
        default: return "u8";
    }
}
