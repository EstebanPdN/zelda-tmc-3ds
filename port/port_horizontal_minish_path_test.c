#include <stdio.h>
#include <string.h>

#include "global.h"

void sub_08058034(void);

u16 gMapDataTopSpecial[0x4000];
u8 gUnk_02006F00[0x4000];

void* port_resolve_write_addr(uintptr_t value) {
    return (void*)value;
}

const void* port_resolve_copy_src(const void* src, u32 size) {
    (void)size;
    return src;
}

void port_hdma_register(int channel, const void* src, void* dest, uint16_t cnt_h, uint16_t count) {
    (void)channel;
    (void)src;
    (void)dest;
    (void)cnt_h;
    (void)count;
}

int main(void) {
    u16* output = (u16*)gUnk_02006F00;
    unsigned failures = 0;

    for (u32 i = 0; i < 0x2000u; ++i) {
        gMapDataTopSpecial[i] = (u16)(i ^ 0x5A5Au);
    }
    for (u32 i = 0x2000u; i < 0x4000u; ++i) {
        gMapDataTopSpecial[i] = 0xD00Du;
    }
    memset(gUnk_02006F00, 0, sizeof(gUnk_02006F00));

    sub_08058034();

    for (u32 layer = 0; layer < 2u; ++layer) {
        for (u32 quadrant = 0; quadrant < 4u; ++quadrant) {
            for (u32 row = 0; row < 0x20u; ++row) {
                for (u32 column = 0; column < 0x20u; ++column) {
                    const u32 src = layer * 0x1000u + quadrant * 0x400u + row * 0x20u + column;
                    const u32 dst = layer * 0x1000u + quadrant * 0x20u + row * 0x80u + column;
                    if (output[dst] != gMapDataTopSpecial[src]) {
                        if (failures++ < 8u) {
                            fprintf(stderr, "FAIL: layer %u destination 0x%X got 0x%04X expected 0x%04X\n",
                                    layer, dst, output[dst], gMapDataTopSpecial[src]);
                        }
                    }
                }
            }
        }
    }

    for (u32 i = 0x2000u; i < 0x4000u; ++i) {
        if (gMapDataTopSpecial[i] != 0xD00Du) {
            fprintf(stderr, "FAIL: host-only tail of gMapDataTopSpecial was used as the aliased destination\n");
            failures++;
            break;
        }
    }

    if (failures != 0u) {
        fprintf(stderr, "port_horizontal_minish_path_test: %u failure(s)\n", failures);
        return 1;
    }
    puts("port_horizontal_minish_path_test: ALL PASS");
    return 0;
}
