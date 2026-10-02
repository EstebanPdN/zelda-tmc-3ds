#ifndef TMC_PORT_SECOND_SCREEN_3DS_H
#define TMC_PORT_SECOND_SCREEN_3DS_H

#include <stdint.h>

#include "port_second_screen_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void Port_SecondScreen_3DS_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                    const SecondScreenSnapshot* snap, uint32_t tick);
void Port_SecondScreen_3DS_SetVisibleInGame(int inGame);
void Port_SecondScreen_3DS_OnTap(int x, int y, int longPress);
int Port_SecondScreen_3DS_NeedsRefresh(void);

#ifdef __cplusplus
}
#endif

#endif
