/*
 * port_gpu_obj_cull.c — per-line OBJ candidate cull. See port_gpu_obj_cull.h.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "port_gpu_obj_cull.h"

#include <stddef.h>

/* OBJ shape/size dimension tables (mirror mode1_obj_widths/heights and the
 * shader's OBJ_W/OBJ_H). */
static const int kObjW[12] = { 8, 16, 32, 64, 16, 32, 32, 64, 8, 8, 16, 32 };
static const int kObjH[12] = { 8, 16, 32, 64, 8, 8, 16, 32, 16, 32, 32, 64 };

void Port_GpuObjCull_Build(const uint16_t* oam, int frame_width, int frame_height, uint32_t* out) {
    const int viewport = frame_width; /* MODE1_GBA_VIEWPORT_X == frame width */

    for (int line = 0; line < frame_height; ++line) {
        uint32_t* row = &out[(size_t)line * PORT_GPU_OBJ_CULL_STRIDE];
        uint32_t count = 0;

        for (int i = 0; i < 128; ++i) {
            uint16_t attr0 = oam[i * 4 + 0];
            uint16_t attr1 = oam[i * 4 + 1];

            int affine = (attr0 >> 8) & 1;
            int double_size = affine && ((attr0 >> 9) & 1);
            int hidden = !affine && ((attr0 >> 9) & 1);
            if (hidden) {
                continue;
            }
            unsigned shape = (attr0 >> 14) & 3u;
            unsigned size = (attr1 >> 14) & 3u;
            if (shape >= 3u) {
                continue;
            }
            int obj_w = kObjW[shape * 4 + size];
            int obj_h = kObjH[shape * 4 + size];
            int bw = obj_w;
            int bh = obj_h;
            if (affine && double_size) {
                bw *= 2;
                bh *= 2;
            }
            int obj_y = attr0 & 0xFF;
            if (obj_y >= 160) {
                obj_y -= 256;
            }
            if (line < obj_y || line >= obj_y + bh) {
                continue; /* vertical cull — matches obj_resolve */
            }
            int obj_x = attr1 & 0x1FF;
            if (obj_x >= frame_width) {
                obj_x -= 512;
            }
            if (obj_x + bw <= 0 || obj_x >= viewport) {
                continue; /* horizontal frustum cull */
            }

            /* Passes every sprite-level test the shader applies before the
             * per-pixel loop. Keep in ascending OAM index order so the shader's
             * lowest-index-wins + window-mask semantics are preserved. */
            row[1 + count] = (uint32_t)i;
            ++count;
        }
        row[0] = count;
    }
}
