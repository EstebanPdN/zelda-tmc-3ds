#ifndef TOP_VIEW_3DS_H
#define TOP_VIEW_3DS_H

#include "port_3ds_full_view_policy.h"

enum {
    TOP_VIEW_3DS_ASPECT_WIDE = 0,
    TOP_VIEW_3DS_ASPECT_ORIGINAL,
    TOP_VIEW_3DS_ASPECT_STRETCH,
};

enum {
    TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT = 0,
    TOP_VIEW_3DS_DISPLAY_SCALED,
    TOP_VIEW_3DS_DISPLAY_BILINEAR,
    TOP_VIEW_3DS_DISPLAY_BLUR,
    TOP_VIEW_3DS_DISPLAY_ULTRA_SHARP,
};

typedef struct TopView3DSPlan {
    Port3DSFullViewMode mode;
    Port3DSFullViewPresentation source;
    int drawX;
    int drawY;
    int drawWidth;
    int drawHeight;
    int linearFilter;
    int useSharpBilinear;
    int sharpBilinearScale;
} TopView3DSPlan;

typedef struct TopView3DSPpuCoherence {
    Port3DSFullViewMode mode;
    int forceNativeFrame;
    int clearFullViewProducer;
} TopView3DSPpuCoherence;

/* Reconcile the freshly selected presentation with the shadow geometry that
 * was published at the previous VBlank. Present runs before the next
 * UpdateDisplayControls, so a dialogue/fade may request E2 while the PPU still
 * owns Full View stride-54 shadows. A mismatch must render one native frame;
 * reading those shadows as a 266-wide E2 layout is not safe. */
TopView3DSPpuCoherence TopView3DS_ResolvePpuCoherence(
    Port3DSFullViewMode selectedMode, int producerFullView);

/* Pure, host-testable plan used directly by platform_gpu_3ds.c. It owns the
 * final fail-closed hardware/config gate, texture source rectangle, filter and
 * physical 400x240 draw rectangle; the Citro2D layer only executes this plan. */
void TopView3DS_BuildPlan(int old3DS, int fullViewComboEnabled,
                          int aspect, int displayStyle,
                          Port3DSFullViewMode requestedMode,
                          int renderWidth, int renderHeight,
                          int validSourceWidth, int validSourceHeight,
                          int cropX, int cropY, TopView3DSPlan* plan);

#endif /* TOP_VIEW_3DS_H */
