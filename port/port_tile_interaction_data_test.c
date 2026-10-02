#include <stdio.h>

#include "asm.h"

extern const KeyValuePair gUnk_080046A4[];
extern const u16 gUnk_080047F6[];

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                      \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

static u16 LookupTileInteraction(u16 tileType) {
    const KeyValuePair* entry = gUnk_080046A4;
    while (entry->key != 0) {
        if (entry->key == tileType) {
            return entry->value;
        }
        entry++;
    }
    return 0;
}

static unsigned CountTileInteractions(void) {
    const KeyValuePair* entry = gUnk_080046A4;
    while (entry->key != 0) {
        entry++;
    }
    return (unsigned)(entry - gUnk_080046A4);
}

int main(void) {
    CHECK_EQ(CountTileInteractions(), 84u, "retail tile-interaction table length");
    CHECK_EQ(LookupTileInteraction(391u), 36u, "first canonical tail entry");
    CHECK_EQ(LookupTileInteraction(408u), 36u, "last canonical tail entry");

    /* Deepwood Shrine's doorway uses tile type 0xA7.  Aliasing it to row 3
     * makes it liftable and replaces it with row 3's tileChange (0x79). */
    CHECK_EQ(LookupTileInteraction(0xA7u), 0u, "Deepwood doorway is not an interactive grass tile");
    CHECK_EQ((gUnk_080047F6[3u * 4u] >> 6) & 1u, 1u, "row 3 remains a legitimate lift interaction");
    CHECK_EQ(gUnk_080047F6[3u * 4u + 3u], 0x79u, "row 3 replacement explains the corrupted doorway tile");

    if (sFailures != 0) {
        return 1;
    }
    puts("port_tile_interaction_data_test: ALL PASS");
    return 0;
}
