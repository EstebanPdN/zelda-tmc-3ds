#include "port_second_screen_quest.h"
#include "port_second_screen_theme.h"

#include "kinstone.h"
#include "region.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int sFailures;
static int sTileRequest;
static int sTextDraws;
static char sLastText[8];

int gActiveRegion = TMC_REGION_USA;

const KinstoneWorldEvent gKinstoneWorldEvents[119] = {
    [118] = { .objPalette = 1, .gfxOffsetPiece = 99 },
};
const KinstoneWorldEvent gKinstoneWorldEvents_eu[119] = {
    [118] = { .objPalette = 2, .gfxOffsetPiece = 43 },
};
const KinstoneWorldEvent gKinstoneWorldEvents_jp[119] = {
    [118] = { .objPalette = 3, .gfxOffsetPiece = 77 },
};

static const uint8_t sFrame0Piece3[] = {
    1,       /* one OBJ piece */
    0xF0,    /* x = -16 */
    0xF0,    /* y = -16 */
    0x20,    /* square, size 2 = 32x32 */
    0x00,    /* tile low */
    0x00,    /* tile/palette high */
};
static uint16_t sObjPalette[16] = { 0, 0x001F };

#define CHECK_EQ(actual, expected, message)                                                       \
    do {                                                                                          \
        int got__ = (int)(actual);                                                                \
        int want__ = (int)(expected);                                                             \
        if (got__ != want__) {                                                                    \
            fprintf(stderr, "FAIL: %s: got %d expected %d\n", message, got__, want__);          \
            sFailures++;                                                                          \
        }                                                                                         \
    } while (0)

void* sub_080AD8F0(u32 sprite, u32 frame) {
    return sprite == 0 && frame == 3 ? (void*)sFrame0Piece3 : NULL;
}

u32 Port_GetKinstonePieceTiles(u32 gfxOffset, u8* out, u32 outBytes) {
    int x;
    int y;
    if (out == NULL || outBytes < 512 || gfxOffset != 43) {
        return 0;
    }
    sTileRequest = (int)gfxOffset;
    memset(out, 0, outBytes);
    /* Exact NEW-4d EU118 ink bounds within the retail 32x32 OBJ. */
    for (y = 4; y <= 27; y++) {
        for (x = 4; x <= 18; x++) {
            size_t tile = (size_t)(y / 8) * 4u + (size_t)(x / 8);
            size_t byte = tile * 32u + (size_t)(y & 7) * 4u + (size_t)(x & 7) / 2u;
            if (x & 1) {
                out[byte] |= 0x10;
            } else {
                out[byte] |= 0x01;
            }
        }
    }
    return 512;
}

const u8* Port_GetRawPaletteGroupBankData(u32 group, u32 destPaletteNum, u32* outNumColors) {
    if (outNumColors != NULL) {
        *outNumColors = (group == 204 && destPaletteNum == 18) ? 16 : 0;
    }
    return (group == 204 && destPaletteNum == 18) ? (const u8*)sObjPalette : NULL;
}

const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail) {
    (void)group;
    (void)vramAddr;
    if (outAvail != NULL) {
        *outAvail = 0;
    }
    return NULL;
}

const uint16_t* Port_SecondScreenTheme_ObjPalette(uint32_t bank) {
    (void)bank;
    return NULL;
}

int32_t Port_SecondScreenTheme_TextWidth(const char* str, int32_t scale) {
    return (int32_t)strlen(str) * 8 * scale;
}

int32_t Port_SecondScreenTheme_DrawText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                        int32_t x, int32_t y, int32_t scale, int style,
                                        const char* str) {
    (void)pixels;
    (void)bufW;
    (void)bufH;
    (void)stride;
    (void)x;
    (void)y;
    (void)scale;
    (void)style;
    sTextDraws++;
    snprintf(sLastText, sizeof(sLastText), "%s", str);
    return (int32_t)strlen(str) * 8 * scale;
}

static int IntegerScaleToFit(SecondScreenQuestRect source, int dstW, int dstH) {
    int scale = dstW / source.w;
    int yScale = dstH / source.h;
    if (yScale < scale) {
        scale = yScale;
    }
    return scale < 1 ? 1 : scale;
}

int main(void) {
    /* sprite 0/frame 3 in both retail USA and EU is:
     *   count=1, x=-16, y=-16, square size=2 (32x32), tile=0.
     * CELL_ORIGIN is 24 in the quest renderer. */
    SecondScreenQuestRect frame = Port_SecondScreenQuest_KinstoneFrameRect(24, 24);
    CHECK_EQ(frame.x, 8, "retail Kinstone OBJ left edge");
    CHECK_EQ(frame.y, 8, "retail Kinstone OBJ top edge");
    CHECK_EQ(frame.w, 32, "retail Kinstone OBJ width");
    CHECK_EQ(frame.h, 32, "retail Kinstone OBJ height");

    /* Exact NEW-4d fixture. EU gKinstoneWorldEvents[118] selects streamed
     * gfx block 43 and OBJ palette 2. Its non-transparent pixels occupy
     * x=4..18/y=4..27 inside the OBJ: 15x24. In the dumped one-row layout,
     * KinstoneListCell gives the art an 83x141 box. Cropping to those pixels
     * therefore produced the reported 5-pixel-wide black contour. */
    {
        SecondScreenQuestRect eu118Ink = { 12, 12, 15, 24 };
        int oldScale = IntegerScaleToFit(eu118Ink, 83, 141);
        int fixedScale = IntegerScaleToFit(frame, 83, 141);
        CHECK_EQ(oldScale, 5, "NEW-4d fixture reproduces cropped 5x enlargement");
        CHECK_EQ(fixedScale, 2, "retail OBJ footprint limits the dump to 2x");
        CHECK_EQ(fixedScale < oldScale, 1, "authentic outline is no longer magnified into a block");
        CHECK_EQ(eu118Ink.x >= frame.x && eu118Ink.y >= frame.y &&
                     eu118Ink.x + eu118Ink.w <= frame.x + frame.w &&
                     eu118Ink.y + eu118Ink.h <= frame.y + frame.h,
                 1, "EU piece pixels remain inside the preserved OBJ footprint");
    }

    /* Execute the actual KinstoneListCell -> frame decoder -> StampCell
     * production chain under the EU runtime table.  With side=141 the fixed
     * 32x32 frame scales 2x; EU118's 15x24 ink must therefore be exactly
     * 30x48, not the old cropped 75x120 block. */
    {
        enum { W = 180, H = 160 };
        uint32_t pixels[W * H];
        SecondScreenSnapshot snapshot;
        int x;
        int y;
        int colored = 0;
        const uint32_t red = 0xFF0000F8u;

        memset(pixels, 0, sizeof(pixels));
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.kinstoneTypes[0] = 118;
        snapshot.kinstoneAmounts[0] = 9;
        gActiveRegion = TMC_REGION_EU;
        Port_SecondScreenQuest_TestDrawKinstoneListCell(pixels, W, H, W, 0, 10, 10, 141, 1,
                                                        &snapshot);

        CHECK_EQ(sTileRequest, 43, "runtime renderer selected EU118's retail tile block");
        CHECK_EQ(sTextDraws, 1, "KinstoneListCell rendered its count exactly once");
        CHECK_EQ(strcmp(sLastText, "9"), 0, "KinstoneListCell used the snapshot amount");
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                uint32_t expected = (x >= 28 && x <= 57 && y >= 56 && y <= 103) ? red : 0;
                if (pixels[y * W + x] == red) {
                    colored++;
                }
                if (pixels[y * W + x] != expected) {
                    fprintf(stderr,
                            "FAIL: EU118 production pixel oracle at (%d,%d): got 0x%08X expected "
                            "0x%08X\n",
                            x, y, pixels[y * W + x], expected);
                    sFailures++;
                    y = H;
                    break;
                }
            }
        }
        CHECK_EQ(colored, 30 * 48, "EU118 ink stayed at the authentic 2x footprint scale");
    }

    /* The renderer is intentionally read-only over save data.  Invalid bag
     * bytes — including the four fuser sentinels — must be ignored before
     * indexing the 119-entry regional event table. */
    {
        static const uint8_t invalidIds[] = {
            KINSTONE_NONE,
            119,
            KINSTONE_NEEDS_REPLACEMENT,
            KINSTONE_JUST_FUSED,
            KINSTONE_FUSER_DONE,
            KINSTONE_RANDOM,
        };
        enum { W = 64, H = 64 };
        uint32_t pixels[W * H];
        SecondScreenSnapshot snapshot;
        size_t i;
        size_t pixel;

        for (i = 0; i < sizeof(invalidIds); i++) {
            memset(pixels, 0, sizeof(pixels));
            memset(&snapshot, 0, sizeof(snapshot));
            snapshot.kinstoneTypes[0] = invalidIds[i];
            snapshot.kinstoneAmounts[0] = 9;
            sTileRequest = -1;
            sTextDraws = 0;
            sLastText[0] = '\0';
            Port_SecondScreenQuest_TestDrawKinstoneListCell(pixels, W, H, W, 0, 0, 0, 48, 1,
                                                            &snapshot);
            CHECK_EQ(sTileRequest, -1, "invalid Kinstone ID did not request streamed tiles");
            CHECK_EQ(sTextDraws, 0, "invalid Kinstone ID did not render a count");
            for (pixel = 0; pixel < W * H; pixel++) {
                if (pixels[pixel] != 0) {
                    fprintf(stderr, "FAIL: invalid Kinstone 0x%02X painted pixel %zu\n",
                            invalidIds[i], pixel);
                    sFailures++;
                    break;
                }
            }
        }
    }

    if (sFailures != 0) {
        return 1;
    }
    puts("port_second_screen_kinstone_layout_test: ALL PASS");
    return 0;
}
