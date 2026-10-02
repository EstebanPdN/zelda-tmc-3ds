/*
 * port_filter.c — CPU CRT/LCD post-process approximations of Sonkun's
 * shader pack and an LCD-grid preset. See port_filter.h for the
 * disclaimer about these being visual approximations rather than
 * literal ports of the GLSL shaders.
 *
 * Implementation strategy: each filter is a single forward pass over
 * the framebuffer with mask lookups indexed by (x, y) modulo the
 * pattern stride. Pattern stride is sized to the internal_scale so
 * one "phosphor triad" or "LCD cell" maps to one GBA-native pixel
 * (8/scale per cell at internal_scale=4 → 2 px per channel stripe).
 *
 * Pixel format throughout: ABGR8888 little-endian
 *   byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A
 * Reading: r=fb[i]&0xFF, g=(fb[i]>>8)&0xFF, b=(fb[i]>>16)&0xFF
 * Writing: fb[i] = 0xFF000000u | (b<<16) | (g<<8) | r
 */

#include "port_filter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const char* Port_Filter_Name(PortFilterType t) {
    switch (t) {
        case PORT_FILTER_NONE:               return "Off";
        case PORT_FILTER_CRT_WARM_COMPOSITE: return "CRT Warm Composite (AG)";
        case PORT_FILTER_LCD_GRID:           return "LCD Grid";
        case PORT_FILTER_CRT_WARM_RF:        return "CRT Warm RF (AG)";
        case PORT_FILTER_SCANLINES:          return "Scanlines";
        case PORT_FILTER_HANDHELD:           return "Handheld Grid";
        case PORT_FILTER_VIGNETTE:           return "Vignette";
        default: return "?";
    }
}

static inline uint8_t Clamp255(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* Multiply each channel of pixel `c` by 8.8-fixed gain triple (gr, gg, gb).
 * Saturating at 255. Used for warm colour casts. */
static inline uint32_t WarmTint(uint32_t c, int gr, int gg, int gb) {
    int r = (int)( c        & 0xFFu);
    int g = (int)((c >>  8) & 0xFFu);
    int b = (int)((c >> 16) & 0xFFu);
    r = (r * gr) >> 8;
    g = (g * gg) >> 8;
    b = (b * gb) >> 8;
    return 0xFF000000u
         | ((uint32_t)Clamp255(b) << 16)
         | ((uint32_t)Clamp255(g) <<  8)
         | (uint32_t)Clamp255(r);
}

/* Aperture-grill mask: each phosphor stripe is `stripe_w` pixels wide
 * and gets one of three RGB-dominance triples in turn. Boost the
 * dominant channel slightly, attenuate the other two — yields the
 * characteristic vertical RGB-bar pattern of an aperture-grill CRT. */
static inline void ApertureGrillStripe(int x, int stripe_w, int* gr, int* gg, int* gb) {
    int stripe = (x / stripe_w) % 3;
    /* Base 256 = 1.0; +25% on dominant, -28% on others → gives clear
     * vertical bars at 4× upscale without dimming the picture too far. */
    switch (stripe) {
        case 0: *gr = 320; *gg = 184; *gb = 184; return;
        case 1: *gr = 184; *gg = 320; *gb = 184; return;
        case 2: *gr = 184; *gg = 184; *gb = 320; return;
    }
    *gr = 256; *gg = 256; *gb = 256;
}

static void Apply_CrtWarmComposite(uint32_t* fb, int w, int h, int scale) {
    /* "Composite" = soft horizontal blur (3-tap) + warm tint + AG mask.
     * Stripe width is sized to the internal scale: at 4× one stripe is
     * ~1 GBA-pixel-of-width, which reads as a clear RGB triad on
     * common monitor sizes. */
    int stripe_w = scale > 0 ? scale : 1;

    for (int y = 0; y < h; ++y) {
        uint32_t* row = &fb[(size_t)y * (size_t)w];

        /* Pass 1: 3-tap horizontal blur in place. Read+write left to
         * right with a one-pixel cache so we don't sample the
         * already-blurred neighbour. */
        uint32_t prev = row[0];
        for (int x = 0; x < w; ++x) {
            uint32_t cur  = row[x];
            uint32_t next = row[x + 1 < w ? x + 1 : x];

            int rL = (int)( prev        & 0xFF), rC = (int)( cur        & 0xFF), rR = (int)( next        & 0xFF);
            int gL = (int)((prev >>  8) & 0xFF), gC = (int)((cur >>  8) & 0xFF), gR = (int)((next >>  8) & 0xFF);
            int bL = (int)((prev >> 16) & 0xFF), bC = (int)((cur >> 16) & 0xFF), bR = (int)((next >> 16) & 0xFF);
            int rn = (rL + 2 * rC + rR) >> 2;
            int gn = (gL + 2 * gC + gR) >> 2;
            int bn = (bL + 2 * bC + bR) >> 2;

            prev = cur;
            row[x] = 0xFF000000u | ((uint32_t)bn << 16) | ((uint32_t)gn << 8) | (uint32_t)rn;
        }

        /* Pass 2: AG mask + warm tint. Warm = R *1.05, B *0.92. */
        for (int x = 0; x < w; ++x) {
            int gr, gg, gb;
            ApertureGrillStripe(x, stripe_w, &gr, &gg, &gb);
            /* Combine warm tint into the mask gains (1.05/0.92 = 268/235
             * in 8.8 fixed). */
            gr = (gr * 268) >> 8;
            gb = (gb * 235) >> 8;
            row[x] = WarmTint(row[x], gr, gg, gb);
        }
    }
}

static void Apply_LcdGrid(uint32_t* fb, int w, int h, int scale) {
    /* GBA-style LCD: one pixel cell = `scale` screen pixels. Cell
     * borders are darkened to ~70%; the cell interior is left alone.
     * No colour cast — LCD reproduces the source faithfully, the grid
     * is the entire "filter". */
    if (scale < 2) return; /* not enough pixels for a visible grid */

    for (int y = 0; y < h; ++y) {
        const bool border_y = (y % scale) == 0;
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        for (int x = 0; x < w; ++x) {
            const bool border_x = (x % scale) == 0;
            if (border_x || border_y) {
                int r = (int)( row[x]        & 0xFF);
                int g = (int)((row[x] >>  8) & 0xFF);
                int b = (int)((row[x] >> 16) & 0xFF);
                r = (r * 180) >> 8;
                g = (g * 180) >> 8;
                b = (b * 180) >> 8;
                row[x] = 0xFF000000u
                       | ((uint32_t)b << 16)
                       | ((uint32_t)g <<  8)
                       | (uint32_t)r;
            }
        }
    }
}

static void Apply_CrtWarmRf(uint32_t* fb, int w, int h, int scale) {
    /* RF = poorer signal than composite. We add: heavier horizontal
     * smear (5-tap), brightness scanlines (every other row dimmed to
     * 80%), and the same warm AG mask. The smear is what makes "RF"
     * look soft — scanlines give it the analog-TV row cadence. */
    int stripe_w = scale > 0 ? scale : 1;

    /* Horizontal smear pass — 5-tap weighted [1,2,3,2,1]/9. */
    for (int y = 0; y < h; ++y) {
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        /* Two-pixel rolling cache so the smear reads pre-blur values. */
        uint32_t p0 = row[0];
        uint32_t p1 = row[0];
        for (int x = 0; x < w; ++x) {
            uint32_t p2 = row[x];
            uint32_t p3 = row[x + 1 < w ? x + 1 : x];
            uint32_t p4 = row[x + 2 < w ? x + 2 : x];

            int r = (int)((p0       )&0xFF) + 2*(int)((p1       )&0xFF) + 3*(int)((p2       )&0xFF) + 2*(int)((p3       )&0xFF) + (int)((p4       )&0xFF);
            int g = (int)((p0>>8    )&0xFF) + 2*(int)((p1>>8    )&0xFF) + 3*(int)((p2>>8    )&0xFF) + 2*(int)((p3>>8    )&0xFF) + (int)((p4>>8    )&0xFF);
            int b = (int)((p0>>16   )&0xFF) + 2*(int)((p1>>16   )&0xFF) + 3*(int)((p2>>16   )&0xFF) + 2*(int)((p3>>16   )&0xFF) + (int)((p4>>16   )&0xFF);
            r /= 9; g /= 9; b /= 9;

            p0 = p1;
            p1 = p2;
            row[x] = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }
    }

    /* AG + warm + scanline pass. */
    for (int y = 0; y < h; ++y) {
        const bool dark_row = ((y / (scale > 0 ? scale : 1)) & 1) != 0;
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        for (int x = 0; x < w; ++x) {
            int gr, gg, gb;
            ApertureGrillStripe(x, stripe_w, &gr, &gg, &gb);
            /* Warm tint gains (R 1.06, B 0.90) folded in. */
            gr = (gr * 271) >> 8;
            gb = (gb * 230) >> 8;
            if (dark_row) {
                gr = (gr * 205) >> 8;
                gg = (gg * 205) >> 8;
                gb = (gb * 205) >> 8;
            }
            row[x] = WarmTint(row[x], gr, gg, gb);
        }
    }
}

/* libretro scanline.glsl equivalent — dim every other "logical row" by
 * ~25%. Stride matches the internal scale so the dark line covers one
 * GBA pixel of height at any upscale level. No colour cast — pairs
 * cleanly with the CRT mask filters or stands alone for a softer
 * CRT-ish look without the RGB stripe pattern. */
static void Apply_Scanlines(uint32_t* fb, int w, int h, int scale) {
    if (scale < 2) return;
    for (int y = 0; y < h; ++y) {
        const bool dark = ((y / scale) & 1) != 0;
        if (!dark) continue;
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        for (int x = 0; x < w; ++x) {
            int r = (int)( row[x]        & 0xFF);
            int g = (int)((row[x] >>  8) & 0xFF);
            int b = (int)((row[x] >> 16) & 0xFF);
            r = (r * 192) >> 8;  /* 0.75× */
            g = (g * 192) >> 8;
            b = (b * 192) >> 8;
            row[x] = 0xFF000000u
                   | ((uint32_t)b << 16)
                   | ((uint32_t)g <<  8)
                   | (uint32_t)r;
        }
    }
}

/* libretro handheld-grid equivalent — like LCD Grid but tighter cells
 * and a cool green-shifted tint matching the unlit GBA / Game Boy
 * Pocket era LCD palette. Cell border dim is harsher (~60%) so the
 * grid reads as a deliberate hardware mimicry. */
static void Apply_HandheldGrid(uint32_t* fb, int w, int h, int scale) {
    if (scale < 2) return;
    /* Tint folded into a per-pixel gain triple — slight green push, red
     * pulled down, blue pulled down further (the cool/sickly cast of
     * an unlit LCD). */
    const int tr = 240;  /* 0.94× */
    const int tg = 264;  /* 1.03× */
    const int tb = 232;  /* 0.91× */
    for (int y = 0; y < h; ++y) {
        const bool border_y = (y % scale) == 0;
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        for (int x = 0; x < w; ++x) {
            const bool border_x = (x % scale) == 0;
            int r = (int)( row[x]        & 0xFF);
            int g = (int)((row[x] >>  8) & 0xFF);
            int b = (int)((row[x] >> 16) & 0xFF);
            r = (r * tr) >> 8;
            g = (g * tg) >> 8;
            b = (b * tb) >> 8;
            if (border_x || border_y) {
                r = (r * 154) >> 8;  /* 0.60× — heavier than LCD Grid */
                g = (g * 154) >> 8;
                b = (b * 154) >> 8;
            }
            row[x] = 0xFF000000u
                   | ((uint32_t)Clamp255(b) << 16)
                   | ((uint32_t)Clamp255(g) <<  8)
                   | (uint32_t)Clamp255(r);
        }
    }
}

/* libretro vignette.glsl equivalent — radial fall-off from screen
 * centre. Strength chosen so corners drop to ~55% brightness and the
 * inner ~60% of the image is untouched. No colour cast, no per-pixel
 * pattern; cheap (mul+shift per pixel) and composable on top of the
 * other filters in spirit, though we keep it as a single mode here for
 * the F8 enum. */
static void Apply_Vignette(uint32_t* fb, int w, int h, int scale) {
    (void)scale;
    if (w < 2 || h < 2) return;
    const int cx = w / 2;
    const int cy = h / 2;
    /* Max distance squared from centre to any corner. */
    const int max_d2 = cx * cx + cy * cy;
    if (max_d2 == 0) return;
    /* Inner radius² where vignette starts kicking in (~60% of max).
     * Below this, leave pixels alone. */
    const int inner_d2 = (max_d2 * 36) / 100;
    const int span_d2  = max_d2 - inner_d2;
    if (span_d2 <= 0) return;
    /* Corner gain — bottom of the fall-off. 0.55× = 141 in 8.8 fixed. */
    const int corner_gain = 141;
    const int unity_gain  = 256;
    for (int y = 0; y < h; ++y) {
        const int dy = y - cy;
        uint32_t* row = &fb[(size_t)y * (size_t)w];
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= inner_d2) continue;
            /* Linear interpolation between unity and corner_gain across
             * the (inner_d2 .. max_d2) band. */
            int t = ((d2 - inner_d2) * 256) / span_d2;
            if (t > 256) t = 256;
            const int gain = unity_gain + (((corner_gain - unity_gain) * t) >> 8);
            int r = (int)( row[x]        & 0xFF);
            int g = (int)((row[x] >>  8) & 0xFF);
            int b = (int)((row[x] >> 16) & 0xFF);
            r = (r * gain) >> 8;
            g = (g * gain) >> 8;
            b = (b * gain) >> 8;
            row[x] = 0xFF000000u
                   | ((uint32_t)Clamp255(b) << 16)
                   | ((uint32_t)Clamp255(g) <<  8)
                   | (uint32_t)Clamp255(r);
        }
    }
}

void Port_Filter_Apply(uint32_t* fb, int w, int h, int internal_scale,
                       PortFilterType filter) {
    if (!fb || w <= 0 || h <= 0) return;
    switch (filter) {
        case PORT_FILTER_NONE: return;
        case PORT_FILTER_CRT_WARM_COMPOSITE: Apply_CrtWarmComposite(fb, w, h, internal_scale); return;
        case PORT_FILTER_LCD_GRID:           Apply_LcdGrid(fb, w, h, internal_scale); return;
        case PORT_FILTER_CRT_WARM_RF:        Apply_CrtWarmRf(fb, w, h, internal_scale); return;
        case PORT_FILTER_SCANLINES:          Apply_Scanlines(fb, w, h, internal_scale); return;
        case PORT_FILTER_HANDHELD:           Apply_HandheldGrid(fb, w, h, internal_scale); return;
        case PORT_FILTER_VIGNETTE:           Apply_Vignette(fb, w, h, internal_scale); return;
        default: return;
    }
}
