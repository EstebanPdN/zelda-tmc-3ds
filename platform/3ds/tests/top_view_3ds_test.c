#include "top_view_3ds.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            fprintf(stderr, "top_view_3ds_test: check failed at line %d: %s\n",         \
                    __LINE__, #condition);                                                 \
            return EXIT_FAILURE;                                                          \
        }                                                                                 \
    } while (0)

int main(void) {
    TopView3DSPlan plan;

    TopView3DS_BuildPlan(0, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_OUTDOOR_1X, 400, 240, 400, 240, 0, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_OUTDOOR_1X);
    CHECK(plan.source.sourceX == 0 && plan.source.sourceY == 0);
    CHECK(plan.source.sourceWidth == 400 && plan.source.sourceHeight == 240);
    CHECK(plan.drawX == 0 && plan.drawY == 0 && plan.drawWidth == 400 && plan.drawHeight == 240);
    CHECK(!plan.linearFilter);
    CHECK(!plan.useSharpBilinear);

    TopView3DS_BuildPlan(0, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_INTERIOR_2X, 200, 120, 200, 120, 0, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_INTERIOR_2X);
    CHECK(plan.source.sourceX == 0 && plan.source.sourceY == 0);
    CHECK(plan.source.sourceWidth == 200 && plan.source.sourceHeight == 120);
    CHECK(plan.drawX == 0 && plan.drawY == 0 && plan.drawWidth == 400 && plan.drawHeight == 240);
    CHECK(!plan.linearFilter);

    /* A composed 240/266x160 crop is rejected, as are non-zero source offsets
     * and undersized logical frames. */
    TopView3DS_BuildPlan(0, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_INTERIOR_2X, 266, 160, 266, 160, 33, 20, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_FALLBACK);
    TopView3DS_BuildPlan(0, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_INTERIOR_2X, 200, 120, 200, 120, 1, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_FALLBACK);

    /* Invalid crop, Old hardware and a torn config selection all fail closed
     * before UVs are built. The established E2 width never exceeds 266. */
    TopView3DS_BuildPlan(0, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_INTERIOR_2X, 240, 160, 240, 160, 41, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(plan.source.sourceWidth == 240 && plan.source.sourceHeight == 160);
    CHECK(plan.drawX == 80 && plan.drawY == 40 && plan.drawWidth == 240 && plan.drawHeight == 160);
    TopView3DS_BuildPlan(1, 1, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT,
                         PORT_3DS_FULL_VIEW_OUTDOOR_1X, 400, 240, 400, 240, 0, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_FALLBACK && plan.source.sourceWidth == 266);
    CHECK(plan.drawWidth == 266 && plan.drawHeight == 160);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_SCALED,
                         PORT_3DS_FULL_VIEW_OUTDOOR_1X, 400, 240, 400, 240, 0, 0, &plan);
    CHECK(plan.mode == PORT_3DS_FULL_VIEW_FALLBACK && plan.source.sourceWidth == 266);
    CHECK(plan.drawWidth == 400 && plan.drawHeight == 240);

    /* PPU/shadow geometry is published one VBlank behind presentation. A
     * mismatch always renders one native frame rather than mixing stride 54
     * Full View shadows with stride 7 E2 shadows. */
    TopView3DSPpuCoherence coherence = TopView3DS_ResolvePpuCoherence(
        PORT_3DS_FULL_VIEW_OUTDOOR_1X, 1);
    CHECK(!coherence.forceNativeFrame && !coherence.clearFullViewProducer);
    coherence = TopView3DS_ResolvePpuCoherence(PORT_3DS_FULL_VIEW_FALLBACK, 1);
    CHECK(coherence.forceNativeFrame && coherence.clearFullViewProducer);
    coherence = TopView3DS_ResolvePpuCoherence(PORT_3DS_FULL_VIEW_OUTDOOR_1X, 0);
    CHECK(coherence.forceNativeFrame && !coherence.clearFullViewProducer);
    coherence = TopView3DS_ResolvePpuCoherence(PORT_3DS_FULL_VIEW_INTERIOR_2X, 0);
    CHECK(!coherence.forceNativeFrame && !coherence.clearFullViewProducer);
    coherence = TopView3DS_ResolvePpuCoherence(PORT_3DS_FULL_VIEW_INTERIOR_2X, 1);
    CHECK(coherence.forceNativeFrame && coherence.clearFullViewProducer);

    /* Existing presentation choices remain byte-for-byte decisions: native
     * 240, Wide 266, Original 360, Stretch 400, and Blur alone is directly
     * linear-filtered. Bilinear requests a distinct 2x-nearest/linear pass. */
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_SCALED,
                         PORT_3DS_FULL_VIEW_FALLBACK, 240, 160, 240, 160, 0, 0, &plan);
    CHECK(plan.source.sourceWidth == 240 && plan.drawWidth == 360 && plan.drawHeight == 240);
    CHECK(!plan.linearFilter && !plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_SCALED,
                         PORT_3DS_FULL_VIEW_FALLBACK, 266, 160, 266, 160, 0, 0, &plan);
    CHECK(plan.source.sourceWidth == 266 && plan.drawWidth == 400 && plan.drawHeight == 240);
    CHECK(!plan.linearFilter && !plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_ORIGINAL,
                         TOP_VIEW_3DS_DISPLAY_BILINEAR,
                         PORT_3DS_FULL_VIEW_FALLBACK, 240, 160, 240, 160, 0, 0, &plan);
    CHECK(plan.source.sourceWidth == 240 && plan.source.sourceHeight == 160);
    CHECK(plan.drawX == 20 && plan.drawY == 0 && plan.drawWidth == 360 && plan.drawHeight == 240);
    CHECK(!plan.linearFilter && plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_STRETCH,
                         TOP_VIEW_3DS_DISPLAY_BILINEAR,
                         PORT_3DS_FULL_VIEW_FALLBACK, 240, 160, 240, 160, 0, 0, &plan);
    CHECK(plan.drawX == 0 && plan.drawWidth == 400 && plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_WIDE,
                         TOP_VIEW_3DS_DISPLAY_BILINEAR,
                         PORT_3DS_FULL_VIEW_FALLBACK, 266, 160, 266, 160, 0, 0, &plan);
    CHECK(plan.source.sourceWidth == 266 && plan.drawWidth == 400 && plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_ORIGINAL,
                         TOP_VIEW_3DS_DISPLAY_BLUR,
                         PORT_3DS_FULL_VIEW_FALLBACK, 266, 160, 266, 160, 0, 0, &plan);
    CHECK(plan.drawX == 20 && plan.drawWidth == 360 && plan.linearFilter);
    CHECK(!plan.useSharpBilinear);
    TopView3DS_BuildPlan(0, 0, TOP_VIEW_3DS_ASPECT_STRETCH,
                         TOP_VIEW_3DS_DISPLAY_SCALED,
                         PORT_3DS_FULL_VIEW_FALLBACK, 240, 160, 240, 160, 0, 0, &plan);
    CHECK(plan.drawX == 0 && plan.drawWidth == 400 && !plan.linearFilter);
    CHECK(!plan.useSharpBilinear);

    puts("top_view_3ds_test: PASS");
    return EXIT_SUCCESS;
}
