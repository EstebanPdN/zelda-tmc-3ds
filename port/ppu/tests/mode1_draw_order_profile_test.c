/* Targeted retail-priority fixture for NEW-1b.
 *
 * This deliberately stays separate from the broad native fast-path fuzzer so
 * it can be compiled against historical E1 and E2 mode1.c revisions without
 * involving the in-progress Full View renderer. It checks the two independent
 * hardware rules involved in the report:
 *
 *   1. BG/OBJ: lower numeric priority wins; OBJ wins an equal-priority tie.
 *   2. OBJ/OBJ: the lowest OAM index wins before that OBJ competes with BGs,
 *      regardless of the two sprites' attr2 priority values.
 */

#include "cpu/mode1.h"
#include "virtuappu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t virtuappu_frame_buffer[VIRTUAPPU_FRAME_BUFFER_SIZE];

static uint8_t sIo[MODE1_IO_MEM_SIZE];
static uint8_t sVram[MODE1_VRAM_SIZE];
static uint16_t sBgPalette[MODE1_PALETTE_COLORS];
static uint16_t sObjPalette[MODE1_PALETTE_COLORS];
static uint16_t sOam[MODE1_OAM_HALFWORDS];

static void WriteIo16(unsigned offset, uint16_t value) {
    sIo[offset] = (uint8_t)value;
    sIo[offset + 1u] = (uint8_t)(value >> 8u);
}

static int ExpectPixel(const char* label, unsigned x, uint32_t expected) {
    const uint32_t actual = virtuappu_frame_buffer[x];
    if (actual == expected) {
        return 1;
    }
    fprintf(stderr, "mode1_draw_order_profile_test: %s x=%u got=%08x expected=%08x\n",
            label, x, actual, expected);
    return 0;
}

static int CheckCanopyProfile(void) {
    enum {
        X_LEEVER_BEHIND_BG0 = 24,
        X_LEEVER_BEHIND_BG1,
        X_LEEVER_TIES_BG2,
        X_TOP_ENTITY_TIES_BG1,
        X_TOP_ENTITY_BEHIND_BG0,
        X_PRIORITY0_TIES_BG0,
    };
    static const uint32_t ground = 0xFF203040u;
    static const uint32_t canopy0 = 0xFF305070u;
    static const uint32_t canopy1 = 0xFF406080u;
    static const uint32_t leever = 0xFF10A0E0u;
    static const uint32_t topEntity = 0xFF20B0F0u;
    uint32_t bg[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH];
    uint8_t bgPriority[MODE1_GBA_BG_COUNT][MODE1_GBA_WIDTH];
    uint32_t obj[MODE1_GBA_WIDTH];
    uint8_t objPriority[MODE1_GBA_WIDTH];
    const uint16_t dispcnt = MODE1_DISP_BG0_ON | MODE1_DISP_BG1_ON |
                             MODE1_DISP_BG2_ON | MODE1_DISP_BG3_ON |
                             MODE1_DISP_OBJ_ON | MODE1_DISP_OBJ_1D;

    memset(bg, 0, sizeof(bg));
    memset(bgPriority, 0xff, sizeof(bgPriority));
    memset(obj, 0, sizeof(obj));
    memset(objPriority, 0xff, sizeof(objPriority));
    memset(virtuappu_frame_buffer, 0, sizeof(virtuappu_frame_buffer));

    /* Priorities captured from the clean Ruins room: 0, 1, 2, 3. */
    WriteIo16(MODE1_IO_BG0CNT, 0u);
    WriteIo16(MODE1_IO_BG1CNT, 1u);
    WriteIo16(MODE1_IO_BG2CNT, 2u);
    WriteIo16(MODE1_IO_BG3CNT, 3u);
    WriteIo16(MODE1_IO_BLDCNT, 0u);
    WriteIo16(MODE1_IO_BLDALPHA, 0u);
    WriteIo16(MODE1_IO_BLDY, 0u);

    for (unsigned x = X_LEEVER_BEHIND_BG0; x <= X_PRIORITY0_TIES_BG0; ++x) {
        bg[2][x] = ground;
    }

    /* A bottom-collision-layer entity has attr2 priority 2 in the retail
     * UpdateSpriteForCollisionLayer table. It is hidden by priority 0/1 tree
     * pixels, but wins the priority-2 tie over ordinary ground. */
    bg[0][X_LEEVER_BEHIND_BG0] = canopy0;
    obj[X_LEEVER_BEHIND_BG0] = leever;
    objPriority[X_LEEVER_BEHIND_BG0] = 2u;

    bg[1][X_LEEVER_BEHIND_BG1] = canopy1;
    obj[X_LEEVER_BEHIND_BG1] = leever;
    objPriority[X_LEEVER_BEHIND_BG1] = 2u;

    obj[X_LEEVER_TIES_BG2] = leever;
    objPriority[X_LEEVER_TIES_BG2] = 2u;

    /* Top-collision-layer entities use attr2 priority 1: they win a BG1 tie,
     * while the priority-0 canopy remains above them. */
    bg[1][X_TOP_ENTITY_TIES_BG1] = canopy1;
    obj[X_TOP_ENTITY_TIES_BG1] = topEntity;
    objPriority[X_TOP_ENTITY_TIES_BG1] = 1u;

    bg[0][X_TOP_ENTITY_BEHIND_BG0] = canopy0;
    obj[X_TOP_ENTITY_BEHIND_BG0] = topEntity;
    objPriority[X_TOP_ENTITY_BEHIND_BG0] = 1u;

    bg[0][X_PRIORITY0_TIES_BG0] = canopy0;
    obj[X_PRIORITY0_TIES_BG0] = topEntity;
    objPriority[X_PRIORITY0_TIES_BG0] = 0u;

    virtuappu_mode1_composite_line(0, bg, bgPriority, obj, objPriority, dispcnt);

    return ExpectPixel("Leever p2 behind BG0 canopy", X_LEEVER_BEHIND_BG0, canopy0) &&
           ExpectPixel("Leever p2 behind BG1 canopy", X_LEEVER_BEHIND_BG1, canopy1) &&
           ExpectPixel("Leever p2 wins BG2 tie", X_LEEVER_TIES_BG2, leever) &&
           ExpectPixel("top entity p1 wins BG1 tie", X_TOP_ENTITY_TIES_BG1, topEntity) &&
           ExpectPixel("top entity p1 behind BG0 canopy", X_TOP_ENTITY_BEHIND_BG0, canopy0) &&
           ExpectPixel("OBJ p0 wins BG0 tie", X_PRIORITY0_TIES_BG0, topEntity);
}

static int CheckOamIndexPrecedence(PPUMemory* ppu) {
    enum { X = 48, Y = 40 };
    uint32_t lowIndexColor;
    uint32_t highIndexColor;

    memset(sIo, 0, sizeof(sIo));
    memset(sVram, 0, sizeof(sVram));
    memset(sBgPalette, 0, sizeof(sBgPalette));
    memset(sObjPalette, 0, sizeof(sObjPalette));
    for (unsigned i = 0; i < MODE1_GBA_OAM_COUNT; ++i) {
        sOam[i * 4u] = 0x0200u; /* disabled non-affine OBJ */
        sOam[i * 4u + 1u] = 0u;
        sOam[i * 4u + 2u] = 0u;
        sOam[i * 4u + 3u] = 0u;
    }

    WriteIo16(MODE1_IO_DISPCNT, MODE1_DISP_OBJ_ON | MODE1_DISP_OBJ_1D);
    sObjPalette[1] = 0x001Fu;
    sObjPalette[2] = 0x03E0u;
    memset(sVram + 0x10000u, 0x11, 32u);       /* OBJ tile 0, palette index 1 */
    memset(sVram + 0x10000u + 32u, 0x22, 32u); /* OBJ tile 1, palette index 2 */

    /* OAM[0] deliberately has the worse attr2 priority (2). */
    sOam[0] = Y;
    sOam[1] = X;
    sOam[2] = (uint16_t)(2u << 10u);

    virtuappu_mode1_set_native_fast_paths_enabled(false);
    virtuappu_mode1_render_frame(ppu);
    lowIndexColor = virtuappu_frame_buffer[Y * MODE1_GBA_WIDTH + X];

    /* OAM[1] overlaps it with attr2 priority 0. Retail first resolves OBJ/OBJ
     * by OAM index, so OAM[0] must remain the winning pixel. */
    sOam[4] = Y;
    sOam[5] = X;
    sOam[6] = (uint16_t)(1u | (0u << 10u));
    virtuappu_mode1_render_frame(ppu);
    if (virtuappu_frame_buffer[Y * MODE1_GBA_WIDTH + X] != lowIndexColor) {
        fprintf(stderr,
                "mode1_draw_order_profile_test: higher OAM index incorrectly replaced lower index\n");
        return 0;
    }

    /* Prove the second sprite is opaque and distinct, rather than accepting a
     * vacuous equality caused by a bad fixture. */
    sOam[0] = 0x0200u;
    virtuappu_mode1_render_frame(ppu);
    highIndexColor = virtuappu_frame_buffer[Y * MODE1_GBA_WIDTH + X];
    if (highIndexColor == lowIndexColor || (highIndexColor & 0xFF000000u) == 0u) {
        fprintf(stderr, "mode1_draw_order_profile_test: OAM fixture colors are not distinct/opaque\n");
        return 0;
    }
    return 1;
}

int main(void) {
    const VirtuaPPUMode1GbaMemory memory = { sIo, sVram, sBgPalette, sObjPalette, sOam };
    PPUMemory ppu;

    memset(&ppu, 0, sizeof(ppu));
    ppu.mode = 1;
    ppu.frame_width = MODE1_GBA_WIDTH;
    ppu.frame_pitch = MODE1_GBA_WIDTH;
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_mode1_set_color_correction(false);

    if (!CheckCanopyProfile() || !CheckOamIndexPrecedence(&ppu)) {
        return 1;
    }

    puts("mode1_draw_order_profile_test: PASS (canopy BG/OBJ + lower-index OAM retail rules)");
    return 0;
}
