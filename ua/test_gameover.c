/* tloz-tmc-ua: behavioural test for the GAME OVER hook (src/gameOverTask.c + port/port_ua.h).
 * Build/run via ua/check.sh. Stubs DrawDirect() and records what would be drawn. */
#include "global.h"
#include "menu.h"
#include "affine.h"
#include "region.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u8* gRomData; u32 gRomSize;
OAMCommand gOamCmd;
u8 _gMenuSharedStorage[0x40] __attribute__((aligned(8)));
int gActiveRegion = TMC_REGION_USA;

static int n; static int xs[16], frames[16], sprites[16];
void DrawDirect(u32 sprite, u32 frame) { sprites[n] = sprite; frames[n] = frame; xs[n] = gOamCmd.x; n++; }
void DrawGameOverText(void);

static int fails;
static void expect(const char* name, int count, const int* f, const int* x, u32 sprite) {
    int ok = n == count;
    for (int i = 0; ok && i < count; i++) ok = frames[i] == f[i] && xs[i] == x[i] && sprites[i] == (int)sprite;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    fails += !ok;
}

static void run(const char* name, int region, int ua) {
    static u8 rom[0x1000000];
    memset(rom + 0xFFFFF0, 0xFF, 16);
    if (ua) memcpy(rom + 0xFFFFF0, "TMC-UA\0\1", 8);
    gRomData = rom; gRomSize = sizeof rom; gActiveRegion = region; n = 0;
    DrawGameOverText();
    printf("%-10s:", name);
    for (int i = 0; i < n; i++) printf(" [f%d x=%d s=0x%x]", frames[i], xs[i], sprites[i]);
    printf("\n");
}

int main(void) {
    static const int f8[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const int xUpstream[] = { 48, 68, 88, 108, 137, 156, 174, 192 };
    static const int fUa[] = { 0, 1, 2, 4, 5, 7 };
    static const int xUa[] = { 40, 72, 104, 136, 168, 200 };

    run("USA", TMC_REGION_USA, 0);
    expect("USA ROM keeps upstream GAME OVER", 8, f8, xUpstream, 0x1fd);
    run("EU", TMC_REGION_EU, 0);
    expect("EU ROM keeps upstream GAME OVER", 8, f8, xUpstream, 0x1fc);
    run("UA (USA)", TMC_REGION_USA, 1);
    expect("UA ROM draws KINETS GRY layout", 6, fUa, xUa, 0x1fd);
    return fails ? 1 : 0;
}
