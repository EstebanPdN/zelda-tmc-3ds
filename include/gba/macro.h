#ifndef MACRO_H
#define MACRO_H
#include <stdio.h>

#ifdef PC_PORT
/*
 * ======== PC Port DMA / CPU overrides ========
 * On GBA, DMA writes go to hardware registers. On PC we emulate
 * them with plain memcpy / memset, resolving GBA addresses to the
 * emulated memory arrays via port_resolve_addr().
 */
#include "port_gba_mem.h"
#include "port_hdma.h"
#include <string.h>

static inline const void* port_resolve_dma_src(const void* src_raw, u32 size) {
    return port_resolve_copy_src(src_raw, size);
}

/* ---- DmaSet: raw DMA register write emulation ---- */
static inline void port_DmaTransfer(const void* src_raw, uintptr_t dest_raw, u32 control) {
    u16 cnt = (u16)(control >> 16);
    u32 units = control & 0xFFFF;
    if (!(cnt & 0x8000))
        return;                         /* DMA_ENABLE not set */
    int is32 = (cnt & 0x0400) != 0;     /* DMA_32BIT */
    int srcFixed = (cnt & 0x0100) != 0; /* DMA_SRC_FIXED */
    int unitSize = is32 ? 4 : 2;
    u32 totalBytes = units * unitSize;

    const void* src = port_resolve_dma_src(src_raw, totalBytes);
    void* dest = port_resolve_write_addr(dest_raw);
    if (!src || !dest)
        return;

    /*
     * HBlank-triggered DMA: register with the per-scanline simulator instead
     * of doing the transfer immediately. Drives the iris/circle WIN0H effect.
     */
    if ((cnt & 0x2000) != 0) {
        port_hdma_register(0, src, dest, cnt, (u16)units);
        return;
    }

    if (srcFixed) {
        /* Fill mode */
        if (is32) {
            u32 val = *(const u32*)src;
            u32* d = (u32*)dest;
            for (u32 i = 0; i < units; i++)
                d[i] = val;
        } else {
            u16 val = *(const u16*)src;
            u16* d = (u16*)dest;
            for (u32 i = 0; i < units; i++)
                d[i] = val;
        }
    } else {
        /* Copy mode */
        memcpy(dest, src, totalBytes);
    }
}

/* A DMA fill's source is an implementation-local scalar, often on the 3DS
 * stack. Do not route it through the GBA-address resolver: the stack can
 * overlap the numeric GBA ROM window. */
static inline void port_DmaFillTransfer(uintptr_t dest_raw, u32 control, u32 value) {
    u16 cnt = (u16)(control >> 16);
    u32 units = control & 0xFFFF;
    if (!(cnt & 0x8000))
        return;
    const int is32 = (cnt & 0x0400) != 0;
    void* dest = port_resolve_write_addr(dest_raw);
    if (!dest)
        return;
    if (is32) {
        u32* d = (u32*)dest;
        for (u32 i = 0; i < units; ++i)
            d[i] = value;
    } else {
        u16* d = (u16*)dest;
        const u16 fill = (u16)value;
        for (u32 i = 0; i < units; ++i)
            d[i] = fill;
    }
}

#define DmaSet(dmaNum, src, dest, control) port_DmaTransfer((const void*)(src), (uintptr_t)(dest), (u32)(control))

#define DMA_FILL(dmaNum, value, dest, size, bit)                                                               \
    port_DmaFillTransfer((uintptr_t)(dest),                                                                    \
                         (0x8000 | 0x0100 | ((bit) == 32 ? 0x0400 : 0)) << 16 | ((u32)(size) / ((bit) / 8)), \
                         (u32)(value))

#define DMA_COPY(dmaNum, src, dest, size, bit)              \
    port_DmaTransfer((const void*)(src), (uintptr_t)(dest), \
                     (0x8000 | ((bit) == 32 ? 0x0400 : 0)) << 16 | ((u32)(size) / ((bit) / 8)))

#define DmaFill16(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 16)
#define DmaFill32(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 32)
#define DmaCopy16(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 16)
#define DmaCopy32(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 32)

#define DmaStop(dmaNum) ((void)0)
#define DmaWait(dmaNum) ((void)0)

#define DMA_CLEAR(dmaNum, dest, size, bit)     \
    {                                          \
        vu##bit* _dest = (vu##bit*)(dest);     \
        u32 _size = size;                      \
        DmaFill##bit(dmaNum, 0, _dest, _size); \
    }
#define DmaClear16(dmaNum, dest, size) DMA_CLEAR(dmaNum, dest, size, 16)
#define DmaClear32(dmaNum, dest, size) DMA_CLEAR(dmaNum, dest, size, 32)

#define DmaCopyLarge(dmaNum, src, dest, size, block, bit)   \
    port_DmaTransfer((const void*)(src), (uintptr_t)(dest), \
                     (0x8000 | ((bit) == 32 ? 0x0400 : 0)) << 16 | ((u32)(size) / ((bit) / 8)))

#define DmaCopyLarge16(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 16)
#define DmaCopyLarge32(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 32)

#define DmaClearLarge(dmaNum, dest, size, block, bit) DMA_CLEAR(dmaNum, dest, size, bit)
#define DmaClearLarge16(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 16)
#define DmaClearLarge32(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 32)

#define DmaFillLarge(dmaNum, value, dest, size, block, bit) DMA_FILL(dmaNum, value, dest, size, bit)
#define DmaFillLarge16(dmaNum, value, dest, size, block) DmaFillLarge(dmaNum, value, dest, size, block, 16)
#define DmaFillLarge32(dmaNum, value, dest, size, block) DmaFillLarge(dmaNum, value, dest, size, block, 32)

#define DmaCopyDefvars(dmaNum, src, dest, size, bit) DMA_COPY(dmaNum, src, dest, size, bit)
#define DmaCopy16Defvars(dmaNum, src, dest, size) DmaCopyDefvars(dmaNum, src, dest, size, 16)
#define DmaCopy32Defvars(dmaNum, src, dest, size) DmaCopyDefvars(dmaNum, src, dest, size, 32)

#define DmaFillDefvars(dmaNum, value, dest, size, bit) DMA_FILL(dmaNum, value, dest, size, bit)
#define DmaFill16Defvars(dmaNum, value, dest, size) DmaFillDefvars(dmaNum, value, dest, size, 16)
#define DmaFill32Defvars(dmaNum, value, dest, size) DmaFillDefvars(dmaNum, value, dest, size, 32)

#define DmaClearDefvars(dmaNum, dest, size, bit) DMA_CLEAR(dmaNum, dest, size, bit)
#define DmaClear16Defvars(dmaNum, dest, size) DmaClearDefvars(dmaNum, dest, size, 16)
#define DmaClear32Defvars(dmaNum, dest, size) DmaClearDefvars(dmaNum, dest, size, 32)

#define IntrEnable(flags)  \
    {                      \
        u16 imeTemp;       \
        imeTemp = REG_IME; \
        REG_IME = 0;       \
        REG_IE |= (flags); \
        REG_IME = imeTemp; \
    }

#else /* !PC_PORT — original GBA DMA macros */
#define CPU_FILL(value, dest, size, bit)                                                                       \
    {                                                                                                          \
        vu##bit tmp = (vu##bit)(value);                                                                        \
        CpuSet((void*)&tmp, dest, CPU_SET_##bit##BIT | CPU_SET_SRC_FIXED | ((size) / ((bit) / 8) & 0x1FFFFF)); \
    }

#define CpuFill16(value, dest, size) CPU_FILL(value, dest, size, 16)
#define CpuFill32(value, dest, size) CPU_FILL(value, dest, size, 32)

#define CPU_COPY(src, dest, size, bit) CpuSet(src, dest, CPU_SET_##bit##BIT | ((size) / ((bit) / 8) & 0x1FFFFF))

#define CpuCopy16(src, dest, size) CPU_COPY(src, dest, size, 16)
#define CpuCopy32(src, dest, size) CPU_COPY(src, dest, size, 32)

#define CpuFastFill(value, dest, size)                                                          \
    {                                                                                           \
        vu32 tmp = (vu32)(value);                                                               \
        CpuFastSet((void*)&tmp, dest, CPU_FAST_SET_SRC_FIXED | ((size) / (32 / 8) & 0x1FFFFF)); \
    }

#define CpuFastFill16(value, dest, size) CpuFastFill(((value) << 16) | (value), (dest), (size))

#define CpuFastFill8(value, dest, size) \
    CpuFastFill(((value) << 24) | ((value) << 16) | ((value) << 8) | (value), (dest), (size))

#define CpuFastCopy(src, dest, size) CpuFastSet(src, dest, ((size) / (32 / 8) & 0x1FFFFF))

#ifdef PC_PORT
#define DmaSet(dmaNum, src, dest, control)                      \
    {                                                           \
        gba_write32(REG_ADDR_DMA##dmaNum, (vu32)(src));         \
        gba_write32(REG_ADDR_DMA##dmaNum + 4, (vu32)(dest));    \
        gba_write32(REG_ADDR_DMA##dmaNum + 8, (vu32)(control)); \
    }
#else
#define DmaSet(dmaNum, src, dest, control)                      \
    {                                                           \
        vu32* dmaRegs = (vu32*)REG_ADDR_DMA##dmaNum;            \
        dmaRegs[0] = (vu32)(src);                               \
        dmaRegs[1] = (vu32)(dest);                              \
        dmaRegs[2] = (vu32)(control);                           \
    }
#endif

#define DMA_FILL(dmaNum, value, dest, size, bit)                                                    \
    {                                                                                               \
        vu##bit tmp = (vu##bit)(value);                                                             \
        DmaSet(dmaNum, &tmp, dest,                                                                  \
               (DMA_ENABLE | DMA_START_NOW | DMA_##bit##BIT | DMA_SRC_FIXED | DMA_DEST_INC) << 16 | \
                   ((size) / ((bit) / 8)));                                                         \
    }

#define DmaFill16(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 16)
#define DmaFill32(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 32)

// Note that the DMA clear macros cause the DMA control value to be calculated
// at runtime rather than compile time. The size is divided by the DMA transfer
// unit size (2 or 4 bytes) and then combined with the DMA control flags using a
// bitwise OR operation.

#define DMA_CLEAR(dmaNum, dest, size, bit)     \
    {                                          \
        vu##bit* _dest = (vu##bit*)(dest);     \
        u32 _size = size;                      \
        DmaFill##bit(dmaNum, 0, _dest, _size); \
    }

#define DmaClear16(dmaNum, dest, size) DMA_CLEAR(dmaNum, dest, size, 16)
#define DmaClear32(dmaNum, dest, size) DMA_CLEAR(dmaNum, dest, size, 32)

#define DMA_COPY(dmaNum, src, dest, size, bit) \
    DmaSet(dmaNum, src, dest,                  \
           (DMA_ENABLE | DMA_START_NOW | DMA_##bit##BIT | DMA_SRC_INC | DMA_DEST_INC) << 16 | ((size) / ((bit) / 8)))

#define DmaCopy16(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 16)
#define DmaCopy32(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 32)

#define DmaStop(dmaNum)                                                       \
    {                                                                         \
        vu16* control = (vu16*)(REG_ADDR_DMA##dmaNum + 8);                    \
        *control &= ~(DMA_ENABLE | DMA_START_MASK | DMA_DREQ_ON | DMA_REPEAT);\
        *control &= ~DMA_ENABLE;                                              \
    }

#define DmaCopyLarge(dmaNum, src, dest, size, block, bit) \
    {                                                     \
        const void* _src = src;                           \
        void* _dest = dest;                               \
        u32 _size = size;                                 \
        while (1) {                                       \
            DmaCopy##bit(dmaNum, _src, _dest, (block));   \
            _src += (block);                              \
            _dest += (block);                             \
            _size -= (block);                             \
            if (_size <= (block)) {                       \
                DmaCopy##bit(dmaNum, _src, _dest, _size); \
                break;                                    \
            }                                             \
        }                                                 \
    }

#define DmaClearLarge(dmaNum, dest, size, block, bit)  \
    {                                                  \
        void* _dest = dest;                            \
        u32 _size = size;                              \
        while (1) {                                    \
            DmaFill##bit(dmaNum, 0, _dest, (block));   \
            _dest += (block);                          \
            _size -= (block);                          \
            if (_size <= (block)) {                    \
                DmaFill##bit(dmaNum, 0, _dest, _size); \
                break;                                 \
            }                                          \
        }                                              \
    }

#define DmaFillLarge(dmaNum, value, dest, size, block, bit) \
    {                                                       \
        void* _dest = (void*)(dest);                        \
        u32 _size = size;                                   \
        while (1) {                                         \
            DmaFill##bit(dmaNum, value, _dest, (block));    \
            _dest += (block);                               \
            _size -= (block);                               \
            if (_size <= (block)) {                         \
                DmaFill##bit(dmaNum, value, _dest, _size);  \
                break;                                      \
            }                                               \
        }                                                   \
    }

#define DmaCopyLarge16(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 16)

#define DmaCopyLarge32(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 32)

#define DmaFillLarge16(dmaNum, value, dest, size, block) DmaFillLarge(dmaNum, value, dest, size, block, 16)

#define DmaFillLarge32(dmaNum, value, dest, size, block) DmaFillLarge(dmaNum, value, dest, size, block, 32)

#define DmaClearLarge16(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 16)
#define DmaClearLarge32(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 32)

#define DmaCopyDefvars(dmaNum, src, dest, size, bit) \
    {                                                \
        const void* _src = src;                      \
        void* _dest = dest;                          \
        u32 _size = size;                            \
        DmaCopy##bit(dmaNum, _src, _dest, _size);    \
    }

#define DmaCopy16Defvars(dmaNum, src, dest, size) DmaCopyDefvars(dmaNum, src, dest, size, 16)
#define DmaCopy32Defvars(dmaNum, src, dest, size) DmaCopyDefvars(dmaNum, src, dest, size, 32)

#define DmaFillDefvars(dmaNum, value, dest, size, bit) \
    {                                                  \
        void* _dest = (void*)(dest);                   \
        u32 _size = size;                              \
        DmaFill##bit(dmaNum, value, _dest, _size);     \
    }

#define DmaFill16Defvars(dmaNum, value, dest, size) DmaFillDefvars(dmaNum, value, dest, size, 16)
#define DmaFill32Defvars(dmaNum, value, dest, size) DmaFillDefvars(dmaNum, value, dest, size, 32)

#define DmaClearDefvars(dmaNum, dest, size, bit) \
    {                                            \
        void* _dest = dest;                      \
        u32 _size = size;                        \
        DmaClear##bit(dmaNum, _dest, _size);     \
    }

#define DmaClear16Defvars(dmaNum, dest, size) DmaClearDefvars(dmaNum, dest, size, 16)
#define DmaClear32Defvars(dmaNum, dest, size) DmaClearDefvars(dmaNum, dest, size, 32)

#define DmaWait(DmaNo)                                           \
    {                                                            \
        vu16* control = (vu16*)(REG_ADDR_DMA##DmaNo + 8);        \
        while (*control & DMA_ENABLE)                            \
            ;                                                    \
    }

#define IntrEnable(flags)  \
    {                      \
        u16 imeTemp;       \
                           \
        imeTemp = REG_IME; \
        REG_IME = 0;       \
        REG_IE |= (flags); \
        REG_IME = imeTemp; \
    }

#endif /* PC_PORT */

#endif // MACRO_H
