#include "top_view_3ds.h"

#include <string.h>

void TopView3DS_BuildPlan(int aspect, int displayStyle, int renderWidth, TopView3DSPlan* plan) {
    TopView3DSPlan result;
    memset(&result, 0, sizeof(result));
    if (renderWidth < 240)
        renderWidth = 240;
    if (renderWidth > 266)
        renderWidth = 266;
    result.source = (TopView3DSSource){ renderWidth, 160, 0, 0, renderWidth, 160 };
    result.linearFilter = displayStyle == TOP_VIEW_3DS_DISPLAY_BLUR;
    result.useSharpBilinear = displayStyle == TOP_VIEW_3DS_DISPLAY_BILINEAR;
    result.sharpBilinearScale = result.useSharpBilinear ? 2 : 0;

    if (displayStyle == TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT) {
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
    if (plan != 0)
        *plan = result;
}
