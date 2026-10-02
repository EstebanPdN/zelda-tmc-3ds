/* Proves DrawSliced's carried per-pixel mapping is bit-identical to SliceMap.
 *
 * DrawSliced used to call SliceMap once per destination pixel, which cost four
 * software divisions on ARM11 (no divide instruction) and dominated the bottom
 * screen paint. The replacement carries sd, se and the tiled-middle modulus
 * incrementally. That is only safe if it reproduces SliceMap exactly for every
 * pixel, so sweep the parameter space rather than trusting the derivation. */
#include <stdint.h>
#include <stdio.h>

/* Reference: verbatim copy of SliceMap from port_second_screen_theme.c. */
static int32_t SliceMapRef(int32_t d, int32_t dstLen, int32_t srcLen, int32_t corner, int32_t snap,
                           int32_t scale) {
    int32_t c = corner;
    int32_t sd, se, span, tspan;
    if (c * 2 * scale > dstLen) {
        c = dstLen / (2 * scale);
        if (c < 1) c = 1;
    }
    sd = d / scale;
    se = (dstLen - 1 - d) / scale;
    if (sd < c) return sd;
    if (se < c) return srcLen - 1 - se;
    span = srcLen - 2 * corner;
    if (span <= 0) return corner;
    tspan = (span / snap) * snap;
    if (tspan <= 0) tspan = span;
    return corner + (sd - c) % tspan;
}

/* Under test: the carried form, mirroring DrawSliced's inner loop. */
static int sweep_one(int32_t w, int32_t srcLen, int32_t cw, int32_t snap, int32_t scale,
                     unsigned long* checked) {
    int32_t cX = cw, spanX, tspanX, sdX = 0, sdAccX = 0, seX, seRemX, modX = 0, dx;
    if (w <= 0) return 1;
    if (cX * 2 * scale > w) {
        cX = w / (2 * scale);
        if (cX < 1) cX = 1;
    }
    spanX = srcLen - 2 * cw;
    tspanX = 0;
    if (spanX > 0) {
        tspanX = (spanX / snap) * snap;
        if (tspanX <= 0) tspanX = spanX;
    }
    seX = (w - 1) / scale;
    seRemX = (w - 1) % scale;
    if (tspanX > 0 && sdX >= cX) modX = (sdX - cX) % tspanX;

    {
        int32_t sd = sdX, sdAcc = sdAccX, se = seX, seRem = seRemX, mod = modX;
        for (dx = 0; dx < w; dx++) {
            int32_t got;
            const int32_t want = SliceMapRef(dx, w, srcLen, cw, snap, scale);
            if (sd < cX) got = sd;
            else if (se < cX) got = srcLen - 1 - se;
            else if (spanX <= 0) got = cw;
            else got = cw + mod;

            if (got != want) {
                fprintf(stderr,
                        "MISMATCH dx=%d w=%d srcLen=%d cw=%d snap=%d scale=%d: got %d want %d\n",
                        dx, w, srcLen, cw, snap, scale, got, want);
                return 0;
            }
            ++*checked;

            if (++sdAcc == scale) {
                sdAcc = 0;
                ++sd;
                if (tspanX > 0) {
                    if (sd == cX) mod = 0;
                    else if (sd > cX) { if (++mod >= tspanX) mod = 0; }
                }
            }
            if (seRem == 0) { --se; seRem = scale - 1; }
            else { --seRem; }
        }
    }
    return 1;
}

int main(void) {
    unsigned long checked = 0;
    /* Values spanning the real call sites (DrawPlate: corner 26/scale 2/snap 16;
     * DrawWell: corner 8/scale 1) plus degenerate and boundary shapes. */
    static const int32_t widths[] = { 1, 2, 3, 7, 8, 15, 16, 28, 31, 32, 63, 64, 137, 242, 313, 320, 2049 };
    static const int32_t srcLens[] = { 1, 2, 3, 8, 16, 17, 32, 40, 48, 64, 96, 128, 240 };
    static const int32_t corners[] = { 0, 1, 2, 8, 13, 26, 40 };
    static const int32_t snaps[] = { 1, 2, 3, 8, 16, 32 };
    static const int32_t scales[] = { 1, 2, 3, 4 };

    for (unsigned wi = 0; wi < sizeof(widths) / sizeof(*widths); ++wi)
        for (unsigned si = 0; si < sizeof(srcLens) / sizeof(*srcLens); ++si)
            for (unsigned ci = 0; ci < sizeof(corners) / sizeof(*corners); ++ci)
                for (unsigned ni = 0; ni < sizeof(snaps) / sizeof(*snaps); ++ni)
                    for (unsigned li = 0; li < sizeof(scales) / sizeof(*scales); ++li)
                        if (!sweep_one(widths[wi], srcLens[si], corners[ci], snaps[ni], scales[li],
                                       &checked))
                            return 1;

    printf("port_second_screen_slicemap_test: PASS (%lu pixel mappings identical)\n", checked);
    return 0;
}
