#include "color.h"
#include "entity.h"
#include "room.h"
#include "main.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

Palette gPaletteList[16];
/* Retained only as a negative-control fixture for the old standalone alias. */
Palette gUnk_02001A3C;
RoomTransition gRoomTransition;
LinkedList gEntityLists[9];
struct_gUnk_020000C0 gUnk_020000C0[0x30];
u16 gPaletteBuffer[512];
u32 gUsedPalettes;

void MemClear(void* dest, u32 size) { memset(dest, 0, size); }
void MemCopy(const void* src, void* dest, u32 size) { memcpy(dest, src, size); }
int Port_IsValidEntityAddr(const void* ptr) { (void)ptr; return 0; }
extern u32 FindFreeObjPalette(u32 count);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    for (unsigned i = 0; i < 9; ++i)
        gEntityLists[i].first = gEntityLists[i].last = (Entity*)&gEntityLists[i];
    ResetPaletteTable(1);
    Palette reserved[6];
    memcpy(reserved, gPaletteList, sizeof(reserved));
    for (unsigned i = 6; i < 15; ++i) {
        gPaletteList[i]._0_0 = 3;
        gPaletteList[i]._0_4 = 1;
        gPaletteList[i].objPaletteId = 100 + i;
    }
    Palette occupied[9];
    memcpy(occupied, &gPaletteList[6], sizeof(occupied));
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        sub_0801D000(1);
        CHECK(gRoomTransition.field2f == 15);
        CHECK(gPaletteList[15]._0_0 == 4);
        CHECK(FindFreeObjPalette(1) == 0xffffffffu);
        sub_0801D000(0);
        CHECK(gRoomTransition.field2f == 0);
        Palette empty = {0};
        CHECK(memcmp(&gPaletteList[15], &empty, sizeof(empty)) == 0);
        CHECK(FindFreeObjPalette(1) == 15);
        CHECK(FindFreeObjPalette(2) == 0xffffffffu);
        sub_0801D000(0);
        CHECK(FindFreeObjPalette(1) == 15);
        CHECK(memcmp(reserved, gPaletteList, sizeof(reserved)) == 0);
        CHECK(memcmp(occupied, &gPaletteList[6], sizeof(occupied)) == 0);
    }
    /* Releasing the reservation must preserve an ordinary allocation there. */
    gPaletteList[15]._0_0 = 3;
    gPaletteList[15]._0_4 = 1;
    gPaletteList[15].objPaletteId = 42;
    Palette allocated = gPaletteList[15];
    sub_0801D000(0);
    CHECK(memcmp(&allocated, &gPaletteList[15], sizeof(allocated)) == 0);
    puts("palette_release_test: reservation/reuse/pressure/idempotence PASS");
    return 0;
}
