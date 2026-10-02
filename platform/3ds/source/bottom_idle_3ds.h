#ifndef TMC_BOTTOM_IDLE_3DS_H
#define TMC_BOTTOM_IDLE_3DS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the redistributable, procedural Triforce card used while the game is
 * on its title and file-select tasks. The buffer uses the same little-endian
 * RGBA8888 convention as the normal Minish Cap second-screen compositor. */
void BottomIdle3DS_Paint(uint32_t* pixels, int width, int height, int strideInPixels, uint32_t tick);

#ifdef __cplusplus
}
#endif

#endif
