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

#include <stdbool.h>

/* This replaces the direct port/port_second_screen.c entry in the 3DS CMake
 * source list. All normal exported functions retain their original symbols. */
#include "../../../port/port_second_screen.c"

static volatile uint8_t sIdleSettingsOpen;
static volatile uint32_t sRefreshRequested;
static volatile uint32_t sRefreshPainted;
static uint8_t sIdleOwnsSettingsTab;
static volatile uint8_t sIdleBackVisible;
static volatile uint8_t sVisibleInGame;

static uint32_t BeginRefresh(void) {
    return __atomic_load_n(&sRefreshRequested, __ATOMIC_ACQUIRE);
}

static void FinishRefresh(uint32_t request) {
    /* A tap may arrive while the worker is painting.  Acknowledge only the
     * generation this frame actually observed; a newer generation remains
     * pending and schedules another paint instead of being lost. */
    __atomic_store_n(&sRefreshPainted, request, __ATOMIC_RELEASE);
}

static void RequestRefresh(void) {
    __atomic_add_fetch(&sRefreshRequested, 1u, __ATOMIC_ACQ_REL);
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
    DrawMenuButton(surface, x0, y0, x1, y1, "НАЗАД", 0, 0, u, ts);
}

void Port_SecondScreen_3DS_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                    const SecondScreenSnapshot* snap, uint32_t tick) {
    if (!pixels || !snap || width <= 0 || height <= 0 || strideInPixels < width) return;

    const uint32_t refreshRequest = BeginRefresh();
    const bool settingsOpen = __atomic_load_n(&sIdleSettingsOpen, __ATOMIC_ACQUIRE) != 0;

    if (snap->inGame) {
        if (settingsOpen) __atomic_store_n(&sIdleSettingsOpen, 0, __ATOMIC_RELEASE);
        if (sIdleOwnsSettingsTab) ResetIdleOnlyState();
        Port_SecondScreen_PaintInto(pixels, width, height, strideInPixels, snap, tick);
        FinishRefresh(refreshRequest);
        return;
    }

    if (!settingsOpen) {
        /* Preserve every reset the shared non-game cinema path performed:
         * stale map fixes, armed items and submenu state must not cross a
         * title/file-select boundary even though the picture is replaced. */
        ResetIdleOnlyState();
        BottomIdle3DS_Paint(pixels, width, height, strideInPixels, tick);
        FinishRefresh(refreshRequest);
        return;
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
}

void Port_SecondScreen_3DS_SetVisibleInGame(int inGame) {
    /* The worker completing a buffer does not make it visible. The PPU calls
     * this only after promoting that buffer to the front, so touch dispatch
     * is classified against the panel the player can actually see. */
    __atomic_store_n(&sVisibleInGame, inGame ? 1 : 0, __ATOMIC_RELEASE);
}

void Port_SecondScreen_3DS_OnTap(int x, int y, int longPress) {
    SecondScreenSnapshot currentSnapshot;
    Port_SecondScreenState_Read(&currentSnapshot);
    /* A task change can make the engine snapshot one frame newer than the
     * visible panel. Never dispatch a hit box across that boundary: request
     * the matching paint and fail closed for this tap. */
    if ((currentSnapshot.inGame != 0) !=
        (__atomic_load_n(&sVisibleInGame, __ATOMIC_ACQUIRE) != 0)) {
        RequestRefresh();
        return;
    }
    if (currentSnapshot.inGame) {
        Port_SecondScreen_OnTap(x, y, longPress);
        RequestRefresh();
        return;
    }

    if (!__atomic_load_n(&sIdleSettingsOpen, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&sIdleSettingsOpen, 1, __ATOMIC_RELEASE);
        RequestRefresh();
        return;
    }

    if (__atomic_load_n(&sIdleBackVisible, __ATOMIC_ACQUIRE) &&
        IsIdleBackTap(x, y, 320, 240)) {
        __atomic_store_n(&sIdleSettingsOpen, 0, __ATOMIC_RELEASE);
        RequestRefresh();
        return;
    }

    Port_SecondScreen_OnTap(x, y, longPress);
    RequestRefresh();
}

int Port_SecondScreen_3DS_NeedsRefresh(void) {
    const uint32_t requested = __atomic_load_n(&sRefreshRequested, __ATOMIC_ACQUIRE);
    const uint32_t painted = __atomic_load_n(&sRefreshPainted, __ATOMIC_ACQUIRE);
    return requested != painted;
}
