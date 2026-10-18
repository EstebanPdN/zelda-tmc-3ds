#ifndef PORT_TYPES_H
#define PORT_TYPES_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define PORT_STATIC_ASSERT static_assert
#else
#define PORT_STATIC_ASSERT _Static_assert
#endif

// Type definitions for GBA-style fixed-width types
typedef uint8_t u8;
typedef uint16_t u16;
#if defined(TMC_N64) || defined(TMC_3DS)
typedef unsigned int u32; /* match gba/types.h: mips-newlib uint32_t is 'unsigned long' */
#else
typedef uint32_t u32;
#endif
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
#if defined(TMC_N64) || defined(TMC_3DS)
typedef int s32;
#else
typedef int32_t s32;
#endif
typedef int64_t s64;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

// Type assertions
PORT_STATIC_ASSERT(sizeof(u8) == 1, "u8 must be 1 byte");
PORT_STATIC_ASSERT(sizeof(u16) == 2, "u16 must be 2 bytes");
PORT_STATIC_ASSERT(sizeof(u32) == 4, "u32 must be 4 bytes");
PORT_STATIC_ASSERT(sizeof(u64) == 8, "u64 must be 8 bytes");
PORT_STATIC_ASSERT(sizeof(s8) == 1, "s8 must be 1 byte");
PORT_STATIC_ASSERT(sizeof(s16) == 2, "s16 must be 2 bytes");
PORT_STATIC_ASSERT(sizeof(s32) == 4, "s32 must be 4 bytes");
PORT_STATIC_ASSERT(sizeof(s64) == 8, "s64 must be 8 bytes");
// Pointer assertions — the PC port targets 64-bit systems only. The N64
// target is the lone 32-bit-pointer consumer of this header.
#if !defined(TMC_N64) && !defined(TMC_3DS)
PORT_STATIC_ASSERT(sizeof(void*) == 8, "PC port targets 64-bit systems only");
#endif

#endif // PORT_TYPES_H
