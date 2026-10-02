/*
 * port/port_memory_watch_test.c — regression test for the F8 memory-watch
 * list (port_debug_memory_watch.c).
 *
 * Pins the two things that are easy to get subtly wrong: (1) the fault-safe,
 * little-endian Port_DebugQuery_MemRead across the GBA address ranges and at a
 * range boundary, and (2) the watch-list bookkeeping (add / de-dup / remove /
 * clear / capacity). Both run with no display and no engine, so they fit the
 * same headless-CI slot as port_debug_actions_test.
 *
 * Standalone binary: it compiles port_debug_memory_watch.c and provides the GBA
 * memory arrays (normally owned by port_gba_mem.c) plus the one stub that file
 * references (Port_LogRomAccess) so the link closes.
 */
#include <stdio.h>
#include <string.h>

#include "port_gba_mem.h"
#include "port_debug_actions.h"

/* ---- GBA memory the watch reader resolves into (owned by port_gba_mem.c in
 *      the real build; defined here so the module links standalone). ---- */
u8  gIoMem[0x400];
u8  gEwram[0x40000];
u8  gIwram[0x8000];
u16 gBgPltt[256];
u16 gObjPltt[256];
u16 gOamMem[0x400 / 2];
u8  gVram[0x18000];
u8* gRomData = NULL;
u32 gRomSize = 0;

/* Referenced by port_gba_mem.h's inline resolvers; never called on our paths. */
void Port_LogRomAccess(u32 gba_addr, const char* caller) { (void)gba_addr; (void)caller; }

/* ---- assertion harness (mirrors port_debug_actions_test.c) ---- */
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", (msg)); ++g_fails; } \
} while (0)

int main(void) {
    /* ---------- MemRead: little-endian, across ranges, fault-safe ---------- */
    unsigned int v = 0xDEAD;

    /* EWRAM u16, little-endian: bytes {0x12, 0x34} -> 0x3412. */
    gEwram[0x10] = 0x12;
    gEwram[0x11] = 0x34;
    CHECK(Port_DebugQuery_MemRead(0x02000010u, 1, &v) == 1, "ewram u16 readable");
    CHECK(v == 0x3412u, "ewram u16 little-endian");

    /* IWRAM u32: bytes {0x78,0x56,0x34,0x12} -> 0x12345678. */
    gIwram[0x00] = 0x78; gIwram[0x01] = 0x56; gIwram[0x02] = 0x34; gIwram[0x03] = 0x12;
    CHECK(Port_DebugQuery_MemRead(0x03000000u, 2, &v) == 1, "iwram u32 readable");
    CHECK(v == 0x12345678u, "iwram u32 little-endian");

    /* u8 read. */
    gIoMem[0x04] = 0xAB;
    CHECK(Port_DebugQuery_MemRead(0x04000004u, 0, &v) == 1, "io u8 readable");
    CHECK(v == 0xABu, "io u8 value");

    /* Unmapped address (gap below EWRAM) -> ok=0, value zeroed. */
    v = 0x1234;
    CHECK(Port_DebugQuery_MemRead(0x01000000u, 0, &v) == 0, "unmapped -> invalid");
    CHECK(v == 0u, "unmapped zeroes out value");

    /* Range boundary: last EWRAM byte is readable as u8, but a u16 there spills
     * into the unmapped 0x02040000 gap and must report invalid (not wrap/fault). */
    CHECK(Port_DebugQuery_MemRead(0x0203FFFFu, 0, &v) == 1, "ewram last byte u8 ok");
    CHECK(Port_DebugQuery_MemRead(0x0203FFFFu, 1, &v) == 0, "ewram u16 over end -> invalid");

    /* ROM only resolves once gRomData is set. */
    static u8 fakeRom[16] = { 0,0,0,0, 0xEF,0xBE,0xAD,0xDE };
    CHECK(Port_DebugQuery_MemRead(0x08000004u, 2, &v) == 0, "rom invalid until loaded");
    gRomData = fakeRom; gRomSize = sizeof(fakeRom);
    CHECK(Port_DebugQuery_MemRead(0x08000004u, 2, &v) == 1, "rom readable when loaded");
    CHECK(v == 0xDEADBEEFu, "rom u32 little-endian");

    /* ---------- watch-list bookkeeping ---------- */
    CHECK(Port_DebugQuery_MemWatchCount() == 0, "starts empty");

    int i0 = Port_DebugAction_MemWatchAdd(0x03000000u, 1);
    CHECK(i0 == 0 && Port_DebugQuery_MemWatchCount() == 1, "first add -> index 0");

    /* Exact (addr,width) de-dups to the existing index, no growth. */
    int dup = Port_DebugAction_MemWatchAdd(0x03000000u, 1);
    CHECK(dup == 0 && Port_DebugQuery_MemWatchCount() == 1, "exact dup is a no-op");

    /* Same address, different width is a distinct watch. */
    int i1 = Port_DebugAction_MemWatchAdd(0x03000000u, 2);
    CHECK(i1 == 1 && Port_DebugQuery_MemWatchCount() == 2, "same addr new width -> new watch");

    int i2 = Port_DebugAction_MemWatchAdd(0x02000010u, 0);
    CHECK(i2 == 2 && Port_DebugQuery_MemWatchCount() == 3, "third add -> index 2");

    /* Remove the middle entry; remaining entries shift down, order preserved. */
    Port_DebugAction_MemWatchRemove(1);
    CHECK(Port_DebugQuery_MemWatchCount() == 2, "remove drops count");
    CHECK(Port_DebugQuery_MemWatchAddr(0) == 0x03000000u && Port_DebugQuery_MemWatchWidth(0) == 1,
          "entry 0 unchanged after middle remove");
    CHECK(Port_DebugQuery_MemWatchAddr(1) == 0x02000010u && Port_DebugQuery_MemWatchWidth(1) == 0,
          "entry 2 shifted into slot 1");

    /* Out-of-range remove is a no-op; out-of-range query returns 0. */
    Port_DebugAction_MemWatchRemove(99);
    CHECK(Port_DebugQuery_MemWatchCount() == 2, "bad remove index is a no-op");
    CHECK(Port_DebugQuery_MemWatchAddr(99) == 0, "bad query index returns 0");

    /* Capacity: fill to the 32-entry cap, the next add returns -1. */
    Port_DebugAction_MemWatchClear();
    CHECK(Port_DebugQuery_MemWatchCount() == 0, "clear empties list");
    int added = 0;
    for (unsigned int a = 0; a < 40; ++a) {
        /* distinct addresses so de-dup never triggers */
        if (Port_DebugAction_MemWatchAdd(0x02000000u + a * 4u, 0) >= 0) ++added;
    }
    CHECK(added == 32, "add caps at 32 entries");
    CHECK(Port_DebugQuery_MemWatchCount() == 32, "count saturates at 32");
    CHECK(Port_DebugAction_MemWatchAdd(0x03001000u, 0) == -1, "add past cap returns -1");

    /* ---------- width names ---------- */
    CHECK(strcmp(Port_DebugQuery_MemWidthName(0), "u8") == 0, "width name u8");
    CHECK(strcmp(Port_DebugQuery_MemWidthName(1), "u16") == 0, "width name u16");
    CHECK(strcmp(Port_DebugQuery_MemWidthName(2), "u32") == 0, "width name u32");
    CHECK(strcmp(Port_DebugQuery_MemWidthName(7), "u8") == 0, "width name default");

    if (g_fails == 0) {
        printf("port_memory_watch_test: ALL PASS\n");
        return 0;
    }
    fprintf(stderr, "port_memory_watch_test: %d FAILED\n", g_fails);
    return 1;
}
