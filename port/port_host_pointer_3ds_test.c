/* Regression coverage for the 32-bit ARM11 host-pointer policy. */
#include <stdint.h>
#include <stdio.h>

#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include "port_rom.h"

u8* gRomData;
u32 gRomSize;

static uintptr_t sMappedAddress;
static uintptr_t sActiveStackAddress;
static int sFailures;

int Platform3DS_IsNativeAddress(uintptr_t value) {
    return value == sMappedAddress;
}

int Platform3DS_IsActiveStackAddress(uintptr_t value) {
    return value == sActiveStackAddress;
}

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

#define CHECK(condition, message)          \
    do {                                   \
        if (!(condition)) {                \
            fprintf(stderr, "FAIL: %s\n", message); \
            ++sFailures;                   \
        }                                  \
    } while (0)

int main(void) {
    static u8 fakeRom[32];

    gRomData = fakeRom;
    gRomSize = sizeof(fakeRom);

    CHECK(!Port_IsValidHostPtr(NULL), "NULL is rejected");
    CHECK(Port_IsValidHostPtr(fakeRom + 8), "loaded ROM allocation is accepted");

    sMappedAddress = 0x14100000u;
    CHECK(Port_IsValidHostPtr((const void*)sMappedAddress), "mapped 3DS heap pointer is accepted");

    sMappedAddress = 0x002EF168u;
    CHECK(Port_IsValidHostPtr((const void*)sMappedAddress), "mapped 3DS static-data pointer is accepted");

    sMappedAddress = 0x08001234u;
    CHECK(!Port_IsValidHostPtr((const void*)sMappedAddress), "raw GBA ROM address is rejected despite stack overlap");

    sMappedAddress = 0x02001234u;
    CHECK(!Port_IsValidHostPtr((const void*)sMappedAddress), "raw GBA EWRAM address is rejected");

    sMappedAddress = 0;
    CHECK(!Port_IsValidHostPtr((const void*)0x14000000u), "unmapped 3DS address is rejected");

    sActiveStackAddress = 0x08000004u;
    CHECK(port_resolve_copy_src((const void*)sActiveStackAddress, 1) == (const void*)sActiveStackAddress,
          "active 3DS stack pointer overlapping ROM stays native");
    sActiveStackAddress = 0;
    CHECK(port_resolve_copy_src((const void*)(uintptr_t)0x08000004u, 1) == fakeRom + 4,
          "raw GBA ROM pointer still resolves into loaded ROM");

    if (sFailures == 0) {
        puts("port_host_pointer_3ds_test: ALL PASS");
        return 0;
    }
    fprintf(stderr, "port_host_pointer_3ds_test: %d FAILED\n", sFailures);
    return 1;
}
