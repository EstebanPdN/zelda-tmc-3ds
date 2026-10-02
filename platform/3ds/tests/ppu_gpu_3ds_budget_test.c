#include "ppu_gpu_3ds_budget.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void) {
    PpuGpu3DSBatch batches[4096] = { 0 };
    PpuGpu3DSCommandBuffer commands = { .batches = batches, .batchCount = 1 };
    const size_t fixed = PPU_GPU3DS_SETUP_WORDS + PPU_GPU3DS_PRESENT_WORDS;
    assert(PpuGpu3DS_CommandWordsRequired(&commands) == fixed);
    batches[0].indexCount = 6; /* Clear is already included in setup. */
    assert(PpuGpu3DS_CommandWordsRequired(&commands) == fixed);
    commands.batchCount = 5;
    batches[1] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_BG0, .indexCount = 6 };
    batches[2] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_OBJ, .indexCount = 6 };
    batches[3] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_OBJ, .objWindow = true, .indexCount = 6 };
    batches[4] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_OBJ, .indexCount = 0 };
    size_t required = PpuGpu3DS_CommandWordsRequired(&commands);
    assert(required == fixed + 4 * PPU_GPU3DS_WORDS_PER_DRAW);
    assert(PpuGpu3DS_CommandBudgetFits(required, true, required, 0));
    assert(!PpuGpu3DS_CommandBudgetFits(required, true, required, 1));
    assert(PpuGpu3DS_CommandBudgetFits(required, true, required + 7, 7));
    assert(!PpuGpu3DS_CommandBudgetFits(required, false, required, 0));
    assert(!PpuGpu3DS_CommandBudgetFits(required, true, 65536, 65536));
    assert(!PpuGpu3DS_CommandBudgetFits(required, true, 0, SIZE_MAX));

    /* A scanline window can fit geometry yet need more than the old 256 KiB
     * hardware list. The enlarged list accepts it without reducing batches. */
    commands.batchCount = 1921;
    for (size_t i = 1; i < commands.batchCount; ++i)
        batches[i] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_BG0, .indexCount = 6 };
    required = PpuGpu3DS_CommandWordsRequired(&commands);
    assert(!PpuGpu3DS_CommandBudgetFits(required, true, 0x40000 / 4, 0));
    assert(PpuGpu3DS_CommandBudgetFits(required, true, PPU_GPU3DS_COMMAND_BUFFER_BYTES / 4, 0));
    /* Full batch table and OBJ prepasses must still fail safely. */
    commands.batchCount = 4096;
    for (size_t i = 1; i < commands.batchCount; ++i)
        batches[i] = (PpuGpu3DSBatch){ .layer = PPU_GPU3DS_OBJ, .indexCount = 6 };
    required = PpuGpu3DS_CommandWordsRequired(&commands);
    assert(!PpuGpu3DS_CommandBudgetFits(required, true, PPU_GPU3DS_COMMAND_BUFFER_BYTES / 4, 0));
    puts("GPU command budget: passes, capacity boundaries and scanline-window pressure passed.");
    return 0;
}
