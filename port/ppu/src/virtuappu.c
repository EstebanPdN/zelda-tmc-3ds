/*
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Software GBA PPU, vendored as first-party port source. Derived from
 * VirtuaPPU by Mathéo Vignaud (https://github.com/MatheoVignaud/VirtuaPPU,
 * commit 5cf5e99) and incorporating this project's 15 accuracy/portability
 * patches (formerly port/patches/viruappu-*.patch; preserved in git history).
 * Maintained here directly — not kept in sync with upstream.
 */

#include "virtuappu.h"

#include <string.h>

#include "modes_impl.h"

uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];
uint8_t virtuappu_vram[VIRTUAPPU_VRAM_SIZE];
PPUMemory virtuappu_registers;

void virtuappu_reset(void)
{
    memset(virtuappu_frame_buffer, 0, sizeof(virtuappu_frame_buffer));
    memset(virtuappu_vram, 0, sizeof(virtuappu_vram));
    memset(&virtuappu_registers, 0, sizeof(virtuappu_registers));
}

/* VPPU render-mode selector — set by port_ppu.cpp from the GBA DISPCNT mode:
 *   1 = tiled  — GBA mode 0: 4 text BGs + OBJ
 *   2 = affine — GBA modes 1/2: affine BG2 + text BGs + OBJ
 * Both are handled by virtuappu_mode1_render_frame, which branches on the mode
 * (BG2 drawn affine vs tiled). The Minish Cap only ever uses GBA modes 0 and 1.
 * The former VPPU mode 0 (a black-fill stub) and mode 7 (a misnamed Game Boy
 * DMG renderer) were dead on this path and have been removed. */
void virtuappu_render_frame(void)
{
    switch (virtuappu_registers.mode) {
    case 1:
    case 2:
        virtuappu_mode1_render_frame(&virtuappu_registers);
        break;
    default:
        break;
    }
}

uint32_t *virtuappu_get_frame_buffer(void)
{
    return virtuappu_frame_buffer;
}

uint8_t *virtuappu_get_vram(void)
{
    return virtuappu_vram;
}

PPUMemory *virtuappu_get_registers(void)
{
    return &virtuappu_registers;
}
