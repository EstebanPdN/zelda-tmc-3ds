#ifndef PORT_SECOND_SCREEN_CAMERA_H
#define PORT_SECOND_SCREEN_CAMERA_H

#include <math.h>

typedef struct {
    int valid;
    float x, y, scale;
} SecondScreenCamera;

/* Paint-owned camera state. Return whether another cadence paint is needed.
 * Snap subpixel residuals so a completed glide can resume static repaint skips.
 * The map crop is smaller than 512 pixels in either dimension. */
static inline int SecondScreenCamera_Advance(SecondScreenCamera* camera,
                                             float x, float y, float scale) {
    if (!camera->valid) {
        camera->valid = 1;
        camera->x = x;
        camera->y = y;
        camera->scale = scale;
        return 0;
    }
    camera->x += (x - camera->x) * 0.22f;
    camera->y += (y - camera->y) * 0.22f;
    camera->scale += (scale - camera->scale) * 0.22f;
    if (fabsf(x - camera->x) * scale < 0.125f &&
        fabsf(y - camera->y) * scale < 0.125f &&
        fabsf(scale - camera->scale) * 512.0f < 0.125f) {
        camera->x = x;
        camera->y = y;
        camera->scale = scale;
        return 0;
    }
    return 1;
}

#endif
