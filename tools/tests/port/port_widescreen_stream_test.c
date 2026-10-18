#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "area.h"
#include "main.h"
#include "room.h"
#include "player.h"
#include "structures.h"
#include "port_widescreen.h"
#include "cpu/mode1.h"
#include "port_gba_mem.h"
#include "port_horizontal_minish_path.h"
u8 gEwram[0x40000];
const AreaHeader gAreaMetadata[0x99] = { 0 };
bool Port_Config_WidescreenEnabled(void) {
    return true;
}
void UpdateScreenShake(void) {
    gScreen.bg1.xOffset = gScreen.bg2.xOffset = (gRoomControls.scroll_x - gRoomControls.origin_x) & 15;
    gScreen.bg1.yOffset = gScreen.bg2.yOffset = ((gRoomControls.scroll_y - gRoomControls.origin_y) & 15) + 8;
}
void sub_0807D280(u16* a, u16* b) {
    (void)a;
    (void)b;
}
void sub_0807D46C(u16* a, u16* b) {
    (void)a;
    (void)b;
}
void sub_0807D6D8(u16* a, u16* b) {
    (void)a;
    (void)b;
}
extern u16 gMapDataBottomSpecial[], gMapDataTopSpecial[];
extern u8 gUpdateVisibleTiles;
extern void UpdateScrollVram(void), Port_Widescreen_UpdateShadows(void);

static unsigned failures;
static u16 tile(int layer, int room, int x, int y) {
    return (u16)(1 + ((layer * 1223 + room * 307 + y * 131 + x) % 65534));
}
static void maps(int room) {
    for (int l = 0; l < 2; l++)
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
                (l ? gMapDataTopSpecial : gMapDataBottomSpecial)[y * 128 + x] = tile(l, room, x, y);
}
static void setup(void) {
    memset(&gRoomControls, 0, sizeof(gRoomControls));
    memset(&gArea, 0, sizeof(gArea));
    memset(&gScreen, 0, sizeof(gScreen));
    gMain.task = TASK_GAME;
    gMain.substate = 2;
    gRoomControls.width = 480;
    gRoomControls.height = 416;
    gRoomControls.scrollAction = 1;
    gRoomControls.origin_x = 480;
    gRoomControls.origin_y = 416;
    gScreen.bg2.control = 0x1c42;
    gScreen.bg1.control = 0x1d45;
    gMapBottom.bgSettings = (BgSettings*)&gScreen.bg2;
    gMapTop.bgSettings = &gScreen.bg1;
    gArea.roomResInfos[1].pixel_width = 480;
    gArea.roomResInfos[1].pixel_height = 416;
    Port_Widescreen_SetWindowPixels(400, 240);
}
static u16 displayed(int l, int x, int y) {
    int bg = l ? 1 : 2;
    const BgSettings* b = l ? &gScreen.bg1 : (const BgSettings*)&gScreen.bg2;
    int bx = x + b->xOffset, by = y + b->yOffset;
    if (x < 240)
        return (l ? gBG2Buffer : gBG1Buffer)[((by & 255) / 8) * 32 + ((bx & 255) / 8)];
    return virtuappu_mode1_ws_shadow[bg][((by & 255) / 8) * 7 +
                                         ((((bx & 255) / 8) - virtuappu_mode1_ws_shadow_base_tile[bg] + 32) & 31)];
}
static void transition(int direction, int width) {
    setup();
    gRoomControls.width = width;
    const int view = width < 266 ? width : 266;
    const int ox = 480, oy = 416;
    int nx = ox, ny = oy;
    gRoomControls.scroll_x = ox + (width - view >= 48 ? 48 : 0);
    gRoomControls.scroll_y = oy + 48;
    if (direction == 0) {
        ny -= 416;
        gRoomControls.scroll_y = oy;
    }
    if (direction == 1) {
        nx += width;
        gRoomControls.scroll_x = ox + width - view;
    }
    if (direction == 2) {
        ny += 416;
        gRoomControls.scroll_y = oy + 416 - 160;
    }
    if (direction == 3) {
        nx -= width;
        gRoomControls.scroll_x = ox;
    }
    maps(0);
    Port_Widescreen_UpdateShadows();
    Port_Widescreen_BeginScroll(1, direction);
    gRoomControls.room = 1;
    gRoomControls.origin_x = nx;
    gRoomControls.origin_y = ny;
    gRoomControls.scrollAction = 2;
    gRoomControls.scrollSubAction = 2;
    gRoomControls.scroll_direction = direction;
    maps(1);
    int oldX = gRoomControls.scroll_x, oldY = gRoomControls.scroll_y;
    int duration = direction & 1 ? 60 : 40;
    for (int t = 0; t <= duration; t++) {
        gRoomControls.unk_18 = t;
        if (!Port_Widescreen_AdvanceScroll()) {
            failures++;
            return;
        }
        if ((direction == 1 && gRoomControls.scroll_x < oldX) || (direction == 3 && gRoomControls.scroll_x > oldX) ||
            (direction == 2 && gRoomControls.scroll_y < oldY) || (direction == 0 && gRoomControls.scroll_y > oldY))
            failures++;
        oldX = gRoomControls.scroll_x;
        oldY = gRoomControls.scroll_y;
        UpdateScreenShake();
        gUpdateVisibleTiles = 2;
        UpdateScrollVram();
        Port_Widescreen_UpdateShadows();
        for (int l = 0; l < 2; l++)
            for (int x = 0; x < view; x++)
                for (int y = 0; y < 160; y++) {
                    int wx = gRoomControls.scroll_x + x, wy = gRoomControls.scroll_y + y;
                    bool incoming = wx >= nx && wx < nx + width && wy >= ny && wy < ny + 416;
                    int mx = (wx - (incoming ? nx : ox)) / 8, my = (wy - (incoming ? ny : oy)) / 8;
                    u16 expected = tile(l, incoming, mx, my), actual = displayed(l, x, y);
                    if (actual != expected) {
                        fprintf(stderr, "direction=%d width=%d t=%d layer=%d pixel=%d,%d got=%u expected=%u\n",
                                direction, width, t, l, x, y, actual, expected);
                        failures++;
                        return;
                    }
                }
    }
    if (direction == 0 && gRoomControls.scroll_y != ny + 416 - 160)
        failures++;
    if (direction == 1 && gRoomControls.scroll_x != nx)
        failures++;
    if (direction == 2 && gRoomControls.scroll_y != ny)
        failures++;
    if (direction == 3 && gRoomControls.scroll_x != nx + width - view)
        failures++;
    gRoomControls.scrollAction = 1;
    Port_Widescreen_UpdateShadows();
    if (Port_Widescreen_AdvanceScroll())
        failures++;
}

static void parallax(void) {
    setup();
    maps(0);
    gMapTop.bgSettings = NULL;
    gRoomControls.width = 800;
    gRoomControls.origin_x = 0;
    gScreen.bg3.control = 0x1d09;
    gScreen.bg1.control = 0x1e09;
    extern u8 gUnk_02006F00[];
    for (int l = 0; l < 2; l++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 128; x++)
                ((u16*)gUnk_02006F00)[l * 4096 + y * 128 + x] = tile(l, 0, x, y);
    for (int camera = -32; camera <= 850; camera++) {
        gRoomControls.scroll_x = camera;
        for (int l = 0; l < 2; l++) {
            BgSettings* b = l ? &gScreen.bg1 : (BgSettings*)&gScreen.bg3;
            int scroll = Port_HorizontalMinishPathScroll(camera, 800, l ? 2 : 3, 266);
            b->subTileMap = gBG3Buffer + l * 1024;
            b->xOffset = scroll & 15;
            b->yOffset = 13;
        }
        Port_Widescreen_UpdateShadows();
        for (int l = 0; l < 2; l++) {
            int bg = l ? 1 : 3;
            int scroll = Port_HorizontalMinishPathScroll(camera, 800, l ? 2 : 3, 266);
            if (!virtuappu_mode1_ws_shadow[bg]) {
                failures++;
                return;
            }
            for (int x = 240; x < 266; x++)
                for (int y = 0; y < 160; y++) {
                    int bx = x + (scroll & 15), by = y + 13;
                    u16 actual =
                        virtuappu_mode1_ws_shadow[bg][(by / 8) * 7 +
                                                      (((bx / 8) - virtuappu_mode1_ws_shadow_base_tile[bg] + 32) & 31)];
                    u16 expected = tile(l, 0, (scroll + x) / 8, by / 8);
                    if (actual != expected) {
                        fprintf(stderr, "parallax camera=%d layer=%d x=%d y=%d\n", camera, l, x, y);
                        failures++;
                        return;
                    }
                }
        }
    }
    /* A menu or another background must not inherit leaf-plane shadows. */
    gMain.task = TASK_TITLE;
    Port_Widescreen_UpdateShadows();
    if (virtuappu_mode1_ws_shadow[1] || virtuappu_mode1_ws_shadow[3])
        failures++;
}

static void shakeReveal(void) {
    setup();
    maps(0);
    for (int phase = 0; phase < 16; ++phase) {
        gRoomControls.scroll_x = 480 + 32 + phase;
        gRoomControls.scroll_y = 416 + 32 + phase;
        gUpdateVisibleTiles = 1;
        UpdateScrollVram();
        for (int dx = -8; dx <= 8; dx += 8) {
            for (int dy = -8; dy <= 8; dy += 8) {
                UpdateScreenShake();
                gScreen.bg1.xOffset += dx;
                gScreen.bg2.xOffset += dx;
                gScreen.bg1.yOffset += dy;
                gScreen.bg2.yOffset += dy;
                Port_Widescreen_UpdateShadows();
                for (int l = 0; l < 2; ++l) {
                    for (int x = 240; x < 266; ++x) {
                        for (int y = 0; y < 160; ++y) {
                            u16 expected = tile(l, 0, (32 + phase + dx + x) / 8, (32 + phase + dy + y) / 8);
                            if (displayed(l, x, y) != expected) {
                                fprintf(stderr, "shake phase=%d delta=%d,%d pixel=%d,%d\n", phase, dx, dy, x, y);
                                ++failures;
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
}

int main(void) {
    for (int d = 0; d < 4; d++) {
        transition(d, 480);
        transition(d, 256);
    }
    parallax();
    shakeReveal();
    if (failures) {
        fprintf(stderr, "widescreen stream: %u failures\n", failures);
        return 1;
    }
    puts("widescreen stream: four directions, two layers, 256/266 widths, exact endpoints and 883 parallax positions "
         "and screen shake PASS");
}
