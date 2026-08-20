/*
 * 3DS-only wrapper around the shared Minish Cap second-screen compositor.
 *
 * Keeping the wrapper in platform/3ds makes the title/file-select behavior
 * independent of Android and desktop. The shared compositor is included as
 * this translation unit's implementation so this small adapter can select
 * its existing Settings page without adding title-menu state to every port.
 */
#include "port_second_screen_3ds.h"
#include "bottom_idle_3ds.h"
#include "bottom_frame_state_3ds.h"
#include "bottom_map_anim_3ds.h"

#include <stdbool.h>

/* This replaces the direct port/port_second_screen.c entry in the 3DS CMake
 * source list. All normal exported functions retain their original symbols. */
#include "../../../port/port_second_screen.c"

static volatile uint8_t sIdleSettingsOpen;
static uint8_t sIdleOwnsSettingsTab;
static volatile uint8_t sIdleBackVisible;
static BottomFrameState3DS sFrameState;

static uint32_t BeginRefresh(void) {
    return BottomFrameState3DS_BeginPaint(&sFrameState);
}

static void FinishRefresh(uint32_t request) {
    /* A tap may arrive while the worker is painting.  Acknowledge only the
     * generation this frame actually observed; a newer generation remains
     * pending and schedules another paint instead of being lost. */
    BottomFrameState3DS_FinishPaint(&sFrameState, request);
}

static uint32_t RequestRefresh(void) {
    return BottomFrameState3DS_Request(&sFrameState);
}

static uint32_t RequestTouchRefresh(void) {
    return BottomFrameState3DS_RequestTouchRefresh(&sFrameState);
}

static void IdleBackRect(int width, int height, float* x0, float* y0, float* x1, float* y1) {
    float u = (float)(width < height ? width : height) / 720.0f;
    int32_t ts = (width < height ? width : height) / 240;
    if (ts < 2) ts = 2;
    if (ts > 6) ts = 6;

    const float panelX = 10.0f * u;
    const float panelY = 10.0f * u;
    const float inset = 6.0f * (float)ts;
    int32_t headerScale = (int32_t)(2.4f * u);
    if (headerScale < 1) headerScale = 1;
    const float headerHeight = MENU_TEXT_BOX * headerScale + 24.0f * u;
    float buttonWidth = 154.0f * u;
    if (buttonWidth < 54.0f) buttonWidth = 54.0f;

    *x0 = panelX + inset + 4.0f * u;
    *y0 = panelY + inset;
    *x1 = *x0 + buttonWidth;
    *y1 = *y0 + headerHeight;
}

static bool IsIdleBackTap(int x, int y, int width, int height) {
    float x0, y0, x1, y1;
    IdleBackRect(width, height, &x0, &y0, &x1, &y1);
    return (float)x >= x0 && (float)x < x1 && (float)y >= y0 && (float)y < y1;
}

static void ResetIdleOnlyState(void) {
    UI_LOCK();
    sTapTargetCount = 0;
    sUi.mapLive = 0;
    sUi.armedRing = 0;
    sUi.floorPreview = SS_NO_FLOOR;
    sUi.playerFloorDisp = SS_NO_FLOOR;
    sUi.regionState = SS_REGION_OFF;
    sUi.questView = SS_QUEST_MAIN;
    sUi.settingsPage = SS_SETTINGS_ROOT;
    sUi.randoConfirmActive = 0;
    if (sIdleOwnsSettingsTab) sUi.tab = SS_TAB_MAP;
    UI_UNLOCK();
    sLastFix.valid = 0;
    sCam.valid = 0;
    sIdleOwnsSettingsTab = 0;
    __atomic_store_n(&sIdleBackVisible, 0, __ATOMIC_RELEASE);
}

static void DrawIdleBack(const SSurf* surface, float u, int32_t ts) {
    float x0, y0, x1, y1;
    IdleBackRect(surface->w, surface->h, &x0, &y0, &x1, &y1);
    DrawMenuButton(surface, x0, y0, x1, y1, "BACK", 0, 0, u, ts);
}

uint32_t Port_SecondScreen_3DS_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                        const SecondScreenSnapshot* snap, uint32_t tick) {
    if (!pixels || !snap || width <= 0 || height <= 0 || strideInPixels < width) return 0;

    const uint32_t refreshRequest = BeginRefresh();
    const bool settingsOpen = __atomic_load_n(&sIdleSettingsOpen, __ATOMIC_ACQUIRE) != 0;

    if (snap->inGame) {
        if (settingsOpen) __atomic_store_n(&sIdleSettingsOpen, 0, __ATOMIC_RELEASE);
        if (sIdleOwnsSettingsTab) ResetIdleOnlyState();
        Port_SecondScreen_PaintInto(pixels, width, height, strideInPixels, snap, tick);
        FinishRefresh(refreshRequest);
        return refreshRequest;
    }

    if (!settingsOpen) {
        /* Preserve every reset the shared non-game cinema path performed:
         * stale map fixes, armed items and submenu state must not cross a
         * title/file-select boundary even though the picture is replaced. */
        ResetIdleOnlyState();
        BottomIdle3DS_Paint(pixels, width, height, strideInPixels, tick);
        FinishRefresh(refreshRequest);
        return refreshRequest;
    }

    /* The existing Settings painter only needs the inGame gate to become
     * visible; its values come from persistent port configuration, not from
     * gameplay coordinates. A zeroed title snapshot is therefore safe. */
    if (!sIdleOwnsSettingsTab) ResetIdleOnlyState();
    SecondScreenSnapshot settingsSnapshot = *snap;
    settingsSnapshot.inGame = 1;
    UI_LOCK();
    sUi.tab = SS_TAB_SETTINGS;
    sIdleOwnsSettingsTab = 1;
    UI_UNLOCK();
    Port_SecondScreen_PaintInto(pixels, width, height, strideInPixels, &settingsSnapshot, tick);

    int settingsPage;
    int confirmationOpen;
    UI_LOCK();
    settingsPage = sUi.settingsPage;
    confirmationOpen = sUi.randoConfirmActive;
    UI_UNLOCK();
    const bool showIdleBack = settingsPage == SS_SETTINGS_ROOT && !confirmationOpen;
    if (showIdleBack) {
        SSurf surface = { pixels, width, height, strideInPixels };
        float u = (float)(width < height ? width : height) / 720.0f;
        int32_t ts = (width < height ? width : height) / 240;
        if (ts < 2) ts = 2;
        if (ts > 6) ts = 6;
        DrawIdleBack(&surface, u, ts);
    }
    __atomic_store_n(&sIdleBackVisible, showIdleBack ? 1 : 0, __ATOMIC_RELEASE);
    FinishRefresh(refreshRequest);
    return refreshRequest;
}

void Port_SecondScreen_3DS_ResetFrameState(void) {
    BottomFrameState3DS_Reset(&sFrameState);
}

uint32_t Port_SecondScreen_3DS_RequestRefresh(void) {
    return RequestRefresh();
}

void Port_SecondScreen_3DS_MarkSubmitted(uint32_t generation, int inGame) {
    BottomFrameState3DS_MarkSubmitted(&sFrameState, generation, inGame);
}

void Port_SecondScreen_3DS_PromoteSubmitted(void) {
    BottomFrameState3DS_PromoteSubmitted(&sFrameState);
}

void Port_SecondScreen_3DS_GetFrameStats(BottomFrameState3DSStats* out) {
    BottomFrameState3DS_GetStats(&sFrameState, out);
}

void Port_SecondScreen_3DS_OnTap(int x, int y, int longPress) {
    SecondScreenSnapshot currentSnapshot;
    Port_SecondScreenState_Read(&currentSnapshot);
    /* Painting publishes the compositor's new hit boxes before its texture
     * can be submitted or shown.  Dispatch only when those hit boxes belong
     * to the confirmed visible generation and task; otherwise fail closed
     * while leaving the per-tick HID polling cadence untouched. */
    if (!BottomFrameState3DS_CanDispatchTouch(&sFrameState, currentSnapshot.inGame != 0)) {
        /* A same-task hidden paint is already on its way to the display; an
         * extra request would only lengthen this fail-closed interval.  Task
         * transitions (and the pre-first-frame state) still need a paint. */
        if (!BottomFrameState3DS_VisibleModeMatches(&sFrameState, currentSnapshot.inGame != 0)) {
            RequestTouchRefresh();
        }
        return;
    }
    if (currentSnapshot.inGame) {
        Port_SecondScreen_OnTap(x, y, longPress);
        RequestTouchRefresh();
        return;
    }

    if (!__atomic_load_n(&sIdleSettingsOpen, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&sIdleSettingsOpen, 1, __ATOMIC_RELEASE);
        RequestTouchRefresh();
        return;
    }

    if (__atomic_load_n(&sIdleBackVisible, __ATOMIC_ACQUIRE) &&
        IsIdleBackTap(x, y, 320, 240)) {
        __atomic_store_n(&sIdleSettingsOpen, 0, __ATOMIC_RELEASE);
        RequestTouchRefresh();
        return;
    }

    Port_SecondScreen_OnTap(x, y, longPress);
    RequestTouchRefresh();
}

int Port_SecondScreen_3DS_NeedsRefresh(void) {
    return BottomFrameState3DS_NeedsPaint(&sFrameState);
}

int Port_SecondScreen_3DS_NeedsPeriodicRefresh(const SecondScreenSnapshot* snap, uint32_t tick,
                                               uint32_t paintedTick, int32_t width,
                                               int32_t height) {
    if (!snap) return 0;

    const bool idleSettings = __atomic_load_n(&sIdleSettingsOpen, __ATOMIC_ACQUIRE) != 0;
    if (!snap->inGame && !idleSettings) {
        /* The title/file-select Triforce breathes. */
        return 1;
    }

    int tab;
    int settingsPage;
    uint32_t dumpFlashUntil;
    uint32_t lastTick;
    int regionState;
    int floorPreview;
    UI_LOCK();
    tab = sUi.tab;
    settingsPage = sUi.settingsPage;
    dumpFlashUntil = sUi.dumpFlashUntil;
    lastTick = sUi.lastTick;
    regionState = sUi.regionState;
    floorPreview = sUi.floorPreview;
    UI_UNLOCK();

    if (tab == SS_TAB_MAP) {
        /* Two state machines retire inside the paint itself — the region
         * bracket at port_second_screen.c:1201 and the floor preview at
         * :1377 — so they must keep painting until they expire or they stall
         * on screen forever. */
        if (regionState == SS_REGION_BRACKET || floorPreview != SS_NO_FLOOR) {
            return 1;
        }
        if (BottomMapAnim_NeedsPaint(tick, paintedTick,
                                     (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0, width,
                                     height)) {
            return 1;
        }
        return 0;
    }
    if (tab == SS_TAB_ITEMS) {
        /* The authentic item cursor blinks even when no values change. */
        return 1;
    }
    if (tab != SS_TAB_SETTINGS) {
        /* Quest status and its two detail lists have no selection cursor. */
        return 0;
    }
    if (settingsPage == SS_SETTINGS_OVERLAY) {
        /* Live diagnostics deliberately retain their slower refresh rate. */
        return 1;
    }
    if (settingsPage == SS_SETTINGS_DEVELOPER &&
        (int32_t)(dumpFlashUntil - lastTick) > 0) {
        /* Keep painting until the post-dump DONE indicator expires. */
        return 1;
    }
    return 0;
}

int Port_SecondScreen_3DS_SnapshotChangeNeedsRefresh(const SecondScreenSnapshot* previous,
                                                     const SecondScreenSnapshot* current,
                                                     int previousValid) {
    if (!previous || !current || !previousValid) return 1;
    if (previous->inGame != current->inGame) return 1;
    if (memcmp(previous, current, sizeof(*current)) == 0) return 0;

    /* Gameplay snapshots continue changing while a settings sheet covers the
     * entire panel, but none of those engine fields are drawn there.  Config
     * mutations originate in the tap handler and request an immediate paint;
     * the overlay and dump indicator are handled by NeedsPeriodicRefresh. */
    if (!current->inGame) return 0;
    int tab;
    UI_LOCK();
    tab = sUi.tab;
    UI_UNLOCK();
    return tab != SS_TAB_SETTINGS;
}
