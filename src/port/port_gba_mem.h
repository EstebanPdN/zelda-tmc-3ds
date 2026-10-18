#pragma once
#include "port_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

extern u8 gIoMem[0x400];       // I/O Memory (0x04000000-0x040003FF)
extern u8 gEwram[0x40000];     // EWRAM (0x02000000-0x0203FFFF)
extern u8 gIwram[0x8000];      // IWRAM (0x03000000-0x03007FFF)
extern u16 gBgPltt[256];       // 0x200 bytes
extern u16 gObjPltt[256];      // 0x200 bytes
extern u16 gOamMem[0x400 / 2]; // 0x400 bytes (OAM)
extern u8 gVram[0x18000];      // 96 KB VRAM GBA (0x06000000-0x06017FFF)

// ROM data (loaded from baserom.gba)
extern u8* gRomData;
extern u32 gRomSize;

// ROM access logging (defined in port_rom.c)
void Port_LogRomAccess(u32 gba_addr, const char* caller);

void gba_write8(uint32_t addr, uint8_t v);
u8 gba_read8(uint32_t addr);
void gba_write16(uint32_t addr, uint16_t v);
u16 gba_read16(uint32_t addr);
void gba_write32(uint32_t addr, uint32_t v);
u32 gba_read32(uint32_t addr);

/*
 * gba_TryMemPtr — non-aborting address resolver.
 * Returns native pointer for known GBA ranges, NULL otherwise.
 */
static inline void* gba_TryMemPtr(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return &gEwram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr < 0x03008000u)
        return &gIwram[addr - 0x03000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return &gIoMem[addr - 0x04000000u];
    if (addr >= 0x05000000u && addr < 0x05000200u)
        return &gBgPltt[(addr - 0x05000000u) >> 1];
    if (addr >= 0x05000200u && addr < 0x05000400u)
        return &gObjPltt[(addr - 0x05000200u) >> 1];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return &gVram[addr - 0x06000000u];
    if (addr >= 0x07000000u && addr < 0x07000400u)
        return &gOamMem[(addr - 0x07000000u) >> 1];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
#ifndef TMC_3DS
        Port_LogRomAccess(addr, "gba_TryMemPtr");
#endif
        return &gRomData[addr - 0x08000000u];
    }
    return NULL;
}

#ifdef TMC_N64
extern volatile uint32_t g_n64_last_bad_addr;
extern volatile uint32_t g_n64_bad_count;
#endif

static inline void* gba_MemPtr(uint32_t addr) {
    void* ptr = gba_TryMemPtr(addr);
    if (ptr)
        return ptr;
#ifdef TMC_N64
    /* #N64: never abort on a stray GBA address (libdragon abort() draws the
     * CPU-exception inspector and halts). Record it (surfaced on-screen by
     * Port_N64_VBlank) and return a shared scratch page so the engine limps. */
    {
        static u8 sN64Scratch[0x10000];
        g_n64_last_bad_addr = addr;
        g_n64_bad_count++;
        return sN64Scratch;
    }
#else
#if defined(__GNUC__)
    void* caller = __builtin_return_address(0);
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X (called from %p)\n", addr, caller);
#elif defined(_MSC_VER)
    void* caller = _ReturnAddress();
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X (called from %p)\n", addr, caller);
#else
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X\n", addr);
#endif
    fflush(stderr);
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile("int3");
#endif
    abort();
    return NULL;
#endif
}

/* Resolve a GBA address or an already-native pointer for reads. */
#ifdef __cplusplus
extern "C" {
#endif
void* port_resolve_addr(uintptr_t val);
/* Resolve a write destination. GBA ROM addresses are never writable, so host
 * heap and stack pointers in the 0x08xxxxxx range remain native. */
void* port_resolve_write_addr(uintptr_t val);
/* Resolve a copy source while preserving known ROM-buffer and active-stack
 * pointers before interpreting numeric GBA addresses. */
const void* port_resolve_copy_src(const void* src, u32 size);
#ifdef __cplusplus
}
#endif

static inline void gba_MemClear(u32 addr, u32 size) {
    void* ptr = gba_MemPtr(addr);
    if (ptr != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)ptr)[i] = 0;
        }
    }
}

static inline void gba_MemCopy(u32 srcAddr, u32 destAddr, u32 size) {
    void* src = gba_MemPtr(srcAddr);
    void* dest = gba_MemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)dest)[i] = ((u8*)src)[i];
        }
    }
}

static inline void port_MemCopyToGBA(const void* src, u32 destAddr, u32 size) {
    void* dest = gba_TryMemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)dest)[i] = ((const u8*)src)[i];
        }
    }
}
