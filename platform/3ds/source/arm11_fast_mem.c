#include "arm11_fast_mem.h"

#include <stdint.h>
#include <string.h>

/* Below this the library call wins: the prologue here costs more than the
 * latency it hides. Two cache lines. */
#define ARM11_FAST_MIN_BYTES 64u

void Arm11FastMemcpy(void* dst, const void* src, size_t bytes) {
#if defined(__3DS__) && defined(__ARM_ARCH_6__)
    if (bytes < ARM11_FAST_MIN_BYTES ||
        (((uintptr_t)dst | (uintptr_t)src) & 3u) != 0u) {
        memcpy(dst, src, bytes);
        return;
    }
    uint32_t* out = (uint32_t*)dst;
    const uint32_t* in = (const uint32_t*)src;
    size_t words = bytes >> 2u;
    while (words >= 8u) {
        /* Three lines ahead: enough to cover an FCRAM miss at this loop's
         * throughput without evicting what is still in use. */
        __builtin_prefetch(in + 24, 0, 0);
        const uint32_t w0 = in[0], w1 = in[1], w2 = in[2], w3 = in[3];
        const uint32_t w4 = in[4], w5 = in[5], w6 = in[6], w7 = in[7];
        out[0] = w0; out[1] = w1; out[2] = w2; out[3] = w3;
        out[4] = w4; out[5] = w5; out[6] = w6; out[7] = w7;
        in += 8;
        out += 8;
        words -= 8u;
    }
    while (words--) *out++ = *in++;
    const size_t tail = bytes & 3u;
    if (tail) memcpy(out, in, tail);
#else
    memcpy(dst, src, bytes);
#endif
}

void Arm11FastMemset(void* dst, int value, size_t bytes) {
#if defined(__3DS__) && defined(__ARM_ARCH_6__)
    if (bytes < ARM11_FAST_MIN_BYTES || ((uintptr_t)dst & 3u) != 0u) {
        memset(dst, value, bytes);
        return;
    }
    const uint8_t byte = (uint8_t)value;
    const uint32_t word = (uint32_t)byte * 0x01010101u;
    uint32_t* out = (uint32_t*)dst;
    size_t words = bytes >> 2u;
    while (words >= 8u) {
        out[0] = word; out[1] = word; out[2] = word; out[3] = word;
        out[4] = word; out[5] = word; out[6] = word; out[7] = word;
        out += 8;
        words -= 8u;
    }
    while (words--) *out++ = word;
    const size_t tail = bytes & 3u;
    if (tail) memset(out, value, tail);
#else
    memset(dst, value, bytes);
#endif
}
