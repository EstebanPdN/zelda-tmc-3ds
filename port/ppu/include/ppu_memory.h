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

#pragma once

#include <stdint.h>

typedef struct PPUMemory {
    /* Visible pixels per scanline. Rendering loops cull to this width. */
    uint16_t frame_width;
    /* Pixels between row starts in virtuappu_frame_buffer. A value of 0
     * means tightly packed rows (`frame_width`). Kept separate so ports can
     * render a 240-wide viewport into a wider fixed-pitch buffer. */
    uint16_t frame_pitch;
    uint8_t mode;
    uint8_t reserved;
} PPUMemory;
