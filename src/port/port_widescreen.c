/* Widescreen camera, room streaming and reveal-tile ownership.
 * The game calls this module before/at VBlank; no platform presentation lives here.
 */
#include "port_widescreen.h"
#include "port_widescreen_internal.h"
#include "port_widescreen_banner_state.h"
#include "port_horizontal_minish_path.h"
#include "port_runtime_config.h"
#include "area.h"
#include "common.h"
#include "game.h"
#include "tileMap.h"
#include "fade.h"
#include "main.h"
#include "map.h"
#include "menu.h"
#include "message.h"
#include "player.h"
#include "room.h"
#include "screen.h"
#include "save.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
#include "cpu/mode1.h"

/*
 * Widescreen Phase 2 (Option A) — port-side shadow tilemap.
 *
 * The PPU (libs/ViruaPPU/src/mode1.c::render_text_bg_line) reads tile
 * entries from these for display cols >= MODE1_GBA_BG_CLIP_X on 32-tile
 * BGs, bypassing VRAM — the 32-tile screenblock only spans 256 px of
 * world and wraps past that, so the reveal columns can't come from VRAM.
 *
 * Storage follows the configured framebuffer capacity. Normal Wide rows remain
 * packed at MODE1_WS_SHADOW_WIDE_COLS, preserving E2's 32*7-cell VBlank work
 * and cache footprint on Old 3DS. Rows are indexed by tile_row (mod 32,
 * mirroring the engine's vertical rolling) and (tile_col - base) mod 32.
 * Populated each
 * scroll-update from gMapData{Bottom,Top}Special (128x128 BG-tiles) at
 * world cols [cam_bg_col + MODE1_GBA_BG_CLIP_X/8 .. + COLS).
 *
 * Gated #if width>240: the native build never compiles this and the PPU
 * shadow pointers stay NULL (render falls back to clip-at-240). */
static u16 sWsShadowBG1[MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];
static u16 sWsShadowBG2[MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];

/* Retain only the outgoing viewport before LoadRoomGfx replaces its maps.
 * This is tile data, not a frozen image: the current palettes/animations still
 * apply. Both the native screenblock and reveal use the same world sampler. */
enum { WS_SCROLL_COLS = 36, WS_SCROLL_ROWS = 24 };
static struct {
    int valid, area, room, direction;
    int startX, startY, tileX, tileY;
    u16 tiles[2][WS_SCROLL_ROWS][WS_SCROLL_COLS];
} sWsScroll;

void Port_Widescreen_BeginScroll(unsigned room, unsigned direction) {
    sWsScroll.valid = 0;
    const int width = Port_Widescreen_GameplayViewWidth();
    if (width <= 240 || width > 266 || direction > 3 || gRoomControls.scrollAction > 1 || gArea.unk_0c_0 ||
        (gRoomControls.scroll_flags & 1) || room >= MAX_ROOMS || gArea.roomResInfos[room].pixel_width <= 240 ||
        gArea.roomResInfos[room].pixel_height < 160 || gRoomControls.height < 160)
        return;
    sWsScroll.valid = 1;
    sWsScroll.area = gRoomControls.area;
    sWsScroll.room = room;
    sWsScroll.direction = direction;
    sWsScroll.startX = gRoomControls.scroll_x;
    sWsScroll.startY = gRoomControls.scroll_y;
    sWsScroll.tileX = (gRoomControls.scroll_x >> 3) - 1;
    sWsScroll.tileY = (gRoomControls.scroll_y >> 3) - 1;
    for (int layer = 0; layer < 2; ++layer) {
        const u16* map = layer ? gMapDataTopSpecial : gMapDataBottomSpecial;
        for (int y = 0; y < WS_SCROLL_ROWS; ++y) {
            const int my = sWsScroll.tileY + y - gRoomControls.origin_y / 8;
            for (int x = 0; x < WS_SCROLL_COLS; ++x) {
                const int mx = sWsScroll.tileX + x - gRoomControls.origin_x / 8;
                sWsScroll.tiles[layer][y][x] = mx >= 0 && mx < gRoomControls.width / 8 && mx < 128 && my >= 0 &&
                                                       my < gRoomControls.height / 8 && my < 128
                                                   ? map[my * 128 + mx]
                                                   : 0;
            }
        }
    }
}

static int Port_WidescreenScrollActive(void) {
    return sWsScroll.valid && gMain.task == TASK_GAME && gRoomControls.area == sWsScroll.area &&
           gRoomControls.room == sWsScroll.room && gRoomControls.scroll_direction == sWsScroll.direction &&
           (gRoomControls.scrollAction == 2 || gRoomControls.scrollAction == 0) && gRoomControls.scrollSubAction >= 1 &&
           !(gRoomControls.scroll_flags & 1) && Port_Config_WidescreenEnabled();
}

int Port_Widescreen_AdvanceScroll(void) {
    if (!Port_WidescreenScrollActive())
        return 0;
    /* Keep the retail 60/40-tick duration and 15px player carry. Only the
     * camera's endpoint changes to the actual viewport edge. */
    int endX = sWsScroll.startX, endY = sWsScroll.startY;
    int duration = 40;
    switch (sWsScroll.direction) {
        case 0:
            endY = gRoomControls.origin_y + gRoomControls.height - 160;
            break;
        case 1:
            endX = gRoomControls.origin_x;
            duration = 60;
            break;
        case 2:
            endY = gRoomControls.origin_y;
            break;
        case 3:
            endX = gRoomControls.origin_x + gRoomControls.width - Port_Widescreen_GameplayViewWidth();
            duration = 60;
            break;
    }
    int progress = gRoomControls.unk_18;
    if (progress > duration)
        progress = duration;
    gRoomControls.scroll_x = sWsScroll.startX + (endX - sWsScroll.startX) * progress / duration;
    gRoomControls.scroll_y = sWsScroll.startY + (endY - sWsScroll.startY) * progress / duration;
    return 1;
}

static u16 Port_WidescreenScrollTile(const u16* map, int col, int row) {
    if (col >= 0 && col < gRoomControls.width / 8 && col < 128 && row >= 0 && row < gRoomControls.height / 8 &&
        row < 128)
        return map[row * 128 + col];
    const int x = col + gRoomControls.origin_x / 8 - sWsScroll.tileX;
    const int y = row + gRoomControls.origin_y / 8 - sWsScroll.tileY;
    return x >= 0 && x < WS_SCROLL_COLS && y >= 0 && y < WS_SCROLL_ROWS
               ? sWsScroll.tiles[map == gMapDataTopSpecial][y][x]
               : 0;
}

int Port_WidescreenFillScroll(u16* map, u16* buffer) {
    if (!Port_WidescreenScrollActive())
        return 0;
    const int col = ((gRoomControls.scroll_x - gRoomControls.origin_x) >> 4) * 2;
    const int row = ((gRoomControls.scroll_y - gRoomControls.origin_y) >> 4) * 2 - 1;
    for (int y = 0; y < 23; ++y)
        for (int x = 0; x < 32; ++x)
            buffer[y * 32 + x] = Port_WidescreenScrollTile(map, col + x, row + y);
    return 1;
}

/* ---- Runtime widescreen gate --------------------------------------------
 * `--widescreen_width=N` only reserves a wider framebuffer. True widescreen
 * is still WIP, so a persisted runtime option decides whether gameplay uses
 * the wider camera/reveal. Disabled, non-gameplay, one-screen (<=240px)
 * rooms, and rooms whose painted content can't fill the room-capped view all
 * fall back to a native 240x160 frame (presented pillarboxed, never
 * stretched); rooms between 240px and the window target run true-wide at
 * their full room width. The camera/culling helpers below report 240 in the
 * fallback state so the engine remains GBA-clean. */
/* ---- Per-room content width (issue: wide room, narrow content) ----------
 * gRoomControls.width can exceed the actual painted map (e.g. a 480-px room
 * whose floor ends ~270 px in). Widescreen then parks void columns on screen
 * permanently (a GBA player only saw them transiently at the right edge).
 * Scan both map buffers for the rightmost non-empty tile column inside the
 * room rect; if that content can't fill the wide view even with the camera
 * pinned left, fall back to a native 240 frame (presented pillarboxed) —
 * which shows exactly what GBA showed. Ratcheted per room (only grows) so
 * late-streaming map data can't flip the mode back and forth mid-room. */
static u32 sWsContentKey = 0xffffffffu;
static s32 sWsContentPx = 0;
static PortWidescreenBannerState sWsEnterRoomBanner;

void Port_Widescreen_SetEnterRoomBannerActive(int active) {
    Port_WidescreenBannerState_Set(&sWsEnterRoomBanner, active, gRoomControls.area, gRoomControls.room);
}

/* Key identifying the room the content cache belongs to. Checked by
 * FallbackNative so a stale cache (previous room) is never consulted —
 * InitializeCamera runs at room entry BEFORE the next vblank rescan. */
static u32 Port_WidescreenRoomKey(void) {
    return ((u32)gRoomControls.area << 24) | ((u32)gRoomControls.room << 16) |
           ((u32)(u16)gRoomControls.origin_x ^ ((u32)(u16)gRoomControls.origin_y << 1));
}

static s32 Port_WidescreenScanContentPx(void) {
    enum { kMapStride = 128, kMapRows = 128 };
    s32 tiles_w = (s32)gRoomControls.width / 8;
    s32 tiles_h = (s32)gRoomControls.height / 8;
    s32 c, r;
    if (tiles_w > kMapStride)
        tiles_w = kMapStride;
    if (tiles_h > kMapRows)
        tiles_h = kMapRows;
    for (c = tiles_w - 1; c >= 0; c--) {
        const u16* bot = gMapDataBottomSpecial + c;
        const u16* top = gMapDataTopSpecial + c;
        for (r = 0; r < tiles_h; r++) {
            if (bot[(size_t)r * kMapStride] != 0 || top[(size_t)r * kMapStride] != 0) {
                return (c + 1) * 8;
            }
        }
    }
    return 0;
}

static int Port_WidescreenFixedCanvasSubtask(void) {
    if (gMain.task != TASK_GAME) {
        return 0;
    }
    /* The Deepwood rolling-barrel state renders a fixed 240x160 affine
     * canvas and never runs the normal scrolling path. Cropping it as a
     * 200x120 interior desynchronizes Link from BG1/BG2 and the HDMA curve. */
    if (gMain.substate == GAMEMAIN_BARRELUPDATE) {
        return 1;
    }
    if (gMain.substate != GAMEMAIN_SUBTASK) {
        return 0;
    }
    /* Pause/inventory replace the world with a fixed GBA canvas. State 3 is
     * still fixed while its fade is active; fail closed until gameplay is
     * visibly restored. */
    return gUI.nextToLoad == 1 || gUI.nextToLoad == 2 || (gUI.nextToLoad == 3 && gFadeControl.active != 0);
}

/* ---- True widescreen: window-aspect-driven view width -------------------
 * MODE1_GBA_WIDTH is the framebuffer CAPACITY (hard cap); the presented
 * view width tracks the live window aspect so the frame fills the monitor
 * exactly: 16:9 -> 284 px, 21:9 -> 373 -> cap, 4:3/3:2 -> 240 (authentic).
 * Published once per frame from the present path; rounded to a multiple of
 * 8 so tile/HUD math stays exact. TMC_WS_VIEW_WIDTH=<px> overrides for
 * headless captures (dummy driver windows don't track a real monitor). */
static int sWsWindowW = 0;
static int sWsWindowH = 0;

void Port_Widescreen_SetWindowPixels(int w, int h) {
    sWsWindowW = w;
    sWsWindowH = h;
}

static int Port_WidescreenBaseTargetViewWidth(void) {
    static int env_w = -1;
    int w;
    if (env_w < 0) {
        const char* e = getenv("TMC_WS_VIEW_WIDTH");
        env_w = 0;
        if (e && *e) {
            int v = atoi(e);
            if (v >= 240 && v <= MODE1_GBA_WIDTH)
                env_w = v;
        }
    }
    if (env_w > 0) {
        return env_w;
    }
    if (sWsWindowW <= 0 || sWsWindowH <= 0) {
        return 240;
    }
    /* Exact aspect fit: the width that makes the 160-line frame fill the
     * window (no rounding — every consumer is pixel-based, and exact fit
     * beats up-to-7px side bars). */
    w = (int)(((long long)sWsWindowW * 160) / sWsWindowH);
    if (w < 240)
        w = 240;
    if (w > MODE1_GBA_WIDTH)
        w = MODE1_GBA_WIDTH;
    return w;
}

int Port_Widescreen_TargetViewWidth(void) {
    return Port_WidescreenBaseTargetViewWidth();
}

/* Widest view the current room can honestly feed: the window-aspect target
 * capped by the room's own width. Rooms narrower than the window target but
 * wider than 240 (0x100/0x110 rooms — most dungeons and larger interiors)
 * still get true widescreen at their full room width; the presenter
 * pillarboxes the small remainder instead of stretching. */
static s32 Port_WidescreenEffectiveTarget(void) {
    s32 target = Port_Widescreen_TargetViewWidth();
    s32 roomW = (s32)gRoomControls.width;
    if (roomW < target) {
        target = roomW;
    }
    return target;
}

static int Port_WidescreenEstablishedViewWidth(void) {
    int target = Port_WidescreenBaseTargetViewWidth();
    int roomWidth = (int)gRoomControls.width;
    if (!Port_Config_WidescreenEnabled() || target <= 240) {
        return 240;
    }
    if (roomWidth < target) {
        target = roomWidth;
    }
    if (target <= 240) {
        return 240;
    }
    if (sWsContentKey == Port_WidescreenRoomKey() && sWsContentPx > 0 && sWsContentPx < target) {
        return 240;
    }
    return target;
}

int Port_Widescreen_FallbackNative(void) {
    s32 eff;
    if (!Port_Config_WidescreenEnabled()) {
        return 1;
    }
    if (Port_Widescreen_TargetViewWidth() <= 240) {
        return 1; /* window is 3:2/4:3 — native view already fills it */
    }
    eff = Port_WidescreenEffectiveTarget();
    if (eff <= 240) {
        return 1; /* one-screen room (<=240px): no extra world to reveal */
    }
    /* Room wider than its painted content: the map can't fill even the
     * room-capped view with the camera pinned left -> permanent void strip.
     * Render native 240 (GBA-identical framing, presented pillarboxed).
     * 0 = not scanned yet -> trust width. Key mismatch = cache belongs to
     * the PREVIOUS room -> trust width. */
    if (sWsContentKey == Port_WidescreenRoomKey() && sWsContentPx > 0 && sWsContentPx < eff) {
        return 1;
    }
    return 0;
}

int Port_Widescreen_IsActive(void) {
    /* No message/controlMode gate here: snapping the viewport wide->240 for
     * every textbox (and back on close) was the single worst widescreen UX
     * issue. Dialogue now stays wide — the PPU centers the BG0 textbox via
     * the ws_msg_* knobs (published in Port_Widescreen_UpdateShadows), and
     * overlay screens that replace the map BGs (prologue storybook, pause
     * menu, ...) are caught by Port_Widescreen_ShadowsLive() instead: no
     * matching BG -> no shadow -> present native 240. */
    return (gMain.task == TASK_GAME && !Port_WidescreenFixedCanvasSubtask() && Port_Config_WidescreenEnabled() &&
            !Port_Widescreen_FallbackNative())
               ? 1
               : 0;
}

int Port_Widescreen_EffectiveViewWidth(void) {
    s32 eff;
    if (!Port_Widescreen_IsActive()) {
        return 240;
    }
    eff = Port_WidescreenEffectiveTarget();
    return eff > 240 ? (int)eff : 240;
}

int Port_Widescreen_EffectiveViewHeight(void) {
    return 160;
}

int Port_Widescreen_GameplayViewWidth(void) {
    return Port_WidescreenEstablishedViewWidth();
}

int Port_Widescreen_GameplayViewHeight(void) {
    return 160;
}

/* Single source of truth for the camera's rest x (see port_widescreen.h).
 * Clamp order (lo wins over hi) matches the GBA branches it replaces. */
int Port_Widescreen_CameraRestX(int target_x) {
    int viewW = Port_WidescreenEstablishedViewWidth();
    int lo = (int)gRoomControls.origin_x;
    int boundsW = (int)gRoomControls.width;

    int hi = lo + boundsW - viewW;
    int want = target_x - viewW / 2;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}

int Port_Widescreen_CameraRestY(int target_y) {
    int viewH = 160;
    int lo = (int)gRoomControls.origin_y;
    int boundsH = (int)gRoomControls.height;

    int hi = lo + boundsH - viewH;
    int want = target_y - viewH / 2;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}

/* True while at least one BG shadow is registered for the current frame —
 * i.e. the map BGs are actually what the PPU is rendering. Overlay screens
 * (prologue storybook, pause/menu subscreens) swap the BG control regs away
 * from the map settings, the shadow match fails, and this drops to 0 so the
 * present path falls back to a native 240 frame instead of showing a black
 * right third. */
int Port_Widescreen_ShadowsLive(void) {
    int i;
    for (i = 0; i < MODE1_GBA_BG_COUNT; i++) {
        if (virtuappu_mode1_ws_shadow[i] != NULL) {
            return 1;
        }
    }
    return 0;
}

int Port_Widescreen_HudRightAnchor(void) {
    if (!Port_Widescreen_IsActive()) {
        return 0;
    }
    if (gHUD.hideFlags != HUD_HIDE_NONE) {
        return 0;
    }
    return 1;
}

int Port_Widescreen_ProducerViewWidth(void) {
    return Port_WidescreenEstablishedViewWidth();
}

int Port_Widescreen_ProducerViewHeight(void) {
    return 160;
}

int Port_Widescreen_HudTargetViewWidth(void) {
    return Port_Widescreen_ProducerViewWidth();
}

int Port_Widescreen_HudTargetViewHeight(void) {
    return Port_Widescreen_ProducerViewHeight();
}

static void Port_WidescreenShadow_Populate(int bg_index, u16* mapSpecial, u16* shadow) {
    /* Populate the port-side shadow tilemap the PPU reads for the reveal
     * columns (display x >= MODE1_GBA_BG_CLIP_X). The engine's 32-tile VRAM
     * screenblock only spans ~256 px of world and wraps past that, so the
     * reveal columns can't come from VRAM; we mirror, from gMapData*Special,
     * exactly what the engine fill (ram_sub_080B197C_c) would have placed.
     *
     * Engine ground truth (ram_sub_080B197C_c, common case ydiff>=8):
     *   VRAM buffer cell (row sr, col c) = mapSpecial[2*row16 - 1 + sr][2*col16 + c]
     *   with col16 = xdiff>>4, row16 = ydiff>>4, mapSpecial stride 128 u16.
     * The PPU reads the shadow at [local_row][shadow_idx] where
     *   local_row  = ((line + BGVOFS) % 256)/8 % 32   (== the VRAM buffer row)
     *   shadow_idx = (tile_col - base) % 32           (tile_col == VRAM col at x)
     *
     * Therefore:
     *   (a) the shadow ROW index IS the VRAM buffer row sr, so fill shadow[sr]
     *       with map row (2*row16 - 1 + sr) — NOT (world_row & 31), which reads
     *       camera-shifted wrong rows.
     *   (b) the consumer base MUST equal the VRAM tile_col of the first reveal
     *       column (display CLIP_X) = CLIP_X/8 + (BGHOFS>=8 ? 1 : 0), so
     *       shadow_idx lands on reveal column index d. BGHOFS = (scroll-origin)
     *       & 0xf (UpdateScreenShake); its upper half carries one extra tile.
     * Getting either wrong shifts/wraps the reveal into stale cells — the
     * far-edge garbage. (No residency gate: the area tileset is resident in
     * char VRAM regardless of the 32-col tilemap window, so gating on that
     * window blacked legitimately-loaded reveal tiles. The PPU's 0x7C1F skip
     * still drops genuinely-unstreamed-palette pixels to clean black.) */
    s16 xdiff = (s16)(gRoomControls.scroll_x - gRoomControls.origin_x);
    s16 ydiff = (s16)(gRoomControls.scroll_y - gRoomControls.origin_y);
    s16 bg_x_offset = 0;
    s16 bg_y_offset = 0;
    int shadow_cols = MODE1_WS_SHADOW_WIDE_COLS;
    if (shadow_cols > MODE1_WS_SHADOW_COLS)
        shadow_cols = MODE1_WS_SHADOW_COLS;
    const s32 first_display_tile = (MODE1_GBA_BG_CLIP_X / 8);
    s32 ws_base_world_col;
    switch (bg_index) {
        case 0:
            bg_x_offset = (s16)gScreen.bg0.xOffset;
            bg_y_offset = (s16)gScreen.bg0.yOffset;
            break;
        case 1:
            bg_x_offset = (s16)gScreen.bg1.xOffset;
            bg_y_offset = (s16)gScreen.bg1.yOffset;
            break;
        case 2:
            bg_x_offset = (s16)gScreen.bg2.xOffset;
            bg_y_offset = (s16)gScreen.bg2.yOffset;
            break;
        case 3:
            bg_x_offset = (s16)gScreen.bg3.xOffset;
            bg_y_offset = (s16)gScreen.bg3.yOffset;
            break;
    }
    ws_base_world_col = (xdiff >> 4) * 2 + (bg_x_offset >> 3) + first_display_tile;
    virtuappu_mode1_ws_shadow_base_tile[bg_index] = first_display_tile + ((bg_x_offset & 0xff) >> 3);

    enum { kMapStride = 128, kMapRows = 128 };
    /* Clamp to the ROOM rect, not just the 128-tile buffer: the buffers are
     * reused across rooms without clearing, so cells past the current room's
     * extent hold the previous room's tiles — sampling them leaked stale
     * graphics into the reveal near room edges. Outside the room = entry 0
     * (transparent; composite force-blacks it), same as GBA's void. */
    s32 room_tiles_w = (s32)gRoomControls.width / 8;
    s32 room_tiles_h = (s32)gRoomControls.height / 8;
    if (room_tiles_w > kMapStride)
        room_tiles_w = kMapStride;
    if (room_tiles_h > kMapRows)
        room_tiles_h = kMapRows;
    const int scrolling = Port_WidescreenScrollActive();
    for (int sr = 0; sr < MODE1_WS_SHADOW_ROWS; sr++) {
        int shadow_row = sr;
        s32 world_row = 2 * (ydiff >> 4) - 1 + sr;
        if (!scrolling && ydiff >= 0 && ydiff < 8 && sr == 0)
            world_row = 0;
        u16* row_dst = shadow + (size_t)shadow_row * (size_t)shadow_cols;
        if (!scrolling && (world_row < 0 || world_row >= room_tiles_h)) {
            for (int C = 0; C < shadow_cols; C++)
                row_dst[C] = 0;
            continue;
        }
        for (int C = 0; C < shadow_cols; C++) {
            s32 world_col = ws_base_world_col + C;
            u16 entry = scrolling ? Port_WidescreenScrollTile(mapSpecial, world_col, world_row)
                                  : ((world_col >= 0 && world_col < room_tiles_w)
                                         ? mapSpecial[world_row * kMapStride + world_col]
                                         : (u16)0);
            row_dst[C] = entry;
        }
    }
    virtuappu_mode1_ws_shadow_cols[bg_index] = (uint8_t)shadow_cols;
    virtuappu_mode1_ws_shadow_stride[bg_index] = (uint8_t)shadow_cols;
    virtuappu_mode1_ws_shadow[bg_index] = shadow;
}

/* Which PPU BG index renders a given map: the PPU selects a BG's tilemap by
 * its BGCNT screen_base, so the map whose bgSettings->control screen_base
 * matches gScreen.bgN.control is rendered as BG N. (In the field this is
 * bottom->BG2, top->BG1 — the reverse of the buffer numbering, which is why a
 * hardcoded shadow[1]=bottom/shadow[2]=top was swapped and showed garbage.) */
static int Port_WidescreenPpuBgForControl(u32 control) {
    u32 sb = (control >> 8) & 0x1fu;
    if (((gScreen.bg0.control >> 8) & 0x1fu) == sb)
        return 0;
    if (((gScreen.bg1.control >> 8) & 0x1fu) == sb)
        return 1;
    if (((gScreen.bg2.control >> 8) & 0x1fu) == sb)
        return 2;
    if (((gScreen.bg3.control >> 8) & 0x1fu) == sb)
        return 3;
    return -1;
}

/* Minish paths own two parallax tilemaps instead of gMapTop. Their reveal
 * must use each manager's actual source/page and scroll, not the ground map. */
static u16 sWsParallax[2][MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_WIDE_COLS];
static void Port_WidescreenParallaxShadows(void) {
    if (gMapTop.bgSettings != NULL || (gScreen.bg3.control & ~0x43) != 0x1d08 ||
        (gScreen.bg1.control & ~0x43) != 0x1e08)
        return;
    for (int layer = 0; layer < 2; ++layer) {
        const int bg = layer ? 1 : 3;
        const BgSettings* settings = layer ? &gScreen.bg1 : (const BgSettings*)&gScreen.bg3;
        const u16* src = settings->subTileMap;
        int cols = 32, start = 0;
        if (src == gBG3Buffer + layer * 0x400) {
            const int scroll =
                Port_HorizontalMinishPathScroll(gRoomControls.scroll_x - gRoomControls.origin_x, gRoomControls.width,
                                                layer ? 2 : 3, Port_Widescreen_GameplayViewWidth());
            start = (scroll >> 4) * 2;
            cols = 128;
            src = (const u16*)(gUnk_02006F00 + layer * 0x2000);
        } else {
            const uintptr_t off = (uintptr_t)src - (uintptr_t)gMapDataTopSpecial;
            if (off > 0x7800 || (off & 1))
                continue;
        }
        const int base = 30 + ((settings->xOffset & 0xff) >> 3);
        for (int row = 0; row < 32; ++row) {
            for (int col = 0; col < MODE1_WS_SHADOW_WIDE_COLS; ++col) {
                int x = start + base + col;
                if (cols == 32)
                    x &= 31;
                sWsParallax[layer][row * MODE1_WS_SHADOW_WIDE_COLS + col] = x < cols ? src[row * cols + x] : 0;
            }
        }
        virtuappu_mode1_ws_shadow[bg] = sWsParallax[layer];
        virtuappu_mode1_ws_shadow_base_tile[bg] = base;
        virtuappu_mode1_ws_shadow_cols[bg] = MODE1_WS_SHADOW_WIDE_COLS;
        virtuappu_mode1_ws_shadow_stride[bg] = MODE1_WS_SHADOW_WIDE_COLS;
    }
}

/* Called per-VBlank from engine/src/interrupts.c::UpdateDisplayControls. */
void Port_Widescreen_UpdateShadows(void) {
    if (gMain.task != TASK_GAME || (gRoomControls.scrollAction != 2 && gRoomControls.scrollAction != 0))
        sWsScroll.valid = 0;
    int enterRoomBannerActive = Port_WidescreenBannerState_IsActive(&sWsEnterRoomBanner, gMain.task == TASK_GAME,
                                                                    gRoomControls.area, gRoomControls.room);
    const u32 roomKey = Port_WidescreenRoomKey();
    for (int i = 0; i < MODE1_GBA_BG_COUNT; i++) {
        virtuappu_mode1_ws_shadow[i] = NULL;
        virtuappu_mode1_ws_shadow_cols[i] = MODE1_WS_SHADOW_WIDE_COLS;
        virtuappu_mode1_ws_shadow_stride[i] = MODE1_WS_SHADOW_WIDE_COLS;
    }
    virtuappu_mode1_ws_hud_right_anchor = 0;
    virtuappu_mode1_ws_msg_shift = 0;

    /* Painted width only grows within a room, avoiding framing changes as tiles stream in. */
    if (gMain.task == TASK_GAME && !Port_WidescreenFixedCanvasSubtask() && Port_Config_WidescreenEnabled()) {
        if (roomKey != sWsContentKey) {
            sWsContentKey = roomKey;
            sWsContentPx = 0;
        }
        s32 px = Port_WidescreenScanContentPx();
        if (px > sWsContentPx) {
            sWsContentPx = px;
            if (getenv("TMC_WS_TRACE") != NULL) {
                fprintf(stderr, "[ws] area=0x%02x room=0x%02x width=%d content_px=%d -> %s\n",
                        (unsigned)gRoomControls.area, (unsigned)gRoomControls.room, (int)gRoomControls.width,
                        (int)sWsContentPx, Port_Widescreen_FallbackNative() ? "native-240" : "wide");
            }
        }
    }

    if (!Port_Widescreen_IsActive()) {
        return;
    }

    if (gMapBottom.bgSettings != NULL) {
        int bg = Port_WidescreenPpuBgForControl(gMapBottom.bgSettings->control);
        if (bg >= 0) {
            Port_WidescreenShadow_Populate(bg, gMapDataBottomSpecial, sWsShadowBG1);
        }
    }
    if (gMapTop.bgSettings != NULL) {
        int bg = Port_WidescreenPpuBgForControl(gMapTop.bgSettings->control);
        if (bg >= 0) {
            Port_WidescreenShadow_Populate(bg, gMapDataTopSpecial, sWsShadowBG2);
        }
    }

    Port_WidescreenParallaxShadows();
    if (!Port_Widescreen_ShadowsLive()) {
        return;
    }

    /* Publish the live textbox rect so the PPU can center it (BG0 composes
     * the box for a 240-px canvas). The engine frame (DispMessageFrame /
     * DeleteWindow, engine/src/message.c) spans (W+2) x (H+2) BG0 tiles STARTING at
     * tile (textWindowPosX, textWindowPosY) — border tiles are drawn inward
     * from that corner, not around it. Publishing a rect short of the real
     * frame left the right border outside the shifted copy (overdrawn by the
     * interior) and the bottom border row outside the y-band (torn by the
     * HUD right-anchor remap). Clamp to the native canvas. */
    if ((gMessage.state & MESSAGE_ACTIVE) != 0) {
        int x0 = (int)gMessage.textWindowPosX * 8;
        int x1 = ((int)gMessage.textWindowPosX + (int)gMessage.textWindowWidth + 2) * 8;
        int y0 = (int)gMessage.textWindowPosY * 8;
        int y1 = ((int)gMessage.textWindowPosY + (int)gMessage.textWindowHeight + 2) * 8;
        if (x0 < 0)
            x0 = 0;
        if (x1 > 240)
            x1 = 240;
        if (y0 < 0)
            y0 = 0;
        if (y1 > 160)
            y1 = 160;
        if (x1 > x0 && y1 > y0) {
            virtuappu_mode1_ws_msg_x0 = x0;
            virtuappu_mode1_ws_msg_x1 = x1;
            virtuappu_mode1_ws_msg_y0 = y0;
            virtuappu_mode1_ws_msg_y1 = y1;
            virtuappu_mode1_ws_msg_shift = (Port_Widescreen_EffectiveViewWidth() - 240) / 2;
        }
    } else if (enterRoomBannerActive) {
        /* The location-name banner bypasses gMessage and owns BG0 rows 5-6
         * across the full native canvas.  Move that whole band together;
         * this also suspends the HUD right-anchor remap on those rows, which
         * otherwise cuts a viewWidth-240 gap through the banner text. */
        virtuappu_mode1_ws_msg_x0 = PORT_WS_ENTER_ROOM_BANNER_X0;
        virtuappu_mode1_ws_msg_x1 = PORT_WS_ENTER_ROOM_BANNER_X1;
        virtuappu_mode1_ws_msg_y0 = PORT_WS_ENTER_ROOM_BANNER_Y0;
        virtuappu_mode1_ws_msg_y1 = PORT_WS_ENTER_ROOM_BANNER_Y1;
        virtuappu_mode1_ws_msg_shift = (Port_Widescreen_EffectiveViewWidth() - 240) / 2;
    }
    virtuappu_mode1_ws_hud_right_anchor = Port_Widescreen_HudRightAnchor();
}
#else
void Port_Widescreen_BeginScroll(unsigned room, unsigned direction) {
    (void)room;
    (void)direction;
}
int Port_Widescreen_AdvanceScroll(void) {
    return 0;
}
void Port_Widescreen_UpdateShadows(void) { /* no-op at native 240 */
}
void Port_Widescreen_SetEnterRoomBannerActive(int active) {
    (void)active;
}
int Port_Widescreen_FallbackNative(void) {
    return 1;
}
int Port_Widescreen_IsActive(void) {
    return 0;
}
int Port_Widescreen_EffectiveViewWidth(void) {
    return 240;
}
int Port_Widescreen_EffectiveViewHeight(void) {
    return 160;
}
int Port_Widescreen_GameplayViewWidth(void) {
    return 240;
}
int Port_Widescreen_GameplayViewHeight(void) {
    return 160;
}
int Port_Widescreen_ProducerViewWidth(void) {
    return 240;
}
int Port_Widescreen_ProducerViewHeight(void) {
    return 160;
}
int Port_Widescreen_HudRightAnchor(void) {
    return 0;
}
int Port_Widescreen_HudTargetViewWidth(void) {
    return 240;
}
int Port_Widescreen_HudTargetViewHeight(void) {
    return 160;
}
int Port_Widescreen_ShadowsLive(void) {
    return 0;
}
void Port_Widescreen_SetWindowPixels(int w, int h) {
    (void)w;
    (void)h;
}
int Port_Widescreen_TargetViewWidth(void) {
    return 240;
}
int Port_Widescreen_CameraRestX(int target_x) {
    /* GBA-exact: center at 120, clamp view inside the room. */
    int lo = (int)gRoomControls.origin_x;
    int hi = lo + (int)gRoomControls.width - 240;
    int want = target_x - 120;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}
int Port_Widescreen_CameraRestY(int target_y) {
    int lo = (int)gRoomControls.origin_y;
    int hi = lo + (int)gRoomControls.height - 160;
    int want = target_y - 80;
    if (want > hi) {
        want = hi;
    }
    if (want < lo) {
        want = lo;
    }
    return want;
}
#endif
