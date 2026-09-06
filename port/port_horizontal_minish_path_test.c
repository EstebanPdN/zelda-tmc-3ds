#include <stdio.h>
#include <string.h>

#include "global.h"

void sub_08058034(void);
void sub_08058004(u32 scroll, void* src, void* dest);

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

    /* Exercise the scrolling consumer as well as the layout producer.
     * Every valid 16-pixel page must update both layers through the last
     * source row, including the second layer at the end of the allocation. */
    for (u32 layer = 0; layer < 2; ++layer) {
        for (u32 page = 0; page <= 48; ++page) {
            u16 actual[0x400];
            memset(actual, 0xCD, sizeof(actual));
            sub_08058004(page * 16u, gUnk_02006F00 + layer * 0x2000u, actual);
            for (u32 row = 0; row < 32; ++row) {
                if (memcmp(actual + row * 32u,
                           gUnk_02006F00 + layer * 0x2000u + page * 4u + row * 0x100u, 64) != 0) {
                    fprintf(stderr, "FAIL: scrolling layer %u page %u row %u was not copied\n", layer, page, row);
                    ++failures;
                    break;
                }
            }
        }
    }
    {
        u16 actual[0x400], unchanged[0x400];
        memset(actual, 0xCD, sizeof(actual));
        memcpy(unchanged, actual, sizeof(actual));
        sub_08058004(49u * 16u, gUnk_02006F00 + 0x2000u, actual);
        sub_08058004(UINT32_MAX, gUnk_02006F00, actual);
        if (memcmp(actual, unchanged, sizeof(actual)) != 0) {
            fputs("FAIL: out-of-range scrolling changed the destination\n", stderr);
            ++failures;
        }
    }

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
