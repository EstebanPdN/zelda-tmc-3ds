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

/**
 * Auto-generated asset header file
 * Generated from: assets.json
 */

#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>

/* ========== Structures ========== */

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb888_t;

typedef struct {
    rgb888_t colors[16];
    uint8_t color_count;
} palette16_t;

typedef struct {
    uint8_t data[32]; /* 8x8 pixels, 4bpp */
} tile_t;

typedef struct {
    const char* name;
    uint16_t width;
    uint16_t height;
    uint16_t tiles_w;
    uint16_t tiles_h;
    uint8_t palette_index;
    uint16_t num_tiles;
    const tile_t* tiles;
} sprite_t;

/* ========== Palettes (16 colors) ========== */

#define NUM_PALETTES_16 1

static const palette16_t PALETTE_0 = {
    .colors = {
        {0, 0, 0},
        {255, 182, 0},
        {255, 255, 0},
        {255, 255, 121},
        {255, 255, 255},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0}
    },
    .color_count = 16
};

static const palette16_t* const PALETTES_16[NUM_PALETTES_16] = {
    &PALETTE_0
};

/* ========== Sprites ========== */

#define NUM_SPRITES 1

static const tile_t SPRITE_0001_TILES[4] = {
    {{ /* Tile 0 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 
        0x00, 0x00, 0x10, 0x21, 0x00, 0x00, 0x11, 0x22, 
        0x00, 0x00, 0x21, 0x22, 0x00, 0x10, 0x22, 0x22
    }},
    {{ /* Tile 1 */
        0x00, 0x00, 0x10, 0x11, 0x00, 0x11, 0x21, 0x22, 
        0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 
        0x22, 0x33, 0x33, 0x33, 0x32, 0x33, 0x33, 0x33
    }},
    {{ /* Tile 2 */
        0x00, 0x21, 0x22, 0x22, 0x00, 0x21, 0x22, 0x32, 
        0x10, 0x22, 0x22, 0x33, 0x10, 0x22, 0x22, 0x33, 
        0x10, 0x22, 0x32, 0x33, 0x21, 0x22, 0x32, 0x33, 
        0x21, 0x22, 0x32, 0x33, 0x21, 0x22, 0x32, 0x33
    }},
    {{ /* Tile 3 */
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x43, 
        0x33, 0x33, 0x33, 0x44, 0x33, 0x33, 0x43, 0x44
    }}
};

static const sprite_t SPRITE_SPRITE_0001 = {
    .name = "Sprite-0001",
    .width = 16,
    .height = 16,
    .tiles_w = 2,
    .tiles_h = 2,
    .palette_index = 0,
    .num_tiles = 4,
    .tiles = SPRITE_0001_TILES
};

static const sprite_t* const SPRITES[NUM_SPRITES] = {
    &SPRITE_SPRITE_0001
};

/* Sprite indices */
#define SPRITE_IDX_SPRITE_0001 0

#endif /* ASSETS_H */
