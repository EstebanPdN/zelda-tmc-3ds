/*
 * Second-screen compositor (AYN Thor bottom panel).
 *
 * Layout is a port of the sibling zelda3-android mod's bottom screen — the
 * design the project treats as reference — dressed in TMC's own art:
 *
 *   - MAP area top-left, full bleed (no inner box): outdoors the decoded
 *     Hyrule map with a gliding follow-cam. ZOOM steps between that close
 *     view and the whole of Hyrule; from either, a tap on a map tile
 *     brackets it and opens the game's own enlarged regional map, which
 *     BACK closes. Windcrest warps draw as pins. Taps on the map never
 *     change the zoom — the chip does that. In dungeons the authentic
 *     floor map with tappable floor plaques (preview auto-returns to
 *     Link's floor) and a name banner.
 *   - SIDEBAR right: hearts on top, the game's contextual R prompt under
 *     them (the cue a hidden top HUD would otherwise cost), the A/B equip
 *     rings in the middle (tap a ring to arm it, then tap an item to
 *     assign that slot), the rupee/keys chip at the bottom.
 *   - TAB BAR bottom: [QUEST][MAP][ITEMS] plus a square settings button,
 *     all in the pause menu's own button plate and all ending well above
 *     the Android gesture zone.
 *   - ITEMS / QUEST / SETTINGS tabs replace the map area with menu-style
 *     panels: the 4x4 equip grid, the quest-status screen, and the
 *     second screen's own toggles (persisted via port_runtime_config).
 *
 * Every metric scales with u = min(w,h)/720 — the same rule (and largely
 * the same constants) as the reference implementation, so the layout
 * survives both the wide dev surface and the Thor's near-square panel.
 *
 * The compositor core (Port_SecondScreen_PaintInto + Port_SecondScreen_OnTap
 * + all UI state) is platform-agnostic C compiled everywhere, so a host
 * harness can render and drive the exact layout the device shows; only the
 * ANativeWindow plumbing and the 20 Hz render thread are Android-only.
 * All art comes from the runtime-decoded theme/render/worldmap/dungeonmap
 * modules — no baked game pixels; every decoded element has a neutral
 * procedural fallback so the panel renders before the ROM is parsed.
 *
 * In-game dressing is the pause menu's own: the Ezlo-doodle parchment
 * backdrop, the carved stone slab as panel plate, its recessed wells as
 * cells/rows, the message chips for headers, and the game's message font
 * for every label (dark ink on the light plates, white on chips — the
 * menu's exact text schemes). The cinema screen is always dark, and the
 * backdrop alone is settable — parchment, the same cream without the
 * doodle lattice, or that same near-black — because the doodles are busy
 * behind a map and a vitals column.
 */

#include "port_second_screen.h"
#include "port_second_screen_dungeonmap.h"
#include "port_second_screen_quest.h"
#include "port_second_screen_render.h"
#include "port_second_screen_state.h"
#include "port_second_screen_theme.h"
#include "port_second_screen_worldmap.h"

#include "port_runtime_config.h"
#include "port_widescreen.h" /* PORT_VIEW_WIDTH: whether this build has a wide path at all */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef TMC_3DS
#include "platform_3ds.h"

extern void Port_PPU_3DS_WriteQuickDump(void);
extern double Port_PPU_3DS_CurrentFps(void);
extern double Port_PPU_3DS_AverageFps(void);
#endif

#ifndef TMC_PORT_VERSION
#define TMC_PORT_VERSION "DEV"
#endif

/* Buffer pixel layout is RGBA_8888: as a little-endian u32 that's
 * A<<24 | B<<16 | G<<8 | R (same convention Rgb555ToRgba8888 in
 * port_second_screen_render.c emits). Build every color through this so
 * channel order mistakes can't creep in per call site. */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* In-game the panel dresses as a screen OF the pause menu: parchment
 * backdrop, stone slab plates, message chips and the message font, all
 * decoded from ROM by port_second_screen_theme.c. The only colors of our
 * own are the cinema screen's (kept deliberately dark and quiet) and the
 * four-element hues. */
/* Idle: near-black. Shared with the DARK panel backdrop (the theme owns
 * the literal) so the two dark states of this panel are one black. */
#define COL_IDLE_BG SS_BACKDROP_DARK_RGBA
#define COL_ELEM_EARTH RGB(0x58, 0xA0, 0x48)
#define COL_ELEM_FIRE RGB(0xD8, 0x58, 0x28)
#define COL_ELEM_WATER RGB(0x40, 0x70, 0xD0)
#define COL_ELEM_WIND RGB(0xC8, 0xD0, 0xD8)

/* Item ids used for icon/ammo special cases — transcribed from
 * include/item_ids.h (kept local so this file stays engine-header-free). */
#define ITEMID_BOMBS 0x07
#define ITEMID_REMOTE_BOMBS 0x08
#define ITEMID_BOW 0x09
#define ITEMID_LIGHT_ARROW 0x0A
#define ITEMID_BOTTLE1 0x1C
#define ITEMID_DUNGEON_MAP 0x50
#define ITEMID_COMPASS 0x51
#define ITEMID_BIG_KEY 0x52
#define ITEMID_KINSTONE 0x5C
/* Quest-screen ids, same transcription. */
#define ITEMID_SMITH_SWORD 0x01
#define ITEMID_QST_TINGLE_TROPHY 0x3D
#define ITEMID_QST_CARLOV_MEDAL 0x3E
#define ITEMID_SHELLS 0x3F
#define ITEMID_EARTH_ELEMENT 0x40 /* +1 fire, +2 water, +3 wind */
#define ITEMID_GRIP_RING 0x44     /* +1 bracelets, +2 flippers */
#define ITEMID_KINSTONE_BAG 0x67

/* Dungeon names + top-floor numbers for the plaque labels, transcribed
 * from gDungeonLayouts / gDungeonFloorMetadatas (src/common.c): display
 * row i (topmost first) is floor highest-2-i, positive = "<n>F", else
 * "B<1-n>" — e.g. Deepwood {3 floors, highest 3} = 1F, B1, B2. */
static const char* const kDungeonNames[7] = {
    NULL,
    "Deepwood Shrine",
    "Cave of Flames",
    "Fortress of Winds",
    "Temple of Droplets",
    "Palace of Winds",
    "Dark Hyrule Castle",
};
static const int8_t kDungeonTopFloor[7] = { 2, 3, 3, 5, 2, 7, 5 };

/* ------------------------------------------------------------------ */
/*  UI state shared with the tap handler                               */
/* ------------------------------------------------------------------ */

enum { SS_TAB_MAP = 0, SS_TAB_ITEMS, SS_TAB_QUEST, SS_TAB_SETTINGS };

/* What a tap target does when hit. arg meaning per action: item id,
 * tab id, ring (1 = A, 2 = B), plaque display-floor index, settings row.
 * SS_ACT_MAPVIEW is the map's own back affordance (out of the region view)
 * and carries no argument; SS_ACT_MAPZOOM steps the world map's zoom
 * between the follow cam and the whole of Hyrule. */
enum {
    SS_ACT_ITEM = 1,
    SS_ACT_TAB,
    SS_ACT_RING,
    SS_ACT_PLAQUE,
    SS_ACT_SETTING,
    SS_ACT_MAP,
    SS_ACT_MAPVIEW,
    SS_ACT_MAPZOOM,
    SS_ACT_QUESTVIEW, /* arg: which quest screen to show */
    SS_ACT_SETTINGS_PAGE,
    SS_ACT_SETTINGS_BACK,
    SS_ACT_DEVELOPER_DUMP,
    SS_ACT_RANDO_CANCEL,
    SS_ACT_RANDO_CONFIRM,
};

enum {
    SS_SETTINGS_ROOT = 0,
    SS_SETTINGS_SCREEN,
    SS_SETTINGS_GAMEPLAY,
    SS_SETTINGS_DEVELOPER,
    SS_SETTINGS_OVERLAY,
    SS_SETTINGS_RANDOMIZER,
};

/* Settings rows, top to bottom. The second-screen-only toggles persist
 * through their Port_Config accessors alone; the port-wide rows reuse the
 * exact live-apply calls the imgui F8 menu makes (port_imgui_menu.cpp) so
 * there is a single mechanism per setting, not parallel state. */
enum {
    SS_SET_TOP_HUD = 0,   /* hide_top_hud (engine gate ships separately) */
    SS_SET_WIDESCREEN,    /* widescreen_enabled; absent from a native-width build */
    SS_SET_FOLLOW,
    SS_SET_CRESTS,
    SS_SET_FLOOR_RETURN,
    SS_SET_TURBO,
    SS_SET_VOLUME,        /* master_volume, cycles 0/25/50/75/100% */
    SS_SET_AUTOSAVE,      /* autosave_enabled via Port_QuickSave */
    SS_SET_COLOR_CORRECTION, /* color_correction + live PPU toggle */
    SS_SET_SHOW_FPS,      /* show_fps (overlay reads it per frame) */
    SS_SET_HOLD_ADVANCE,  /* hold_advance_text (message.c reads per frame) */
    SS_SET_RANDOMIZER,
    SS_SET_BACKDROP,      /* second_screen_backdrop: cycles SS_BACKDROP_* */
    SS_SET_SWAP_SCREENS,  /* second_screen_swap; applied at the next launch */
#ifdef TMC_3DS
    SS_SET_ASPECT_RATIO,
    SS_SET_DISPLAY_STYLE,
#endif
    SS_SET_COUNT
};

/* Port-wide live-apply entry points, declared like port_imgui_menu.cpp
 * declares them (this file stays engine-header-free): the audio mixer's
 * live volume, the quick-save autosaver's live toggle, and the PPU's live
 * color-correction switch. All extern "C" on their defining side. */
extern void Port_Audio_SetMasterVolume(float volume);
extern float Port_Audio_GetMasterVolume(void);
extern int Port_QuickSave_AutoEnabled(void);
extern void Port_QuickSave_SetAutoEnabled(int enabled);
extern void Port_Config_SetAutosaveEnabled(bool enabled);
extern void Port_PPU_SetColorCorrection(bool enabled);
extern bool Port_Config_WidescreenEnabled(void);
extern void Port_Config_SetWidescreenEnabled(bool enabled);
extern bool Port_Config_GetSecondScreenSwap(void);
extern void Port_Config_SetSecondScreenSwap(bool on);

/* Which display the game's own window landed on this launch, reported by
 * the Android shell (SecondScreenManager) once it knows. Written once from
 * the Java main thread before the panel has a surface, read afterwards by
 * the paint/tap threads — a plain int is enough, no torn value exists. */
static int sGameOnSecondaryDisplay = 0;

void Port_SecondScreen_SetGameOnSecondaryDisplay(int onSecondary) {
    sGameOnSecondaryDisplay = onSecondary ? 1 : 0;
}

int Port_SecondScreen_GameOnSecondaryDisplay(void) {
    return sGameOnSecondaryDisplay;
}

#define SS_MAX_TARGETS 48
#define SS_NO_FLOOR (-128)
#define SS_FLOOR_RETURN_TICKS 120 /* 6 s at the 20 Hz paint rate */

/* Map-tile zoom states. The bracket beat exists so the zoom reads without
 * a cursor: the tapped tile is outlined on the whole map for a moment
 * (0.35 s at the 20 Hz paint rate) before the regional map replaces it. */
enum { SS_REGION_OFF = 0, SS_REGION_BRACKET, SS_REGION_VIEW };
#define SS_REGION_BRACKET_TICKS 7

typedef struct {
    int32_t x0, y0, x1, y1;
    uint8_t action;
    uint8_t arg;
} TapTarget;

typedef struct {
    TapTarget t[SS_MAX_TARGETS];
    int n;
} TargetList;

/* Tap targets + interactive UI state of the most recently painted frame.
 * The layout moves with surface size, so hit boxes are captured at paint
 * time and consumed by Port_SecondScreen_OnTap on the JNI thread; the same
 * lock guards the small state that thread mutates (active tab, map view,
 * region zoom...). Own mutex on Android (not the window mutex): taps must
 * never wait out a whole frame paint. The 3DS uses its platform lock because
 * BottomWorkerMain paints concurrently with touch dispatch on the main thread.
 * Other host harnesses remain single-threaded, so the lock compiles away. */
static TapTarget sTapTargets[SS_MAX_TARGETS];
static int sTapTargetCount = 0;

static struct {
    uint8_t tab;
    uint8_t settingsPage;
    uint8_t wholeMap;   /* map tab: whole-Hyrule view instead of follow cam */
    uint8_t armedRing;  /* 0 none, 1 = next item tap assigns A, 2 = B */
    int8_t floorPreview; /* plaque-selected display floor, SS_NO_FLOOR = live */
    uint32_t floorPreviewTick;
    int8_t previewBaseFloor; /* Link's floor when the preview was taken */
    uint8_t previewDungeon;
    /* Stamped by paint for the tap handler's decisions. */
    int8_t playerFloorDisp;
    uint8_t curDungeon;
    uint32_t lastTick;
    /* Overworld map transform of the last painted frame, so a tap can be
     * turned back into map-image coordinates (region picking). */
    uint8_t mapLive;
    float mapOx, mapOy, mapScale, mapU;
    int32_t mapImgW, mapImgH;
    /* Map-tile zoom: SS_REGION_* state, the picked tile's region id, and
     * its rect in world-map image pixels (what the bracket outlines). */
    uint8_t regionState;
    int32_t regionId;
    int32_t regionX0, regionY0, regionX1, regionY1;
    uint32_t regionTick;
    /* Set by paint when the zoom grid answers, so the tap handler knows
     * whether a map tap can zoom at all (stub -> plain follow/whole
     * toggle, exactly the behavior before the zoom existed). */
    uint8_t regionGridReady;
    /* Quest tab: which of its screens is up — the status screen itself or
     * one of the two lists it opens, the same step in the pause menu. */
    uint8_t questView;
    uint32_t dumpFlashUntil;
    uint8_t randoConfirmActive;
    uint8_t randoDesired;
} sUi = { .floorPreview = SS_NO_FLOOR, .playerFloorDisp = SS_NO_FLOOR };

/* sUi.questView */
enum { SS_QUEST_MAIN = 0, SS_QUEST_KINSTONES, SS_QUEST_TECHNIQUES };

#ifdef __ANDROID__
#include <pthread.h>
static pthread_mutex_t sTapTargetMutex = PTHREAD_MUTEX_INITIALIZER;
#define UI_LOCK() pthread_mutex_lock(&sTapTargetMutex)
#define UI_UNLOCK() pthread_mutex_unlock(&sTapTargetMutex)
#elif defined(TMC_3DS)
#include "port_second_screen_sync_3ds.h"
/* Keep libctru's typedefs out of this engine translation unit: region.h and
 * libctru intentionally use different underlying C types for GBA-style u32.
 * The platform adapter owns the LightLock and exposes a type-neutral API. */
#define UI_LOCK() Port_SecondScreen_3DS_LockUI()
#define UI_UNLOCK() Port_SecondScreen_3DS_UnlockUI()
#else
#define UI_LOCK() ((void)0)
#define UI_UNLOCK() ((void)0)
#endif

/* Last successful world-map position fix. While Link is indoors (houses,
 * caves — anywhere LocatePlayer has no answer) the map keeps showing this
 * frozen fix, zelda3-android's doorway-marker behavior. Paint-thread
 * private; cleared when gameplay ends. */
static struct {
    int valid;
    int32_t mapX, mapY;
} sLastFix = { 0, 0, 0 };

/* Follow-cam state: view center (map-image coords) and zoom glide toward
 * their targets each frame, which animates both the follow pan and the
 * follow<->whole toggle. Paint-thread private. */
static struct {
    int valid;
    float x, y, scale;
} sCam = { 0, 0, 0, 0 };

/* ------------------------------------------------------------------ */
/*  Surface + primitives                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t* px;
    int32_t w, h, stride;
} SSurf;

static void FillRect(const SSurf* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = s->px + (size_t)y * (size_t)s->stride;
        for (int32_t x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

static void OutlineRect(const SSurf* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thickness,
                        uint32_t color) {
    FillRect(s, x0, y0, x1, y0 + thickness, color);
    FillRect(s, x0, y1 - thickness, x1, y1, color);
    FillRect(s, x0, y0, x0 + thickness, y1, color);
    FillRect(s, x1 - thickness, y0, x1, y1, color);
}

/* Channel-wise mix of two panel colors, t/256 toward b — used to derive
 * plate/well tones from the decoded window palette instead of inventing
 * new hues. */
static uint32_t MixColor(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t r = ((a & 0xFF) * (256 - t) + (b & 0xFF) * t) >> 8;
    uint32_t g = (((a >> 8) & 0xFF) * (256 - t) + ((b >> 8) & 0xFF) * t) >> 8;
    uint32_t bl = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
    return RGB(r, g, bl);
}

/* Nearest-neighbor integer-scale blit of a cached theme sprite; alpha-0
 * source pixels are skipped so sprites sit on whatever is behind them. */
static void BlitSprite(const SSurf* s, const SecondScreenThemeSprite* spr, int32_t x, int32_t y,
                       int32_t scale) {
    if (spr == NULL || spr->px == NULL || scale < 1) {
        return;
    }
    for (int32_t sy = 0; sy < spr->h; sy++) {
        for (int32_t sx = 0; sx < spr->w; sx++) {
            uint32_t c = spr->px[sy * spr->w + sx];
            if (c == 0) {
                continue;
            }
            int32_t dy0 = y + sy * scale;
            for (int32_t ey = 0; ey < scale; ey++) {
                int32_t dy = dy0 + ey;
                if (dy < 0 || dy >= s->h) {
                    continue;
                }
                uint32_t* row = s->px + (size_t)dy * (size_t)s->stride;
                int32_t dx0 = x + sx * scale;
                for (int32_t ex = 0; ex < scale; ex++) {
                    int32_t dx = dx0 + ex;
                    if (dx >= 0 && dx < s->w) {
                        row[dx] = c;
                    }
                }
            }
        }
    }
}

/* Chunky filled diamond (the four-element motif); r is the half-height. */
static void FillDiamond(const SSurf* s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t i = -r; i <= r; i++) {
        int32_t half = r - (i < 0 ? -i : i);
        FillRect(s, cx - half, cy + i, cx + half + 1, cy + i + 1, color);
    }
}

/* Annulus between rIn..rOut — the equip rings and their pulse halo. */
static void FillRing(const SSurf* s, int32_t cx, int32_t cy, int32_t rOut, int32_t rIn, uint32_t color) {
    int32_t rO2 = rOut * rOut, rI2 = rIn * rIn;
    for (int32_t dy = -rOut; dy <= rOut; dy++) {
        int32_t y = cy + dy;
        if (y < 0 || y >= s->h) continue;
        uint32_t* row = s->px + (size_t)y * (size_t)s->stride;
        for (int32_t dx = -rOut; dx <= rOut; dx++) {
            int32_t x = cx + dx;
            if (x < 0 || x >= s->w) continue;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 <= rO2 && d2 >= rI2) {
                row[x] = color;
            }
        }
    }
}

static void FillCircle(const SSurf* s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    FillRing(s, cx, cy, r, 0, color);
}

/* Horizontal inset of a rounded rect's edge at scanline center `yc`. */
static float RoundRectInset(float y0, float y1, float r, float yc) {
    float dy = 0;
    if (yc < y0 + r) {
        dy = y0 + r - yc;
    } else if (yc > y1 - r) {
        dy = yc - (y1 - r);
    }
    if (dy <= 0) {
        return 0;
    }
    float t2 = r * r - dy * dy;
    return r - (t2 > 0 ? sqrtf(t2) : 0);
}

static float ClampRadius(float x0, float y0, float x1, float y1, float r) {
    if (r < 0) r = 0;
    if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;
    if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
    return r;
}

/* Filled rounded rectangle (quarter-circle corners) — the base stroke of
 * the menu button plate; nesting insets gives keyline / keyline / fill
 * like the game's own SLEEP/SAVE buttons. */
static void FillRoundRect(const SSurf* s, float x0, float y0, float x1, float y1, float r,
                          uint32_t color) {
    r = ClampRadius(x0, y0, x1, y1, r);
    for (int32_t y = (int32_t)y0; y < (int32_t)y1; y++) {
        float dx = RoundRectInset(y0, y1, r, y + 0.5f);
        FillRect(s, (int32_t)(x0 + dx), y, (int32_t)(x1 - dx + 0.5f), y + 1, color);
    }
}

/* Rounded-rect keyline of thickness t, drawn without touching what it
 * encloses — so it can be laid over already-composed art (the active
 * tab's gold line over the button plate, the map's tile bracket). */
static void StrokeRoundRect(const SSurf* s, float x0, float y0, float x1, float y1, float r, float t,
                            uint32_t color) {
    if (t <= 0 || x1 - x0 <= 2 * t || y1 - y0 <= 2 * t) {
        return;
    }
    float ro = ClampRadius(x0, y0, x1, y1, r);
    float ri = ClampRadius(x0 + t, y0 + t, x1 - t, y1 - t, r - t);
    for (int32_t y = (int32_t)y0; y < (int32_t)y1; y++) {
        float yc = y + 0.5f;
        float dxo = RoundRectInset(y0, y1, ro, yc);
        if (yc < y0 + t || yc > y1 - t) {
            FillRect(s, (int32_t)(x0 + dxo), y, (int32_t)(x1 - dxo + 0.5f), y + 1, color);
            continue;
        }
        float dxi = t + RoundRectInset(y0 + t, y1 - t, ri, yc);
        FillRect(s, (int32_t)(x0 + dxo), y, (int32_t)(x0 + dxi + 0.5f), y + 1, color);
        FillRect(s, (int32_t)(x1 - dxi), y, (int32_t)(x1 - dxo + 0.5f), y + 1, color);
    }
}

/* Draws a '#' grid as square pixels of side `cell`, top-left at (x, y). */
static void DrawPixelArt(const SSurf* s, const char* const* rows, int nRows, float x, float y, float cell,
                         uint32_t color) {
    for (int ry = 0; ry < nRows; ry++) {
        const char* row = rows[ry];
        for (int rx = 0; row[rx] != '\0'; rx++) {
            if (row[rx] == '#') {
                FillRect(s, (int32_t)(x + rx * cell), (int32_t)(y + ry * cell),
                         (int32_t)(x + (rx + 1) * cell), (int32_t)(y + (ry + 1) * cell), color);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Label font                                                         */
/* ------------------------------------------------------------------ */

/* Labels render in the game's own message font (decoded from ROM by the
 * theme module). This original 5x7 caps face exists only as the pre-ROM
 * stand-in: the first frames before Port_LoadRom finishes, when no glyph
 * data exists to decode yet. One byte per row, bit 4 = leftmost column. */
static const uint8_t kFont5x7[][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
    { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* 9 */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, /* D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
    { 0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0F }, /* G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, /* W */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* Z */
    { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 }, /* - */
    { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, /* / */
};

static int GlyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a'); /* fold: no lowercase face */
    if (c == '-') return 36;
    if (c == '/') return 37;
    return -1; /* space and anything unknown: advance only */
}

static void DrawChar(const SSurf* s, int32_t x, int32_t y, int32_t scale, char c, uint32_t color) {
    int gi = GlyphIndex(c);
    if (gi < 0) return;
    for (int32_t py = 0; py < 7; py++) {
        uint8_t bits = kFont5x7[gi][py];
        for (int32_t px = 0; px < 5; px++) {
            if (bits & (0x10u >> px)) {
                FillRect(s, x + px * scale, y + py * scale, x + (px + 1) * scale, y + (py + 1) * scale,
                         color);
            }
        }
    }
}

/* Text with the 1px-scaled dark outline light glyphs need to read on any
 * backdrop (8-neighbor dark copies underneath, then the fill on top).
 * Returns the advance. */
static int32_t DrawTextStr(const SSurf* s, const char* str, int32_t x, int32_t y, int32_t scale,
                           uint32_t color, uint32_t outline) {
    int32_t cx = x;
    for (const char* p = str; *p; p++) {
        for (int32_t oy = -1; oy <= 1; oy++) {
            for (int32_t ox = -1; ox <= 1; ox++) {
                if (ox || oy) {
                    DrawChar(s, cx + ox * scale, y + oy * scale, scale, *p, outline);
                }
            }
        }
        cx += 6 * scale;
    }
    cx = x;
    for (const char* p = str; *p; p++) {
        DrawChar(s, cx, y, scale, *p, color);
        cx += 6 * scale;
    }
    return cx - x;
}

static int32_t TextWidthPx(const char* str, int32_t scale) {
    return (int32_t)strlen(str) * 6 * scale - scale;
}

/* ------------------------------------------------------------------ */
/*  Menu text (the game's stylized banner font)                        */
/* ------------------------------------------------------------------ */

/* Every panel label renders in the fat banner lettering (theme bank 8 —
 * the "South Hyrule Field" face: white body, silver shade, navy outline,
 * recolored per SS_TEXT_* on light surfaces). Metrics are in that font's
 * units: a glyph box is 16 rows tall at scale ms; ink fills nearly the
 * whole box, so vertical centering uses the box middle at 8*ms. The 5x7
 * stand-in only appears pre-ROM; it maps ms onto a similar visual size.
 * The face covers A-Z a-z 0-9 - . , : ' ! ? — keep strings inside that. */
#define MENU_TEXT_BOX 16

static int32_t FallbackScale5x7(int32_t ms) {
    int32_t f = ms * 2;
    return f < 1 ? 1 : f;
}

static int32_t MenuTextWidth(const char* str, int32_t ms) {
    int32_t w = Port_SecondScreenTheme_BigTextWidth(str, ms);
    if (w == 0 && str != NULL && *str != '\0') {
        w = TextWidthPx(str, FallbackScale5x7(ms));
    }
    return w;
}

/* yTop is the glyph-box top. Returns the advance. */
static int32_t MenuTextDraw(const SSurf* s, const char* str, int32_t x, int32_t yTop, int32_t ms,
                            int style) {
    int32_t adv =
        Port_SecondScreenTheme_DrawBigText(s->px, s->w, s->h, s->stride, x, yTop, ms, style, str);
    if (adv == 0 && str != NULL && *str != '\0') {
        /* Pre-ROM stand-in in the matching palette role. */
        static const int kColorId[SS_TEXT_STYLE_COUNT] = { SSC_MENU_INK, SSC_MENU_WHITE, SSC_MENU_RED,
                                                           SSC_RUPEE_GREEN, SSC_BANNER_NAVY };
        uint32_t color = Port_SecondScreenTheme_Color(kColorId[style >= 0 && style < SS_TEXT_STYLE_COUNT
                                                                  ? style
                                                                  : SS_TEXT_INK]);
        uint32_t outline = (style == SS_TEXT_INK || style == SS_TEXT_NAVY)
                               ? Port_SecondScreenTheme_Color(SSC_MENU_CREAM)
                               : Port_SecondScreenTheme_Color(SSC_MENU_BLACK);
        adv = DrawTextStr(s, str, x, yTop + 3 * ms, FallbackScale5x7(ms), color, outline);
    }
    return adv;
}

/* Centered helper: centers the string's caps on (cx, cy). */
static void MenuTextCentered(const SSurf* s, const char* str, float cx, float cy, int32_t ms, int style) {
    int32_t w = MenuTextWidth(str, ms);
    MenuTextDraw(s, str, (int32_t)(cx - w / 2.0f), (int32_t)(cy - 8 * ms), ms, style);
}

/* Panel header: the pause screens' red name chip, centered at cx with
 * its top at topY (over the slab's carved top band, like the game hangs
 * its own screen-name chip). */
static void DrawPanelHeaderChip(const SSurf* s, float cx, float topY, const char* title, int32_t ms,
                                float u) {
    int32_t tw = MenuTextWidth(title, ms);
    float h = MENU_TEXT_BOX * ms + 24 * u;
    float x0 = cx - tw / 2.0f - 24 * u, x1 = cx + tw / 2.0f + 24 * u;
    int32_t cts = (int32_t)(h / 26.0f);
    if (cts < 1) cts = 1;
    Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)topY,
                                    (int32_t)(x1 - x0), (int32_t)h, cts, SS_CHIP_RED);
    MenuTextCentered(s, title, cx, topY + h / 2.0f, ms, SS_TEXT_WHITE);
}

/* ------------------------------------------------------------------ */
/*  HUD-font numbers                                                   */
/* ------------------------------------------------------------------ */

/* Right-aligned counter in the HUD's own 8x16 digit font (yellow variant
 * for maxed counters, exactly like RenderDigits). Returns the left edge
 * reached so callers can place an icon in front. minDigits pads with
 * leading zeros the way the HUD pads the rupee counter. */
static int32_t DrawHudNumber(const SSurf* s, int32_t xRight, int32_t y, int32_t scale, uint32_t value,
                             int32_t minDigits, int yellow) {
    int32_t x = xRight;
    int32_t drawn = 0;
    do {
        const SecondScreenThemeSprite* d =
            Port_SecondScreenTheme_Get((yellow ? SST_DIGIT_YELLOW_0 : SST_DIGIT_WHITE_0) + (int)(value % 10));
        x -= 8 * scale;
        if (d != NULL) {
            BlitSprite(s, d, x, y, scale);
        } else {
            char digit[2] = { (char)('0' + value % 10), 0 };
            DrawTextStr(s, digit, x + scale, y + 2 * scale, scale + scale / 2,
                        Port_SecondScreenTheme_Color(SSC_MENU_INK),
                        Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
        }
        value /= 10;
        drawn++;
    } while (value != 0 || drawn < minDigits);
    return x;
}

/* Two-glyph ammo count in the HUD's small 8x8 font (tens glyph is
 * right-aligned in its tile, ones left-aligned — sub_0801C2F0's pair). */
static void DrawAmmoCount(const SSurf* s, int32_t x, int32_t y, int32_t scale, uint32_t value) {
    if (value > 99) {
        value = 99;
    }
    const SecondScreenThemeSprite* tens = Port_SecondScreenTheme_Get(SST_SMALL_TENS_0 + (int)(value / 10));
    const SecondScreenThemeSprite* ones = Port_SecondScreenTheme_Get(SST_SMALL_ONES_0 + (int)(value % 10));
    if (tens == NULL || ones == NULL) {
        return; /* purely decorative — skip rather than substitute */
    }
    BlitSprite(s, tens, x, y, scale);
    BlitSprite(s, ones, x + 8 * scale, y, scale);
}

/* One production path for both item-grid and equipped-ring counters.  The
 * return value is intentionally observable by the host compositor test so
 * a genuine zero-ammo badge cannot be confused with an unsupported item. */
static int DrawItemAmmoCount(const SSurf* s, const SecondScreenSnapshot* snap, uint8_t itemId,
                             int32_t x, int32_t y, int32_t scale) {
    int16_t ammo = Port_SecondScreenItemAmmo(snap, itemId);
    if (ammo < 0) {
        return 0;
    }
    DrawAmmoCount(s, x, y, scale, (uint32_t)ammo);
    return 1;
}

#ifdef PORT_SECOND_SCREEN_TEST
int Port_SecondScreen_TestPaintEquippedAmmo(uint32_t* pixels, int32_t width, int32_t height,
                                            int32_t stride, const SecondScreenSnapshot* snap,
                                            int32_t scale) {
    SSurf s;
    int painted;

    if (pixels == NULL || snap == NULL || width <= 0 || height <= 0 || stride < width || scale < 1) {
        return 0;
    }
    s.px = pixels;
    s.w = width;
    s.h = height;
    s.stride = stride;
    painted = DrawItemAmmoCount(&s, snap, snap->equippedA, 0, 0, scale);
    painted += DrawItemAmmoCount(&s, snap, snap->equippedB, 20 * scale, 0, scale);
    return painted;
}
#endif

/* ------------------------------------------------------------------ */
/*  Menu button (tab bar + R glyph plate)                              */
/* ------------------------------------------------------------------ */

/* The pause menu's SLEEP/SAVE button, procedurally: pale near-white
 * plate, slate-blue outer keyline, a paler blue keyline inside it, blue
 * lettering. Only the stand-in for Port_SecondScreenTheme_DrawMenuButton
 * until that art decodes — same rect, same height, and `pressed` changes
 * nothing but the plate's shading, so a button never moves or resizes
 * between its states. Every tone is mixed from the decoded banner/menu
 * palette rather than invented. */
static void DrawButtonPlateFallback(const SSurf* s, float x0, float y0, float x1, float y1, int pressed,
                                    int32_t bts) {
    uint32_t navy = Port_SecondScreenTheme_Color(SSC_BANNER_NAVY);
    uint32_t white = Port_SecondScreenTheme_Color(SSC_MENU_WHITE);
    float bt = (float)bts;
    float r = 5.0f * bts;
    /* Pressed reads as the plate sinking: the interior takes a touch more
     * of the keyline's blue and the inner keyline brightens to white. */
    uint32_t keyline = MixColor(navy, white, 96);
    uint32_t inner = pressed ? white : MixColor(white, navy, 40);
    uint32_t fill = MixColor(white, navy, pressed ? 34u : 6u);
    FillRoundRect(s, x0, y0, x1, y1, r, keyline);
    FillRoundRect(s, x0 + bt, y0 + bt, x1 - bt, y1 - bt, r - bt, inner);
    FillRoundRect(s, x0 + 2 * bt, y0 + 2 * bt, x1 - 2 * bt, y1 - 2 * bt, r - 2 * bt, fill);
}

/* One tab-bar-style button: the game's own art when it decodes, the
 * stand-in plate otherwise, plus (for the tab that owns the panel) a thin
 * gold keyline inside the plate edge. Label drawing stays on this side
 * only for the fallback — the authentic call letters its own button. */
static void DrawMenuButton(const SSurf* s, float x0, float y0, float x1, float y1, const char* label,
                           int pressed, int accent, float u, int32_t ts) {
    int32_t bts = (int32_t)((y1 - y0) / 26.0f);
    if (bts < 1) bts = 1;
    if (bts > ts) bts = ts;

    if (!Port_SecondScreenTheme_DrawMenuButton(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                               (int32_t)(x1 - x0), (int32_t)(y1 - y0), label, pressed)) {
        DrawButtonPlateFallback(s, x0, y0, x1, y1, pressed, bts);
        if (label != NULL && label[0] != '\0') {
            int32_t ms = (int32_t)(1.8f * u);
            if (ms < 1) ms = 1;
            MenuTextCentered(s, label, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, ms, SS_TEXT_NAVY);
        }
    }
    if (accent) {
        StrokeRoundRect(s, x0 + 2 * bts, y0 + 2 * bts, x1 - 2 * bts, y1 - 2 * bts, 4.0f * bts,
                        (float)bts, Port_SecondScreenTheme_Color(SSC_GOLD));
    }
}

/* ------------------------------------------------------------------ */
/*  Pin + cog pixel art                                                */
/* ------------------------------------------------------------------ */

/* A windcrest warp point: a little pennant on a pole, foot at the marked
 * spot — drawn on a pixel grid so it sits next to the decoded HUD art
 * instead of looking like a vector overlay. (Original art.) */
static const char* const kPinArt[12] = {
    "########", "########", "#######.", "######..", "#####...", "###.....",
    "##......", "##......", "##......", "##......", "##......", "##......",
};

static void DrawPin(const SSurf* s, float x, float y, float cell, uint32_t color, uint32_t outline) {
    float px = x - 1 * cell, py = y - 12 * cell;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx || dy) DrawPixelArt(s, kPinArt, 12, px + dx * cell, py + dy * cell, cell, outline);
        }
    }
    DrawPixelArt(s, kPinArt, 12, px, py, cell, color);
}

/* Three sliders on a 9x9 grid — the settings-button glyph. A gear shape
 * reads as a blob at this size (same call the reference made). */
static const char* const kCogArt[9] = {
    ".###.....", "#########", ".###.....", ".....###.", "#########",
    ".....###.", "..###....", "#########", "..###....",
};

/* ------------------------------------------------------------------ */
/*  Target list                                                        */
/* ------------------------------------------------------------------ */

static void AddTarget(TargetList* tl, float x0, float y0, float x1, float y1, uint8_t action, uint8_t arg) {
    if (tl->n >= SS_MAX_TARGETS) {
        return;
    }
    TapTarget* t = &tl->t[tl->n++];
    t->x0 = (int32_t)x0;
    t->y0 = (int32_t)y0;
    t->x1 = (int32_t)x1;
    t->y1 = (int32_t)y1;
    t->action = action;
    t->arg = arg;
}

/* ------------------------------------------------------------------ */
/*  Cinema screen                                                      */
/* ------------------------------------------------------------------ */

/* Title screen / file select / cutscenes: nothing to mirror, so go quiet —
 * near-black, a thin dark-gold inset frame, and the four-element diamond
 * cluster breathing on a ~2.4 s sine in the middle. Deliberately minimal:
 * decorated idle cards were rejected on zelda3. */
static void PaintCinema(const SSurf* s, uint32_t tick) {
    FillRect(s, 0, 0, s->w, s->h, COL_IDLE_BG);

    float u = (s->w < s->h ? s->w : s->h) / 720.0f;
    int32_t inset = (int32_t)(12 * u);
    int32_t th = (int32_t)(2 * u) > 0 ? (int32_t)(2 * u) : 1;
    OutlineRect(s, inset, inset, s->w - inset, s->h - inset, th,
                MixColor(Port_SecondScreenTheme_Color(SSC_GOLD), COL_IDLE_BG, 140));

    /* 2.4 s period at the 20 Hz paint rate = 48 ticks. */
    float pulse = 0.55f + 0.30f * sinf((float)(tick % 48) * (6.28318f / 48.0f));
    int32_t cx = s->w / 2;
    int32_t cy = s->h / 2;
    int32_t r = (s->w < s->h ? s->w : s->h) / 32;
    int32_t d = r * 2 + r / 4;
    static const uint32_t kElem[4] = { COL_ELEM_WIND, COL_ELEM_FIRE, COL_ELEM_EARTH, COL_ELEM_WATER };
    static const int32_t kOff[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    for (int e = 0; e < 4; e++) {
        uint32_t c = kElem[e];
        uint32_t rr = (uint32_t)((c & 0xFF) * pulse);
        uint32_t gg = (uint32_t)(((c >> 8) & 0xFF) * pulse);
        uint32_t bb = (uint32_t)(((c >> 16) & 0xFF) * pulse);
        FillDiamond(s, cx + kOff[e][0] * d, cy + kOff[e][1] * d, r, RGB(rr, gg, bb));
    }
}

/* ------------------------------------------------------------------ */
/*  Schematic map (shared fallback)                                    */
/* ------------------------------------------------------------------ */

static void DrawMapMarker(const SSurf* s, int32_t px, int32_t py, int32_t unit, uint32_t color) {
    FillDiamond(s, px, py, unit + 1, Port_SecondScreenTheme_Color(SSC_MENU_INK));
    FillDiamond(s, px, py, unit, color);
}

/* Schematic area map — the styled fallback whenever the authentic art
 * modules report "not ready". Rooms placed by their real in-area geometry
 * (RoomResInfo), toned in the menu's stone/ink palette so it reads as part
 * of the pause-menu theme, not programmer art. In dungeons it obeys the
 * same reveal rule as the real map screen: unvisited rooms only appear
 * once the dungeon map item is owned. */
static void PaintSchematic(const SSurf* s, const SecondScreenSnapshot* snap, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, uint32_t tick, int32_t unit) {
    uint32_t fill = Port_SecondScreenTheme_Color(SSC_MENU_STONE);
    uint32_t dark = Port_SecondScreenTheme_Color(SSC_MENU_INK);
    uint32_t colSeen = fill;
    uint32_t colUnseen = MixColor(fill, dark, 72);
    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0;
    int layoutKnown = !isDungeon || (snap->dungeonItemBits & 1) != 0;

    int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
    for (int i = 0; i < SECOND_SCREEN_MAX_ROOMS; i++) {
        const SecondScreenRoom* r = &snap->rooms[i];
        if (r->w == 0 || r->h == 0) {
            continue;
        }
        if (r->x < minX) minX = r->x;
        if (r->y < minY) minY = r->y;
        if (r->x + r->w > maxX) maxX = r->x + r->w;
        if (r->y + r->h > maxY) maxY = r->y + r->h;
    }
    if (minX >= maxX || minY >= maxY) {
        return;
    }

    float scaleX = (float)(x1 - x0) / (float)(maxX - minX);
    float scaleY = (float)(y1 - y0) / (float)(maxY - minY);
    float scale = scaleX < scaleY ? scaleX : scaleY;
    int32_t ox = x0 + (int32_t)((x1 - x0) - (maxX - minX) * scale) / 2;
    int32_t oy = y0 + (int32_t)((y1 - y0) - (maxY - minY) * scale) / 2;
    int32_t seam = unit > 2 ? unit / 2 : 1;

    for (int i = 0; i < SECOND_SCREEN_MAX_ROOMS; i++) {
        const SecondScreenRoom* r = &snap->rooms[i];
        if (r->w == 0 || r->h == 0) {
            continue;
        }
        bool visited = (snap->visitedMask >> (i & 63)) & 1;
        if (!visited && !layoutKnown) {
            continue; /* no map item -> the layout stays secret, like the real screen */
        }
        int32_t rx0 = ox + (int32_t)((r->x - minX) * scale);
        int32_t ry0 = oy + (int32_t)((r->y - minY) * scale);
        int32_t rx1 = ox + (int32_t)((r->x - minX + r->w) * scale);
        int32_t ry1 = oy + (int32_t)((r->y - minY + r->h) * scale);
        uint32_t roomFill = i == snap->room ? MixColor(Port_SecondScreenTheme_Color(SSC_GOLD), fill, 96)
                                            : visited ? colSeen : colUnseen;
        FillRect(s, rx0, ry0, rx1, ry1, dark);
        FillRect(s, rx0 + seam, ry0 + seam, rx1 - seam, ry1 - seam, roomFill);
    }

    int32_t px = ox + (int32_t)((snap->playerX - minX) * scale);
    int32_t py = oy + (int32_t)((snap->playerY - minY) * scale);
    DrawMapMarker(s, px, py, unit,
                  (tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : Port_SecondScreenTheme_Color(SSC_GOLD));
}

/* ------------------------------------------------------------------ */
/*  Overworld map (MAP tab)                                            */
/* ------------------------------------------------------------------ */

/* Stone-frame bounding box within the decoded 240x160 map-screen
 * composite. The composite replicates the whole GBA screen, so it carries
 * the map screen's OWN parchment margins (plus their doodles) around the
 * frame; rendered over this panel's re-stamped parchment those margins
 * read as a hard halo band (owner feedback). Only the frame+map region is
 * ever sampled. Coordinates measured on the composed image: the frame's
 * outer outline spans x 16..222, y 3..147 inclusive (its rounded corners
 * leave sub-pixel cream slivers that match our backdrop cream anyway). */
#define WMAP_CROP_X0 16
#define WMAP_CROP_Y0 3
#define WMAP_CROP_X1 223
#define WMAP_CROP_Y1 148

/* Nearest-neighbor blit of the map image under transform (ox, oy, scale),
 * clipped to the given rect; samples only the frame crop of the source.
 * Incremental fixed-step sampling: one float add + cast per pixel. */
static void BlitMapRegion(const SSurf* s, const uint32_t* img, int32_t imgW, int32_t imgH, float ox,
                          float oy, float scale, int32_t cx0, int32_t cy0, int32_t cx1, int32_t cy1) {
    int32_t sxMin = WMAP_CROP_X0, syMin = WMAP_CROP_Y0;
    int32_t sxMax = imgW < WMAP_CROP_X1 ? imgW : WMAP_CROP_X1;
    int32_t syMax = imgH < WMAP_CROP_Y1 ? imgH : WMAP_CROP_Y1;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > s->w) cx1 = s->w;
    if (cy1 > s->h) cy1 = s->h;
    if (scale <= 0.0f) return;
    float inv = 1.0f / scale;
    for (int32_t y = cy0; y < cy1; y++) {
        int32_t sy = (int32_t)((y - oy) * inv);
        if (sy < syMin || sy >= syMax) continue;
        const uint32_t* srow = img + (size_t)sy * (size_t)imgW;
        uint32_t* drow = s->px + (size_t)y * (size_t)s->stride;
        float sxf = (cx0 - ox) * inv;
        for (int32_t x = cx0; x < cx1; x++, sxf += inv) {
            int32_t sx = (int32_t)sxf;
            if (sx >= sxMin && sx < sxMax) {
                drow[x] = srow[sx];
            }
        }
    }
}

/* A small dark message chip with a banner-font word — the furniture the
 * game floats over its own map screen. Used for the map's view/back
 * affordance. Returns the rect it covered so callers can make it tappable. */
static void DrawMapChip(const SSurf* s, const char* label, float cx, float cyBottom, float u, float* out) {
    /* Sized as a thumb target rather than as map decoration: this is the only
     * way into a region zoom, and out of the lists the quest tab opens. */
    int32_t ms = (int32_t)(2.6f * u);
    if (ms < 1) ms = 1;
    int32_t tw = MenuTextWidth(label, ms);
    float ch = MENU_TEXT_BOX * ms + 22 * u;
    float x0 = cx - tw / 2.0f - 26 * u, x1 = cx + tw / 2.0f + 26 * u;
    float y1 = cyBottom, y0 = y1 - ch;
    int32_t cts = (int32_t)(ch / 24.0f);
    if (cts < 1) cts = 1;
    Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                    (int32_t)(x1 - x0), (int32_t)(y1 - y0), cts, SS_CHIP_DARK);
    MenuTextCentered(s, label, cx, (y0 + y1) / 2.0f, ms, SS_TEXT_WHITE);
    out[0] = x0;
    out[1] = y0;
    out[2] = x1;
    out[3] = y1;
}

/* The interactive overworld map, full-bleed in the map area: a gliding
 * follow-cam centered on Link, tap to toggle the whole-map fitted view,
 * and from the whole view a tap on a map tile brackets it and zooms into
 * that region; windcrest warp points draw as small pins. While Link is
 * indoors the marker holds the doorway fix. */
static void PaintOverworld(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                           float ry0, float rx1, float ry1, float u, uint32_t tick, int wholeMap,
                           int followCfg, int crestCfg, uint32_t tickForPulse) {
    int32_t imgW = 0, imgH = 0;
    const uint32_t* img = Port_SecondScreenWorldMap_GetImage(&imgW, &imgH);
    if (img == NULL || imgW <= 0 || imgH <= 0) {
        PaintSchematic(s, snap, (int32_t)rx0, (int32_t)ry0, (int32_t)rx1, (int32_t)ry1, tick,
                       (int32_t)u > 0 ? (int32_t)u : 1);
        return;
    }

    int32_t mx, my;
    if (Port_SecondScreenWorldMap_LocatePlayer(snap->area, snap->playerX, snap->playerY, &mx, &my)) {
        sLastFix.valid = 1;
        sLastFix.mapX = mx;
        sLastFix.mapY = my;
    }

    /* All view math runs on the stone-frame crop, not the raw composite,
     * so the fitted whole view shows frame-to-frame with no margin halo
     * and the follow cam can never pan onto the composite's margins. */
    float cw = (float)(WMAP_CROP_X1 - WMAP_CROP_X0), chh = (float)(WMAP_CROP_Y1 - WMAP_CROP_Y0);
    float rw = rx1 - rx0, rh = ry1 - ry0;
    float wholeScale = (rw / cw < rh / chh) ? rw / cw : rh / chh;
    float followScale = wholeScale * 2.1f;

    /* Camera target: follow Link unless the whole map is asked for (or the
     * follow cam is switched off / there is no fix yet). */
    int wantWhole = wholeMap || !followCfg || !sLastFix.valid;
    float tScale = wantWhole ? wholeScale : followScale;
    float tx, ty;
    if (wantWhole) {
        tx = WMAP_CROP_X0 + cw / 2.0f;
        ty = WMAP_CROP_Y0 + chh / 2.0f;
    } else {
        float halfW = rw / (2.0f * tScale), halfH = rh / (2.0f * tScale);
        tx = (float)sLastFix.mapX;
        ty = (float)sLastFix.mapY;
        if (halfW * 2 < cw) {
            tx = tx < WMAP_CROP_X0 + halfW ? WMAP_CROP_X0 + halfW
                                           : (tx > WMAP_CROP_X1 - halfW ? WMAP_CROP_X1 - halfW : tx);
        } else {
            tx = WMAP_CROP_X0 + cw / 2.0f;
        }
        if (halfH * 2 < chh) {
            ty = ty < WMAP_CROP_Y0 + halfH ? WMAP_CROP_Y0 + halfH
                                           : (ty > WMAP_CROP_Y1 - halfH ? WMAP_CROP_Y1 - halfH : ty);
        } else {
            ty = WMAP_CROP_Y0 + chh / 2.0f;
        }
    }

    /* Smooth glide toward the target (pan and zoom both), snapping on the
     * first frame so a fresh surface doesn't animate in from nowhere. */
    if (!sCam.valid) {
        sCam.valid = 1;
        sCam.x = tx;
        sCam.y = ty;
        sCam.scale = tScale;
    } else {
        sCam.x += (tx - sCam.x) * 0.22f;
        sCam.y += (ty - sCam.y) * 0.22f;
        sCam.scale += (tScale - sCam.scale) * 0.22f;
    }

    float ox = (rx0 + rx1) / 2.0f - sCam.x * sCam.scale;
    float oy = (ry0 + ry1) / 2.0f - sCam.y * sCam.scale;
    BlitMapRegion(s, img, imgW, imgH, ox, oy, sCam.scale, (int32_t)rx0, (int32_t)ry0, (int32_t)rx1,
                  (int32_t)ry1);

    /* Map hints — the red checks and errand glyphs the game's own world map
     * shows — right above the map art, below the crest pins and the player
     * marker. Kinstone fusions deliberately do NOT belong here: the game
     * puts those on the enlarged region map, and so does PaintRegion. Both
     * worldmap calls degrade to 0 while their data/sprite isn't decodable,
     * and a marker simply skips that frame. */
    {
        SecondScreenMapMarker hints[16];
        int32_t nHints = Port_SecondScreenWorldMap_GetMapHints(snap->mapHints, hints, 16);
        /* Stamp size is pinned to the FITTED map scale, never the live camera
         * scale. Sized off sCam.scale the glyphs inflated along with the
         * terrain, so the follow view — the one that exists to show detail —
         * ended up the more crowded of the two: every glyph came out 2.1x
         * wider than in the fitted view (over four times the area), and
         * neighbouring hints ran into each other exactly where the extra
         * detail was meant to show. Held at the fitted size a zoom buys map
         * rather than bigger markers, and the fitted view is untouched. */
        int32_t hscale = (int32_t)(wholeScale * 0.75f + 0.5f);
        if (hscale < 1) hscale = 1;
        uint32_t hintInk = Port_SecondScreenTheme_Color(SSC_MENU_INK);
        for (int32_t i = 0; i < nHints; i++) {
            float px = ox + (hints[i].x + 0.5f) * sCam.scale;
            float py = oy + (hints[i].y + 0.5f) * sCam.scale;
            if (px >= rx0 && px < rx1 && py >= ry0 && py < ry1) {
                /* A marker stamp is a 16x16 frame drawn from its top-left
                 * (see port_second_screen_worldmap.h) — center on the spot.
                 * Ringed in the menu's ink, the same separation the crest
                 * pins below get, because both land on the same busy art. */
                Port_SecondScreenWorldMap_DrawMarker(s->px, s->w, s->h, s->stride,
                                                     (int32_t)(px - 8 * hscale),
                                                     (int32_t)(py - 8 * hscale), hscale, hints[i].frame,
                                                     SECOND_SCREEN_WORLDMAP_NO_REGION, hintInk);
            }
        }
    }

    /* Windcrest warp points as small pins (green — the fast-travel accent),
     * gated by the settings toggle. Ids are the bit index within the
     * windcrests word's top byte, exactly what GetWindcrestPin expects. */
    if (crestCfg) {
        for (int32_t id = 0; id < 8; id++) {
            int32_t wx, wy;
            if (((snap->windcrests >> (24 + id)) & 1u) &&
                Port_SecondScreenWorldMap_GetWindcrestPin(id, &wx, &wy)) {
                float px = ox + (wx + 0.5f) * sCam.scale;
                float py = oy + (wy + 0.5f) * sCam.scale;
                if (px >= rx0 && px < rx1 && py >= ry0 && py < ry1) {
                    DrawPin(s, px, py, 0.8f * u, Port_SecondScreenTheme_Color(SSC_RUPEE_GREEN),
                            Port_SecondScreenTheme_Color(SSC_MENU_INK));
                }
            }
        }
    }

    /* Player marker: a pulsing gold diamond (live it also blinks white,
     * like the game's own map dot; the frozen doorway fix holds steady). */
    if (sLastFix.valid) {
        float px = ox + (sLastFix.mapX + 0.5f) * sCam.scale;
        float py = oy + (sLastFix.mapY + 0.5f) * sCam.scale;
        int32_t base =
            (int32_t)((6.5f + 1.5f * sinf((float)(tickForPulse % 32) * (6.28318f / 32.0f))) * u);
        uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
        uint32_t c = (snap->areaFlags & SECOND_SCREEN_AR_IS_OVERWORLD)
                         ? ((tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : gold)
                         : gold;
        DrawMapMarker(s, (int32_t)px, (int32_t)py, base, c);
    }

    /* Zoom-grid availability, asked once per frame at the view center: the
     * map screen's own tile grid answering means a tap can zoom, which is
     * also what decides whether the view chip below is worth showing. */
    int32_t probeRegion, probeRect[4];
    int gridReady = Port_SecondScreenWorldMap_GetRegionAt(
        WMAP_CROP_X0 + (int32_t)(cw / 2), WMAP_CROP_Y0 + (int32_t)(chh / 2), &probeRegion, &probeRect[0],
        &probeRect[1], &probeRect[2], &probeRect[3]);

    /* The tapped tile, bracketed on the map for a beat before the zoom —
     * the game's cursor brackets are the model, so the zoom reads as a
     * place you picked rather than a view that just swapped. */
    uint8_t regionState;
    float br[4];
    uint32_t sinceTap;
    UI_LOCK();
    regionState = sUi.regionState;
    br[0] = (float)sUi.regionX0;
    br[1] = (float)sUi.regionY0;
    br[2] = (float)sUi.regionX1;
    br[3] = (float)sUi.regionY1;
    sinceTap = tick - sUi.regionTick;
    UI_UNLOCK();
    if (regionState == SS_REGION_BRACKET) {
        float bx0 = ox + br[0] * sCam.scale, by0 = oy + br[1] * sCam.scale;
        float bx1 = ox + br[2] * sCam.scale, by1 = oy + br[3] * sCam.scale;
        float t = (float)u;
        if (t < 1) t = 1;
        StrokeRoundRect(s, bx0 - t, by0 - t, bx1 + t, by1 + t, 2 * t, t,
                        Port_SecondScreenTheme_Color(SSC_MENU_INK));
        StrokeRoundRect(s, bx0, by0, bx1, by1, 2 * t, t, Port_SecondScreenTheme_Color(SSC_GOLD));
        if (sinceTap >= SS_REGION_BRACKET_TICKS) {
            UI_LOCK();
            if (sUi.regionState == SS_REGION_BRACKET) {
                sUi.regionState = SS_REGION_VIEW;
            }
            UI_UNLOCK();
        }
    }

    /* Publish the transform (a tap becomes map-image coordinates for the
     * tile pick) and the map rect as one big view target. */
    UI_LOCK();
    sUi.mapLive = 1;
    sUi.mapOx = ox;
    sUi.mapOy = oy;
    sUi.mapScale = sCam.scale;
    sUi.mapU = u;
    sUi.mapImgW = imgW;
    sUi.mapImgH = imgH;
    sUi.regionGridReady = (uint8_t)(gridReady != 0);
    UI_UNLOCK();

    /* Changing the zoom is the chip's job and only the chip's: ZOOM steps
     * between Link's close view and the whole of Hyrule, and stays labelled
     * ZOOM in both because both are somewhere you can zoom from. Only the
     * regional map is a level down, and only it says BACK.
     *
     * Tapping the map itself only ever opens the rectangle under the
     * finger. It used to toggle the view as well, which meant every tap
     * that missed a rectangle took you somewhere you hadn't asked to go.
     * Added ahead of the map's own target so it wins the hit test. */
    if (followCfg && sLastFix.valid) {
        /* With no fix, or the follow cam switched off, the whole map is the
         * only view there is and the chip would have nothing to step to. */
        float chip[4];
        DrawMapChip(s, "ZOOM", (rx0 + rx1) / 2.0f, ry1 - 12 * u, u, chip);
        AddTarget(tl, chip[0], chip[1], chip[2], chip[3], SS_ACT_MAPZOOM, 0);
    }
    AddTarget(tl, rx0, ry0, rx1, ry1, SS_ACT_MAP, 0);
}

/* ------------------------------------------------------------------ */
/*  Regional map (MAP tab, after a tile zoom)                          */
/* ------------------------------------------------------------------ */

/* The enlarged map of one zoom-grid tile — the screen the game opens when
 * the player puts the map cursor on a tile — filling the map area, with the
 * markers that screen carries (map hints in their region glyphs, one glyph
 * per active kinstone fusion), Link's marker where that region places him
 * and a BACK chip in the game's own chip furniture. Returns 0 when the
 * region can't be drawn, and the caller simply stays on the world map (so a
 * not-ready art module can never leave the panel on a blank view). */
static int PaintRegion(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                       float ry0, float rx1, float ry1, float u, uint32_t tick, int32_t region) {
    int32_t dx = (int32_t)rx0, dy = (int32_t)ry0;
    int32_t dw = (int32_t)(rx1 - rx0), dh = (int32_t)(ry1 - ry0);
    if (dw <= 0 || dh <= 0) {
        return 0;
    }

    /* Letterbox to the artwork's aspect instead of filling the rect. The map
     * area is close to square on this panel while the region art is not, so
     * stretching to fill visibly distorted the tiles — trees and bridges came
     * out elongated along whichever axis had the surplus.
     *
     * The fit is computed ONCE here and then used for the art, the markers and
     * Link's pin alike. All three scale by the destination size they are
     * handed, so fitting only the art would slide every marker off the terrain
     * it belongs to. */
    {
        int32_t artW = 0, artH = 0;
        if (Port_SecondScreenWorldMap_GetRegionSize(region, &artW, &artH) && artW > 0 && artH > 0) {
            /* Scale every region by the SAME factor rather than blowing each
             * one up to fill the panel. The regions differ enormously in size
             * (the sky is a fraction of Hyrule Field), and fit-to-fill turned
             * the small ones into a handful of enormous blocks. The game
             * itself draws them all at 1:1 inside one 240x160 frame, so take
             * that frame as the reference: the biggest region fills the map
             * area, the rest stay proportionally smaller — which also keeps
             * how much of Hyrule a region covers readable at a glance. */
            float refScale = (float)dw / 240.0f;
            float byH = (float)dh / 160.0f;
            if (byH < refScale) {
                refScale = byH;
            }
            int32_t fitW = (int32_t)(artW * refScale + 0.5f);
            int32_t fitH = (int32_t)(artH * refScale + 0.5f);
            if (fitW > dw) fitW = dw;
            if (fitH > dh) fitH = dh;
            if (fitW > 0 && fitH > 0) {
                dx += (dw - fitW) / 2;
                dy += (dh - fitH) / 2;
                dw = fitW;
                dh = fitH;
            }
        }
    }

    if (!Port_SecondScreenWorldMap_DrawRegion(s->px, s->w, s->h, s->stride, dx, dy, dw, dh, region)) {
        return 0;
    }

    /* A region carries few markers even fully fused — the busiest is Minish
     * Woods at 14 — so this is sized well past any real save; the game's own
     * marker buffer (gMapDataBottomSpecial) holds 128. */
    {
        SecondScreenMapMarker marks[48];
        int32_t n = Port_SecondScreenWorldMap_GetRegionMarkers(region, snap->mapHints,
                                                              snap->fusedKinstones, snap->fusionUnmarked,
                                                              dw, dh, marks, 48);
        /* The game stamps a 16 px marker on artwork it shows at 1:1; this
         * view fits the whole region instead, so the stamp is scaled by the
         * panel's own unit to read at about the same share of the map. */
        int32_t mscale = (int32_t)(2.5f * u + 0.5f);
        if (mscale < 1) mscale = 1;
        uint32_t markInk = Port_SecondScreenTheme_Color(SSC_MENU_INK);
        for (int32_t i = 0; i < n; i++) {
            /* Ringed in ink like the world map's hints: the enlarged region
             * art is the busiest surface on the panel, and a fused-kinstone
             * or errand glyph dropped bare onto a treeline vanished into it. */
            Port_SecondScreenWorldMap_DrawMarker(s->px, s->w, s->h, s->stride,
                                                 dx + marks[i].x - 8 * mscale,
                                                 dy + marks[i].y - 8 * mscale, mscale, marks[i].frame,
                                                 region, markInk);
        }
    }

    int32_t px, py;
    if (Port_SecondScreenWorldMap_LocateInRegion(region, snap->area, snap->playerX, snap->playerY, dw, dh,
                                                 &px, &py)) {
        int32_t base = (int32_t)(7.0f * u);
        uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
        DrawMapMarker(s, dx + px, dy + py, base, (tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : gold);
    }

    float chip[4];
    DrawMapChip(s, "BACK", (rx0 + rx1) / 2.0f, ry1 - 12 * u, u, chip);
    AddTarget(tl, chip[0], chip[1], chip[2], chip[3], SS_ACT_MAPVIEW, 0);
    /* Anywhere else on the region map goes back too — the same "tap again"
     * gesture that opened it. */
    AddTarget(tl, rx0, ry0, rx1, ry1, SS_ACT_MAP, 0);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Dungeon map (MAP tab, in dungeons)                                 */
/* ------------------------------------------------------------------ */

/* Dungeon mode: name banner up top, tappable floor plaques down the left
 * (tap previews that floor; the preview auto-returns to Link's floor after
 * a few seconds when that toggle is on; Link's own plaque keeps a gold
 * edge), and the authentic floor map full-bleed in the remaining space. */
static void PaintDungeon(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                         float ry0, float rx1, float ry1, float u, int32_t ts, uint32_t tick,
                         int returnCfg) {
    SecondScreenDungeonMapInfo info;
    int haveInfo = Port_SecondScreenDungeonMap_GetInfo(snap->dungeonIdx, snap->area, snap->room, &info) &&
                   info.floorCount > 0;

    uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);

    if (!haveInfo) {
        PaintSchematic(s, snap, (int32_t)(rx0 + 8 * u), (int32_t)(ry0 + 8 * u), (int32_t)(rx1 - 8 * u),
                       (int32_t)(ry1 - 8 * u), tick, ts);
        UI_LOCK();
        sUi.playerFloorDisp = SS_NO_FLOOR;
        UI_UNLOCK();
        return;
    }

    /* Resolve (and possibly expire) the plaque preview under the lock:
     * it drops when Link changes floor or dungeon, and — with the
     * auto-return toggle on — after SS_FLOOR_RETURN_TICKS. */
    int preview;
    UI_LOCK();
    if (sUi.floorPreview != SS_NO_FLOOR) {
        if (sUi.previewDungeon != snap->dungeonIdx || sUi.previewBaseFloor != info.currentFloor ||
            (returnCfg && (uint32_t)(tick - sUi.floorPreviewTick) > SS_FLOOR_RETURN_TICKS)) {
            sUi.floorPreview = SS_NO_FLOOR;
        }
    }
    preview = sUi.floorPreview;
    sUi.playerFloorDisp = info.currentFloor;
    sUi.curDungeon = snap->dungeonIdx;
    UI_UNLOCK();

    int viewFloor = (preview != SS_NO_FLOOR) ? preview : info.currentFloor;
    if (viewFloor < 0) viewFloor = 0;
    if (viewFloor >= info.floorCount) viewFloor = info.floorCount - 1;

    /* Name banner, top center — the pause menu's red header chip. */
    float topY = ry0 + 12 * u;
    const char* name = snap->dungeonIdx < 7 ? kDungeonNames[snap->dungeonIdx] : NULL;
    if (name != NULL) {
        int32_t ms = (int32_t)(2.2f * u);
        if (ms < 1) ms = 1;
        int32_t tw = MenuTextWidth(name, ms);
        float cx = (rx0 + rx1) / 2.0f;
        float bx0 = cx - tw / 2.0f - 22 * u, bx1 = cx + tw / 2.0f + 22 * u;
        float by0 = topY, by1 = topY + MENU_TEXT_BOX * ms + 18 * u;
        int32_t cts = (int32_t)((by1 - by0) / 32.0f);
        if (cts < 1) cts = 1;
        Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)bx0, (int32_t)by0,
                                        (int32_t)(bx1 - bx0), (int32_t)(by1 - by0), cts, SS_CHIP_RED);
        MenuTextCentered(s, name, cx, (by0 + by1) / 2.0f, ms, SS_TEXT_WHITE);
        topY = by1;
    }

    /* Floor plaques, topmost floor first, down the left side: menu chips
     * (the previewed floor turns the header red, the rest stay dark). */
    float pw = 100 * u, ph = 50 * u, pgap = 8 * u;
    float px0 = rx0 + 20 * u;
    float py0 = topY + 12 * u;
    int8_t topFloorNum = snap->dungeonIdx < 7 ? kDungeonTopFloor[snap->dungeonIdx] : 2;
    for (int i = 0; i < info.floorCount && i < 8; i++) {
        float y0 = py0 + i * (ph + pgap);
        int sel = (i == viewFloor);
        int isPlayers = (i == info.currentFloor);
        int32_t cts = (int32_t)(ph / 26.0f);
        if (cts < 1) cts = 1;
        Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)px0, (int32_t)y0,
                                        (int32_t)pw, (int32_t)ph, cts,
                                        sel ? SS_CHIP_RED : SS_CHIP_DARK);
        /* Label: nF above ground, B(n) below — see kDungeonTopFloor. */
        int fl = topFloorNum - 2 - i;
        char label[6];
        if (fl >= 1) {
            snprintf(label, sizeof(label), "%dF", fl);
        } else {
            snprintf(label, sizeof(label), "B%d", 1 - fl);
        }
        int32_t ms = (int32_t)(1.7f * u);
        if (ms < 1) ms = 1;
        MenuTextCentered(s, label, px0 + pw / 2 + (isPlayers ? 5 * u : 0), y0 + ph / 2, ms,
                         SS_TEXT_WHITE);
        /* A small gold marker keeps Link's floor obvious while another one
         * is being previewed. */
        if (isPlayers) {
            DrawMapMarker(s, (int32_t)(px0 + 13 * u), (int32_t)(y0 + ph / 2), (int32_t)(3 * u), gold);
        }
        AddTarget(tl, px0, y0, px0 + pw, y0 + ph, SS_ACT_PLAQUE, (uint8_t)i);
    }

    /* The floor map, full-bleed in what's left right of the plaques. */
    float mx0 = px0 + pw + 20 * u;
    float my0 = topY + 10 * u;
    if (!Port_SecondScreenDungeonMap_Draw(s->px, s->w, s->h, s->stride, (int32_t)mx0, (int32_t)my0,
                                          (int32_t)(rx1 - 12 * u - mx0), (int32_t)(ry1 - 10 * u - my0),
                                          snap->dungeonIdx, viewFloor, snap->area, snap->room,
                                          snap->visitedMask, snap->dungeonItemBits, snap->playerX,
                                          snap->playerY, tick)) {
        PaintSchematic(s, snap, (int32_t)mx0, (int32_t)my0, (int32_t)(rx1 - 12 * u),
                       (int32_t)(ry1 - 10 * u), tick, ts);
    }
}

/* ------------------------------------------------------------------ */
/*  Items panel                                                        */
/* ------------------------------------------------------------------ */

/* ITEMS tab: the pause menu's 16 equip slots as a 4x4 touch grid on the
 * menu's stone slab — same slot order as the real Items screen, the
 * slab's own recessed wells as cells, real item icons, the real blinking
 * gold equip cursor and the HUD's A/B bubbles on the equipped slots. Tap
 * equips to A, hold to B — or to whichever slot an armed sidebar ring
 * selected (that cell breathes while armed). */
static void PaintItemsPanel(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                            float ry0, float rx1, float ry1, float u, int32_t ts, uint32_t tick,
                            int armedRing) {
    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 12 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    /* Header: the menu's red chip, hung over the slab's top band exactly
     * like the pause screens hang theirs. */
    int32_t hms = (int32_t)(2.4f * u);
    if (hms < 1) hms = 1;
    DrawPanelHeaderChip(s, (rx0 + rx1) / 2.0f, iy0, "ITEMS", hms, u);
    iy0 += MENU_TEXT_BOX * hms + 32 * u;

    const int cols = 4, rows = 4;
    int32_t cellW = (int32_t)(ix1 - ix0) / cols;
    int32_t cellH = (int32_t)(iy1 - iy0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    if (cell < 24) {
        return;
    }
    int32_t gx0 = (int32_t)(ix0 + ((ix1 - ix0) - cell * cols) / 2);
    int32_t gy0 = (int32_t)(iy0 + ((iy1 - iy0) - cell * rows) / 2);
    int32_t gap = cell / 12;
    int32_t seam = ts > 2 ? ts / 2 : 1;

    uint32_t stoneDark = Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK);

    const SecondScreenThemeSprite* cursor =
        Port_SecondScreenTheme_Get((tick & 8) ? SST_CURSOR_1 : SST_CURSOR_0);

    /* The cell an armed ring would overwrite breathes slowly (2.4 s). */
    int pulseSlot = -1;
    if (armedRing == 1) pulseSlot = snap->equippedSlotA;
    if (armedRing == 2) pulseSlot = snap->equippedSlotB;
    float pt = 0.5f + 0.5f * sinf((float)(tick % 48) * (6.28318f / 48.0f));

    for (int slot = 0; slot < SECOND_SCREEN_ITEM_SLOTS; slot++) {
        int32_t cx0 = gx0 + (slot % cols) * cell + gap;
        int32_t cy0 = gy0 + (slot / cols) * cell + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        uint8_t itemId = snap->menuItems[slot];

        /* The slab's own recessed well; the game shows every slot's well
         * whether or not something is in it. Well scale follows the cell
         * so the 8 px rim never dominates small cells. */
        int32_t wts = (cx1 - cx0) / 40;
        if (wts < 1) wts = 1;
        if (wts > ts) wts = ts;
        Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, cx0, cy0, cx1 - cx0, cy1 - cy0, wts);
        if (slot == pulseSlot) {
            /* Armed-ring breath: a gold wash over the well interior. */
            int32_t in = 3 * wts;
            FillRect(s, cx0 + in, cy0 + in, cx1 - in, cy1 - in,
                     MixColor(stoneDark, Port_SecondScreenTheme_Color(SSC_GOLD),
                              (uint32_t)(30 + 90 * pt)));
        }
        if (itemId == 0) {
            continue;
        }

        /* Bottles are containers; the icon players recognize is what's
         * inside (the pause menu makes the same substitution). */
        uint8_t iconId = itemId;
        if (slot >= 12) {
            uint8_t content = snap->bottleContents[slot - 12];
            if (content != 0) {
                iconId = content;
            }
        }
        int32_t iconScale = (cell - 2 * gap - 4 * seam) / 16;
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale, iconId);

        /* Ammo under bombs/bow — the HUD's own tiny digit pair. */
        int32_t ss = (iconScale + 1) / 2;
        DrawItemAmmoCount(s, snap, itemId, iconX, cy1 - seam - 8 * ss, ss);

        if (slot == snap->equippedSlotA || slot == snap->equippedSlotB) {
            bool isA = slot == snap->equippedSlotA;
            if (cursor != NULL) {
                int32_t cmax = cursor->w > cursor->h ? cursor->w : cursor->h;
                int32_t cs = (cell + cell / 4) / cmax;
                if (cs < 1) cs = 1;
                BlitSprite(s, cursor, (cx0 + cx1) / 2 - cursor->w * cs / 2,
                           (cy0 + cy1) / 2 - cursor->h * cs / 2, cs);
            } else {
                OutlineRect(s, cx0, cy0, cx1, cy1, seam * 2, Port_SecondScreenTheme_Color(SSC_GOLD));
            }
            const SecondScreenThemeSprite* badge =
                Port_SecondScreenTheme_Get(isA ? SST_BUTTON_A : SST_BUTTON_B);
            if (badge != NULL) {
                int32_t bmax = badge->w > badge->h ? badge->w : badge->h;
                int32_t bs = (cell / 3) / bmax;
                if (bs < 1) bs = 1;
                int32_t bx = isA ? cx0 - seam : cx1 + seam - badge->w * bs;
                BlitSprite(s, badge, bx, cy0 - seam, bs);
            } else {
                int32_t tagS = (int32_t)(1.4f * u) > 0 ? (int32_t)(1.4f * u) : 1;
                char tag[2] = { isA ? 'A' : 'B', 0 };
                int32_t tagX = isA ? cx0 + seam : cx1 - seam - 5 * tagS;
                DrawTextStr(s, tag, tagX, cy0 + seam, tagS, Port_SecondScreenTheme_Color(SSC_MENU_INK),
                            Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
            }
        }

        AddTarget(tl, (float)cx0, (float)cy0, (float)cx1, (float)cy1, SS_ACT_ITEM, itemId);
    }
}

/* ------------------------------------------------------------------ */
/*  Quest status panel                                                 */
/* ------------------------------------------------------------------ */

/* QUEST tab: the pause screen's own quest status, reflowed for this panel by
 * port_second_screen_quest.c — same wells, same slot art, same clusters the
 * game groups its entries into, given a near-square panel instead of the GBA's
 * 3:2 screen. The furniture a passive panel has no use for is dropped there:
 * the QUEST STATUS banner, the L/R tab arrows (this panel has real tabs) and
 * the SLEEP / SAVE plates (nothing here can sleep or save).
 *
 * Everything below is the fallback for the frames before that screen's OBJ
 * tiles resolve: the same 4x4 well grid the ITEMS tab uses, so a cold start
 * shows the tab's contents instead of an empty slab.
 *
 *   row 0  kinstone bag (fused count) | heart pieces | sword skills | shells
 *          or the trophies that replace them
 *   row 1  the three carried quest items
 *   row 2  the four elements
 *   row 3  grip ring, bracelets, flippers
 */
static void PaintQuestPanel(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl,
                            float rx0, float ry0, float rx1, float ry1, float u, int32_t ts,
                            uint32_t tick, int questView) {
    int32_t px = (int32_t)rx0, py = (int32_t)ry0;
    int32_t pw = (int32_t)(rx1 - rx0), ph = (int32_t)(ry1 - ry0);

    /* The two lists the bag and the scroll open. Each keeps a BACK chip and
     * nothing else — the pause menu's own way out is the B button, which
     * this panel doesn't have. A list that can't draw yet falls through to
     * the status screen rather than showing an empty plate. */
    if (questView == SS_QUEST_KINSTONES || questView == SS_QUEST_TECHNIQUES) {
        int drawn = (questView == SS_QUEST_KINSTONES)
                        ? Port_SecondScreenQuest_DrawKinstones(s->px, s->w, s->h, s->stride, px, py,
                                                               pw, ph, snap)
                        : Port_SecondScreenQuest_DrawTechniques(s->px, s->w, s->h, s->stride, px, py,
                                                                pw, ph, snap);
        if (drawn) {
            float chip[4];
            DrawMapChip(s, "BACK", (rx0 + rx1) / 2.0f, ry1 - 12 * u, u, chip);
            AddTarget(tl, chip[0], chip[1], chip[2], chip[3], SS_ACT_QUESTVIEW, SS_QUEST_MAIN);
            AddTarget(tl, rx0, ry0, rx1, ry1, SS_ACT_QUESTVIEW, SS_QUEST_MAIN);
            return;
        }
    }

    {
        SecondScreenQuestHotspots hot;
        if (Port_SecondScreenQuest_Draw(s->px, s->w, s->h, s->stride, px, py, pw, ph, snap, tick,
                                        &hot)) {
            if (hot.bag.w > 0) {
                AddTarget(tl, (float)hot.bag.x, (float)hot.bag.y, (float)(hot.bag.x + hot.bag.w),
                          (float)(hot.bag.y + hot.bag.h), SS_ACT_QUESTVIEW, SS_QUEST_KINSTONES);
            }
            if (hot.skill.w > 0) {
                AddTarget(tl, (float)hot.skill.x, (float)hot.skill.y,
                          (float)(hot.skill.x + hot.skill.w), (float)(hot.skill.y + hot.skill.h),
                          SS_ACT_QUESTVIEW, SS_QUEST_TECHNIQUES);
            }
            return;
        }
    }

    (void)tick; /* nothing here animates: the panel has no selection cursor */

    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 12 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    /* No header chip: the tab bar below already says QUEST, and the banner is
     * what used to eat the top of the slab. */

    const int cols = 4, rows = 4;
    int32_t cellW = (int32_t)(ix1 - ix0) / cols;
    int32_t cellH = (int32_t)(iy1 - iy0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    if (cell < 24) {
        return;
    }
    int32_t gx0 = (int32_t)(ix0 + ((ix1 - ix0) - cell * cols) / 2);
    int32_t gy0 = (int32_t)(iy0 + ((iy1 - iy0) - cell * rows) / 2);
    int32_t gap = cell / 12;
    int32_t seam = ts > 2 ? ts / 2 : 1;

    /* What each of the sixteen cells holds. iconItem 0 means "empty well". */
    uint8_t iconItem[16];
    uint16_t count[16];
    uint8_t hasCount[16];
    memset(iconItem, 0, sizeof(iconItem));
    memset(count, 0, sizeof(count));
    memset(hasCount, 0, sizeof(hasCount));

    /* Cell 0: the Tingle trophy takes the bag's well once won; otherwise the
     * bag itself, and only when owned — pieces without the bag show nothing,
     * the same rule sub_080A5594 follows. The corner count is fusions made,
     * which is the number players actually track. */
    if (snap->tingleTrophy == 1) {
        iconItem[0] = ITEMID_QST_TINGLE_TROPHY;
    } else if (snap->kinstoneBagOwned) {
        iconItem[0] = ITEMID_KINSTONE_BAG;
        count[0] = snap->kinstoneFused;
        hasCount[0] = 1;
    }

    /* Cell 1: heart pieces, drawn as the quarter-heart sprite for the count
     * so a glance reads the fraction without needing the digits. */
    /* Cell 2: sword techniques, count only when any are known. */
    if (snap->swordSkills != 0) {
        count[2] = snap->swordSkills;
        hasCount[2] = 1;
    }

    /* Cell 3: the medal once won, else the shell counter. */
    if (snap->carlovMedal == 1) {
        iconItem[3] = ITEMID_QST_CARLOV_MEDAL;
    } else if (snap->shellsOwned != 0) {
        iconItem[3] = ITEMID_SHELLS;
        count[3] = snap->shells;
        hasCount[3] = 1;
    }

    /* Row 1: the carried quest items, in the snapshot's own tray order. */
    for (int i = 0; i < 3; i++) {
        iconItem[4 + i] = snap->questItems[i];
    }
    /* The spare cell carries the figurine tally, which the ROM screen has no
     * well for but the panel has room for. */
    if (snap->figurineCount != 0) {
        count[7] = snap->figurineCount;
        hasCount[7] = 1;
    }

    /* Row 2: the four elements. Row 3: grip ring, bracelets, flippers. */
    for (int i = 0; i < 4; i++) {
        if (snap->elements & (1u << i)) {
            iconItem[8 + i] = (uint8_t)(ITEMID_EARTH_ELEMENT + i);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (snap->passives & (1u << i)) {
            iconItem[12 + i] = (uint8_t)(ITEMID_GRIP_RING + i);
        }
    }

    for (int slot = 0; slot < 16; slot++) {
        int32_t cx0 = gx0 + (slot % cols) * cell + gap;
        int32_t cy0 = gy0 + (slot / cols) * cell + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        int32_t wts = (cx1 - cx0) / 40;
        if (wts < 1) wts = 1;
        if (wts > ts) wts = ts;
        Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, cx0, cy0, cx1 - cx0, cy1 - cy0,
                                        wts);

        int32_t iconScale = (cell - 2 * gap - 4 * seam) / 16;
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;

        if (slot == 1) {
            /* Heart pieces: the HUD's own quarter-heart faces, so three of
             * four reads as a three-quarter heart rather than as "3". */
            int hp = snap->heartPieces > 3 ? 3 : snap->heartPieces;
            const SecondScreenThemeSprite* heart =
                Port_SecondScreenTheme_Get(SST_HEART_EMPTY + hp);
            if (heart != NULL) {
                int32_t hmax = heart->w > heart->h ? heart->w : heart->h;
                int32_t hs = (cell - 4 * gap) / (hmax > 0 ? hmax : 8);
                if (hs < 1) hs = 1;
                BlitSprite(s, heart, (cx0 + cx1) / 2 - heart->w * hs / 2,
                           (cy0 + cy1) / 2 - heart->h * hs / 2, hs);
            }
            continue;
        }
        if (slot == 2 && hasCount[2]) {
            /* Sword techniques have no inventory icon of their own — the
             * player's own sword stands in, with the technique count on it. */
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale,
                                                 ITEMID_SMITH_SWORD);
        } else if (iconItem[slot] != 0) {
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale,
                                                 iconItem[slot]);
        }

        if (hasCount[slot]) {
            int32_t ss = (iconScale + 1) / 2;
            if (ss < 1) ss = 1;
            DrawAmmoCount(s, iconX, cy1 - seam - 8 * ss, ss, count[slot]);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Settings panel                                                     */
/* ------------------------------------------------------------------ */

static const char* const kSettingLabels[SS_SET_COUNT] = {
    "TOP HUD",          "WIDESCREEN",        "FOLLOW CAM",       "WINDCREST PINS",
    "FLOOR AUTO RETURN", "TURBO SPEED",       "MASTER VOLUME",    "AUTOSAVE",
    "COLOR CORRECTION", "SHOW FPS",           "HOLD TO ADVANCE TEXT",
    "RANDOMIZER",       "PANEL BACKDROP",     "SWAP SCREENS",
#ifdef TMC_3DS
    "ASPECT RATIO",     "DISPLAY STYLE",
#endif
};

/* Value words of the PANEL BACKDROP row, indexed by SS_BACKDROP_*. The
 * default reads PATTERN rather than "PARCHMENT" because what actually
 * changes between it and CREAM is the doodle lattice — and because the
 * longer word would not fit the value chip. Order follows the enum, which
 * appends so stored config values keep meaning the same style — it is not
 * a brightness ramp (DARK sits mid-list). Every word here is inside
 * SS_SET_WIDEST_VALUE, so none of them widen the value chip. */
static const char* const kBackdropWords[SS_BACKDROP_COUNT] = {
    "PATTERN", "CREAM", "DARK", "DIM", "STONE", "SLATE", "NAVY"
};

/* Reserve only the width a row can actually use. A single global value
 * column sized for PATTERN needlessly crowded long GAMEPLAY labels even
 * though their chips only ever say ON/OFF. */
static const char* SettingValueMinWord(int setting) {
    switch (setting) {
        case SS_SET_TOP_HUD: return "SHOW";
        case SS_SET_TURBO: return "X5";
        case SS_SET_VOLUME: return "100";
        case SS_SET_BACKDROP: return "PATTERN";
        case SS_SET_SWAP_SCREENS: return "RESTART";
#ifdef TMC_3DS
        case SS_SET_ASPECT_RATIO: return "ORIGINAL";
        case SS_SET_DISPLAY_STYLE: return "PIXEL PERFECT";
#endif
        default: return "OFF";
    }
}

/* The stored backdrop style, sanitised. config.json is hand-editable and
 * the style list may grow, so every read of it goes through here. */
static int BackdropStyleCfg(void) {
    int style = Port_Config_GetSecondScreenBackdrop();
    return (style > SS_BACKDROP_PARCHMENT && style < SS_BACKDROP_COUNT) ? style : SS_BACKDROP_PARCHMENT;
}

/* Group the controls by what they affect. This mirrors the sibling 3DS
 * port's root -> submenu navigation while retaining Minish Cap's own
 * settings. Rows that cannot do anything in a build are omitted. */
static int SettingsPageRows(int page, uint8_t* out) {
    int n = 0;
    if (page == SS_SETTINGS_SCREEN) {
#ifdef TMC_3DS
        out[n++] = SS_SET_ASPECT_RATIO;
        out[n++] = SS_SET_DISPLAY_STYLE;
        out[n++] = SS_SET_TOP_HUD;
        out[n++] = SS_SET_COLOR_CORRECTION;
        out[n++] = SS_SET_BACKDROP;
#else
        out[n++] = SS_SET_TOP_HUD;
        if (PORT_VIEW_WIDTH > 240) out[n++] = SS_SET_WIDESCREEN;
        out[n++] = SS_SET_FOLLOW;
        out[n++] = SS_SET_COLOR_CORRECTION;
        out[n++] = SS_SET_BACKDROP;
#ifdef __ANDROID__
        out[n++] = SS_SET_SWAP_SCREENS;
#endif
#endif
    } else if (page == SS_SETTINGS_GAMEPLAY) {
#ifdef TMC_3DS
        out[n++] = SS_SET_TURBO;
#endif
        out[n++] = SS_SET_CRESTS;
        out[n++] = SS_SET_FLOOR_RETURN;
        out[n++] = SS_SET_VOLUME;
        out[n++] = SS_SET_AUTOSAVE;
        out[n++] = SS_SET_HOLD_ADVANCE;
    }
#ifdef TMC_3DS
    else if (page == SS_SETTINGS_RANDOMIZER) {
        out[n++] = SS_SET_RANDOMIZER;
    }
#endif
    return n;
}

static const char* SettingsPageTitle(int page) {
    switch (page) {
        case SS_SETTINGS_SCREEN: return "SCREEN";
        case SS_SETTINGS_GAMEPLAY: return "GAMEPLAY";
        case SS_SETTINGS_DEVELOPER: return "DEVELOPER";
        case SS_SETTINGS_OVERLAY: return "OVERLAY";
        case SS_SETTINGS_RANDOMIZER: return "RANDOMIZER";
        default: return "SETTINGS";
    }
}

static int SettingsBackPage(int page) {
    return page == SS_SETTINGS_OVERLAY ? SS_SETTINGS_DEVELOPER : SS_SETTINGS_ROOT;
}

static void DrawSettingsChevron(const SSurf* s, float cx, float cy, float u) {
    int32_t step = (int32_t)(5 * u);
    int32_t thick = (int32_t)(4 * u);
    if (step < 1) step = 1;
    if (thick < 1) thick = 1;
    uint32_t color = Port_SecondScreenTheme_Color(SSC_BANNER_NAVY);
    for (int i = -3; i <= 3; ++i) {
        int a = i < 0 ? -i : i;
        int32_t x = (int32_t)cx - a * step;
        int32_t y = (int32_t)cy + i * step;
        FillRect(s, x - thick, y - thick, x + thick + 1, y + thick + 1, color);
    }
}

static void DrawSettingsNavRow(const SSurf* s, TargetList* tl, float x0, float y0, float x1, float y1,
                               const char* label, int page, float u, int32_t ts) {
    DrawMenuButton(s, x0, y0, x1, y1, "", 0, 0, u, ts);
    int32_t ms = (int32_t)(2.0f * u);
    if (ms < 1) ms = 1;
    MenuTextDraw(s, label, (int32_t)(x0 + 24 * u), (int32_t)((y0 + y1) / 2 - 8 * ms), ms, SS_TEXT_NAVY);
    DrawSettingsChevron(s, x1 - 28 * u, (y0 + y1) / 2, u);
    AddTarget(tl, x0, y0, x1, y1, SS_ACT_SETTINGS_PAGE, (uint8_t)page);
}

static void DrawSettingsBack(const SSurf* s, TargetList* tl, float x0, float y0, float h, int backPage,
                             float u, int32_t ts) {
    float w = 154 * u;
    if (w < 54) w = 54;
    DrawMenuButton(s, x0, y0, x0 + w, y0 + h, "BACK", 0, 0, u, ts);
    AddTarget(tl, x0, y0, x0 + w, y0 + h, SS_ACT_SETTINGS_BACK, (uint8_t)backPage);
}

static int GetSettingState(int row, char* out, int outCap);

static void DrawSettingsValueRow(const SSurf* s, TargetList* tl, float x0, float y0, float x1, float y1,
                                 int setting, float u, int32_t ts) {
    char val[16];
    int on = GetSettingState(setting, val, sizeof(val));
    DrawMenuButton(s, x0, y0, x1, y1, "", 0, 0, u, ts);

    int32_t ms = (int32_t)(((y1 - y0) * 0.56f) / MENU_TEXT_BOX);
    if (ms < 1) ms = 1;
    int32_t maxMs = (int32_t)(1.8f * u);
    if (maxMs < 1) maxMs = 1;
    if (ms > maxMs) ms = maxMs;

    float ch = y1 - y0 - 10 * u;
    if (ch < MENU_TEXT_BOX * ms) ch = MENU_TEXT_BOX * ms;
    float cw = MenuTextWidth(val, ms);
    float cwMin = MenuTextWidth(SettingValueMinWord(setting), ms);
    if (cw < cwMin) cw = cwMin;
    cw += 20 * u;
    float cx1 = x1 - 10 * u, cx0 = cx1 - cw;
    float cy0 = (y0 + y1 - ch) / 2;
    int32_t cts = (int32_t)(ch / 24.0f);
    if (cts < 1) cts = 1;
    Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)cx0, (int32_t)cy0,
                                    (int32_t)cw, (int32_t)ch, cts, on ? SS_CHIP_RED : SS_CHIP_DARK);
    MenuTextCentered(s, val, (cx0 + cx1) / 2, cy0 + ch / 2, ms, SS_TEXT_WHITE);

    MenuTextDraw(s, kSettingLabels[setting], (int32_t)(x0 + 18 * u),
                 (int32_t)((y0 + y1) / 2 - 8 * ms), ms, SS_TEXT_NAVY);
    AddTarget(tl, x0, y0, x1, y1, SS_ACT_SETTING, (uint8_t)setting);
}

static void DrawDiagnosticRow(const SSurf* s, float x0, float y0, float x1, float y1, const char* label,
                              const char* value, float u, int32_t ts) {
    int32_t wts = ts > 3 ? 3 : ts;
    Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                    (int32_t)(x1 - x0), (int32_t)(y1 - y0), wts);
    int32_t ms = (int32_t)(((y1 - y0) * 0.55f) / MENU_TEXT_BOX);
    if (ms < 1) ms = 1;
    int32_t maxMs = (int32_t)(1.7f * u);
    if (maxMs < 1) maxMs = 1;
    if (ms > maxMs) ms = maxMs;
    MenuTextDraw(s, label, (int32_t)(x0 + 18 * u), (int32_t)((y0 + y1) / 2 - 8 * ms), ms, SS_TEXT_INK);
    MenuTextDraw(s, value, (int32_t)(x1 - 18 * u - MenuTextWidth(value, ms)),
                 (int32_t)((y0 + y1) / 2 - 8 * ms), ms, SS_TEXT_RED);
}

static void PaintDeveloperOverlay(const SSurf* s, const SecondScreenSnapshot* snap, float x0, float y0,
                                  float x1, float y1, float u, int32_t ts) {
    const char* labels[8] = { "VERSION", "MODEL", "FPS NOW", "FPS AVG", "CORE1", "SCREEN", "AREA", "ROOM" };
    char values[8][16];
#ifndef __ANDROID__
#ifdef TMC_3DS
    double currentFps = Port_PPU_3DS_CurrentFps();
    double averageFps = Port_PPU_3DS_AverageFps();
    snprintf(values[0], sizeof(values[0]), "%s", TMC_PORT_VERSION);
    snprintf(values[1], sizeof(values[1]), "%s", Platform3DS_IsNew3DS() ? "NEW 3DS" : "OLD 3DS");
    snprintf(values[2], sizeof(values[2]), "%.0f", currentFps);
    snprintf(values[3], sizeof(values[3]), "%.0f", averageFps);
    snprintf(values[4], sizeof(values[4]), "%u", Platform3DS_Core1TimeLimit());
    snprintf(values[5], sizeof(values[5]), "%s", Port_Config_WidescreenEnabled() ? "WIDE" : "NATIVE");
#else
    for (int i = 0; i < 6; ++i) snprintf(values[i], sizeof(values[i]), "N A");
#endif
#else
    for (int i = 0; i < 6; ++i) snprintf(values[i], sizeof(values[i]), "N A");
#endif
    snprintf(values[6], sizeof(values[6]), "%02X", snap->area);
    snprintf(values[7], sizeof(values[7]), "%02X", snap->room);

    float gap = 7 * u;
    float rowH = (y1 - y0 - 7 * gap) / 8;
    for (int i = 0; i < 8; ++i) {
        float ry = y0 + i * (rowH + gap);
        DrawDiagnosticRow(s, x0, ry, x1, ry + rowH, labels[i], values[i], u, ts);
    }
}

/* Master volume as the nearest cycle stop (0/25/50/75/100), read from the
 * live mixer exactly like the imgui slider does. */
static int GetVolumeStop(void) {
    int pct = (int)(Port_Audio_GetMasterVolume() * 100.0f + 12.5f) / 25 * 25;
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

/* Current display state of a row: fills the value label and returns
 * nonzero when the row should wear the red "active" chip. */
static int GetSettingState(int row, char* out, int outCap) {
    int on = 0;
    const char* txt = "OFF";
    switch (row) {
        case SS_SET_TOP_HUD:
            /* The row states what the top screen DOES: SHOW is the (red)
             * default, HIDE hands vitals duty to this panel. */
            on = !Port_Config_GetHideTopHud();
            txt = on ? "SHOW" : "HIDE";
            break;
        case SS_SET_WIDESCREEN: on = Port_Config_WidescreenEnabled(); break;
        case SS_SET_FOLLOW: on = Port_Config_GetSecondScreenFollowCam(); break;
        case SS_SET_CRESTS: on = Port_Config_GetSecondScreenCrestPins(); break;
        case SS_SET_FLOOR_RETURN: on = Port_Config_GetSecondScreenFloorReturn(); break;
        case SS_SET_TURBO:
#ifdef TMC_3DS
            snprintf(out, (size_t)outCap, "X%u", Port_Config_GetTurboMultiplier());
            return 1;
#else
            snprintf(out, (size_t)outCap, "N A");
            return 0;
#endif
        case SS_SET_VOLUME: {
            int pct = GetVolumeStop();
            snprintf(out, (size_t)outCap, "%d", pct);
            return pct > 0;
        }
        case SS_SET_AUTOSAVE: on = Port_QuickSave_AutoEnabled() != 0; break;
        case SS_SET_COLOR_CORRECTION: on = Port_Config_GetColorCorrection(); break;
        case SS_SET_SHOW_FPS: on = Port_Config_GetShowFps(); break;
        case SS_SET_HOLD_ADVANCE: on = Port_Config_GetHoldToAdvanceText(); break;
        case SS_SET_RANDOMIZER: on = Port_Config_GetRandoEnabled(); break;
        case SS_SET_BACKDROP: {
            /* A cycle row like MASTER VOLUME: the value names the style and
             * returns early with its own text. The chip turns red once the
             * panel is off the menu's own parchment, which is how every
             * other row reads "not the shipped behaviour". */
            int style = BackdropStyleCfg();
            snprintf(out, (size_t)outCap, "%s", kBackdropWords[style]);
            return style != SS_BACKDROP_PARCHMENT;
        }
        case SS_SET_SWAP_SCREENS: {
            /* Nothing about this one is live: the shell picks the game's
             * display at launch. So report what is actually true right now
             * — ON/OFF while the flag matches the screens the player is
             * looking at, RESTART while it doesn't (just toggled, or the
             * firmware refused the display the shell asked for and the app
             * fell back to a normal launch). */
            int want = Port_Config_GetSecondScreenSwap() ? 1 : 0;
            int active = Port_SecondScreen_GameOnSecondaryDisplay();
            snprintf(out, (size_t)outCap, "%s", want != active ? "RESTART" : (want ? "ON" : "OFF"));
            return want;
        }
#ifdef TMC_3DS
        case SS_SET_ASPECT_RATIO:
            snprintf(out, (size_t)outCap, "%s", Port_Config_Get3DSAspectRatioName());
            return 1;
        case SS_SET_DISPLAY_STYLE:
            snprintf(out, (size_t)outCap, "%s", Port_Config_Get3DSDisplayStyleName());
            return Port_Config_Get3DSDisplayStyle() != PORT_3DS_DISPLAY_BILINEAR;
#endif
    }
    if (row != SS_SET_TOP_HUD) {
        txt = on ? "ON" : "OFF";
    }
    snprintf(out, (size_t)outCap, "%s", txt);
    return on;
}

/* Root and submenu compositor. Large menu-button plates provide the same
 * hierarchy and tap language as the sibling port; Minish Cap's decoded
 * parchment, chips, font, and palette keep it native to this game. */
static void PaintSettingsPanel(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                               float ry0, float rx1, float ry1, float u, int32_t ts, int page, uint32_t tick,
                               uint32_t dumpFlashUntil) {
    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 6 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    int32_t hms = (int32_t)(2.4f * u);
    if (hms < 1) hms = 1;
    float headerH = MENU_TEXT_BOX * hms + 24 * u;
    DrawPanelHeaderChip(s, (rx0 + rx1) / 2, iy0, SettingsPageTitle(page), hms, u);
    if (page != SS_SETTINGS_ROOT) {
        DrawSettingsBack(s, tl, ix0 + 4 * u, iy0, headerH, SettingsBackPage(page), u, ts);
    }

    float x0 = ix0 + 6 * u, x1 = ix1 - 6 * u;
    float y0 = iy0 + headerH + 12 * u;
    if (page == SS_SETTINGS_ROOT) {
#ifdef TMC_3DS
        static const char* const labels[4] = { "SCREEN", "GAMEPLAY", "DEVELOPER", "RANDOMIZER" };
        static const uint8_t pages[4] = {
            SS_SETTINGS_SCREEN, SS_SETTINGS_GAMEPLAY, SS_SETTINGS_DEVELOPER, SS_SETTINGS_RANDOMIZER
        };
        const int rootRows = 4;
#else
        static const char* const labels[3] = { "SCREEN", "GAMEPLAY", "DEVELOPER" };
        static const uint8_t pages[3] = { SS_SETTINGS_SCREEN, SS_SETTINGS_GAMEPLAY, SS_SETTINGS_DEVELOPER };
        const int rootRows = 3;
#endif
        float gap = 14 * u;
        float rowH = (iy1 - y0 - (rootRows - 1) * gap) / rootRows;
        if (rowH > 124 * u) rowH = 124 * u;
        for (int i = 0; i < rootRows; ++i) {
            float ry = y0 + i * (rowH + gap);
            DrawSettingsNavRow(s, tl, x0, ry, x1, ry + rowH, labels[i], pages[i], u, ts);
        }
        return;
    }

    if (page == SS_SETTINGS_DEVELOPER) {
        float gap = 10 * u;
        float rowH = (iy1 - y0 - 2 * gap) / 3;
        if (rowH > 92 * u) rowH = 92 * u;
        char dumpValue[16];
        snprintf(dumpValue, sizeof(dumpValue), "%s",
                 (int32_t)(dumpFlashUntil - tick) > 0 ? "DONE" : "WRITE");
        DrawMenuButton(s, x0, y0, x1, y0 + rowH, "", 0, 0, u, ts);
        int32_t ms = (int32_t)(2.0f * u);
        if (ms < 1) ms = 1;
        MenuTextDraw(s, "MEM DUMP", (int32_t)(x0 + 24 * u), (int32_t)(y0 + rowH / 2 - 8 * ms), ms,
                     SS_TEXT_NAVY);
        MenuTextDraw(s, dumpValue, (int32_t)(x1 - 24 * u - MenuTextWidth(dumpValue, ms)),
                     (int32_t)(y0 + rowH / 2 - 8 * ms), ms, SS_TEXT_RED);
        AddTarget(tl, x0, y0, x1, y0 + rowH, SS_ACT_DEVELOPER_DUMP, 0);
        DrawSettingsValueRow(s, tl, x0, y0 + rowH + gap, x1, y0 + 2 * rowH + gap,
                             SS_SET_SHOW_FPS, u, ts);
        DrawSettingsNavRow(s, tl, x0, y0 + 2 * (rowH + gap), x1, y0 + 3 * rowH + 2 * gap, "OVERLAY",
                           SS_SETTINGS_OVERLAY, u, ts);
        return;
    }

    if (page == SS_SETTINGS_OVERLAY) {
        PaintDeveloperOverlay(s, snap, x0, y0, x1, iy1, u, ts);
        return;
    }

    uint8_t rows[SS_SET_COUNT];
    int nRows = SettingsPageRows(page, rows);
    float gap = 7 * u;
    float rowH = (iy1 - y0 - (nRows - 1) * gap) / nRows;
    if (rowH > 76 * u) rowH = 76 * u;
    for (int slot = 0; slot < nRows; ++slot) {
        float ry = y0 + slot * (rowH + gap);
        DrawSettingsValueRow(s, tl, x0, ry, x1, ry + rowH, rows[slot], u, ts);
    }
}

#ifdef TMC_3DS
static void PaintRandomizerConfirmation(const SSurf* s, TargetList* tl, float u, int32_t ts, int enable) {
    tl->n = 0;
    const float x0 = 18 * u;
    const float x1 = s->w - 18 * u;
    const float y0 = 18 * u;
    const float y1 = s->h - 18 * u;
    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                     (int32_t)(x1 - x0), (int32_t)(y1 - y0), ts);

    int32_t titleScale = (int32_t)(2.2f * u);
    if (titleScale < 1) titleScale = 1;
    MenuTextCentered(s, enable ? "ENABLE RANDOMIZER" : "DISABLE RANDOMIZER", (x0 + x1) / 2,
                     y0 + 10, titleScale, SS_TEXT_NAVY);

    static const char* const lines[] = {
        "RANDOMIZER REQUIRES A NEW GAME.",
        "THE ACTIVE PROFILE SAVE, AUTOSAVES,",
        "SAVESTATES AND RANDOMIZER DATA",
        "WILL BE DELETED. THE ROM IS KEPT.",
        "THE GAME WILL RESTART.",
    };
    int32_t textScale = (int32_t)(1.55f * u);
    if (textScale < 1) textScale = 1;
    float lineY = y0 + 38;
    const float lineStep = MENU_TEXT_BOX * textScale + 2;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        MenuTextCentered(s, lines[i], (x0 + x1) / 2, lineY + i * lineStep, textScale, SS_TEXT_INK);
    }

    const float gap = 8;
    const float buttonY1 = y1 - 8;
    const float buttonY0 = buttonY1 - 34;
    const float middle = (x0 + x1) / 2;
    DrawMenuButton(s, x0 + 14 * u, buttonY0, middle - gap / 2, buttonY1, "CANCEL", 0, 0, u, ts);
    DrawMenuButton(s, middle + gap / 2, buttonY0, x1 - 14 * u, buttonY1, "CONTINUE", 0, 0, u, ts);
    AddTarget(tl, x0 + 14 * u, buttonY0, middle - gap / 2, buttonY1, SS_ACT_RANDO_CANCEL, 0);
    AddTarget(tl, middle + gap / 2, buttonY0, x1 - 14 * u, buttonY1, SS_ACT_RANDO_CONFIRM,
              (uint8_t)(enable != 0));
}
#endif

int Port_SecondScreen_IsDeveloperOverlayOpen(void) {
    int open;
    UI_LOCK();
    open = sUi.tab == SS_TAB_SETTINGS && sUi.settingsPage == SS_SETTINGS_OVERLAY;
    UI_UNLOCK();
    return open;
}

/* ------------------------------------------------------------------ */
/*  Sidebar                                                            */
/* ------------------------------------------------------------------ */

/* One equip ring: recessed stone disc, double gold ring, the equipped
 * item's icon, and the HUD's own A/B bubble as the badge. An armed ring
 * breathes. (The quest screen's circular sockets are the model.) */
static void DrawItemRing(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float cx,
                         float cy, float r, int isA, int armed, float u, uint32_t tick) {
    uint32_t ink = Port_SecondScreenTheme_Color(SSC_MENU_INK);
    uint32_t cream = Port_SecondScreenTheme_Color(SSC_MENU_CREAM);
    uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
    uint32_t goldDim = MixColor(gold, cream, 96);

    if (armed) {
        /* Slow gold breath around the ring while it waits for an item tap.
         * The band lies outside the ring, on the bare backdrop, so it is
         * blended out of the BACKDROP color rather than out of cream — on
         * the dark backdrop a cream-based halo read as a bright smear
         * around the ring instead of the ring glowing. */
        float pt = 0.5f + 0.5f * sinf((float)(tick % 48) * (6.28318f / 48.0f));
        FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r + 7 * u), (int32_t)(r + 3 * u),
                 MixColor(Port_SecondScreenTheme_BackdropColor(), gold, (uint32_t)(60 + 150 * pt)));
    }

    FillCircle(s, (int32_t)cx, (int32_t)cy, (int32_t)r,
               Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK));

    /* Icon first, rings after, and sized to fit INSIDE the gold band: the
     * item art is a 16x16 square, so what has to clear the band is its
     * diagonal, not its side (owner feedback — icons with corner ink, like
     * the sword, poked out of the ring). The innermost band edge sits at
     * r - 6u; keep a pixel of daylight inside that, then take the largest
     * integer nearest-neighbor scale whose circumscribed circle still
     * fits. Capped like the item grid, which also keeps the icon clear of
     * the shoulder badge. */
    uint8_t itemId = isA ? snap->equippedA : snap->equippedB;
    if (itemId >= ITEMID_BOTTLE1 && itemId < ITEMID_BOTTLE1 + 4) {
        uint8_t content = snap->bottleContents[itemId - ITEMID_BOTTLE1];
        if (content != 0) {
            itemId = content;
        }
    }
    if (itemId != 0) {
        float rFit = r - 7.0f * u;
        int32_t iconScale = (int32_t)(rFit / (8.0f * 1.41421356f));
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, (int32_t)(cx - 8 * iconScale),
                                             (int32_t)(cy - 8 * iconScale), iconScale, itemId);
    }

    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)r, (int32_t)(r - 1.5f * u), ink);
    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r - 3 * u), (int32_t)(r - 4.5f * u), goldDim);
    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r - 4.5f * u), (int32_t)(r - 6 * u), gold);

    /* Keep the native HUD's bomb/arrow count attached to the equipped A/B
     * item as well as to its item-grid cell.  At the 3DS' 320x240 layout a
     * ring is only about forty pixels across, so the authentic 16x8 pair
     * sits on the lower shoulder just as it overlays the top-HUD icon. */
    {
        int32_t ammoScale = (int32_t)(r / 24.0f);
        if (ammoScale < 1) ammoScale = 1;
        if (ammoScale > 4) ammoScale = 4;
        DrawItemAmmoCount(s, snap, itemId, (int32_t)(cx - 8 * ammoScale),
                          (int32_t)(cy + r - 8 * ammoScale - 2 * u), ammoScale);
    }

    /* Button badge on the ring's top-right shoulder, sized to sit ON the
     * band rather than reach into the icon's space. */
    const SecondScreenThemeSprite* badge = Port_SecondScreenTheme_Get(isA ? SST_BUTTON_A : SST_BUTTON_B);
    float bx = cx + r * 0.71f, by = cy - r * 0.71f;
    if (badge != NULL) {
        int32_t bs = (int32_t)(r * 0.6f / badge->w + 0.5f);
        if (bs < 1) bs = 1;
        BlitSprite(s, badge, (int32_t)(bx - badge->w * bs / 2.0f), (int32_t)(by - badge->h * bs / 2.0f),
                   bs);
    } else {
        FillCircle(s, (int32_t)bx, (int32_t)by, (int32_t)(10 * u),
                   Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK));
        FillRing(s, (int32_t)bx, (int32_t)by, (int32_t)(10 * u), (int32_t)(8.5f * u), gold);
        char tag[2] = { isA ? 'A' : 'B', 0 };
        int32_t tfs = (int32_t)(1.6f * u) > 0 ? (int32_t)(1.6f * u) : 1;
        DrawTextStr(s, tag, (int32_t)(bx - 2.5f * tfs), (int32_t)(by - 3.5f * tfs), tfs,
                    Port_SecondScreenTheme_Color(SSC_MENU_INK), Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
    }

    AddTarget(tl, cx - r, cy - r, cx + r, cy + r, SS_ACT_RING, isA ? 1 : 2);
}

/* ------------------------------------------------------------------ */
/*  R-button prompt                                                    */
/* ------------------------------------------------------------------ */

/* The words the HUD's own prompt art spells, by ActionRButton value
 * (include/player.h). Only the lettering stand-in: once the theme can
 * stamp the real label frames this table stops being reached. */
static const char* const kRActionWords[] = {
    NULL,  "CANCEL", "DROP", "THROW",  "READ", "CHECK", "OPEN",
    "SPEAK", "GRAB", "LIFT", "GROW", "SHRINK", "ROLL",
};

/* The game's contextual R prompt, on the panel because the player may be
 * running with the top HUD hidden. Laid out in a band whose height never
 * changes, so nothing below it moves when the prompt comes and goes; the
 * band simply stays empty when there is no action. Authentic art first
 * (the HUD's R glyph + the game's own label frame), then the panel's own
 * button plate lettered R plus the word in the banner font. */
static void DrawRPrompt(const SSurf* s, const SecondScreenSnapshot* snap, float x, float y, float w,
                        float bandH, float u, int32_t ts) {
    if (snap->rActionFrame == 0) {
        return;
    }
    const char* word = NULL;
    if (snap->rActionId < (uint8_t)(sizeof(kRActionWords) / sizeof(kRActionWords[0]))) {
        word = kRActionWords[snap->rActionId];
    }

    /* Half-pixel units give the 3DS panel a true 1.5x prompt: midway
     * between v0.14's small 1x art and v0.15's oversized 2x art. */
    int32_t scale2 = (int32_t)(9.0f * u + 0.5f);
    if (scale2 < 2) scale2 = 2;
    int32_t ms = scale2 / 2;
    if (ms < 1) ms = 1;
    while (ms > 1 && word != NULL && MenuTextWidth(word, ms) > (int32_t)(w - 4 * u)) --ms;
    const SecondScreenThemeSprite* rSprite = Port_SecondScreenTheme_Get(SST_BUTTON_R);
    int32_t rw = ((rSprite != NULL ? rSprite->w : 18) * scale2 + 1) / 2;
    int32_t rh = ((rSprite != NULL ? rSprite->h : 16) * scale2 + 1) / 2;
    float gap = 4 * u;
    float labelH = 16 * ms;
    float top = y + (bandH - rh - gap - labelH) / 2;
    if (top < y) top = y;
    float gx = x + (w - rw) / 2;

    if (!Port_SecondScreenTheme_DrawRButton(s->px, s->w, s->h, s->stride, (int32_t)gx,
                                            (int32_t)top, scale2)) {
        int32_t bts = (int32_t)(rh / 26.0f);
        if (bts < 1) bts = 1;
        if (bts > ts) bts = ts;
        DrawButtonPlateFallback(s, gx, top, gx + rw, top + rh, 0, bts);
        MenuTextCentered(s, "R", gx + rw / 2, top + rh / 2, ms, SS_TEXT_NAVY);
    }

    float ly = top + rh + gap;
    if (Port_SecondScreenTheme_DrawActionLabel(s->px, s->w, s->h, s->stride,
                                               (int32_t)(x + w / 2),
                                               (int32_t)ly, scale2, snap->rActionFrame) == 0 &&
        word != NULL) {
        float labelW = (float)MenuTextWidth(word, ms);
        float lx = x + (w - labelW) / 2;
        /* The one label on this panel lettered straight onto the backdrop
         * with nothing behind it, so it is the one that has to follow the
         * backdrop: the menu's dark ink vanishes on the near-black. (The
         * decoded label art above is HUD sprites — outlined, so it reads on
         * either.) */
        MenuTextDraw(s, word, (int32_t)lx, (int32_t)ly, ms,
                     Port_SecondScreenTheme_BackdropIsDark() ? SS_TEXT_WHITE : SS_TEXT_INK);
    }
}

/* Bottom-screen counterpart of the native ten-cell/forty-quarter sword
 * charge meter.  It is intentionally present only while the gameplay HUD
 * is hidden: with the HUD visible the original BG0 meter remains the source
 * of truth.  Full-charge actions cycle through four brightness phases at
 * the same 8/16-tick cadence as DrawChargeBar's action 4/5 artwork. */
static void DrawChargeIndicator(const SSurf* s, const SecondScreenSnapshot* snap, float x, float y,
                                float w, float bandH, float u, uint32_t tick) {
    if (!Port_SecondScreenChargeVisible(snap)) {
        return;
    }

    const int steps = Port_SecondScreenChargeSteps(snap);
    /* Preserve the native meter's roughly 12:1 aspect.  At the 320x240 3DS
     * scale this also leaves every cell at least four pixels wide, so a
     * one-pixel rim never consumes its quarter-fill interior. */
    float barW = 160 * u;
    if (barW > w - 16 * u) barW = w - 16 * u;
    float gap = u;
    if (gap < 1) gap = 1;
    float cellW = (barW - 9 * gap) / 10;
    float barH = 14 * u;
    if (barH < 5) barH = 5;
    float bx = x + (w - barW) / 2;
    float by = y + (bandH - barH) / 2;
    int32_t rim = (int32_t)u;
    if (rim < 1) rim = 1;

    uint32_t ink = Port_SecondScreenTheme_Color(SSC_MENU_INK);
    uint32_t empty = Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK);
    uint32_t fill = Port_SecondScreenTheme_Color(SSC_GOLD);
    if (snap->chargeAction == 4 || snap->chargeAction == 5) {
        uint32_t cadence = snap->chargeAction == 4 ? 8u : 16u;
        uint32_t phase = (tick / cadence) & 3u;
        fill = MixColor(fill, Port_SecondScreenTheme_Color(SSC_MENU_WHITE), phase * 42u);
    }

    for (int cell = 0; cell < 10; cell++) {
        int quarters = steps - cell * 4;
        if (quarters < 0) quarters = 0;
        if (quarters > 4) quarters = 4;
        int32_t x0 = (int32_t)(bx + cell * (cellW + gap));
        int32_t x1 = (int32_t)(bx + cell * (cellW + gap) + cellW);
        int32_t y0 = (int32_t)by;
        int32_t y1 = (int32_t)(by + barH);
        FillRect(s, x0, y0, x1, y1, empty);
        OutlineRect(s, x0, y0, x1, y1, rim, ink);
        if (quarters != 0 && x1 - x0 > rim * 2 && y1 - y0 > rim * 2) {
            int32_t innerW = x1 - x0 - rim * 2;
            int32_t fillW = (innerW * quarters + 3) / 4;
            FillRect(s, x0 + rim, y0 + rim, x0 + rim + fillW, y1 - rim, fill);
        }
    }
}

/* Sidebar, right edge: hearts on top (the most-glanced info), the R
 * prompt band under them, the A/B equip rings centered in the middle, the
 * rupee/keys chip anchored to the bottom, just above the tab bar. */
static void PaintSidebar(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float x,
                         float y, float w, float h, float u, int32_t ts, uint32_t tick, int armedRing) {
    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_HAS_KEYS) != 0;

    /* Base art scale for the sidebar's decoded HUD pieces: the 220u column
     * fits about 84 art pixels across. */
    int32_t k = (int32_t)(w / 84.0f);
    if (k < 1) k = 1;

    /* Hearts: up to TMC's 20, five to a row wrapping to at most four rows,
     * quarter-heart states exactly like DrawHearts (health is in eighths;
     * the HUD shows quarters). Ten across meant one row had to span the
     * whole sidebar, which held the tiles near the base scale — a ~24px
     * heart on a 1080p panel, too small to read at a glance, which is the
     * one job the vitals block has. Half the columns buys better than
     * double the scale out of the sidebar's spare height. */
    int maxHearts = snap->maxHealth / 8;
    if (maxHearts > 20) maxHearts = 20;
    if (maxHearts < 1) maxHearts = 1;
    int cols = maxHearts < 5 ? maxHearts : 5;
    int rows = (maxHearts + cols - 1) / cols;
    /* Tiles butt together like the HUD's tilemap, so a row is just cols*8
     * art pixels wide; 8u of side air keeps it off the sidebar edges.
     * Early saves narrow the grid and so grow the tile, but only to 6u per
     * art pixel — past that a three-heart file reads as a title card
     * rather than a status line. */
    int32_t hk = (int32_t)((w - 8 * u) / (cols * 8.0f));
    int32_t hkMax = (int32_t)(6 * u);
    if (hk > hkMax) hk = hkMax;
    /* Short panels only: hold the block to a third of the column so the R
     * band, rings and chip below always keep somewhere to sit. */
    float blockCap = h / 3;
    if (rows * 8 * hk > blockCap) hk = (int32_t)(blockCap / (rows * 8));
    if (hk < 1) hk = 1;
    float hx0 = x + (w - cols * 8 * hk) / 2;
    float hy = y + 12 * u;
    int quarters = snap->health == 1 ? 1 : snap->health / 2;
    int fullHearts = quarters / 4;
    int frac = quarters & 3;
    for (int i = 0; i < maxHearts; i++) {
        int state;
        if (i < fullHearts) {
            state = SST_HEART_FULL;
        } else if (i == fullHearts && frac != 0) {
            state = SST_HEART_Q1 + (frac - 1);
        } else {
            state = SST_HEART_EMPTY;
        }
        const SecondScreenThemeSprite* hs = Port_SecondScreenTheme_Get(state);
        int32_t hx = (int32_t)(hx0 + (i % cols) * 8 * hk);
        int32_t hyy = (int32_t)(hy + (i / cols) * 8 * hk);
        if (hs != NULL) {
            BlitSprite(s, hs, hx, hyy, hk);
        } else {
            uint32_t c = state == SST_HEART_EMPTY ? RGB(0x38, 0x20, 0x20)
                                                  : Port_SecondScreenTheme_Color(SSC_HEART_RED);
            FillRect(s, hx + hk, hyy + hk, hx + 7 * hk, hyy + 7 * hk, c);
        }
    }
    float vitalsBottom = hy + rows * 8 * hk + 4 * u;

    /* When the top HUD is disabled, preserve the sword-charge cue beside
     * the other glanceable vitals.  Reserve no space while inactive so the
     * established sidebar geometry is unchanged during ordinary play. */
    if (Port_SecondScreenChargeVisible(snap)) {
        float chargeBandH = 28 * u;
        DrawChargeIndicator(s, snap, x, vitalsBottom, w, chargeBandH, u, tick);
        vitalsBottom += chargeBandH;
    }

    /* R prompt band, reserved whether or not there is a prompt so the
     * rings below never shift when one appears. */
    /* The prompt shares the sidebar with the A/B rings, and at 34u it was
     * dwarfed by them — this is the one contextual control on the panel, so
     * it gets a band it can actually be read in. */
    float rBandH = 144 * u;
    DrawRPrompt(s, snap, x, vitalsBottom, w, rBandH, u, ts);
    vitalsBottom += rBandH;

    /* Bare counters sit above the tab bar without another stone tray. */
    int chipRows = isDungeon ? 2 : 1;
    int32_t ik = 2 * k;              /* icon scale (16x16 art) */
    int32_t ck = k + (k + 1) / 2;    /* digit scale (8x16 HUD font) */
    float chipPad = 2 * u;
    float numberW = 3 * 8 * ck;
    float groupGap = 6 * u;
    float innerW = w - 2 * chipPad;
    while (ck > 1 && 16 * ik + groupGap + 3 * 8 * ck > innerW) --ck;
    while (ik > 1 && 16 * ik + groupGap + 3 * 8 * ck > innerW) --ik;
    numberW = 3 * 8 * ck;
    float rowH = 16 * (ik > ck ? ik : ck);
    float chipH = chipRows * rowH + (chipRows - 1) * 6 * u + 2 * chipPad;
    float chipY = y + h - chipH - 30 * u;
    {
        float ry = chipY + chipPad;
        float groupW = 16 * ik + groupGap + numberW;
        float ixl = x + (w - groupW) / 2;
        float ixr = ixl + groupW;
        float dy = (16 * ik - 16 * ck) / 2.0f; /* digits centered on the icon row */
        BlitSprite(s, Port_SecondScreenTheme_Get(SST_RUPEE_WALLET0 + (snap->walletType & 3)), (int32_t)ixl,
                   (int32_t)ry, ik);
        DrawHudNumber(s, (int32_t)ixr, (int32_t)(ry + dy), ck, snap->rupees, 3,
                      snap->walletMax != 0 && snap->rupees >= snap->walletMax);
        if (isDungeon) {
            ry += rowH + 6 * u;
            BlitSprite(s, Port_SecondScreenTheme_Get(SST_KEY), (int32_t)ixl, (int32_t)ry, ik);
            DrawHudNumber(s, (int32_t)ixr, (int32_t)(ry + dy), ck, snap->dungeonKeys, 2, 0);
        }
    }

    /* Equip rings, centered between the vitals and the chip: A above B.
     * Tap a ring to arm it; the next item-grid tap assigns that slot. */
    float mid = (vitalsBottom + chipY) / 2;
    float ringR = 60 * u;
    if (ringR > w / 2 - 14 * u) ringR = w / 2 - 14 * u;
    float quarter = (chipY - vitalsBottom) / 4 - 7 * u;
    if (ringR > quarter) ringR = quarter;
    if (ringR >= 10 * u) {
        float lowerBy = 24 * u;
        float maxLower = chipY - (mid + 2 * ringR + 6 * u);
        if (lowerBy > maxLower) lowerBy = maxLower;
        if (lowerBy > 0) mid += lowerBy;
        float rcx = x + w / 2;
        DrawItemRing(s, snap, tl, rcx, mid - ringR - 6 * u, ringR, 1, armedRing == 1, u, tick);
        DrawItemRing(s, snap, tl, rcx, mid + ringR + 6 * u, ringR, 0, armedRing == 2, u, tick);
    }
}

/* ------------------------------------------------------------------ */
/*  Tab bar                                                            */
/* ------------------------------------------------------------------ */

/* One tab: the pause menu's labelled button (9.png's SLEEP/SAVE plate).
 * The tab that owns the panel is the SAME button at the SAME geometry,
 * drawn in the button's own pressed state and wearing a thin gold keyline
 * — owner feedback: the old red chip differed in look and height from its
 * neighbours. */
static void DrawTabButton(const SSurf* s, TargetList* tl, float x0, float y0, float x1, float y1,
                          const char* label, int active, int tabId, float u, int32_t ts) {
    DrawMenuButton(s, x0, y0, x1, y1, label, active, active, u, ts);
    AddTarget(tl, x0, y0, x1, y1, SS_ACT_TAB, (uint8_t)tabId);
}

/* Bottom tab bar: [QUEST][MAP][ITEMS] + the square settings button, all
 * one button family at one height. The 34u dead band underneath keeps
 * every button clear of the Android gesture zone (~30 real px). */
static void PaintTabBar(const SSurf* s, TargetList* tl, float u, int32_t ts, int activeTab) {
    float tabH = 96 * u;
    float y = s->h - tabH + 4 * u;
    float bh = tabH - 34 * u;
    float sq = bh;
    float sx1 = s->w - 8 * u;
    float sx0 = sx1 - sq;
    float x0 = 8 * u, xr = sx0 - 8 * u, gap = 8 * u;
    float bw = (xr - x0 - 2 * gap) / 3.0f;

    DrawTabButton(s, tl, x0, y, x0 + bw, y + bh, "QUEST", activeTab == SS_TAB_QUEST, SS_TAB_QUEST, u, ts);
    DrawTabButton(s, tl, x0 + bw + gap, y, x0 + 2 * bw + gap, y + bh, "MAP", activeTab == SS_TAB_MAP,
                  SS_TAB_MAP, u, ts);
    DrawTabButton(s, tl, x0 + 2 * (bw + gap), y, x0 + 3 * bw + 2 * gap, y + bh, "ITEMS",
                  activeTab == SS_TAB_ITEMS, SS_TAB_ITEMS, u, ts);
    /* Settings keeps its cog glyph instead of a word, on the same plate —
     * an empty label, not a null one, so the art path never has to guess. */
    DrawTabButton(s, tl, sx0, y, sx1, y + bh, "", activeTab == SS_TAB_SETTINGS, SS_TAB_SETTINGS, u, ts);
    float cog = bh * 0.5f / 9.0f;
    DrawPixelArt(s, kCogArt, 9, (sx0 + sx1) / 2 - 4.5f * cog, y + bh / 2 - 4.5f * cog, cog,
                 Port_SecondScreenTheme_Color(SSC_BANNER_NAVY));
}

/* ------------------------------------------------------------------ */
/*  Frame composition                                                  */
/* ------------------------------------------------------------------ */

void Port_SecondScreen_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                 const SecondScreenSnapshot* snap, uint32_t tick) {
    SSurf s = { pixels, width, height, strideInPixels };
    if (pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    if (!snap->inGame) {
        UI_LOCK();
        sTapTargetCount = 0; /* no gameplay, no touch UI */
        sUi.mapLive = 0;
        sUi.armedRing = 0;
        sUi.floorPreview = SS_NO_FLOOR;
        sUi.playerFloorDisp = SS_NO_FLOOR;
        sUi.regionState = SS_REGION_OFF; /* a zoom never survives a load */
        sUi.questView = SS_QUEST_MAIN;   /* nor does an open list */
        sUi.settingsPage = SS_SETTINGS_ROOT;
        sUi.randoConfirmActive = 0;
        UI_UNLOCK();
        sLastFix.valid = 0; /* stale fixes must not survive into a new save */
        sCam.valid = 0;
        PaintCinema(&s, tick);
        return;
    }

    /* Theme decode happens lazily here — only during gameplay, when the
     * ROM tables are guaranteed resolved. Until it reports ready
     * everything below still renders through its per-element fallbacks. */
    Port_SecondScreenTheme_Ready();

    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0;

    int tab, armedRing, regionState, settingsPage, randoConfirmActive, randoDesired;
    int32_t regionId;
    uint32_t dumpFlashUntil;
    UI_LOCK();
    if (isDungeon) {
        sUi.regionState = SS_REGION_OFF; /* the world map is gone; so is its zoom */
    }
    tab = sUi.tab;
    armedRing = sUi.armedRing;
    int questView = sUi.questView;
    int wholeMap = sUi.wholeMap;
    regionState = sUi.regionState;
    regionId = sUi.regionId;
    settingsPage = sUi.settingsPage;
    dumpFlashUntil = sUi.dumpFlashUntil;
    randoConfirmActive = sUi.randoConfirmActive;
    randoDesired = sUi.randoDesired;
    sUi.mapLive = 0; /* set again by PaintOverworld when the map is up */
    UI_UNLOCK();

    int followCfg = Port_Config_GetSecondScreenFollowCam();
    int crestCfg = Port_Config_GetSecondScreenCrestPins();
    int returnCfg = Port_Config_GetSecondScreenFloorReturn();

    /* Scale units: u drives the layout (reference design is a 720p min
     * axis), ts is the integer art scale for the decoded menu dressing. */
    float u = (width < height ? width : height) / 720.0f;
    int32_t minAxis = width < height ? width : height;
    int32_t ts = minAxis / 240;
    if (ts < 2) ts = 2;
    if (ts > 6) ts = 6;

    /* The backdrop style has to reach the theme before ANY paint: the fill
     * below reads it, so do the quest sub-screens (which draw their own
     * backdrop) and the two places that pick a color to sit on it. */
    Port_SecondScreenTheme_SetBackdropStyle(BackdropStyleCfg());

    /* The whole surface is the panel's backdrop; panels lay their
     * slab/chips over it. By default that is the pause menu's parchment
     * (flat cream until the pattern decodes); the settings row can quiet it
     * to flat cream or to the idle near-black.
     *
     * Nothing else needs re-toning for the dark option: the pause menu's
     * dressing is light art with light outlines — stone slabs, pale button
     * plates, chips whose border_type-9 frame is light around a dark
     * interior, and HUD sprites the game already draws over arbitrary
     * scenes — so it all reads as light-on-dark. The two exceptions are
     * handled at their sites: the R prompt's lettered fallback (ink) and
     * the armed ring's breath (blended out of cream). */
    Port_SecondScreenTheme_DrawBackdrop(s.px, s.w, s.h, s.stride, 0, 0, s.w, s.h, ts);

    float tabH = 96 * u;
    float sideW = 220 * u; /* widened for the grown rings/chip; map stays dominant */
    float mx0 = 10 * u, my0 = 10 * u;
    float mx1 = tab == SS_TAB_SETTINGS ? width - 10 * u : width - sideW - 4 * u;
    float my1 = height - tabH - 4 * u;

    TargetList tl = { .n = 0 };

    if (tab == SS_TAB_ITEMS) {
        PaintItemsPanel(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, tick, armedRing);
    } else if (tab == SS_TAB_QUEST) {
        PaintQuestPanel(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, tick, questView);
    } else if (tab == SS_TAB_SETTINGS) {
        PaintSettingsPanel(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, settingsPage, tick,
                           dumpFlashUntil);
    } else if (isDungeon) {
        PaintDungeon(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, tick, returnCfg);
    } else {
        /* A live region zoom replaces the map area; if its art can't be
         * drawn the view drops straight back to the world map, so the
         * panel is never left on a blank region. */
        int drewRegion = 0;
        if (regionState == SS_REGION_VIEW) {
            drewRegion = PaintRegion(&s, snap, &tl, mx0, my0, mx1, my1, u, tick, regionId);
            if (!drewRegion) {
                UI_LOCK();
                sUi.regionState = SS_REGION_OFF;
                UI_UNLOCK();
            }
        }
        if (!drewRegion) {
            PaintOverworld(&s, snap, &tl, mx0, my0, mx1, my1, u, tick, wholeMap, followCfg, crestCfg,
                           tick);
        }
    }

    if (tab != SS_TAB_SETTINGS) {
        PaintSidebar(&s, snap, &tl, width - sideW + 4 * u, 10 * u, sideW - 14 * u, height - tabH - 14 * u, u,
                     ts, tick, armedRing);
    }
    PaintTabBar(&s, &tl, u, ts, tab);
#ifdef TMC_3DS
    if (randoConfirmActive) {
        PaintRandomizerConfirmation(&s, &tl, u, ts, randoDesired);
    }
#endif

    /* Publish this frame's hit boxes for the tap thread. */
    UI_LOCK();
    memcpy(sTapTargets, tl.t, sizeof(TapTarget) * (size_t)tl.n);
    sTapTargetCount = tl.n;
    sUi.lastTick = tick;
    UI_UNLOCK();
}

/* ------------------------------------------------------------------ */
/*  Tap handling                                                       */
/* ------------------------------------------------------------------ */

/* Tap on the whole-Hyrule map: turn screen pixels into map-image
 * coordinates through the transform of the last painted frame, ask the
 * map screen's own zoom grid which tile that is, and — when it answers —
 * bracket that tile for a beat before the regional map opens. Returns 0
 * when nothing was picked (off-grid, or the grid isn't decodable), which
 * leaves the caller's plain follow/whole toggle in charge. */
static int PickMapRegion(int x, int y) {
    int picked = 0;
    UI_LOCK();
    if (sUi.mapLive && sUi.regionGridReady && sUi.mapScale > 0.0f) {
        int32_t ix = (int32_t)(((float)x - sUi.mapOx) / sUi.mapScale);
        int32_t iy = (int32_t)(((float)y - sUi.mapOy) / sUi.mapScale);
        int32_t region, rx0, ry0, rx1, ry1;
        /* Only the frame crop ever renders, so a tap on the letterbox
         * around the fitted view is not a tap on the map. */
        if (ix >= WMAP_CROP_X0 && iy >= WMAP_CROP_Y0 && ix < WMAP_CROP_X1 && iy < WMAP_CROP_Y1 &&
            ix < sUi.mapImgW && iy < sUi.mapImgH &&
            Port_SecondScreenWorldMap_GetRegionAt(ix, iy, &region, &rx0, &ry0, &rx1, &ry1)) {
            sUi.regionId = region;
            sUi.regionX0 = rx0;
            sUi.regionY0 = ry0;
            sUi.regionX1 = rx1;
            sUi.regionY1 = ry1;
            sUi.regionTick = sUi.lastTick;
            sUi.regionState = SS_REGION_BRACKET;
            picked = 1;
        }
    }
    UI_UNLOCK();
    return picked;
}


void Port_SecondScreen_OnTap(int x, int y, int longPress) {
    TapTarget hit;
    int found = 0;

    UI_LOCK();
    for (int i = 0; i < sTapTargetCount; i++) {
        const TapTarget* t = &sTapTargets[i];
        if (x >= t->x0 && x < t->x1 && y >= t->y0 && y < t->y1) {
            hit = *t;
            found = 1;
            break;
        }
    }
    UI_UNLOCK();
    if (!found) {
        return;
    }

    switch (hit.action) {
        case SS_ACT_TAB:
            UI_LOCK();
            /* Re-tapping the active panel tab returns to the map — the
             * reference's "toggle back" behavior. */
            sUi.tab = (sUi.tab == hit.arg && hit.arg != SS_TAB_MAP) ? SS_TAB_MAP : hit.arg;
            sUi.armedRing = 0;             /* changing tabs cancels a pending assignment */
            sUi.questView = SS_QUEST_MAIN; /* ...and closes a list left open */
            sUi.settingsPage = SS_SETTINGS_ROOT;
            UI_UNLOCK();
            break;
        case SS_ACT_SETTINGS_PAGE:
            UI_LOCK();
            sUi.settingsPage = hit.arg;
            UI_UNLOCK();
            break;
        case SS_ACT_SETTINGS_BACK:
            UI_LOCK();
            sUi.settingsPage = hit.arg;
            UI_UNLOCK();
            break;
        case SS_ACT_DEVELOPER_DUMP:
            UI_LOCK();
            sUi.dumpFlashUntil = sUi.lastTick + 40;
            UI_UNLOCK();
#ifdef TMC_3DS
            Port_PPU_3DS_WriteQuickDump();
#endif
            break;
        case SS_ACT_RANDO_CANCEL:
            UI_LOCK();
            sUi.randoConfirmActive = 0;
            UI_UNLOCK();
            break;
        case SS_ACT_RANDO_CONFIRM:
            UI_LOCK();
            sUi.randoConfirmActive = 0;
            UI_UNLOCK();
#ifdef TMC_3DS
            {
                extern void Port_Rando3DS_ConfirmModeChange(bool enabled);
                Port_Rando3DS_ConfirmModeChange(hit.arg != 0);
            }
#endif
            break;
        case SS_ACT_QUESTVIEW:
            UI_LOCK();
            sUi.questView = hit.arg;
            UI_UNLOCK();
            break;
        case SS_ACT_RING:
            UI_LOCK();
            sUi.armedRing = (sUi.armedRing == hit.arg) ? 0 : hit.arg;
            UI_UNLOCK();
            break;
        case SS_ACT_ITEM: {
            uint8_t armed;
            UI_LOCK();
            armed = sUi.armedRing;
            sUi.armedRing = 0;
            UI_UNLOCK();
            /* An armed ring picks the slot; otherwise tap = A, hold = B —
             * both through the engine-side RequestEquip flow. */
            uint8_t slot = armed ? (uint8_t)(armed - 1) : (uint8_t)(longPress ? 1 : 0);
            Port_SecondScreenState_RequestEquip(hit.arg, slot);
            break;
        }
        case SS_ACT_PLAQUE:
            UI_LOCK();
            if ((int8_t)hit.arg == sUi.playerFloorDisp) {
                sUi.floorPreview = SS_NO_FLOOR; /* tap Link's floor: back to live */
            } else {
                sUi.floorPreview = (int8_t)hit.arg;
                sUi.floorPreviewTick = sUi.lastTick;
                sUi.previewBaseFloor = sUi.playerFloorDisp;
                sUi.previewDungeon = sUi.curDungeon;
            }
            UI_UNLOCK();
            break;
        case SS_ACT_SETTING:
            switch (hit.arg) {
                case SS_SET_TOP_HUD:
                    /* Just the flag — the engine-side DrawUIElements gate
                     * reads it each frame. */
                    Port_Config_SetHideTopHud(!Port_Config_GetHideTopHud());
                    break;
                case SS_SET_WIDESCREEN:
                    /* Just the flag: the PPU's view width and the engine's
                     * cull bounds both read it per frame, so the wider
                     * viewport takes effect on the next one. */
                    Port_Config_SetWidescreenEnabled(!Port_Config_WidescreenEnabled());
                    break;
#ifdef TMC_3DS
                case SS_SET_ASPECT_RATIO:
                    Port_Config_Cycle3DSAspectRatio();
                    break;
                case SS_SET_DISPLAY_STYLE:
                    Port_Config_Cycle3DSDisplayStyle();
                    break;
#endif
                case SS_SET_FOLLOW:
                    Port_Config_SetSecondScreenFollowCam(!Port_Config_GetSecondScreenFollowCam());
                    break;
                case SS_SET_CRESTS:
                    Port_Config_SetSecondScreenCrestPins(!Port_Config_GetSecondScreenCrestPins());
                    break;
                case SS_SET_FLOOR_RETURN:
                    Port_Config_SetSecondScreenFloorReturn(!Port_Config_GetSecondScreenFloorReturn());
                    break;
                case SS_SET_TURBO:
#ifdef TMC_3DS
                {
                    unsigned multiplier = Port_Config_GetTurboMultiplier();
                    Port_Config_SetTurboMultiplier(multiplier >= 5 ? 2 : multiplier + 1);
                    break;
                }
#else
                    break;
#endif
                case SS_SET_VOLUME: {
                    /* Cycle 0 -> 25 -> 50 -> 75 -> 100 -> 0, applied to
                     * the live mixer + persisted — the same call pair as
                     * the imgui volume slider. */
                    float v = (float)((GetVolumeStop() + 25) % 125) / 100.0f;
                    Port_Audio_SetMasterVolume(v);
                    Port_Config_SetMasterVolume(v);
                    break;
                }
                case SS_SET_AUTOSAVE: {
                    /* Live autosaver toggle + persisted flag — the imgui
                     * checkbox's exact pair. */
                    int on = !Port_QuickSave_AutoEnabled();
                    Port_QuickSave_SetAutoEnabled(on);
                    Port_Config_SetAutosaveEnabled(on != 0);
                    break;
                }
                case SS_SET_COLOR_CORRECTION: {
                    /* Persisted flag (whose config DEFAULT is itself
                     * #ifdef __ANDROID__-guarded — off on device, on
                     * elsewhere; reading the getter tracks either) plus
                     * the PPU's live switch. */
                    bool on = !Port_Config_GetColorCorrection();
                    Port_Config_SetColorCorrection(on);
                    Port_PPU_SetColorCorrection(on);
                    break;
                }
                case SS_SET_SHOW_FPS:
                    /* The FPS overlay polls the flag every frame. */
                    Port_Config_SetShowFps(!Port_Config_GetShowFps());
                    break;
                case SS_SET_HOLD_ADVANCE:
                    /* src/message.c polls the flag at every advance. */
                    Port_Config_SetHoldToAdvanceText(!Port_Config_GetHoldToAdvanceText());
                    break;
                case SS_SET_RANDOMIZER:
#ifdef TMC_3DS
                    UI_LOCK();
                    sUi.randoDesired = Port_Config_GetRandoEnabled() ? 0 : 1;
                    sUi.randoConfirmActive = 1;
                    UI_UNLOCK();
#endif
                    break;
                case SS_SET_BACKDROP:
                    /* Cycle PARCHMENT -> CREAM -> DARK -> PARCHMENT. Only
                     * the flag: the paint pass hands the stored style to the
                     * theme before it draws, so the next frame wears it. */
                    Port_Config_SetSecondScreenBackdrop((BackdropStyleCfg() + 1) % SS_BACKDROP_COUNT);
                    break;
                case SS_SET_SWAP_SCREENS:
                    /* Persist only. Which display the game's window belongs
                     * to is fixed when that window is created, so the Java
                     * launcher applies this on the next cold start; until
                     * then the row reads RESTART. */
                    Port_Config_SetSecondScreenSwap(!Port_Config_GetSecondScreenSwap());
                    break;
            }
            break;
        case SS_ACT_MAP: {
            /* A tap on the map opens the rectangle under it, from either
             * zoom — the close view shows fewer tiles but they are the same
             * tiles, so there is no reason to make the player zoom out to
             * reach the one they are standing on. Nothing else: changing the
             * zoom is the chip's job, and a tap that misses stays a miss. */
            uint8_t state;
            UI_LOCK();
            state = sUi.regionState;
            UI_UNLOCK();
            if (state == SS_REGION_OFF) {
                PickMapRegion(x, y);
            }
            break;
        }
        case SS_ACT_MAPVIEW:
            /* The map's own back step: out of the region view first, then
             * out of the whole map back to the follow cam. */
            UI_LOCK();
            if (sUi.regionState != SS_REGION_OFF) {
                sUi.regionState = SS_REGION_OFF;
            } else {
                sUi.wholeMap = 0;
            }
            UI_UNLOCK();
            break;
        case SS_ACT_MAPZOOM:
            /* Step the zoom: Link's close view <-> the whole of Hyrule. */
            UI_LOCK();
            sUi.wholeMap = !sUi.wholeMap;
            UI_UNLOCK();
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Android surface plumbing                                           */
/* ------------------------------------------------------------------ */

#ifdef __ANDROID__

#include <android/native_window.h>
#include <time.h>

static ANativeWindow* sWindow = NULL;
static pthread_mutex_t sWindowMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sRenderThread;
static bool sRenderThreadStarted = false;

static void PaintFrame(ANativeWindow* window) {
    static uint32_t sTick = 0;
    sTick++;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(window, &buf, NULL) != 0) {
        fprintf(stderr, "[second_screen] ANativeWindow_lock failed\n");
        return;
    }
    /* Java pins the SurfaceHolder to RGBA_8888, but verify rather than trust
     * across the JNI boundary — a format mismatch here (e.g. 2-bytes/pixel
     * RGB_565) would overflow these 4-bytes/pixel writes and crash native. */
    if (buf.format != WINDOW_FORMAT_RGBA_8888 && buf.format != WINDOW_FORMAT_RGBX_8888) {
        fprintf(stderr, "[second_screen] unexpected buffer format %d, skipping paint\n", buf.format);
        ANativeWindow_unlockAndPost(window);
        return;
    }

    SecondScreenSnapshot snap;
    Port_SecondScreenState_Read(&snap);
    Port_SecondScreen_PaintInto((uint32_t*)buf.bits, buf.width, buf.height, buf.stride, &snap, sTick);
    ANativeWindow_unlockAndPost(window);
}

static void* RenderThreadMain(void* arg) {
    (void)arg;
    fprintf(stderr, "[second_screen] render thread started\n");

    for (;;) {
        pthread_mutex_lock(&sWindowMutex);
        if (sWindow) {
            PaintFrame(sWindow);
        }
        pthread_mutex_unlock(&sWindowMutex);

        struct timespec sleepTime = { 0, 50 * 1000 * 1000 }; /* 50ms ~= 20 Hz */
        nanosleep(&sleepTime, NULL);
    }
    return NULL;
}

/* Started lazily on the first surface, then runs for the process lifetime,
 * idling (no window held -> no paint, just the sleep) across surface
 * loss/recreation rather than being torn down and rebuilt each time —
 * simpler than thread lifecycle management for what's still a HUD panel. */
static void EnsureRenderThreadStarted(void) {
    if (!sRenderThreadStarted) {
        pthread_create(&sRenderThread, NULL, RenderThreadMain, NULL);
        pthread_detach(sRenderThread);
        sRenderThreadStarted = true;
    }
}

void Port_SecondScreen_Init(void) {
    fprintf(stderr, "[second_screen] init\n");
}

void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    ANativeWindow* nativeWindow = (ANativeWindow*)window;
    fprintf(stderr, "[second_screen] surface ready %dx%d\n", width, height);

    pthread_mutex_lock(&sWindowMutex);
    if (sWindow) {
        ANativeWindow_release(sWindow);
    }
    sWindow = nativeWindow;
    pthread_mutex_unlock(&sWindowMutex);

    EnsureRenderThreadStarted();
}

void Port_SecondScreen_OnSurfaceLost(void) {
    pthread_mutex_lock(&sWindowMutex);
    if (sWindow) {
        ANativeWindow_release(sWindow);
        sWindow = NULL;
    }
    pthread_mutex_unlock(&sWindowMutex);
    fprintf(stderr, "[second_screen] surface lost\n");
}

#else /* !__ANDROID__ — no second display; surface entry points are no-ops
       * (the compositor + tap handler above still compile and run, which
       * is what the host harness drives). */

void Port_SecondScreen_Init(void) {}
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
}
void Port_SecondScreen_OnSurfaceLost(void) {}

#endif
