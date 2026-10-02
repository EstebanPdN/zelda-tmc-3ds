/* Regression coverage for the explicit read, write, and copy-source address
 * resolvers used by the native ports. */
#include <stdint.h>
#include <stdio.h>

#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include "port_rom.h"

u8* gRomData;
u32 gRomSize;

static int sFailures;

#define CHECK(condition, message)                                                                                 \
    do {                                                                                                          \
        if (!(condition)) {                                                                                       \
            fprintf(stderr, "FAIL: %s\n", message);                                                              \
            sFailures++;                                                                                          \
        }                                                                                                         \
    } while (0)

void Port_LogRomAccess(u32 gbaAddr, const char* caller) {
    (void)gbaAddr;
    (void)caller;
}

bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size) {
    uintptr_t at = (uintptr_t)ptr;
    uintptr_t begin = (uintptr_t)gRomData;
    uintptr_t end = begin + gRomSize;
    return gRomData && at >= begin && at <= end && size <= end - at;
}

int main(void) {
    static u8 fakeRom[32];
    u32 stackValue = 0x12345678;

    gRomData = fakeRom;
    gRomSize = sizeof(fakeRom);

    CHECK(port_resolve_addr(0x08000004u) == fakeRom + 4, "ROM address resolves into loaded ROM");
    CHECK(port_resolve_addr((uintptr_t)(fakeRom + 8)) == fakeRom + 8, "native ROM pointer stays native");
    CHECK(port_resolve_addr(0x02000010u) == gEwram + 0x10, "read address resolves into EWRAM");

    CHECK(port_resolve_write_addr(0x06000020u) == gVram + 0x20, "write address resolves into VRAM");
    CHECK(port_resolve_write_addr(0x08000004u) == (void*)(uintptr_t)0x08000004u,
          "ROM-range write destination stays native");

    CHECK(port_resolve_copy_src((const void*)(uintptr_t)0x0800000Cu, 4) == fakeRom + 12,
          "raw ROM copy source resolves into loaded ROM");
    CHECK(port_resolve_copy_src(fakeRom + 16, 4) == fakeRom + 16, "native ROM copy source stays native");
    CHECK(port_resolve_copy_src(&stackValue, sizeof(stackValue)) == &stackValue,
          "ordinary native copy source stays native");

    CHECK(Port_MapLayerNativeOffset(0x0004u) == offsetof(MapLayer, mapData),
          "GBA MapLayer mapData offset translates to native layout");
    CHECK(Port_MapLayerNativeOffset(0x7004u) == offsetof(MapLayer, subTiles),
          "GBA MapLayer subTiles offset translates to native layout");
    CHECK(Port_MapLayerNativeOffset(0xb004u) == offsetof(MapLayer, actTiles),
          "GBA MapLayer actTiles offset translates to native layout");

    if (sFailures == 0) {
        puts("port_gba_mem_test: ALL PASS");
        return 0;
    }
    fprintf(stderr, "port_gba_mem_test: %d FAILED\n", sFailures);
    return 1;
}
