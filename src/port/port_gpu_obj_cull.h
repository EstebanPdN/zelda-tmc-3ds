#pragma once
/*
 * port_gpu_obj_cull.h — per-line OBJ candidate cull for the GPU rasterizers.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Both GPU raster backends (Vulkan via port_gpu_raster.cpp, GLES via
 * port_gpu_raster_gl.cpp) otherwise scan all 128 OAM entries per pixel. On weak
 * tiled GPUs (e.g. Adreno 405) that per-pixel 128-scan dominates. This builds,
 * once per frame on the CPU, the list of OAM indices that can contribute to
 * each scanline — applying EXACTLY the sprite-level (line-independent + vertical)
 * skip tests the shader's obj_resolve applies — so the shader loops a short
 * per-line list instead. The per-pixel tests stay in the shader, so output is
 * bit-identical to the full scan.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max candidates per line = all 128 sprites (no hardware per-line cap is
 * modelled). Buffer layout is row-major with stride (1 + MAX): slot 0 holds the
 * count, slots 1..count hold OAM indices in ascending order. */
#define PORT_GPU_OBJ_CULL_MAX 128
#define PORT_GPU_OBJ_CULL_STRIDE (PORT_GPU_OBJ_CULL_MAX + 1)

/* Fill `out` (frame_height * PORT_GPU_OBJ_CULL_STRIDE u32) with per-line OBJ
 * candidate lists from `oam` (512 halfwords). `frame_width` is the visible
 * width (== the OBJ viewport clip). */
void Port_GpuObjCull_Build(const uint16_t* oam, int frame_width, int frame_height, uint32_t* out);

#ifdef __cplusplus
}
#endif
