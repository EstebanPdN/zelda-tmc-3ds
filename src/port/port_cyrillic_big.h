#ifndef PORT_CYRILLIC_BIG_H
#define PORT_CYRILLIC_BIG_H

/* Cyrillic capitals for the stylized banner font (ROM bank 8).
 *
 * The ROM face has no Cyrillic, so the letters are drawn here as 1-bit
 * masks (original artwork, 11 rows, 2px strokes) and turned into cells of
 * the ROM's own format at runtime: two 8x16 4bpp cells, row 0 = metrics,
 * pixel values 14 body, 10..13 shades, 1 outline, 15 rim. The weight, cap
 * height, per-row shading, outline connectivity and rim side are measured
 * from the ROM's own 'H', so the new letters pick up the lettering style of
 * whichever ROM is loaded instead of guessing it. */

#include <stdint.h>
#include <string.h>

#include "port_cyrillic.h"

#define PORT_CYR_MASK_ROWS 11
#define PORT_CYR_BIG_CELL_BYTES 128

typedef struct PortCyrMask {
    uint8_t width;
    const char* rows[PORT_CYR_MASK_ROWS];
} PortCyrMask;

static const PortCyrMask kPortCyrBigMasks[PORT_CYR_LETTER_COUNT] = {
    { 8,
      { "..####..", ".######.", "##....##", "##....##", "##....##", "########", "########", "##....##", "##....##",
        "##....##", "##....##" } }, /* А */
    { 8,
      { "########", "########", "##......", "##......", "#######.", "########", "##....##", "##....##", "##....##",
        "########", "#######." } }, /* Б */
    { 8,
      { "#######.", "########", "##....##", "##....##", "#######.", "#######.", "##....##", "##....##", "##....##",
        "########", "#######." } }, /* В */
    { 7,
      { "#######", "#######", "##.....", "##.....", "##.....", "##.....", "##.....", "##.....", "##.....", "##.....",
        "##....." } }, /* Г */
    { 9,
      { "..######.", "..######.", "..##..##.", "..##..##.", "..##..##.", "..##..##.", ".##...##.", ".##...##.",
        "#########", "#########", "##.....##" } }, /* Д */
    { 7,
      { "#######", "#######", "##.....", "##.....", "######.", "######.", "##.....", "##.....", "##.....", "#######",
        "#######" } }, /* Е */
    { 10,
      { "##..##..##", "##..##..##", ".##.##.##.", ".##.##.##.", "..######..", "..######..", ".##.##.##.", ".##.##.##.",
        "##..##..##", "##..##..##", "##..##..##" } }, /* Ж */
    { 8,
      { ".######.", "########", "##....##", "......##", "...####.", "...####.", "......##", "......##", "##....##",
        "########", ".######." } }, /* З */
    { 8,
      { "##....##", "##....##", "##...###", "##...###", "##..####", "##.##.##", "####..##", "###...##", "###...##",
        "##....##", "##....##" } }, /* И */
    { 8,
      { ".##..##.", "..####..", "##....##", "##...###", "##..####", "##.##.##", "####..##", "###...##", "##....##",
        "##....##", "##....##" } }, /* Й */
    { 8,
      { "##....##", "##...##.", "##..##..", "##.##...", "####....", "####....", "##.##...", "##..##..", "##...##.",
        "##....##", "##....##" } }, /* К */
    { 8,
      { "..######", "..######", "..##..##", "..##..##", "..##..##", "..##..##", ".##...##", ".##...##", ".##...##",
        "##....##", "##....##" } }, /* Л */
    { 10,
      { "##......##", "###....###", "####..####", "##.####.##", "##..##..##", "##......##", "##......##", "##......##",
        "##......##", "##......##", "##......##" } }, /* М */
    { 8,
      { "##....##", "##....##", "##....##", "##....##", "########", "########", "##....##", "##....##", "##....##",
        "##....##", "##....##" } }, /* Н */
    { 8,
      { ".######.", "########", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##",
        "########", ".######." } }, /* О */
    { 8,
      { "########", "########", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##", "##....##",
        "##....##", "##....##" } }, /* П */
    { 8,
      { "#######.", "########", "##....##", "##....##", "##....##", "########", "#######.", "##......", "##......",
        "##......", "##......" } }, /* Р */
    { 8,
      { ".######.", "########", "##....##", "##......", "##......", "##......", "##......", "##......", "##....##",
        "########", ".######." } }, /* С */
    { 8,
      { "########", "########", "...##...", "...##...", "...##...", "...##...", "...##...", "...##...", "...##...",
        "...##...", "...##..." } }, /* Т */
    { 8,
      { "##....##", "##....##", "##....##", "##....##", ".##..###", "..######", "......##", "......##", ".....##.",
        "#######.", "######.." } }, /* У */
    { 10,
      { "....##....", "..######..", ".########.", "##..##..##", "##..##..##", "##..##..##", "##..##..##", ".########.",
        "..######..", "....##....", "....##...." } }, /* Ф */
    { 8,
      { "##....##", "##....##", ".##..##.", "..####..", "...##...", "...##...", "..####..", ".##..##.", "##....##",
        "##....##", "##....##" } }, /* Х */
    { 9,
      { "##....##.", "##....##.", "##....##.", "##....##.", "##....##.", "##....##.", "##....##.", "##....##.",
        "#########", "#########", ".......##" } }, /* Ц */
    { 8,
      { "##....##", "##....##", "##....##", "##....##", "##...###", ".#######", "..######", "......##", "......##",
        "......##", "......##" } }, /* Ч */
    { 10,
      { "##..##..##", "##..##..##", "##..##..##", "##..##..##", "##..##..##", "##..##..##", "##..##..##", "##..##..##",
        "##..##..##", "##########", "##########" } }, /* Ш */
    { 11,
      { "##..##..##.", "##..##..##.", "##..##..##.", "##..##..##.", "##..##..##.", "##..##..##.", "##..##..##.",
        "##..##..##.", "###########", "###########", ".........##" } }, /* Щ */
    { 9,
      { "###......", "###......", ".##......", ".##......", ".#######.", ".########", ".##....##", ".##....##",
        ".##....##", ".########", ".#######." } }, /* Ъ */
    { 11,
      { "##.......##", "##.......##", "##.......##", "##.......##", "######...##", "#######..##", "##...##..##",
        "##...##..##", "##...##..##", "#######..##", "######...##" } }, /* Ы */
    { 7,
      { "##.....", "##.....", "##.....", "##.....", "######.", "#######", "##...##", "##...##", "##...##", "#######",
        "######." } }, /* Ь */
    { 8,
      { ".######.", "########", "##....##", "......##", "...#####", "...#####", "......##", "......##", "##....##",
        "########", ".######." } }, /* Э */
    { 11,
      { "##...#####.", "##..#######", "##..##...##", "##..##...##", "######...##", "######...##", "##..##...##",
        "##..##...##", "##..##...##", "##..#######", "##...#####." } }, /* Ю */
    { 8,
      { ".#######", "########", "##....##", "##....##", "##....##", "########", ".#######", "...##.##", "..##..##",
        ".##...##", "##....##" } }, /* Я */
};

typedef struct PortCyrBigStyle {
    int32_t top;         /* first body row inside the 16-row cell */
    int32_t height;      /* body rows (cap height) */
    int32_t stem;        /* vertical stroke width */
    int32_t diagonal;    /* outline covers diagonal neighbours too */
    int32_t rimDirs;     /* PORT_CYR_RIM_* sides the rim sits on, past the outline */
    uint8_t rowFill[16]; /* body/shade value per body row */
} PortCyrBigStyle;

#define PORT_CYR_RIM_DOWN 1
#define PORT_CYR_RIM_RIGHT 2
#define PORT_CYR_RIM_UP 4
#define PORT_CYR_RIM_LEFT 8

static inline int PortCyr_IsInk(uint8_t v) {
    return v >= 10 && v <= 14;
}

static inline void PortCyr_DefaultStyle(PortCyrBigStyle* st) {
    int32_t i;
    st->top = 2;
    st->height = PORT_CYR_MASK_ROWS;
    st->stem = 2;
    st->diagonal = 1;
    st->rimDirs = PORT_CYR_RIM_DOWN | PORT_CYR_RIM_RIGHT;
    for (i = 0; i < 16; i++) {
        st->rowFill[i] = (uint8_t)(i < 7 ? 14 : 12);
    }
}

/* Reads one cell's metrics row the way the game does: leading 0xF nibbles
 * give the start column, the following non-0xF nibbles the width. */
static inline void PortCyr_CellSpan(const uint8_t* cell, int32_t* start, int32_t* width) {
    int32_t i = 0, j;
    for (; i < 8; i++) {
        uint8_t nib = (uint8_t)((i & 1) ? (cell[i >> 1] >> 4) : (cell[i >> 1] & 0x0Fu));
        if (nib != 0x0Fu) {
            break;
        }
    }
    j = i;
    for (; i < 8; i++) {
        uint8_t nib = (uint8_t)((i & 1) ? (cell[i >> 1] >> 4) : (cell[i >> 1] & 0x0Fu));
        if (nib == 0x0Fu) {
            break;
        }
    }
    *start = j;
    *width = i - j;
}

/* Unpacks a two-cell stylized glyph into a 16x16 value grid as drawn
 * (each cell's metric span placed back to back). Returns the width. */
static inline int32_t PortCyr_UnpackBig(const uint8_t* glyph, uint8_t grid[16][16]) {
    int32_t cell, w = 0;
    memset(grid, 0, 16 * 16);
    for (cell = 0; cell < 2; cell++) {
        const uint8_t* cp = glyph + cell * 64;
        int32_t gs, gw, row, col;
        PortCyr_CellSpan(cp, &gs, &gw);
        for (row = 1; row < 16; row++) {
            for (col = gs; col < gs + gw && col < 8; col++) {
                uint8_t packed = cp[row * 4 + (col >> 1)];
                uint8_t pix = (uint8_t)((col & 1) ? (packed >> 4) : (packed & 0x0Fu));
                if (w + col - gs < 16) {
                    grid[row][w + col - gs] = pix;
                }
            }
        }
        w += gw;
    }
    return w > 16 ? 16 : w;
}

/* Measures the lettering style from the ROM's 'H' (two straight stems and
 * a bar: the cleanest sample of weight, height and shading). Anything that
 * does not look like a sane glyph keeps the default for that parameter. */
static inline void PortCyr_MeasureStyle(const uint8_t* romH, PortCyrBigStyle* st) {
    uint8_t g[16][16];
    int32_t w, row, col, top = -1, bottom = -1, left = 16;
    PortCyr_DefaultStyle(st);
    if (romH == NULL) {
        return;
    }
    w = PortCyr_UnpackBig(romH, g);
    for (row = 1; row < 16; row++) {
        for (col = 0; col < w; col++) {
            if (PortCyr_IsInk(g[row][col])) {
                if (top < 0) {
                    top = row;
                }
                bottom = row;
                if (col < left) {
                    left = col;
                }
            }
        }
    }
    if (top < 1 || bottom - top + 1 < 7 || left >= w) {
        return;
    }
    st->top = top;
    st->height = bottom - top + 1;

    /* Stem: ink run from the left edge, a quarter of the way down. */
    {
        int32_t r = top + st->height / 4, n = 0;
        for (col = left; col < w && PortCyr_IsInk(g[r][col]); col++) {
            n++;
        }
        if (n >= 1 && n <= 4) {
            st->stem = n < 2 ? 2 : n;
        }
    }

    /* Per-row shading along the left stem. */
    for (row = 0; row < st->height && row < 16; row++) {
        int32_t counts[16] = { 0 }, best = 14, bestN = 0, v;
        for (col = left; col < left + st->stem && col < w; col++) {
            uint8_t px = g[top + row][col];
            if (PortCyr_IsInk(px)) {
                counts[px]++;
            }
        }
        for (v = 10; v <= 14; v++) {
            if (counts[v] > bestN) {
                bestN = counts[v];
                best = v;
            }
        }
        st->rowFill[row] = (uint8_t)best;
    }

    /* Outline connectivity: is the pixel diagonally outside the stem's
     * top-left corner outline too? */
    if (left >= 1 && top >= 2) {
        st->diagonal = g[top - 1][left - 1] == 1;
    }

    /* Rim side(s): which side(s) of the outline's bounding box carry 15.
     * Only rim pixels beyond the outline's extent count, so the H's inner
     * counter cannot vote; a side needs two pixels (one is just a corner). */
    {
        int32_t minR = 16, maxR = -1, minC = 16, maxC = -1, votes[4] = { 0 }, d, dirs = 0;
        for (row = 1; row < 16; row++) {
            for (col = 0; col < w; col++) {
                if (g[row][col] == 1) {
                    minR = row < minR ? row : minR;
                    maxR = row > maxR ? row : maxR;
                    minC = col < minC ? col : minC;
                    maxC = col > maxC ? col : maxC;
                }
            }
        }
        for (row = 1; row < 16 && maxR >= 0; row++) {
            for (col = 0; col < w; col++) {
                if (g[row][col] != 15) {
                    continue;
                }
                votes[0] += row > maxR; /* down  */
                votes[1] += col > maxC; /* right */
                votes[2] += row < minR; /* up    */
                votes[3] += col < minC; /* left  */
            }
        }
        for (d = 0; d < 4; d++) {
            if (votes[d] >= 2) {
                dirs |= 1 << d;
            }
        }
        st->rimDirs = dirs;
    }
}

/* Builds one letter as a 128-byte stylized cell pair in the ROM format. */
static inline void PortCyr_BuildBigGlyph(const PortCyrBigStyle* st, int letter, uint8_t out[PORT_CYR_BIG_CELL_BYTES]) {
    uint8_t g[16][16];
    const PortCyrMask* m;
    int32_t extra, padL, padT, w, row, col, cell;
    memset(out, 0, PORT_CYR_BIG_CELL_BYTES);
    if (letter < 0 || letter >= PORT_CYR_LETTER_COUNT) {
        return;
    }
    m = &kPortCyrBigMasks[letter];
    memset(g, 0, sizeof(g));
    extra = st->stem > 2 ? st->stem - 2 : 0;
    padL = 1 + ((st->rimDirs & PORT_CYR_RIM_LEFT) ? 1 : 0);
    padT = st->top;
    w = padL + m->width + extra + 1 + ((st->rimDirs & PORT_CYR_RIM_RIGHT) ? 1 : 0);
    if (w > 16) {
        w = 16;
    }

    /* Body: rows resampled to the measured cap height, strokes widened to
     * the measured stem, each row in the measured body/shade value. */
    for (row = 0; row < st->height && padT + row < 16; row++) {
        const char* src = m->rows[row * PORT_CYR_MASK_ROWS / st->height];
        for (col = 0; col < m->width; col++) {
            int32_t k;
            if (src[col] != '#') {
                continue;
            }
            for (k = 0; k <= extra; k++) {
                int32_t x = padL + col + k;
                if (x < w) {
                    g[padT + row][x] = st->rowFill[row];
                }
            }
        }
    }

    /* Outline around the body. */
    for (row = 1; row < 16; row++) {
        for (col = 0; col < w; col++) {
            int32_t dy, dx, hit = 0;
            if (g[row][col] != 0) {
                continue;
            }
            for (dy = -1; dy <= 1 && !hit; dy++) {
                for (dx = -1; dx <= 1 && !hit; dx++) {
                    int32_t y = row + dy, x = col + dx;
                    if ((dx == 0 && dy == 0) || (!st->diagonal && dx != 0 && dy != 0)) {
                        continue;
                    }
                    if (y >= 1 && y < 16 && x >= 0 && x < w && PortCyr_IsInk(g[y][x])) {
                        hit = 1;
                    }
                }
            }
            if (hit) {
                g[row][col] = 2; /* temporary mark, becomes 1 below */
            }
        }
    }
    for (row = 1; row < 16; row++) {
        for (col = 0; col < w; col++) {
            if (g[row][col] == 2) {
                g[row][col] = 1;
            }
        }
    }

    /* Rim past the outline on the measured side(s). */
    if (st->rimDirs) {
        for (row = 1; row < 16; row++) {
            for (col = 0; col < w; col++) {
                if (g[row][col] != 0) {
                    continue;
                }
                if (((st->rimDirs & PORT_CYR_RIM_DOWN) && row > 1 && g[row - 1][col] == 1) ||
                    ((st->rimDirs & PORT_CYR_RIM_RIGHT) && col > 0 && g[row][col - 1] == 1) ||
                    ((st->rimDirs & PORT_CYR_RIM_UP) && row < 15 && g[row + 1][col] == 1) ||
                    ((st->rimDirs & PORT_CYR_RIM_LEFT) && col < w - 1 && g[row][col + 1] == 1)) {
                    g[row][col] = 3; /* temporary mark */
                }
            }
        }
        for (row = 1; row < 16; row++) {
            for (col = 0; col < w; col++) {
                if (g[row][col] == 3) {
                    g[row][col] = 15;
                }
            }
        }
    }

    /* Pack into two cells; row 0 carries each cell's span. */
    for (cell = 0; cell < 2; cell++) {
        uint8_t* cp = out + cell * 64;
        int32_t x0 = cell * 8, span = w - x0;
        if (span < 0) {
            span = 0;
        }
        if (span > 8) {
            span = 8;
        }
        for (col = 0; col < 8; col++) {
            uint8_t nib = (uint8_t)(col < span ? 0x0u : 0xFu);
            cp[col >> 1] |= (uint8_t)((col & 1) ? (nib << 4) : nib);
        }
        for (row = 1; row < 16; row++) {
            for (col = 0; col < span; col++) {
                uint8_t v = g[row][x0 + col];
                cp[row * 4 + (col >> 1)] |= (uint8_t)((col & 1) ? (v << 4) : v);
            }
        }
    }
}

#endif /* PORT_CYRILLIC_BIG_H */
