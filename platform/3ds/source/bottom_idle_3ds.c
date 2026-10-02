#include "bottom_idle_3ds.h"

#include <math.h>
#include <stddef.h>

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

/* Same equilateral scanline construction as zelda3-3DS v3.0-E3's
 * draw_cinema(): a one-pixel apex, widening by 2/sqrt(3) each row. */
static void FillUpTriangle(uint32_t* pixels, int width, int height, int stride,
                           float centerX, float top, int size, uint32_t color) {
    for (int row = 0; row <= size; ++row) {
        const float lineWidth = (float)row * 1.1546f;
        int x0 = (int)ceilf(centerX - lineWidth * 0.5f);
        int x1 = (int)floorf(centerX + lineWidth * 0.5f);
        const int y = (int)floorf(top + (float)row + 0.5f);
        if (x1 < x0) x1 = x0;
        FillRect(pixels, width, height, stride, x0, y, x1 + 1, y + 1, color);
    }
}

void BottomIdle3DS_Paint(uint32_t* pixels, int width, int height, int strideInPixels, uint32_t tick) {
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
    const float pulse = sinf((float)tick * (1.5f / 20.0f)) * 0.5f + 0.5f;
    const uint8_t gold = (uint8_t)(150.0f + 100.0f * pulse);
    const uint32_t triangleColor = IDLE_RGBA(gold, (uint8_t)(gold * 0.83f),
                                             (uint8_t)(gold * 0.41f));
    const int size = (int)floorf((float)(width < height ? width : height) * 0.06f + 0.5f);
    const float centerX = (float)width * 0.5f;
    const float centerY = (float)height * 0.5f;

    FillUpTriangle(pixels, width, height, strideInPixels,
                   centerX, centerY - (float)size, size, triangleColor);
    FillUpTriangle(pixels, width, height, strideInPixels,
                   centerX - (float)size * 0.58f, centerY, size, triangleColor);
    FillUpTriangle(pixels, width, height, strideInPixels,
                   centerX + (float)size * 0.58f, centerY, size, triangleColor);
}
