/* Deterministic parity fuzzer for the native-width 4bpp renderer.
 *
 * Every generated PPU state is rendered by the same production source through
 * the optimized Old and New profiles, then with native fast paths disabled so
 * the long-standing generic renderer acts as the oracle. Any differing pixel
 * is a hard failure. */

#include "cpu/mode1.h"
#include "virtuappu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];

static uint8_t sIo[MODE1_IO_MEM_SIZE];
static uint8_t sVram[MODE1_VRAM_SIZE];
static uint16_t sBgPalette[MODE1_PALETTE_COLORS];
static uint16_t sObjPalette[MODE1_PALETTE_COLORS];
static uint16_t sOam[MODE1_OAM_HALFWORDS];
static uint16_t sShadow[MODE1_GBA_BG_COUNT][MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];
static uint32_t sFast[MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT];
static uint32_t sNewFast[MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT];
static uint32_t sReference[MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT];
static uint32_t sRandom = 0x6D2B79F5u;

typedef enum ScanlineProfile {
    SCANLINE_NONE = 0,
    SCANLINE_DARKNESS,
    SCANLINE_DARKNESS_WRAP,
    SCANLINE_MINISH_RAYS,
    SCANLINE_STEAM,
    SCANLINE_DUAL_WINDOW
} ScanlineProfile;

static ScanlineProfile sScanlineProfile;

static uint32_t NextRandom(void) {
    uint32_t x = sRandom;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    sRandom = x;
    return x;
}

static void WriteIo16(unsigned offset, uint16_t value);

/* The enter-room banner occupies complete BG0 rows but is not a gMessage
 * textbox.  At 266px the HUD right-anchor alone suppresses x=176..201 — the
 * exact 26px tear from the report.  Publishing the banner as a full-width
 * message band must instead reproduce every native x=0..239 pixel, centered
 * at x=13..252 without a hole. */
static int CheckEnterRoomBannerBand(void) {
    enum {
        SCREEN_BASE = 31,
        BANNER_LINE = 40,
        SHIFT = (MODE1_GBA_WIDTH - 240) / 2,
    };
    uint32_t nativeLine[MODE1_GBA_WIDTH];
    uint32_t bannerLine[MODE1_GBA_WIDTH];
    uint8_t nativePriority[MODE1_GBA_WIDTH];
    uint8_t bannerPriority[MODE1_GBA_WIDTH];

    memset(sIo, 0, sizeof(sIo));
    memset(sVram, 0, sizeof(sVram));
    memset(sBgPalette, 0, sizeof(sBgPalette));
    /* BG0: 4bpp, char base 0, screen base 31, 32x32 tiles. */
    WriteIo16(MODE1_IO_BG0CNT, (uint16_t)(SCREEN_BASE << 8u));
    for (int color = 1; color < 16; color++) {
        sBgPalette[color] = (uint16_t)(color | (color << 5u) | (color << 10u));
    }
    for (int tile = 1; tile <= 32; tile++) {
        uint8_t color = (uint8_t)(1 + (tile - 1) % 15);
        memset(sVram + tile * 32, (int)(color | (color << 4u)), 32);
    }
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            size_t map = (size_t)SCREEN_BASE * 0x800u + (size_t)(row * 32 + col) * 2u;
            uint16_t entry = (uint16_t)(col + 1);
            sVram[map] = (uint8_t)entry;
            sVram[map + 1u] = (uint8_t)(entry >> 8u);
        }
    }

    virtuappu_mode1_ws_shadow[0] = NULL;
    virtuappu_mode1_ws_hud_right_anchor = 0;
    virtuappu_mode1_ws_msg_shift = 0;
    memset(nativeLine, 0, sizeof(nativeLine));
    memset(nativePriority, 0xff, sizeof(nativePriority));
    virtuappu_mode1_render_text_bg_line(0, BANNER_LINE, nativeLine, nativePriority);

    virtuappu_mode1_ws_hud_right_anchor = 1;
    virtuappu_mode1_ws_msg_shift = SHIFT;
    virtuappu_mode1_ws_msg_x0 = 0;
    virtuappu_mode1_ws_msg_x1 = 240;
    virtuappu_mode1_ws_msg_y0 = 40;
    virtuappu_mode1_ws_msg_y1 = 56;
    memset(bannerLine, 0, sizeof(bannerLine));
    memset(bannerPriority, 0xff, sizeof(bannerPriority));
    virtuappu_mode1_render_text_bg_line(0, BANNER_LINE, bannerLine, bannerPriority);

    for (int x = 0; x < 240; x++) {
        if (bannerLine[x + SHIFT] == nativeLine[x] && bannerPriority[x + SHIFT] == nativePriority[x] &&
            nativePriority[x] != 0xff) {
            continue;
        }
        fprintf(stderr,
                "mode1_native_fast_path_test: enter-room banner pixel %d: centered=%08x/%02x native=%08x/%02x\n",
                x, bannerLine[x + SHIFT], bannerPriority[x + SHIFT], nativeLine[x], nativePriority[x]);
        return 0;
    }
    for (int x = 0; x < SHIFT; x++) {
        if (bannerPriority[x] != 0xff || bannerPriority[240 + SHIFT + x] != 0xff) {
            fprintf(stderr, "mode1_native_fast_path_test: banner pillar pixel unexpectedly drawn at %d\n", x);
            return 0;
        }
    }
    return 1;
}

static void WriteIo16(unsigned offset, uint16_t value) {
    sIo[offset] = (uint8_t)value;
    sIo[offset + 1u] = (uint8_t)(value >> 8u);
}

static uint16_t ReadIo16(unsigned offset) {
    return (uint16_t)sIo[offset] | ((uint16_t)sIo[offset + 1u] << 8u);
}

static void ApplyScanlineProfile(int line) {
    switch (sScanlineProfile) {
        case SCANLINE_DARKNESS: {
            const int half_width = 18 + ((line * 3) & 31);
            const int center = 116 + ((line >> 3) & 7);
            WriteIo16(MODE1_IO_WIN0H,
                      (uint16_t)(((center - half_width) << 8u) | (center + half_width)));
            WriteIo16(MODE1_IO_WIN0V, 0x00A0u);
            WriteIo16(MODE1_IO_BLDALPHA, 0x1000u);
            WriteIo16(MODE1_IO_BLDY, (uint16_t)(line & 0x1Fu));
            break;
        }
        case SCANLINE_DARKNESS_WRAP:
            WriteIo16(MODE1_IO_WIN0H,
                      (uint16_t)(((180 + (line & 31)) << 8u) | (40 + ((line * 3) & 31))));
            WriteIo16(MODE1_IO_WIN0V, 0x7828u);
            WriteIo16(MODE1_IO_BLDALPHA, 0x0C04u);
            WriteIo16(MODE1_IO_BLDY, (uint16_t)((31 - line) & 0x1Fu));
            break;
        case SCANLINE_MINISH_RAYS:
            WriteIo16(MODE1_IO_BG3HOFS, (uint16_t)((line * 3 + line / 7) & 0x1FF));
            WriteIo16(MODE1_IO_BLDALPHA, 0x1000u);
            WriteIo16(MODE1_IO_BLDY, (uint16_t)((line >> 2) & 0x1Fu));
            break;
        case SCANLINE_STEAM:
            WriteIo16(MODE1_IO_BG3HOFS, (uint16_t)((0x1FF - line * 5) & 0x1FF));
            WriteIo16(MODE1_IO_BLDALPHA, 0x0808u);
            WriteIo16(MODE1_IO_BLDY, (uint16_t)((line >> 1) & 0x1Fu));
            break;
        case SCANLINE_DUAL_WINDOW:
            WriteIo16(MODE1_IO_WIN0H,
                      (uint16_t)(((32 + (line & 15)) << 8u) | (208 - (line & 15))));
            WriteIo16(MODE1_IO_WIN1H,
                      (uint16_t)(((192 + (line & 15)) << 8u) | (48 - (line & 15))));
            WriteIo16(MODE1_IO_BLDY, (uint16_t)(line & 0x1Fu));
            break;
        default:
            break;
    }
}

static void ConfigureTiledProfile(uint16_t dispcnt, uint16_t bg3cnt,
                                  uint16_t bldcnt, uint16_t bldalpha) {
    WriteIo16(MODE1_IO_DISPCNT, dispcnt);
    WriteIo16(MODE1_IO_BG0CNT, 0x1F0Cu);
    WriteIo16(MODE1_IO_BG1CNT, 0x1D45u);
    WriteIo16(MODE1_IO_BG2CNT, 0x1C42u);
    WriteIo16(MODE1_IO_BG3CNT, bg3cnt);
    WriteIo16(MODE1_IO_BLDCNT, bldcnt);
    WriteIo16(MODE1_IO_BLDALPHA, bldalpha);
    WriteIo16(MODE1_IO_MOSAIC, 0u);
}

static void ConfigureObject(uint16_t object_mode) {
    sOam[0] = (uint16_t)(48u | (object_mode << 10u));
    sOam[1] = (uint16_t)(80u | (2u << 14u));
    sOam[2] = (uint16_t)(32u | (1u << 10u) | (3u << 12u));
}

static void ConfigureAffineObject(bool double_size, bool mosaic, bool bpp8,
                                  int16_t pa, int16_t pb, int16_t pc, int16_t pd) {
    sOam[0] = (uint16_t)(48u | 0x0100u | (double_size ? 0x0200u : 0u) |
                         (mosaic ? 0x1000u : 0u) | (bpp8 ? 0x2000u : 0u));
    sOam[1] = (uint16_t)(88u | (2u << 14u));
    sOam[2] = (uint16_t)(32u | (1u << 10u) | (3u << 12u));
    sOam[3] = (uint16_t)pa;
    sOam[7] = (uint16_t)pb;
    sOam[11] = (uint16_t)pc;
    sOam[15] = (uint16_t)pd;
}

static void BuildState(unsigned scene) {
    sScanlineProfile = SCANLINE_NONE;
    for (size_t i = 0; i < sizeof(sVram); ++i) sVram[i] = (uint8_t)NextRandom();
    for (int i = 0; i < MODE1_PALETTE_COLORS; ++i) {
        sBgPalette[i] = (uint16_t)(NextRandom() & 0x7FFFu);
        sObjPalette[i] = (uint16_t)(NextRandom() & 0x7FFFu);
    }
    memset(sIo, 0, sizeof(sIo));
    for (int i = 0; i < MODE1_GBA_OAM_COUNT; ++i) sOam[i * 4] = 0x0200u;
    for (int bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
        virtuappu_mode1_ws_shadow[bg] = NULL;
        virtuappu_mode1_ws_shadow_base_tile[bg] = 0;
        for (size_t i = 0; i < MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS; ++i) {
            sShadow[bg][i] = (uint16_t)NextRandom();
        }
    }
    virtuappu_mode1_ws_hud_right_anchor = 0;
    virtuappu_mode1_ws_msg_shift = 0;
    virtuappu_mode1_ws_msg_x0 = 0;
    virtuappu_mode1_ws_msg_x1 = 0;
    virtuappu_mode1_ws_msg_y0 = 0;
    virtuappu_mode1_ws_msg_y1 = 0;

    uint16_t dispcnt = MODE1_DISP_OBJ_1D;
    for (int bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
        if ((NextRandom() & 3u) != 0u) dispcnt |= (uint16_t)(MODE1_DISP_BG0_ON << bg);
        const uint16_t priority = (uint16_t)(NextRandom() & 3u);
        const uint16_t charBase = (uint16_t)(NextRandom() & 3u);
        const uint16_t screenBase = (uint16_t)(16u + (NextRandom() & 0x0Fu));
        const uint16_t size = (uint16_t)(NextRandom() & 3u);
        WriteIo16((unsigned)(MODE1_IO_BG0CNT + bg * 2),
                  (uint16_t)(priority | (charBase << 2u) | (screenBase << 8u) | (size << 14u)));
        WriteIo16((unsigned)(MODE1_IO_BG0HOFS + bg * 4), (uint16_t)(NextRandom() & 0x1FFu));
        WriteIo16((unsigned)(MODE1_IO_BG0VOFS + bg * 4), (uint16_t)(NextRandom() & 0x1FFu));
    }

    if ((NextRandom() & 1u) != 0u) {
        dispcnt |= MODE1_DISP_OBJ_ON;
        const int objectCount = 1 + (int)(NextRandom() % 24u);
        for (int i = 0; i < objectCount; ++i) {
            const uint16_t shape = (uint16_t)(NextRandom() % 3u);
            const uint16_t size = (uint16_t)(NextRandom() & 3u);
            const uint16_t mode = (uint16_t)(NextRandom() % 3u);
            const uint16_t bpp8 = (uint16_t)(NextRandom() & 1u);
            const uint16_t mosaic = (uint16_t)(NextRandom() & 1u);
            const uint16_t y = (uint16_t)(NextRandom() & 0xFFu);
            const uint16_t x = (uint16_t)(NextRandom() & 0x1FFu);
            const uint16_t hflip = (uint16_t)(NextRandom() & 1u);
            const uint16_t vflip = (uint16_t)(NextRandom() & 1u);
            sOam[i * 4] =
                (uint16_t)(y | (mode << 10u) | (mosaic << 12u) | (bpp8 << 13u) | (shape << 14u));
            sOam[i * 4 + 1] = (uint16_t)(x | (hflip << 12u) | (vflip << 13u) | (size << 14u));
            sOam[i * 4 + 2] = (uint16_t)((NextRandom() & 0x3FFu) |
                                          ((NextRandom() & 3u) << 10u) |
                                          ((NextRandom() & 0x0Fu) << 12u));
        }
    }
    WriteIo16(MODE1_IO_DISPCNT, dispcnt);
    WriteIo16(MODE1_IO_MOSAIC, (uint16_t)(NextRandom() & 0xFFFFu));

    const uint16_t effect = (uint16_t)(NextRandom() & 3u);
    WriteIo16(MODE1_IO_BLDCNT,
              (uint16_t)((NextRandom() & 0x3Fu) | ((uint32_t)effect << 6u) |
                         ((NextRandom() & 0x3Fu) << 8u)));
    WriteIo16(MODE1_IO_BLDALPHA,
              (uint16_t)((NextRandom() & 0x1Fu) | ((NextRandom() & 0x1Fu) << 8u)));
    WriteIo16(MODE1_IO_BLDY, (uint16_t)(NextRandom() & 0x1Fu));

    /* Deterministically include dump profiles plus the reported Old-3DS
     * trouble spots. Profiles 4..15 are all eligible for compact tokens, so
     * the hit-count assertion in main proves the oracle actually exercises
     * the optimized path instead of silently comparing fallback to itself. */
    switch (scene % 16u) {
        case 0u:
        case 3u:
            ConfigureTiledProfile(0x1F40u, 0x1E05u, 0x3648u, 0x0E04u);
            break;
        case 1u:
        case 2u:
            ConfigureTiledProfile(0x1740u, 0x1E05u, 0u, 0u);
            break;
        case 4u: /* LightManager darkness/spotlight (Giant Octorok rooms). */
            ConfigureTiledProfile(0x3F40u, 0x1E0Cu, 0x3E48u, 0x1000u);
            WriteIo16(MODE1_IO_WININ, 0x3F37u);
            WriteIo16(MODE1_IO_WINOUT, 0x003Fu);
            sScanlineProfile = SCANLINE_DARKNESS;
            break;
        case 5u: /* Wrapped WIN0H and WIN0V spans used by iris-style masks. */
            ConfigureTiledProfile(0x3F40u, 0x1E0Cu, 0x3E48u, 0x0C04u);
            WriteIo16(MODE1_IO_WININ, 0x3F27u);
            WriteIo16(MODE1_IO_WINOUT, 0x001Fu);
            sScanlineProfile = SCANLINE_DARKNESS_WRAP;
            break;
        case 6u: /* Minish Woods light-ray BG3 with per-line horizontal DMA. */
            ConfigureTiledProfile(0x1F40u, 0x1E04u, 0x3648u, 0x1000u);
            sScanlineProfile = SCANLINE_MINISH_RAYS;
            break;
        case 7u: /* Steam/fog overlay BG3 with its distinct target mask. */
            ConfigureTiledProfile(0x1F40u, 0x1E04u, 0x3E48u, 0x0808u);
            sScanlineProfile = SCANLINE_STEAM;
            break;
        case 8u: /* WIN1 normal vertical span plus wrapped horizontal span. */
            ConfigureTiledProfile(0x5F40u, 0x1E04u, 0x3FFFu, 0x0A06u);
            WriteIo16(MODE1_IO_WIN1H, 0xBE2Du);
            WriteIo16(MODE1_IO_WIN1V, 0x1490u);
            WriteIo16(MODE1_IO_WININ, 0x173Fu);
            WriteIo16(MODE1_IO_WINOUT, 0x003Fu);
            break;
        case 9u: /* OBJ-window visibility and SFX mask. */
            ConfigureTiledProfile(0x9F40u, 0x1E04u, 0x3E48u, 0x0808u);
            WriteIo16(MODE1_IO_WINOUT, 0x173Fu);
            ConfigureObject(2u);
            break;
        case 10u: /* Full WIN0 > WIN1 > OBJ-window > outside precedence. */
            ConfigureTiledProfile(0xFF40u, 0x1E04u, 0x3FFFu, 0x0808u);
            WriteIo16(MODE1_IO_WIN0V, 0x2090u);
            WriteIo16(MODE1_IO_WIN1V, 0x7828u);
            WriteIo16(MODE1_IO_WININ, 0x2717u);
            WriteIo16(MODE1_IO_WINOUT, 0x0F3Fu);
            ConfigureObject(2u);
            sScanlineProfile = SCANLINE_DUAL_WINDOW;
            break;
        case 11u: /* Window disables color effects over a semitransparent OBJ. */
            ConfigureTiledProfile(0x3F40u, 0x1E04u, 0x3E48u, 0x0808u);
            WriteIo16(MODE1_IO_WIN0H, 0x4090u);
            WriteIo16(MODE1_IO_WIN0V, 0x2090u);
            WriteIo16(MODE1_IO_WININ, 0x3F1Fu);
            WriteIo16(MODE1_IO_WINOUT, 0x003Fu);
            ConfigureObject(1u);
            break;
        case 12u: /* Affine 4bpp OBJ: incremental PA/PC coordinate walk. */
            ConfigureTiledProfile(0x1F40u, 0x1E04u, 0x3648u, 0x1000u);
            ConfigureAffineObject(false, false, false, 0x00D9, -0x0080, 0x0080, 0x00D9);
            break;
        case 13u: /* Double-size affine 8bpp OBJ with signed coefficients. */
            ConfigureTiledProfile(0x1F40u, 0x1E04u, 0x3E48u, 0x0808u);
            ConfigureAffineObject(true, false, true, -0x0100, 0x0040, -0x0080, 0x00C0);
            break;
        case 14u: /* Affine OBJ mosaic retains the non-incremental oracle. */
            ConfigureTiledProfile(0x1F40u, 0x1E04u, 0x3E48u, 0x0808u);
            WriteIo16(MODE1_IO_MOSAIC, 0x0500u);
            ConfigureAffineObject(false, true, false, 0x0120, -0x0030, 0x0050, 0x00E0);
            break;
        case 15u: /* No-effect compact top-layer path with a window mask. */
            ConfigureTiledProfile(0x3F40u, 0x1E04u, 0u, 0u);
            WriteIo16(MODE1_IO_WIN0H, 0xC828u);
            WriteIo16(MODE1_IO_WIN0V, 0x7828u);
            WriteIo16(MODE1_IO_WININ, 0x3F15u);
            WriteIo16(MODE1_IO_WINOUT, 0x003Fu);
            break;
    }

    /* Profiles 8..15 enumerate the complete hardware coefficient matrix.
     * Their 2,048 sequential slots cover all 17*17 clamped GBA EVA/EVB pairs
     * at least seven times while leaving the exact reported profiles above
     * unchanged. */
    if ((scene % 16u) >= 8u) {
        const unsigned blend_slot = (scene / 16u) * 8u + (scene % 16u) - 8u;
        const unsigned blend_pair = blend_slot % (17u * 17u);
        const uint16_t eva = (uint16_t)(blend_pair % 17u);
        const uint16_t evb = (uint16_t)(blend_pair / 17u);
        WriteIo16(MODE1_IO_BLDALPHA, (uint16_t)(eva | (evb << 8u)));
    }

    virtuappu_mode1_pre_line_callback =
        sScanlineProfile == SCANLINE_NONE ? NULL : ApplyScanlineProfile;

    /* Exercise the actual 266-wide 3DS field path. The GBA's 32-tile
     * screenblock supplies x<240 while map shadows supply the reveal columns;
     * the fast renderer and generic oracle must agree on both sides of that
     * boundary, including scroll-induced partial tiles. */
    for (int bg = 0; bg < MODE1_GBA_BG_COUNT; ++bg) {
        const uint16_t control = (uint16_t)sIo[MODE1_IO_BG0CNT + bg * 2] |
                                 ((uint16_t)sIo[MODE1_IO_BG0CNT + bg * 2 + 1] << 8u);
        const bool enabled = ((uint16_t)sIo[MODE1_IO_DISPCNT] |
                              ((uint16_t)sIo[MODE1_IO_DISPCNT + 1] << 8u)) &
                             (uint16_t)(MODE1_DISP_BG0_ON << bg);
        if (enabled && (control & 0x4000u) == 0u && ((scene + (unsigned)bg) % 3u) != 2u) {
            virtuappu_mode1_ws_shadow[bg] = sShadow[bg];
            virtuappu_mode1_ws_shadow_base_tile[bg] = 30 + (int)(NextRandom() & 1u);
        }
    }
    if ((scene % 31u) == 7u) {
        virtuappu_mode1_ws_hud_right_anchor = 1;
    }
    if ((scene % 37u) == 11u) {
        virtuappu_mode1_ws_msg_shift = 13;
        virtuappu_mode1_ws_msg_x0 = 16;
        virtuappu_mode1_ws_msg_x1 = 224;
        virtuappu_mode1_ws_msg_y0 = 20;
        virtuappu_mode1_ws_msg_y1 = 140;
    }
}

int main(void) {
    const VirtuaPPUMode1GbaMemory memory = { sIo, sVram, sBgPalette, sObjPalette, sOam };
    PPUMemory ppu;
    memset(&ppu, 0, sizeof(ppu));
    ppu.mode = 1;
    ppu.frame_width = MODE1_GBA_WIDTH;
    ppu.frame_pitch = MODE1_GBA_WIDTH;
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_mode1_pre_line_callback = NULL;

    if (!CheckEnterRoomBannerBand()) {
        return 1;
    }

    enum { SCENES = 4096 };
    bool blendPairCoverage[17][17] = { { false } };
    for (unsigned scene = 0; scene < SCENES; ++scene) {
        BuildState(scene);
        const uint16_t bldalpha = ReadIo16(MODE1_IO_BLDALPHA);
        const unsigned eva = bldalpha & 0x1Fu;
        const unsigned evb = (bldalpha >> 8u) & 0x1Fu;
        if (eva <= 16u && evb <= 16u) blendPairCoverage[eva][evb] = true;
        virtuappu_mode1_set_color_correction((scene & 1u) != 0u);

        virtuappu_mode1_set_old3ds_profile(true);
        virtuappu_mode1_set_native_fast_paths_enabled(true);
        virtuappu_mode1_reset_native_compact_test_lines();
        virtuappu_mode1_render_frame(&ppu);
        const uint32_t oldCompactLines = virtuappu_mode1_get_native_compact_test_lines();
        memcpy(sFast, virtuappu_frame_buffer, sizeof(sFast));

        if ((scene % 16u) >= 4u && oldCompactLines != MODE1_GBA_HEIGHT) {
            fprintf(stderr,
                    "mode1_native_fast_path_test: scene %u expected %u compact lines, got %u\n",
                    scene, MODE1_GBA_HEIGHT, oldCompactLines);
            return 1;
        }

        virtuappu_mode1_set_old3ds_profile(false);
        virtuappu_mode1_set_native_fast_paths_enabled(true);
        virtuappu_mode1_render_frame(&ppu);
        memcpy(sNewFast, virtuappu_frame_buffer, sizeof(sNewFast));

        virtuappu_mode1_set_native_fast_paths_enabled(false);
        virtuappu_mode1_render_frame(&ppu);
        memcpy(sReference, virtuappu_frame_buffer, sizeof(sReference));

        for (size_t pixel = 0; pixel < MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT; ++pixel) {
            if (sFast[pixel] == sReference[pixel]) continue;
            fprintf(stderr,
                    "mode1_native_fast_path_test: scene %u pixel (%zu,%zu): fast=%08x reference=%08x\n",
                    scene, pixel % MODE1_GBA_WIDTH, pixel / MODE1_GBA_WIDTH,
                    sFast[pixel], sReference[pixel]);
            return 1;
        }
        for (size_t pixel = 0; pixel < MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT; ++pixel) {
            if (sNewFast[pixel] == sReference[pixel]) continue;
            fprintf(stderr,
                    "mode1_native_fast_path_test: New profile scene %u pixel (%zu,%zu): fast=%08x reference=%08x\n",
                    scene, pixel % MODE1_GBA_WIDTH, pixel / MODE1_GBA_WIDTH,
                    sNewFast[pixel], sReference[pixel]);
            return 1;
        }
    }

    for (unsigned eva = 0; eva <= 16u; ++eva) {
        for (unsigned evb = 0; evb <= 16u; ++evb) {
            if (blendPairCoverage[eva][evb]) continue;
            fprintf(stderr, "mode1_native_fast_path_test: missing EVA=%u EVB=%u oracle coverage\n", eva, evb);
            return 1;
        }
    }

    printf("mode1_native_fast_path_test: PASS (%u total scenes, %u/profile, width=%d, EVA/EVB=289/289)\n",
           SCENES, SCENES / 16u, MODE1_GBA_WIDTH);
    return 0;
}
