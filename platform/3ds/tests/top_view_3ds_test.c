#include "top_view_3ds.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void) {
    const int widths[] = { 240, 256, 266, 400 };
    for (int aspect = 0; aspect < 3; ++aspect) {
        for (int style = 0; style < 3; ++style) {
            for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
                TopView3DSPlan plan;
                TopView3DS_BuildPlan(aspect, style, widths[w], &plan);
                const int source = widths[w] > 266 ? 266 : widths[w];
                assert(plan.source.renderWidth == source);
                assert(plan.source.renderHeight == 160);
                assert(plan.source.sourceWidth == source && plan.source.sourceHeight == 160);
                assert(plan.source.sourceX == 0 && plan.source.sourceY == 0);
                assert(plan.linearFilter == (style == TOP_VIEW_3DS_DISPLAY_BLUR));
                assert(plan.useSharpBilinear == (style == TOP_VIEW_3DS_DISPLAY_BILINEAR));
                assert(plan.sharpBilinearScale == (plan.useSharpBilinear ? 2 : 0));
                if (style == TOP_VIEW_3DS_DISPLAY_PIXEL_PERFECT) {
                    assert(plan.drawWidth == source && plan.drawHeight == 160);
                } else {
                    const int expected = aspect == TOP_VIEW_3DS_ASPECT_STRETCH    ? 400
                                         : aspect == TOP_VIEW_3DS_ASPECT_ORIGINAL ? 360
                                         : source >= 266                          ? 400
                                                                                  : 360;
                    assert(plan.drawWidth == expected && plan.drawHeight == 240);
                }
                assert(plan.drawX == (400 - plan.drawWidth) / 2);
                assert(plan.drawY == (240 - plan.drawHeight) / 2);
            }
        }
    }
    puts("top_view_3ds_test: 36 aspect/filter/source combinations PASS");
    return 0;
}
