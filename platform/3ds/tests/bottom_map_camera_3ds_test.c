/* Compile the production wrapper so the scheduler and camera publication
 * are exercised together, including the static-skip regression. */
#include "../source/port_second_screen_3ds.c"
#include <assert.h>
#include <stdio.h>

void Port_SecondScreen_3DS_LockUI(void) {}
void Port_SecondScreen_3DS_UnlockUI(void) {}
bool Platform3DS_IsNew3DS(void) { return true; }
bool Port_Config_BottomMapSkip(void) { return true; }


static uint32_t frameArt[240*160];
static int artAvailable = 1;
const uint32_t* Port_SecondScreenWorldMap_GetFrameImage(int32_t* w, int32_t* h) {
    *w = 240; *h = 160; return artAvailable ? frameArt : NULL;
}
static void Glide(float x, float y, float scale) {
    float start = sCam.scale;
    unsigned paints = 0;
    SecondScreenSnapshot snap = {0};
    snap.inGame = snap.hasWorldMap = 1;
    do {
        AdvanceMapCamera(x, y, scale);
        ++paints;
        assert(sCam.scale >= fminf(start, scale));
        assert(sCam.scale <= fmaxf(start, scale));
        if (sMapCameraMoving) {
            /* Equal animation ticks deliberately eliminate marker animation:
             * the camera alone must keep the production scheduler painting. */
            assert(Port_SecondScreen_3DS_NeedsPeriodicRefresh(&snap, 1, 1, 320, 240));
        }
        assert(paints < 64);
    } while (sMapCameraMoving);
    assert(paints > 1 && sCam.x == x && sCam.y == y && sCam.scale == scale);
    assert(!Port_SecondScreen_3DS_NeedsPeriodicRefresh(&snap, 1, 1, 320, 240));
    printf("glide settled in %u paints\n", paints);
}

int main(void) {
    uint32_t pixels[322 * 240];
    for (unsigned i = 0; i < sizeof(pixels)/sizeof(*pixels); ++i) pixels[i] = 0x12345678;
    sTapTargetCount = 7; sUi.mapLive = 1; sUi.regionState = SS_REGION_VIEW;
    sIdleSettingsOpen = 1;
    PaintIntroCinema(pixels, 320, 240, 322);
    assert(!sTapTargetCount && !sUi.mapLive && !sIdleSettingsOpen);
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) assert(pixels[y*322+x] == RGB(0, 0, 0));
        assert(pixels[y*322+320] == 0x12345678 && pixels[y*322+321] == 0x12345678);
    }
    SSurf surface = {pixels, 320, 240, 322};
    sCam.valid = sLastFix.valid = sUi.mapLive = 1;
    sUi.regionState = SS_REGION_VIEW;
    PaintUnavailableWorldMap(&surface, 3, 3, 245, 206);
    assert(!sCam.valid && !sLastFix.valid && !sUi.mapLive && sUi.regionState == SS_REGION_OFF);
    /* Only decoded frame pixels may replace the backdrop. The smaller,
     * left-shifted artwork still leaves both panel edges unclipped. */
    float scale = WholeMapScale(242,203);
    assert(fabsf(scale - (242.0f/207)*.97f*.98f) < .00001f);
    float shift = (WholeMapCenterX(242,scale) - (WMAP_CROP_X0+WMAP_CROP_X1)*.5f)*scale;
    assert(fabsf(shift - 2.42f) < .00001f);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 320; ++x)
            assert(pixels[y*322+x] == 0 || pixels[y*322+x] == RGB(0,0,0));
    assert(pixels[100*322+3] == RGB(0,0,0));
    assert(pixels[100*322+244] == RGB(0,0,0));
    assert(pixels[100*322+100] == 0);
    artAvailable = 0;
    pixels[100*322+100] = 0xffabcdef;
    PaintUnavailableWorldMap(&surface, 3,3,245,206);
    assert(pixels[100*322+100] == 0xffabcdef); /* Preserve backdrop while ROM data loads. */
    artAvailable = 1;
    ResetIdleOnlyState();
    AdvanceMapCamera(120, 100, .6f);
    assert(!sMapCameraMoving);
    Glide(60, 170, 1.26f);
    Glide(120, 100, .6f);
    AdvanceMapCamera(60, 170, 1.26f); /* Reverse before reaching the target. */
    Glide(120, 100, .6f);
    AdvanceMapCamera(60, 170, 1.26f);
    ResetIdleOnlyState();
    assert(!sCam.valid && !sMapCameraMoving);
    SecondScreenSnapshot before = {0}, after = {0};
    before.inGame = after.inGame = 1;
    after.introCinema = 1;
    sUi.tab = SS_TAB_SETTINGS;
    assert(Port_SecondScreen_3DS_SnapshotChangeNeedsRefresh(&before, &after, 1));
    assert(Port_SecondScreen_3DS_SnapshotChangeNeedsRefresh(&after, &before, 1));
    assert(!Port_SecondScreen_3DS_NeedsPeriodicRefresh(&after, 10, 0, 320, 240));
    puts("bottom_map_camera_3ds_test: ALL PASS");
    return 0;
}
