#ifndef PORT_WIDESCREEN_INTERNAL_H
#define PORT_WIDESCREEN_INTERNAL_H
#include "global.h"
/* Native backing storage for the engine parallax tilemap workspace. */
extern u8 gUnk_02006F00[0x4000];

/* Engine bridge for rolling tilemap fills. Returns zero when the caller must
 * use the original native fill. Inputs are borrowed map/screenblock buffers. */
#if defined(MODE1_GBA_WIDTH) && MODE1_GBA_WIDTH > 240
int Port_WidescreenFillScroll(u16* map, u16* buffer);
#endif
#endif
