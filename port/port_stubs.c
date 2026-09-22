/**
 * @file port_stubs.c
 * @brief C implementations of functions originally in GBA assembly.
 *
 * These replace the auto-generated stubs in stubs_autogen.c.
 */

#include "global.h"
#include "port_rom.h"
#include "script.h"

/*
 * Script command reading functions.
 * Original GBA assembly in asm/src/script.s reads 16-bit aligned data
 * from the script instruction pointer.
 *
 * Script instructions are packed as halfwords (u16):
 *   [0] = opcode: bits 0-9 = command index, bits 10-15 = command size (halfwords)
 *   [1..N] = arguments (halfwords, combined into words as needed)
 */

u32 GetNextScriptCommandHalfword(u16* ptr) {
#ifdef PC_PORT
    if (!ptr) {
        fprintf(stderr, "[SCRIPT] FATAL: GetNextScriptCommandHalfword called with NULL ptr\n");
        return 0xFFFF;
    }
    {
        uintptr_t addr = (uintptr_t)ptr;
        /* Check if ptr is a small/garbage value (not a real heap/data pointer) */
        if (addr < 0x10000u) {
            fprintf(stderr, "[SCRIPT] FATAL: GetNextScriptCommandHalfword bad ptr %p (too small)\n", (void*)ptr);
            return 0xFFFF;
        }
        /* Script pointers are either inside the native ROM buffer or still
         * encoded as GBA addresses. Do not infer provenance from whether a
         * 3DS virtual page happens to be mapped. */
        bool nativePointer =
            gRomData && addr >= (uintptr_t)gRomData && addr < (uintptr_t)(gRomData + gRomSize);
        if (addr >= 0x08000000u && addr < 0x0A000000u && !nativePointer) {
            fprintf(stderr, "[SCRIPT] FATAL: GetNextScriptCommandHalfword called with unresolved GBA addr 0x%08X\n",
                    (unsigned)addr);
            return 0xFFFF;
        }
        /* Check: if ptr is inside gRomData, make sure it's within bounds */
        if (gRomData && addr >= (uintptr_t)gRomData && addr < (uintptr_t)(gRomData + gRomSize)) {
            /* Valid ROM pointer — OK */
        } else {
            /* Not in ROM — check if it's a plausible native pointer.
             * On 64-bit Windows, user-mode addresses have upper 16+ bits zero.
             * If upper 32 bits are non-zero and it's not a valid heap/stack range, reject it. */
#if PORT_POINTERS_ARE_64_BIT
            if ((addr >> 48) != 0) {
                fprintf(stderr,
                        "[SCRIPT] FATAL: GetNextScriptCommandHalfword corrupted ptr=%p entity context corrupted\n",
                        (void*)ptr);
                return 0xFFFF;
            }
#endif
        }
    }
#endif
    return ptr[0];
}

u32 GetNextScriptCommandHalfwordAfterCommandMetadata(u16* ptr) {
#ifdef PC_PORT
    if (!ptr)
        return 0xFFFF;
#endif
    return ptr[1];
}

u32 GetNextScriptCommandWord(u16* ptr) {
#ifdef PC_PORT
    if (!ptr)
        return 0xFFFFFFFF;
#endif
    return (u32)ptr[0] | ((u32)ptr[1] << 16);
}

u32 GetNextScriptCommandWordAfterCommandMetadata(u16* ptr) {
#ifdef PC_PORT
    if (!ptr)
        return 0xFFFFFFFF;
#endif
    return (u32)ptr[1] | ((u32)ptr[2] << 16);
}

/*
 * Subtask function pointer tables.
 * Originally in data/const/subtask.s as .4byte arrays.
 */

extern void Subtask_FadeIn(void);
extern void Subtask_Init(void);
extern void Subtask_Update(void);
extern void Subtask_FadeOut(void);
extern void Subtask_Die(void);

void (*const gUnk_0812901C[])(void) = {
    Subtask_FadeIn, Subtask_Init, Subtask_Update, Subtask_FadeOut, Subtask_Die,
};

extern void Subtask_Exit(void);
extern void Subtask_PauseMenu(void);
extern void Subtask_MapHint(void);
extern void Subtask_KinstoneMenu(void);
extern void Subtask_AuxCutscene(void);
extern void Subtask_PortalCutscene(void);
extern void Subtask_FigurineMenu(void);
extern void Subtask_WorldEvent(void);
extern void Subtask_FastTravel(void);
extern void Subtask_LocalMapHint(void);

void (*const gSubtasks[])(void) = {
    Subtask_Exit,         Subtask_PauseMenu,    Subtask_Exit, /* index 2 = same as Exit */
    Subtask_MapHint,      Subtask_KinstoneMenu, Subtask_AuxCutscene, Subtask_PortalCutscene,
    Subtask_FigurineMenu, Subtask_WorldEvent,   Subtask_FastTravel,  Subtask_LocalMapHint,
};
