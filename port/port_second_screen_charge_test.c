#include "port_second_screen.h"
#include "port_second_screen_theme.h"
#include "port_second_screen_worldmap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int sFailures;
static uint32_t sGlyphPixels[SST_COUNT][8 * 8];
static SecondScreenThemeSprite sGlyphs[SST_COUNT];

#define CHECK_EQ(actual, expected, message)                                                \
    do {                                                                                   \
        unsigned got__ = (unsigned)(actual);                                               \
        unsigned want__ = (unsigned)(expected);                                            \
        if (got__ != want__) {                                                             \
            fprintf(stderr, "FAIL: %s: got %u expected %u\n", message, got__, want__); \
            sFailures++;                                                                   \
        }                                                                                  \
    } while (0)

const SecondScreenThemeSprite* Port_SecondScreenTheme_Get(int id) {
    if (id < 0 || id >= SST_COUNT) {
        return NULL;
    }
    return &sGlyphs[id];
}

static uint32_t GlyphColor(int id) {
    return 0xFF000000u | (uint32_t)(id + 1);
}

static void InitGlyphOracle(void) {
    int id;
    int pixel;
    for (id = 0; id < SST_COUNT; id++) {
        for (pixel = 0; pixel < 8 * 8; pixel++) {
            sGlyphPixels[id][pixel] = GlyphColor(id);
        }
        sGlyphs[id].px = sGlyphPixels[id];
        sGlyphs[id].w = 8;
        sGlyphs[id].h = 8;
    }
}

static void CheckSolidRect(const uint32_t* pixels, int stride, int x0, int y0, int w, int h,
                           uint32_t expected, const char* message) {
    int x;
    int y;
    for (y = y0; y < y0 + h; y++) {
        for (x = x0; x < x0 + w; x++) {
            if (pixels[y * stride + x] != expected) {
                fprintf(stderr, "FAIL: %s: pixel (%d,%d) got 0x%08X expected 0x%08X\n", message,
                        x, y, pixels[y * stride + x], expected);
                sFailures++;
                return;
            }
        }
    }
}

int main(void) {
    SecondScreenSnapshot snapshot;
    PortSecondScreenTestLoadStateLayout loadLayout;
    PortSecondScreenTestLoadStateLayout randomizerLayout;
    uint32_t pixels[40 * 8];
    memset(&snapshot, 0, sizeof(snapshot));
    InitGlyphOracle();

    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 0, "inactive charge is hidden");
    snapshot.chargeAction = 1;
    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 0, "visible top HUD owns the charge cue");
    snapshot.topHudHidden = 1;
    CHECK_EQ(Port_SecondScreenChargeVisible(&snapshot), 1, "hidden top HUD publishes active charge");

    snapshot.chargeTimer = -1;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "negative pre-charge timer is empty");
    snapshot.chargeTimer = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "zero timer is empty");
    snapshot.chargeTimer = 1;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 1, "first native tick fills one quarter");
    snapshot.chargeTimer = 20;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 1, "twenty ticks remain one quarter");
    snapshot.chargeTimer = 21;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 2, "twenty-one ticks round up");
    snapshot.chargeTimer = 799;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "last pre-full tick rounds to full");
    snapshot.chargeTimer = 800;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "native full timer is forty quarters");
    snapshot.chargeTimer = 32767;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 40, "oversized timer is clamped");

    snapshot.topHudHidden = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "visible top HUD suppresses bottom progress");
    snapshot.topHudHidden = 1;
    snapshot.chargeAction = 0;
    CHECK_EQ(Port_SecondScreenChargeSteps(&snapshot), 0, "released sword suppresses bottom progress");
    CHECK_EQ(Port_SecondScreenChargeVisible(NULL), 0, "null snapshot is hidden safely");
    CHECK_EQ(Port_SecondScreenChargeSteps(NULL), 0, "null snapshot has no progress");

    snapshot.bombCount = 0;
    snapshot.arrowCount = 73;
    CHECK_EQ(Port_SecondScreenItemAmmo(&snapshot, 0x07), 0, "empty bombs still publish a 00 counter");
    CHECK_EQ(Port_SecondScreenItemAmmo(&snapshot, 0x08), 0, "remote bombs share bomb ammo");
    CHECK_EQ(Port_SecondScreenItemAmmo(&snapshot, 0x09), 73, "bow publishes arrow ammo");
    CHECK_EQ(Port_SecondScreenItemAmmo(&snapshot, 0x0A), 73, "light arrows share arrow ammo");
    CHECK_EQ(Port_SecondScreenItemAmmo(&snapshot, 0x01), (unsigned)-1,
             "ordinary equipped items have no counter");
    CHECK_EQ(Port_SecondScreenItemAmmo(NULL, 0x07), (unsigned)-1,
             "null snapshots have no counter");

    /* Production pixel path: equipped A is empty bombs (must still paint
     * 00), equipped B is the bow with 73 arrows.  The synthetic theme makes
     * every selected native glyph a byte-exact solid-color oracle. */
    memset(pixels, 0, sizeof(pixels));
    snapshot.equippedA = 0x07;
    snapshot.equippedB = 0x09;
    CHECK_EQ(Port_SecondScreen_TestPaintEquippedAmmo(pixels, 40, 8, 40, &snapshot, 1), 2,
             "A/B production compositor paints both supported counters");
    CheckSolidRect(pixels, 40, 0, 0, 8, 8, GlyphColor(SST_SMALL_TENS_0), "bomb 00 tens glyph");
    CheckSolidRect(pixels, 40, 8, 0, 8, 8, GlyphColor(SST_SMALL_ONES_0), "bomb 00 ones glyph");
    CheckSolidRect(pixels, 40, 20, 0, 8, 8, GlyphColor(SST_SMALL_TENS_0 + 7),
                   "bow 73 tens glyph");
    CheckSolidRect(pixels, 40, 28, 0, 8, 8, GlyphColor(SST_SMALL_ONES_0 + 3),
                   "bow 73 ones glyph");
    CHECK_EQ(pixels[16], 0, "A/B glyph groups retain the four-pixel gap");
    CHECK_EQ(pixels[39], 0, "paint stays inside the two 16-pixel counters");

    /* Variants share the same snapshot counts and exact paint path. */
    memset(pixels, 0, sizeof(pixels));
    snapshot.equippedA = 0x08;
    snapshot.equippedB = 0x0A;
    CHECK_EQ(Port_SecondScreen_TestPaintEquippedAmmo(pixels, 40, 8, 40, &snapshot, 1), 2,
             "remote bombs/light arrows paint through the production path");
    CheckSolidRect(pixels, 40, 0, 0, 8, 8, GlyphColor(SST_SMALL_TENS_0),
                   "remote-bomb 00 tens glyph");
    CheckSolidRect(pixels, 40, 28, 0, 8, 8, GlyphColor(SST_SMALL_ONES_0 + 3),
                   "light-arrow 73 ones glyph");

    memset(pixels, 0, sizeof(pixels));
    snapshot.equippedA = 0x01;
    snapshot.equippedB = 0;
    CHECK_EQ(Port_SecondScreen_TestPaintEquippedAmmo(pixels, 40, 8, 40, &snapshot, 1), 0,
             "ordinary A/B items do not paint an ammo counter");
    CHECK_EQ(pixels[0], 0, "ordinary items leave the target pixels untouched");

    /* Loading confirmation: title is lowered by twenty pixels from the
     * original center and the controls use compact, inset 3DS geometry. */
    memset(&loadLayout, 0, sizeof(loadLayout));
    Port_SecondScreen_TestLoadStateConfirmationLayout(320, 240, &loadLayout);
    CHECK_EQ(loadLayout.titleCenterY, 36, "load-state title clears the top rim");
    CHECK_EQ(loadLayout.firstLineCenterY, 68, "load-state copy starts below the title");
    CHECK_EQ(loadLayout.lastLineCenterY, 140, "five copy lines stay above the buttons");
    CHECK_EQ(loadLayout.buttonLeft, 26, "load-state buttons have a left inset");
    CHECK_EQ(loadLayout.buttonRight, 294, "load-state buttons have a right inset");
    CHECK_EQ(loadLayout.buttonBottom - loadLayout.buttonTop, 26, "load-state buttons stay compact");

    memset(&randomizerLayout, 0, sizeof(randomizerLayout));
    Port_SecondScreen_TestRandomizerConfirmationLayout(320, 240, &randomizerLayout);
    CHECK_EQ(randomizerLayout.titleCenterY, loadLayout.titleCenterY,
             "randomizer title matches load-state rhythm");
    CHECK_EQ(randomizerLayout.firstLineCenterY, loadLayout.firstLineCenterY,
             "randomizer copy starts at the compact load-state position");
    CHECK_EQ(randomizerLayout.lastLineCenterY, 158,
             "six randomizer copy lines stay above the buttons");
    CHECK_EQ(randomizerLayout.buttonLeft, loadLayout.buttonLeft,
             "randomizer controls match load-state left inset");
    CHECK_EQ(randomizerLayout.buttonRight, loadLayout.buttonRight,
             "randomizer controls match load-state right inset");
    CHECK_EQ(randomizerLayout.buttonBottom - randomizerLayout.buttonTop, 26,
             "randomizer buttons stay compact");

    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, 7), 1,
             "native map always reveals navigation region 7");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, 10), 1,
             "native map always reveals navigation region 10");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, 16), 1,
             "native map always reveals navigation region 16");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, 0), 0,
             "undiscovered map region stays covered");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(1u << 3, 3), 1,
             "save discovery bit reveals its matching region");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(1u << 24, 0), 0,
             "windcrest travel bits do not reveal map regions");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, -1), 0,
             "negative map region is rejected");
    CHECK_EQ(Port_SecondScreenWorldMap_IsRegionRevealed(0, 17), 0,
             "out-of-range map region is rejected");

    /* The charge meter shares the always-reserved R band. Its appearance
     * therefore cannot alter the ring radius derived by production. */
    CHECK_EQ((int)(Port_SecondScreen_TestSidebarRingRadius(50.0f, 190.0f, 70.0f, 1.0f / 3.0f, 0) *
                   1000.0f),
             (int)(Port_SecondScreen_TestSidebarRingRadius(50.0f, 190.0f, 70.0f, 1.0f / 3.0f, 1) *
                   1000.0f),
             "charge meter preserves equipped-ring size");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_second_screen_charge_test: ALL PASS\n");
    return 0;
}
