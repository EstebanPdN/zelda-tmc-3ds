#ifndef GLOBAL_H
#define GLOBAL_H

/**
 * @defgroup Tasks Tasks
 * @defgroup Subtasks Subtasks
 * @brief Subtasks override the game task for short periods.
 * @defgroup WorldEvents World Events
 * @brief Cutscenes that happen after a kinstone fusion.
 */

/**
 * @defgroup Entities Entities
 */
///@{
/**
 * @defgroup Player Player
 * @defgroup Enemies Enemies
 * @defgroup Projectiles Projectiles
 * @defgroup Objects Objects
 * @defgroup NPCs NPCs
 * @defgroup Items Items
 * @defgroup Managers Managers
 * @brief Entities with a smaller footprint of 0x40 bytes.
 */
///@}

#include "gba/gba.h"
#include <stddef.h>
#include <string.h>

// Prevent cross-jump optimization.
#define BLOCK_CROSS_JUMP asm("");

// to help in decompiling
#define asm_comment(x) asm volatile("@ -- " x " -- ")
#define asm_unified(x) asm(".syntax unified\n" x "\n.syntax divided")

#if defined(__APPLE__) || defined(__CYGWIN__)
// Get the IDE to stfu

// We define it this way to fool preproc.
#define INCBIN(...) { 0 }
#define INCBIN_U8 INCBIN
#define INCBIN_U16 INCBIN
#define INCBIN_U32 INCBIN
#define INCBIN_S8 INCBIN
#define INCBIN_S16 INCBIN
#define INCBIN_S32 INCBIN
#define _(x) (x)
#define __(x) (x)
#endif // __APPLE__

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#define SWAP(a, b, temp) \
    {                    \
        (temp) = a;      \
        (a) = b;         \
        (b) = temp;      \
    }

// useful math macros

// Converts a number to Q8.8 fixed-point format
#define Q_8_8(n) ((s16)((n) * 256))

// Converts a number to Q16.16 fixed-point format
#define Q_16_16(n) ((s32)((n) * (1 << 16)))

#ifndef __cplusplus
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) >= (b) ? (a) : (b))
#endif

#if defined(PORT_IGNORE_STATIC_ASSERTS)
#define static_assert(...)
#elif defined(PC_PORT)
#if defined(__cplusplus)
/* Use the built-in C++11 keyword on the PC port. */
#else
#define static_assert(...) _Static_assert(__VA_ARGS__)
#endif
#else
#define static_assert(...) //_Static_assert(__VA_ARGS__)
#endif

#if defined(PC_PORT) && (__SIZEOF_POINTER__ == 8)
#define PORT_POINTERS_ARE_64_BIT 1
#define PORT_STATIC_ASSERT_SIZE(type, gba_size, pc_size, msg) static_assert(sizeof(type) == (pc_size), msg)
#define PORT_STATIC_ASSERT_EXPR(expr, gba_size, pc_size, msg) static_assert((expr) == (pc_size), msg)
#define PORT_STATIC_ASSERT_OFFSET(type, field, gba_off, pc_off, msg) static_assert(offsetof(type, field) == (pc_off), msg)
#else
#define PORT_POINTERS_ARE_64_BIT 0
#define PORT_STATIC_ASSERT_SIZE(type, gba_size, pc_size, msg) static_assert(sizeof(type) == (gba_size), msg)
#define PORT_STATIC_ASSERT_EXPR(expr, gba_size, pc_size, msg) static_assert((expr) == (gba_size), msg)
#define PORT_STATIC_ASSERT_OFFSET(type, field, gba_off, pc_off, msg) static_assert(offsetof(type, field) == (gba_off), msg)
#endif

#define super (&this->base)

#if NON_MATCHING
#define ASM_FUNC(path, decl)
#else
#define ASM_FUNC(path, decl)    \
    NAKED decl {                \
        asm(".include " #path); \
    }
#endif

#if NON_MATCHING
#define NONMATCH(path, decl) decl
#define END_NONMATCH
#else
#define NONMATCH(path, decl)    \
    NAKED decl {                \
        asm(".include " #path); \
        if (0)
#define END_NONMATCH }
#endif

#define FORCE_REGISTER(var, reg) var

#define MEMORY_BARRIER

typedef union {
    s32 WORD;
    struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        s16 y, x; /* #N64 BE: reversed so .x/.y alias the same WORD bits as little-endian */
#else
        s16 x, y;
#endif
    } HALF;
} Coords;

typedef struct {
    s8 x;
    s8 y;
} PACKED Coords8;

union SplitDWord {
    s64 DWORD;
    u64 DWORD_U;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct {
        s32 HI, LO;
    } HALF;
    struct {
        u32 HI, LO;
    } HALF_U;
#else
    struct {
        s32 LO, HI;
    } HALF;
    struct {
        u32 LO, HI;
    } HALF_U;
#endif
};

union SplitWord {
    s32 WORD;
    u32 WORD_U;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct {
        s16 HI, LO;
    } HALF;
    struct {
        u16 HI, LO;
    } HALF_U;
    struct {
        u8 byte3, byte2, byte1, byte0;
    } BYTES;
#else
    struct {
        s16 LO, HI;
    } HALF;
    struct {
        u16 LO, HI;
    } HALF_U;
    struct {
        u8 byte0, byte1, byte2, byte3;
    } BYTES;
#endif
};

union SplitHWord {
    u16 HWORD;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    struct {
        u8 HI, LO;
    } PACKED HALF;
#else
    struct {
        u8 LO, HI;
    } PACKED HALF;
#endif
} PACKED;

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
#define FORCE_WORD_ALIGNED __declspec(align(2))
#else
#define FORCE_WORD_ALIGNED __attribute__((packed, aligned(2)))
#endif

/* forward decls */
struct Entity_;

/**
 * bitset macros
 */

#define BIT(bit) (1 << (bit))
#define IS_BIT_SET(value, bit) ((value) & BIT(bit))

/**
 * Multi return function data type casts
 */
typedef u64 (*MultiReturnTypeSingleEntityArg)(struct Entity_*);
typedef s64 (*MultiReturnTypeTwoS32Arg)(s32, s32);

#include "region.h"
#endif // GLOBAL_H
