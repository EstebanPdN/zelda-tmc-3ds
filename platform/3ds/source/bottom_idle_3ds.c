#include "bottom_idle_3ds.h"

#include <math.h>
#include <stddef.h>
#include "ss_tex_triforce.h"

/* The second-screen upload buffer stores RGBA bytes. On little-endian ARM11
 * that is A | B | G | R when viewed as a uint32_t. */
#define IDLE_RGBA(r, g, b) \
    (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

static void FillRect(uint32_t* pixels, int width, int height, int stride,
                     int x0, int y0, int x1, int y1, uint32_t color) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y) {
        uint32_t* row = pixels + (size_t)y * (size_t)stride;
        for (int x = x0; x < x1; ++x) row[x] = color;
    }
}

static void OutlineRect(uint32_t* pixels, int width, int height, int stride,
                        int x0, int y0, int x1, int y1, int thickness, uint32_t color) {
    FillRect(pixels, width, height, stride, x0, y0, x1, y0 + thickness, color);
    FillRect(pixels, width, height, stride, x0, y1 - thickness, x1, y1, color);
    FillRect(pixels, width, height, stride, x0, y0, x0 + thickness, y1, color);
    FillRect(pixels, width, height, stride, x1 - thickness, y0, x1, y1, color);
}

/* PR #32's procedural AA mask, sampled linearly into the software UI.
 * Pixel-center sampling keeps the three silhouettes smooth at native 320x240. */
static void BlitTriangle(uint32_t* pixels, int width, int height, int stride,
                         float left, float top, float size, uint8_t gold) {
    for (int y = (int)floorf(top); y < (int)ceilf(top + size); ++y) {
        for (int x = (int)floorf(left); x < (int)ceilf(left + size); ++x) {
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
            float sx = ((x + .5f - left) / size) * 64 - .5f;
            float sy = ((y + .5f - top) / size) * 64 - .5f;
            if (sx < 0) sx = 0; if (sx > 63) sx = 63;
            if (sy < 0) sy = 0; if (sy > 63) sy = 63;
            int ix = (int)sx, iy = (int)sy;
            int nx = ix < 63 ? ix + 1 : ix, ny = iy < 63 ? iy + 1 : iy;
            float fx = sx - ix, fy = sy - iy;
            float a0 = (kSSTexTriforce[iy*64+ix] >> 24) * (1-fx) +
                       (kSSTexTriforce[iy*64+nx] >> 24) * fx;
            float a1 = (kSSTexTriforce[ny*64+ix] >> 24) * (1-fx) +
                       (kSSTexTriforce[ny*64+nx] >> 24) * fx;
            unsigned alpha = (unsigned)(a0*(1-fy)+a1*fy+.5f);
            uint32_t old = pixels[y*stride+x];
            unsigned r = (gold*alpha + (old&255)*(255-alpha)+127)/255;
            unsigned g = ((uint8_t)(gold*.83f)*alpha + ((old>>8)&255)*(255-alpha)+127)/255;
            unsigned b = ((uint8_t)(gold*.41f)*alpha + ((old>>16)&255)*(255-alpha)+127)/255;
            pixels[y*stride+x] = IDLE_RGBA(r,g,b);
        }
    }
}

void BottomIdle3DS_Paint(uint32_t* pixels, int width, int height, int strideInPixels, uint32_t tick, bool animate) {
    if (!pixels || width <= 0 || height <= 0 || strideInPixels < width) return;

    const uint32_t black = IDLE_RGBA(0, 0, 0);
    const uint32_t darkGold = IDLE_RGBA(122, 88, 30);
    FillRect(pixels, width, height, strideInPixels, 0, 0, width, height, black);

    float unit = (float)(width < height ? width : height) / 720.0f;
    if (unit < 0.5f) unit = 0.5f;
    int inset = (int)floorf(12.0f * unit + 0.5f);
    int thickness = (int)floorf(2.0f * unit + 0.5f);
    if (thickness < 1) thickness = 1;
    OutlineRect(pixels, width, height, strideInPixels, inset, inset,
                width - inset, height - inset, thickness, darkGold);

    /* zelda3's source uses SDL_GetTicks()/1000 and sin(t * 1.5). The Minish
     * Cap panel advances at 20 Hz, so tick/20 preserves the same pulse. */
    const float pulse = animate ? sinf((float)tick * (1.5f / 20.0f)) * 0.5f + 0.5f : 0;
    const uint8_t gold = animate ? (uint8_t)(150.0f + 100.0f * pulse) : 214;
    const float size = (float)(width < height ? width : height) * .06f;
    const float cx = width * .5f, cy = height * .5f;
    BlitTriangle(pixels, width, height, strideInPixels, cx-size/2, cy-size+1, size, gold);
    BlitTriangle(pixels, width, height, strideInPixels, cx-size*.58f-size/2, cy, size, gold);
    BlitTriangle(pixels, width, height, strideInPixels, cx+size*.58f-size/2-1, cy, size, gold);
}
