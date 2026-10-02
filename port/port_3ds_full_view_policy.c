#include "port_3ds_full_view_policy.h"

#include "area.h"

enum {
    PORT_3DS_FULL_VIEW_WIDTH = 400,
    PORT_3DS_FULL_VIEW_HEIGHT = 240,
    PORT_3DS_INTERIOR_CROP_WIDTH = 200,
    PORT_3DS_INTERIOR_CROP_HEIGHT = 120,
    PORT_3DS_EXISTING_WIDE_MAX = 266,
};

int Port3DSFullViewPolicy_AreaIsExterior(int area, int room, unsigned metadataFlags) {
    if (area < 0 || area > AREA_98 || room < 0) {
        return 0;
    }
    if ((metadataFlags & AR_IS_OVERWORLD) != 0) {
        return 1;
    }

    switch ((AreaID)area) {
        case AREA_MINISH_VILLAGE:
            return room <= 1;
        case AREA_BEANSTALKS:
            return room <= 4 || (room >= 16 && room <= 20);
        case AREA_MINISH_PATHS:
            return room <= 4;
        case AREA_GARDEN_FOUNTAINS:
            return room <= 1;
        case AREA_CRENEL_MINISH_PATHS:
            return room <= 3;
        case AREA_OUTER_FORTRESS_OF_WINDS:
            return room <= 4;
        case AREA_DEEPWOOD_SHRINE_ENTRY:
        case AREA_WIND_TRIBE_TOWER_ROOF:
        case AREA_FORTRESS_OF_WINDS_TOP:
        case AREA_PALACE_OF_WINDS_BOSS:
            return room == 0;
        case AREA_DARK_HYRULE_CASTLE_OUTSIDE:
            return room <= 7;
        default:
            return 0;
    }
}

Port3DSFullViewMode Port3DSFullViewPolicy_Decide(const Port3DSFullViewInputs* inputs,
                                                 Port3DSFullViewFallbackReason* reason) {
    Port3DSFullViewFallbackReason localReason = PORT_3DS_FULL_VIEW_REASON_NONE;
    if (inputs == 0 || !inputs->isNew3DS || !inputs->aspectWide || !inputs->pixelPerfect) {
        localReason = PORT_3DS_FULL_VIEW_REASON_COMBO_DISABLED;
    } else if (!inputs->gameTask) {
        localReason = PORT_3DS_FULL_VIEW_REASON_NON_GAME_TASK;
    } else if (inputs->fixedCanvas) {
        localReason = PORT_3DS_FULL_VIEW_REASON_FIXED_CANVAS;
    } else if (inputs->uiOverlay) {
        /* Retail dialogue and room banners own a 240-pixel BG0 canvas. Keep
         * their established E2 geometry instead of cutting or repositioning
         * them in either experimental mode. */
        localReason = PORT_3DS_FULL_VIEW_REASON_FIXED_CANVAS;
    } else if (inputs->transitioning) {
        localReason = PORT_3DS_FULL_VIEW_REASON_TRANSITION;
    } else if (!inputs->playerValid) {
        localReason = PORT_3DS_FULL_VIEW_REASON_PLAYER_INVALID;
    } else if (inputs->unsupportedScene) {
        localReason = PORT_3DS_FULL_VIEW_REASON_UNSUPPORTED_SCENE;
    }
    if (localReason != PORT_3DS_FULL_VIEW_REASON_NONE) {
        if (reason != 0) *reason = localReason;
        return PORT_3DS_FULL_VIEW_FALLBACK;
    }

    if (inputs->areaIsExterior) {
        /* Never ask the engine for a 400x240 camera unless both the room and
         * the ratcheted painted-content scan can feed every requested pixel. */
        if (inputs->roomWidth < PORT_3DS_FULL_VIEW_WIDTH ||
            inputs->roomHeight < PORT_3DS_FULL_VIEW_HEIGHT ||
            inputs->contentWidth < PORT_3DS_FULL_VIEW_WIDTH ||
            inputs->contentHeight < PORT_3DS_FULL_VIEW_HEIGHT) {
            if (reason != 0) *reason = PORT_3DS_FULL_VIEW_REASON_OUTDOOR_BOUNDS;
            return PORT_3DS_FULL_VIEW_FALLBACK;
        }
        if (reason != 0) *reason = PORT_3DS_FULL_VIEW_REASON_NONE;
        return PORT_3DS_FULL_VIEW_OUTDOOR_1X;
    }

    if (inputs->roomWidth < PORT_3DS_INTERIOR_CROP_WIDTH ||
        inputs->roomHeight < PORT_3DS_INTERIOR_CROP_HEIGHT ||
        inputs->contentWidth < PORT_3DS_INTERIOR_CROP_WIDTH ||
        inputs->contentHeight < PORT_3DS_INTERIOR_CROP_HEIGHT) {
        if (reason != 0) *reason = PORT_3DS_FULL_VIEW_REASON_INTERIOR_BOUNDS;
        return PORT_3DS_FULL_VIEW_FALLBACK;
    }
    if (reason != 0) *reason = PORT_3DS_FULL_VIEW_REASON_NONE;
    return PORT_3DS_FULL_VIEW_INTERIOR_2X;
}

Port3DSFullViewMode Port3DSFullViewPolicy_Desired(const Port3DSFullViewInputs* inputs) {
    return Port3DSFullViewPolicy_Decide(inputs, 0);
}

const char* Port3DSFullViewPolicy_FallbackReasonName(Port3DSFullViewFallbackReason reason) {
    static const char* const names[] = {
        "none", "combo-disabled", "non-game-task", "fixed-canvas", "transition",
        "player-invalid", "unsupported-scene", "outdoor-bounds", "interior-bounds", "room-generation",
        "shadows-unavailable", "latch-mismatch", "presentation-bounds",
    };
    const unsigned index = (unsigned)reason;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}

Port3DSFullViewMode Port3DSFullViewPolicy_Latch(Port3DSFullViewMode previous,
                                                Port3DSFullViewMode desired,
                                                int currentRoomGeneration,
                                                int shadowsPrepared) {
    (void)previous;
    if (!currentRoomGeneration || !shadowsPrepared) {
        return PORT_3DS_FULL_VIEW_FALLBACK;
    }
    if (desired != PORT_3DS_FULL_VIEW_OUTDOOR_1X &&
        desired != PORT_3DS_FULL_VIEW_INTERIOR_2X) {
        return PORT_3DS_FULL_VIEW_FALLBACK;
    }
    return desired;
}

Port3DSFullViewMode Port3DSFullViewPolicy_ResolvePresentation(
    Port3DSFullViewMode requested, int renderWidth, int renderHeight,
    int validSourceWidth, int validSourceHeight,
    int cropX, int cropY, Port3DSFullViewPresentation* presentation) {
    Port3DSFullViewPresentation resolved;
    int fallbackWidth = renderWidth;
    if (fallbackWidth < 240) fallbackWidth = 240;
    if (fallbackWidth > PORT_3DS_EXISTING_WIDE_MAX) fallbackWidth = PORT_3DS_EXISTING_WIDE_MAX;
    resolved.renderWidth = fallbackWidth;
    resolved.renderHeight = PORT_3DS_FULL_VIEW_HEIGHT * 2 / 3;
    resolved.sourceX = 0;
    resolved.sourceY = 0;
    resolved.sourceWidth = fallbackWidth;
    resolved.sourceHeight = PORT_3DS_FULL_VIEW_HEIGHT * 2 / 3;
    resolved.outputWidth = fallbackWidth;
    resolved.outputHeight = PORT_3DS_FULL_VIEW_HEIGHT * 2 / 3;

    if (requested == PORT_3DS_FULL_VIEW_OUTDOOR_1X &&
        renderWidth == PORT_3DS_FULL_VIEW_WIDTH && renderHeight == PORT_3DS_FULL_VIEW_HEIGHT &&
        validSourceWidth >= PORT_3DS_FULL_VIEW_WIDTH &&
        validSourceHeight >= PORT_3DS_FULL_VIEW_HEIGHT) {
        resolved.renderWidth = renderWidth;
        resolved.renderHeight = renderHeight;
        resolved.sourceWidth = PORT_3DS_FULL_VIEW_WIDTH;
        resolved.sourceHeight = PORT_3DS_FULL_VIEW_HEIGHT;
        resolved.outputWidth = PORT_3DS_FULL_VIEW_WIDTH;
        resolved.outputHeight = PORT_3DS_FULL_VIEW_HEIGHT;
        if (presentation != 0) *presentation = resolved;
        return requested;
    }

    if (requested == PORT_3DS_FULL_VIEW_INTERIOR_2X &&
        renderWidth == PORT_3DS_INTERIOR_CROP_WIDTH &&
        renderHeight == PORT_3DS_INTERIOR_CROP_HEIGHT &&
        validSourceWidth == PORT_3DS_INTERIOR_CROP_WIDTH &&
        validSourceHeight == PORT_3DS_INTERIOR_CROP_HEIGHT &&
        cropX == 0 && cropY == 0) {
        resolved.renderWidth = renderWidth;
        resolved.renderHeight = renderHeight;
        resolved.sourceX = cropX;
        resolved.sourceY = cropY;
        resolved.sourceWidth = PORT_3DS_INTERIOR_CROP_WIDTH;
        resolved.sourceHeight = PORT_3DS_INTERIOR_CROP_HEIGHT;
        resolved.outputWidth = PORT_3DS_FULL_VIEW_WIDTH;
        resolved.outputHeight = PORT_3DS_FULL_VIEW_HEIGHT;
        if (presentation != 0) *presentation = resolved;
        return requested;
    }

    if (presentation != 0) *presentation = resolved;
    return PORT_3DS_FULL_VIEW_FALLBACK;
}
