#ifndef PORT_VERTICAL_MINISH_PATH_H
#define PORT_VERTICAL_MINISH_PATH_H

#include <stdint.h>

/* Vertical Minish Path tilemap offsets are byte offsets.  Keeping the
 * calculation on byte pointers prevents u16 pointer scaling from selecting
 * the wrong parallax page after gMapDataTopSpecial was typed as u16[]. */
static inline void* Port_VerticalMinishPathSubTileMap(void* mapData, int32_t bgOffset, int32_t baseAdd) {
    int32_t delta = (bgOffset / 0x40) * 0x200;
    int32_t lo = -baseAdd;
    int32_t hi = 0x7800 - baseAdd;

    if (delta < lo)
        delta = lo;
    if (delta > hi)
        delta = hi;
    return (uint8_t*)mapData + baseAdd + delta;
}

#endif // PORT_VERTICAL_MINISH_PATH_H
