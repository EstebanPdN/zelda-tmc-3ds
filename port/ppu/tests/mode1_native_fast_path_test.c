/* Deterministic parity fuzzer for the native-width 4bpp renderer.
 *
 * Every generated PPU state is rendered twice by the same production source:
 * first with the Old-3DS-oriented tile/direct paths enabled, then with those
 * paths disabled so the long-standing generic renderer acts as the oracle.
 * Any differing pixel is a hard failure. */

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
static uint32_t sReference[MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT];
static uint32_t sRandom = 0x6D2B79F5u;

static uint32_t NextRandom(void) {
    uint32_t x = sRandom;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    sRandom = x;
    return x;
}

static void WriteIo16(unsigned offset, uint16_t value) {
    sIo[offset] = (uint8_t)value;
    sIo[offset + 1u] = (uint8_t)(value >> 8u);
}

static void BuildState(unsigned scene) {
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

    /* Deterministically include the exact structural profiles of the E2 dump
     * corpus in addition to the randomized states. */
    if (scene == 0u) {
        WriteIo16(MODE1_IO_DISPCNT, 0x1F40u);
        WriteIo16(MODE1_IO_BG0CNT, 0x1F0Cu);
        WriteIo16(MODE1_IO_BG1CNT, 0x1D45u);
        WriteIo16(MODE1_IO_BG2CNT, 0x1C42u);
        WriteIo16(MODE1_IO_BG3CNT, 0x1E05u);
        WriteIo16(MODE1_IO_BLDCNT, 0x3648u);
        WriteIo16(MODE1_IO_BLDALPHA, 0x0E04u);
    } else if (scene == 1u) {
        WriteIo16(MODE1_IO_DISPCNT, 0x1740u);
        WriteIo16(MODE1_IO_BG0CNT, 0x1F0Cu);
        WriteIo16(MODE1_IO_BG1CNT, 0x1D45u);
        WriteIo16(MODE1_IO_BG2CNT, 0x1C42u);
        WriteIo16(MODE1_IO_BLDCNT, 0u);
    }

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

    enum { SCENES = 1024 };
    for (unsigned scene = 0; scene < SCENES; ++scene) {
        BuildState(scene);
        virtuappu_mode1_set_color_correction((scene & 1u) != 0u);

        virtuappu_mode1_set_native_fast_paths_enabled(true);
        virtuappu_mode1_render_frame(&ppu);
        memcpy(sFast, virtuappu_frame_buffer, sizeof(sFast));

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
    }

    printf("mode1_native_fast_path_test: PASS (%u deterministic scenes)\n", SCENES);
    return 0;
}
