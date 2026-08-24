#include "top_view_3ds.h"

#include <string.h>

TopView3DSPpuCoherence TopView3DS_ResolvePpuCoherence(
    Port3DSFullViewMode selectedMode, int producerFullView) {
    TopView3DSPpuCoherence result = {
        .mode = selectedMode,
        .forceNativeFrame = 0,
        .clearFullViewProducer = 0,
    };
    const int selectedFullView = selectedMode == PORT_3DS_FULL_VIEW_OUTDOOR_1X;
    if (selectedFullView == (producerFullView != 0)) {
        return result;
    }

    result.mode = PORT_3DS_FULL_VIEW_FALLBACK;
    result.forceNativeFrame = 1;
    result.clearFullViewProducer = producerFullView != 0;
    return result;
}

void TopView3DS_BuildPlan(int old3DS, int fullViewComboEnabled,
                          int aspect, int displayStyle,
                          Port3DSFullViewMode requestedMode,
                          int renderWidth, int renderHeight,
                          int validSourceWidth, int validSourceHeight,
                          int cropX, int cropY, TopView3DSPlan* plan) {
    TopView3DSPlan result;
    memset(&result, 0, sizeof(result));
    if (old3DS || !fullViewComboEnabled) {
        requestedMode = PORT_3DS_FULL_VIEW_FALLBACK;
    }
    result.mode = Port3DSFullViewPolicy_ResolvePresentation(
        requestedMode, renderWidth, renderHeight, validSourceWidth,
        validSourceHeight, cropX, cropY, &result.source);
    result.linearFilter = result.mode == PORT_3DS_FULL_VIEW_FALLBACK &&
                          displayStyle == TOP_VIEW_3DS_DISPLAY_BLUR;
    result.useSharpBilinear = result.mode == PORT_3DS_FULL_VIEW_FALLBACK &&
                              displayStyle == TOP_VIEW_3DS_DISPLAY_BILINEAR;

    if (result.mode != PORT_3DS_FULL_VIEW_FALLBACK) {
        result.drawWidth = 400;
        result.drawHeight = 240;
    } else if (displayStyle == TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT) {
        result.drawWidth = result.source.sourceWidth;
        result.drawHeight = result.source.sourceHeight;
    } else {
        result.drawHeight = 240;
        switch (aspect) {
            case TOP_VIEW_3DS_ASPECT_STRETCH:
                result.drawWidth = 400;
                break;
            case TOP_VIEW_3DS_ASPECT_ORIGINAL:
                result.drawWidth = 360;
                break;
            case TOP_VIEW_3DS_ASPECT_WIDE:
            default:
                result.drawWidth = result.source.sourceWidth >= 266 ? 400 : 360;
                break;
        }
    }
    result.drawX = (400 - result.drawWidth) / 2;
    result.drawY = (240 - result.drawHeight) / 2;
    if (plan != 0) *plan = result;
}
