#pragma once

#include "port_ppu_gpu_3ds_model.h"

enum {
    /* The scanline windows used by lanterns can exceed Citro3D's 256 KiB
     * default even when the vertex/index/batch buffers all fit. Allocate once
     * at startup; never grow or recycle an in-flight command list. */
    PPU_GPU3DS_COMMAND_BUFFER_BYTES = 1024 * 1024,
    /* Citro3D emits at most 36 words for six TEV stages, 34 for effects,
     * 30 for DrawElements, 4 for scissor and 6 for the offset uniform per
     * steady-state draw. Round up and separately reserve shader/target setup,
     * the stencil clear, both screen presenters, Bilinear, FPS and finalizers.
     * Re-audit these bounds when adding GPU state or changing the SDK. */
    PPU_GPU3DS_WORDS_PER_DRAW = 128,
    PPU_GPU3DS_SETUP_WORDS = 4096,
    PPU_GPU3DS_PRESENT_WORDS = 8192,
};

static inline size_t PpuGpu3DS_CommandWordsRequired(const PpuGpu3DSCommandBuffer* commands) {
    size_t words = PPU_GPU3DS_SETUP_WORDS + PPU_GPU3DS_PRESENT_WORDS;
    /* Batch zero is the target clear. OBJ colour batches also participate in
     * the depth prepass; object-window batches are drawn only once. */
    for (size_t i = 1; i < commands->batchCount; ++i) {
        const PpuGpu3DSBatch* batch = &commands->batches[i];
        if (batch->indexCount == 0) continue;
        const size_t draws = batch->layer == PPU_GPU3DS_OBJ && !batch->objWindow ? 2u : 1u;
        if (words > SIZE_MAX - draws * PPU_GPU3DS_WORDS_PER_DRAW) return SIZE_MAX;
        words += draws * PPU_GPU3DS_WORDS_PER_DRAW;
    }
    return words;
}

static inline bool PpuGpu3DS_CommandBudgetFits(size_t required, bool hasBuffer,
                                               size_t capacity, size_t used) {
    return hasBuffer && used <= capacity && required <= capacity - used;
}
