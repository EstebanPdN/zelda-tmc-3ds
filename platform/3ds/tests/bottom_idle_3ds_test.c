#include "bottom_idle_3ds.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RGBA(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

enum { WIDTH = 320, HEIGHT = 240, STRIDE = 512 };

int main(int argc, char** argv) {
    static uint32_t frame[STRIDE * HEIGHT];
    const uint32_t paddingSentinel = 0x12345678u;
    for (unsigned i = 0; i < STRIDE * HEIGHT; ++i) frame[i] = paddingSentinel;

    BottomIdle3DS_Paint(frame, WIDTH, HEIGHT, STRIDE, 0, true);

    assert(frame[0] == RGBA(0, 0, 0));
    assert(frame[6 * STRIDE + 6] == RGBA(122, 88, 30));
    assert(frame[115 * STRIDE + 160] != RGBA(0, 0, 0));
    assert(frame[130 * STRIDE + 152] != RGBA(0, 0, 0));
    assert(frame[130 * STRIDE + 168] != RGBA(0, 0, 0));
    assert(frame[100 * STRIDE + 160] == RGBA(0, 0, 0));
    assert(frame[100 * STRIDE + WIDTH] == paddingSentinel);

    const uint32_t firstPulse = frame[115 * STRIDE + 160];
    BottomIdle3DS_Paint(frame, WIDTH, HEIGHT, STRIDE, 20, true);
    assert(frame[115 * STRIDE + 160] != firstPulse);

    BottomIdle3DS_Paint(frame, WIDTH, HEIGHT, STRIDE, 0, false);
    uint32_t steady[STRIDE*HEIGHT]; memcpy(steady,frame,sizeof(frame));
    BottomIdle3DS_Paint(frame, WIDTH, HEIGHT, STRIDE, 400, false);
    assert(memcmp(steady,frame,sizeof(frame))==0);
    unsigned aa=0;
    for(int y=100;y<140;y++)for(int x=140;x<180;x++) {
        uint32_t color=frame[y*STRIDE+x];
        if(color!=RGBA(0,0,0) && color!=RGBA(214,177,87)) ++aa;
    }
    assert(aa>20);
    if (argc == 2) {
        FILE* file = fopen(argv[1], "wb");
        assert(file != NULL);
        for (int y = 0; y < HEIGHT; ++y) {
            assert(fwrite(frame + y * STRIDE, sizeof(uint32_t), WIDTH, file) == WIDTH);
        }
        assert(fclose(file) == 0);
    }

    puts("bottom_idle_3ds_test: PASS");
    return 0;
}
