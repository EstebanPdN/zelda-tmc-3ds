#ifndef PORT_3DS_FULL_VIEW_POLICY_H
#define PORT_3DS_FULL_VIEW_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/* This is presentation state, not a persisted screen-aspect option. The 3DS
 * settings and INI continue to expose only Wide / Original / Stretch. */
typedef enum Port3DSFullViewMode {
    PORT_3DS_FULL_VIEW_FALLBACK = 0,
    PORT_3DS_FULL_VIEW_OUTDOOR_1X,
    PORT_3DS_FULL_VIEW_INTERIOR_2X,
} Port3DSFullViewMode;

typedef enum Port3DSFullViewFallbackReason {
    PORT_3DS_FULL_VIEW_REASON_NONE = 0,
    PORT_3DS_FULL_VIEW_REASON_COMBO_DISABLED,
    PORT_3DS_FULL_VIEW_REASON_NON_GAME_TASK,
    PORT_3DS_FULL_VIEW_REASON_FIXED_CANVAS,
    PORT_3DS_FULL_VIEW_REASON_TRANSITION,
    PORT_3DS_FULL_VIEW_REASON_PLAYER_INVALID,
    PORT_3DS_FULL_VIEW_REASON_UNSUPPORTED_SCENE,
    PORT_3DS_FULL_VIEW_REASON_OUTDOOR_BOUNDS,
    PORT_3DS_FULL_VIEW_REASON_INTERIOR_BOUNDS,
    PORT_3DS_FULL_VIEW_REASON_ROOM_GENERATION,
    PORT_3DS_FULL_VIEW_REASON_SHADOWS_UNAVAILABLE,
    PORT_3DS_FULL_VIEW_REASON_LATCH_MISMATCH,
    PORT_3DS_FULL_VIEW_REASON_PRESENTATION_BOUNDS,
} Port3DSFullViewFallbackReason;

typedef struct Port3DSFullViewInputs {
    int isNew3DS;
    int aspectWide;
    int pixelPerfect;
    int gameTask;
    int fixedCanvas;
    int uiOverlay;
    int transitioning;
    int playerValid;
    int unsupportedScene;
    int areaIsExterior;
    int roomWidth;
    int roomHeight;
    int contentWidth;
    int contentHeight;
} Port3DSFullViewInputs;

/* Classify visual exterior scenes without trusting the mutable runtime area
 * flags. `metadataFlags` is the immutable gAreaMetadata row after the caller
 * has bounds-checked `area`; the helper independently rejects unknown IDs and
 * invalid room numbers. */
int Port3DSFullViewPolicy_AreaIsExterior(int area, int room, unsigned metadataFlags);

/* Select the experimental presentation requested by a fully-known scene.
 * FALLBACK means "use the existing E2 presentation"; it never means black or
 * an uninitialised framebuffer. */
Port3DSFullViewMode Port3DSFullViewPolicy_Desired(const Port3DSFullViewInputs* inputs);
Port3DSFullViewMode Port3DSFullViewPolicy_Decide(const Port3DSFullViewInputs* inputs,
                                                 Port3DSFullViewFallbackReason* reason);
const char* Port3DSFullViewPolicy_FallbackReasonName(Port3DSFullViewFallbackReason reason);

/* Publish a desired mode only after this room generation has produced live
 * map shadows. This is the fail-closed latch used at room/menu boundaries. */
Port3DSFullViewMode Port3DSFullViewPolicy_Latch(Port3DSFullViewMode previous,
                                                Port3DSFullViewMode desired,
                                                int currentRoomGeneration,
                                                int shadowsPrepared);

typedef struct Port3DSFullViewPresentation {
    int renderWidth;
    int renderHeight;
    int sourceX;
    int sourceY;
    int sourceWidth;
    int sourceHeight;
    int outputWidth;
    int outputHeight;
} Port3DSFullViewPresentation;

/* BG0 stores the key/rupee widgets in a 32x32 tilemap. Outdoor Full View
 * moves them ten rows down and lets the renderer keep their right edge
 * anchored. The 200x120 interior viewport moves them five rows and five
 * columns up/left so the complete widgets remain inside the logical frame. */
static inline int Port3DSFullViewPolicy_HudTilemapOffset(int viewWidth, int viewHeight) {
    const int rowTiles = (viewHeight - 160) / 8;
    const int columnTiles = viewWidth < 240 ? (viewWidth - 240) / 8 : 0;
    return rowTiles * 0x20 + columnTiles;
}

/* Viewport-relative gameplay bounds. At 240x160 these reduce exactly to the
 * retail constants used by the original entities. */
static inline int Port3DSFullViewPolicy_ExitLimit(int viewExtent, int margin) {
    return viewExtent + margin;
}

/* Room-border and circular cave transitions do not always raise the global
 * fade/transitioningOut flags. Their reload state still mixes two map
 * generations on the retail 240x160 canvas, so Full View must fail closed. */
static inline int Port3DSFullViewPolicy_RoomTransitionActive(int transitioningOut,
                                                             int fadeActive,
                                                             int reloadFlags,
                                                             int scrollAction) {
    return transitioningOut || fadeActive || reloadFlags != 0 || scrollAction == 5;
}

/* WIN0/WIN1/OBJWIN coordinates are authored for the native 240x160 canvas.
 * Until an effect supplies explicit viewport-relative bounds, keep that
 * effect on the established E2 renderer instead of stretching its mask. */
static inline int Port3DSFullViewPolicy_NativeWindowActive(unsigned displayControl) {
    return (displayControl & 0xE000u) != 0u;
}

/* Map a camera's travelled range onto a fixed parallax span without dividing
 * by zero in rooms exactly as large as the logical viewport. */
static inline int Port3DSFullViewPolicy_ParallaxOffset(int scrollDelta,
                                                       int roomExtent,
                                                       int viewExtent,
                                                       int outputSpan) {
    const int travel = roomExtent - viewExtent;
    return travel > 0 ? (scrollDelta * outputSpan) / travel : 0;
}

/* Resolve texture UV/output geometry without trusting the caller. Invalid
 * experimental requests return FALLBACK and the established E2 240/266x160
 * source geometry, so a torn transition can never address outside the top
 * upload texture. Interior 2x is a real 200x120 engine viewport, never a crop
 * of a composed 240/266x160 frame (which would cut or move the HUD). */
Port3DSFullViewMode Port3DSFullViewPolicy_ResolvePresentation(
    Port3DSFullViewMode requested, int renderWidth, int renderHeight,
    int validSourceWidth, int validSourceHeight,
    int cropX, int cropY, Port3DSFullViewPresentation* presentation);

#ifdef __cplusplus
}
#endif

#endif /* PORT_3DS_FULL_VIEW_POLICY_H */
