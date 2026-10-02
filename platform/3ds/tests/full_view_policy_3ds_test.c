#include "port_3ds_full_view_policy.h"
#include "area.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "full_view_policy_3ds_test: check failed at line %d: %s\n",    \
                    __LINE__, #condition);                                                   \
            return EXIT_FAILURE;                                                            \
        }                                                                                   \
    } while (0)

static Port3DSFullViewInputs ReadyOutdoor(void) {
    Port3DSFullViewInputs inputs = {
        .isNew3DS = 1,
        .aspectWide = 1,
        .pixelPerfect = 1,
        .gameTask = 1,
        .playerValid = 1,
        .areaIsExterior = 1,
        .roomWidth = 512,
        .roomHeight = 512,
        .contentWidth = 512,
        .contentHeight = 512,
    };
    return inputs;
}

int main(void) {
    Port3DSFullViewInputs inputs = ReadyOutdoor();
    Port3DSFullViewPresentation presentation;
    Port3DSFullViewFallbackReason reason = PORT_3DS_FULL_VIEW_REASON_NONE;

    /* Settings/hardware matrix: only New + Wide + Pixel Perfect opts in. */
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_OUTDOOR_1X);
    inputs.isNew3DS = 0;
    CHECK(Port3DSFullViewPolicy_Decide(&inputs, &reason) == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(reason == PORT_3DS_FULL_VIEW_REASON_COMBO_DISABLED);
    inputs = ReadyOutdoor();
    inputs.aspectWide = 0;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);
    inputs = ReadyOutdoor();
    inputs.pixelPerfect = 0;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);

    /* Full exterior frames require complete 400x240 room and content bounds. */
    inputs = ReadyOutdoor();
    inputs.contentWidth = 399;
    CHECK(Port3DSFullViewPolicy_Decide(&inputs, &reason) == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(reason == PORT_3DS_FULL_VIEW_REASON_OUTDOOR_BOUNDS);
    inputs = ReadyOutdoor();
    inputs.contentHeight = 239;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);

    /* The same combo inside selects a real 200x120 logical viewport. Both
     * room and painted content must be large enough to feed it. */
    inputs = ReadyOutdoor();
    inputs.areaIsExterior = 0;
    inputs.roomWidth = 240;
    inputs.roomHeight = 160;
    inputs.contentWidth = 240;
    inputs.contentHeight = 160;

    /* Exterior classification is positive and fail-closed. Use immutable
     * metadata bits so runtime AR_HAS_NO_ENEMIES mutations cannot turn Royal
     * Valley into an interior, while explicit outdoor maps such as Minish
     * Village remain Full View despite lacking AR_IS_OVERWORLD. */
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(
              AREA_MINISH_WOODS, 0, AR_IS_OVERWORLD | AR_ALLOWS_WARP));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(
              AREA_ROYAL_VALLEY, 0, AR_IS_OVERWORLD | AR_ALLOWS_WARP | AR_HAS_NO_ENEMIES));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_MINISH_VILLAGE, 0, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_MINISH_VILLAGE, 1, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_MINISH_VILLAGE, 2, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_BEANSTALKS, 16, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_BEANSTALKS, 15, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_OUTER_FORTRESS_OF_WINDS, 4, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_DARK_HYRULE_CASTLE_OUTSIDE, 7, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_GARDEN_FOUNTAINS, 0, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_GARDEN_FOUNTAINS, 1, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_GARDEN_FOUNTAINS, 2, 0));
    CHECK(Port3DSFullViewPolicy_AreaIsExterior(AREA_PALACE_OF_WINDS_BOSS, 0, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_PALACE_OF_WINDS_BOSS, 1, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_HOUSE_INTERIORS_1, 0, 0));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_LAKE_WOODS_CAVE, 0, AR_IS_MOLE_CAVE));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_98 + 1, 0, AR_IS_OVERWORLD));
    CHECK(!Port3DSFullViewPolicy_AreaIsExterior(AREA_MINISH_VILLAGE, -1, 0));
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_INTERIOR_2X);
    inputs.contentWidth = 199;
    CHECK(Port3DSFullViewPolicy_Decide(&inputs, &reason) == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(reason == PORT_3DS_FULL_VIEW_REASON_INTERIOR_BOUNDS);
    inputs.contentWidth = 240;
    inputs.contentHeight = 119;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);
    inputs.contentHeight = 160;

    /* Menus, dialogue/banners, outgoing transitions and invalid player
     * generations fail closed to the established E2 canvas. */
    inputs.fixedCanvas = 1;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);
    inputs.fixedCanvas = 0;
    inputs.uiOverlay = 1;
    CHECK(Port3DSFullViewPolicy_Decide(&inputs, &reason) == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(reason == PORT_3DS_FULL_VIEW_REASON_FIXED_CANVAS);
    inputs.uiOverlay = 0;
    inputs.transitioning = 1;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);
    inputs.transitioning = 0;
    inputs.playerValid = 0;
    CHECK(Port3DSFullViewPolicy_Desired(&inputs) == PORT_3DS_FULL_VIEW_FALLBACK);
    inputs = ReadyOutdoor();
    inputs.unsupportedScene = 1;
    CHECK(Port3DSFullViewPolicy_Decide(&inputs, &reason) == PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(reason == PORT_3DS_FULL_VIEW_REASON_UNSUPPORTED_SCENE);

    /* A new room cannot inherit the previous generation's experimental mode. */
    CHECK(Port3DSFullViewPolicy_Latch(PORT_3DS_FULL_VIEW_OUTDOOR_1X,
                                     PORT_3DS_FULL_VIEW_INTERIOR_2X, 0, 1) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(Port3DSFullViewPolicy_Latch(PORT_3DS_FULL_VIEW_FALLBACK,
                                     PORT_3DS_FULL_VIEW_INTERIOR_2X, 1, 0) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(Port3DSFullViewPolicy_Latch(PORT_3DS_FULL_VIEW_FALLBACK,
                                     PORT_3DS_FULL_VIEW_INTERIOR_2X, 1, 1) ==
          PORT_3DS_FULL_VIEW_INTERIOR_2X);
    CHECK(Port3DSFullViewPolicy_Latch(PORT_3DS_FULL_VIEW_INTERIOR_2X,
                                     PORT_3DS_FULL_VIEW_OUTDOOR_1X, 1, 1) ==
          PORT_3DS_FULL_VIEW_OUTDOOR_1X);

    /* Presenter matrix: native240, existing wide266 and exact full400 stay
     * distinct; malformed full/crop requests fail closed to the E2 source. */
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_FALLBACK,
                                                    240, 160, 240, 160, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(presentation.sourceWidth == 240 && presentation.sourceHeight == 160);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_FALLBACK,
                                                    266, 160, 266, 160, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(presentation.sourceWidth == 266 && presentation.sourceHeight == 160);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_OUTDOOR_1X,
                                                    400, 240, 400, 240, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_OUTDOOR_1X);
    CHECK(presentation.sourceWidth == 400 && presentation.sourceHeight == 240);
    CHECK(presentation.outputWidth == 400 && presentation.outputHeight == 240);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_OUTDOOR_1X,
                                                    399, 240, 399, 240, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(presentation.sourceWidth == 266 && presentation.sourceHeight == 160);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_INTERIOR_2X,
                                                    200, 120, 200, 120, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_INTERIOR_2X);
    CHECK(presentation.sourceX == 0 && presentation.sourceY == 0);
    CHECK(presentation.sourceWidth == 200 && presentation.sourceHeight == 120);
    CHECK(presentation.outputWidth == 400 && presentation.outputHeight == 240);
    /* The old post-composition crop design is deliberately rejected: it
     * would cut/move HUD and dialogue pixels. */
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_INTERIOR_2X,
                                                    266, 160, 266, 160, 33, 20, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(presentation.sourceWidth == 266 && presentation.sourceHeight == 160);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_INTERIOR_2X,
                                                    200, 120, 200, 120, 1, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);
    CHECK(Port3DSFullViewPolicy_ResolvePresentation(PORT_3DS_FULL_VIEW_INTERIOR_2X,
                                                    200, 119, 200, 119, 0, 0, &presentation) ==
          PORT_3DS_FULL_VIEW_FALLBACK);

    /* Exact BG0 tile offsets keep right/bottom HUD widgets in each viewport. */
    CHECK(Port3DSFullViewPolicy_HudTilemapOffset(240, 160) == 0);
    CHECK(Port3DSFullViewPolicy_HudTilemapOffset(266, 160) == 0);
    CHECK(Port3DSFullViewPolicy_HudTilemapOffset(400, 240) == 10 * 0x20);
    CHECK(Port3DSFullViewPolicy_HudTilemapOffset(200, 120) == -5 * 0x20 - 5);
    CHECK(Port3DSFullViewPolicy_ExitLimit(240, 24) == 0x108);
    CHECK(Port3DSFullViewPolicy_ExitLimit(400, 24) == 424);
    CHECK(Port3DSFullViewPolicy_ExitLimit(120, 40) == 160);
    CHECK(!Port3DSFullViewPolicy_RoomTransitionActive(0, 0, 0, 1));
    CHECK(Port3DSFullViewPolicy_RoomTransitionActive(1, 0, 0, 1));
    CHECK(Port3DSFullViewPolicy_RoomTransitionActive(0, 1, 0, 1));
    CHECK(Port3DSFullViewPolicy_RoomTransitionActive(0, 0, 1, 2));
    CHECK(Port3DSFullViewPolicy_RoomTransitionActive(0, 0, 2, 5));
    CHECK(Port3DSFullViewPolicy_RoomTransitionActive(0, 0, 0, 5));
    CHECK(!Port3DSFullViewPolicy_NativeWindowActive(0));
    CHECK(Port3DSFullViewPolicy_NativeWindowActive(0x2000));
    CHECK(Port3DSFullViewPolicy_NativeWindowActive(0x4000));
    CHECK(Port3DSFullViewPolicy_NativeWindowActive(0x8000));
    CHECK(Port3DSFullViewPolicy_ParallaxOffset(48, 256, 160, 32) == 16);
    CHECK(Port3DSFullViewPolicy_ParallaxOffset(68, 256, 120, 32) == 16);
    CHECK(Port3DSFullViewPolicy_ParallaxOffset(0, 200, 200, 48) == 0);
    CHECK(strcmp(Port3DSFullViewPolicy_FallbackReasonName(
                     PORT_3DS_FULL_VIEW_REASON_PRESENTATION_BOUNDS),
                 "presentation-bounds") == 0);

    puts("full_view_policy_3ds_test: PASS");
    return EXIT_SUCCESS;
}
