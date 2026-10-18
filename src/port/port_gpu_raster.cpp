/*
 * port_gpu_raster.cpp — GPU PPU rasterizer host side.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See port_gpu_raster.h and docs/gpu-rasterizer-design.md. Uploads the
 * CPU-prepared GBA memory to storage buffers, runs the fragment PPU shader
 * into an offscreen R8G8B8A8_UNORM target, and reads it back on demand.
 */

#include "port_gpu_raster.h"
#include "port_gpu_obj_cull.h"

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
/* Storage-buffer slots, ordered by feature-onset so each implementation phase
 * binds a contiguous prefix (must match ppu_raster.frag's binding numbers).
 * Bump PORT_GPU_RASTER_NUM_SSBO as phases add layers; keep it equal to the
 * shader's num_storage_buffers. */
enum {
    SSBO_BG_PALETTE = 0,
    SSBO_IO_PER_LINE = 1,
    SSBO_VRAM = 2,
    SSBO_OBJ_PALETTE = 3,
    SSBO_OAM = 4,
    SSBO_AFFINE_REF = 5,
    SSBO_WS_SHADOW = 6,
    SSBO_OBJ_CULL = 7,
    SSBO_SLOT_COUNT = 8
};
/* Binds all 8: +per-line OBJ candidate cull. */
#define PORT_GPU_RASTER_NUM_SSBO 8
struct PortGpuRaster {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUShader* vert = nullptr;
    SDL_GPUShader* frag = nullptr;

    /* Persistent per-slot storage buffers (GPU-side), grown on demand. */
    SDL_GPUBuffer* ssbo[SSBO_SLOT_COUNT] = {};
    Uint32 ssbo_cap[SSBO_SLOT_COUNT] = {};

    /* ONE coalesced upload arena for all SSBOs — mapped once per frame, then
     * one GPU-timeline copy per slot from a sub-offset. Replaces 8 separate
     * map/unmap pairs (each a driver sync point ~= the whole raster's cost). */
    SDL_GPUTransferBuffer* arena = nullptr;
    Uint32 arena_cap = 0;

    SDL_GPUTexture* target = nullptr;
    int target_w = 0;
    int target_h = 0;

    SDL_GPUTransferBuffer* download = nullptr; /* sync path */
    Uint32 download_cap = 0;

    /* Deferred (double-buffered) readback ring: submit frame N without waiting,
     * read frame N-1 next call via SDL_QueryGPUFence — the CPU never stalls on
     * the GPU. Costs 1 frame of latency (opt-in). */
    enum { DEFER_RING = 2 };
    SDL_GPUTransferBuffer* defer_xfer[DEFER_RING] = {};
    SDL_GPUFence* defer_fence[DEFER_RING] = {};
    int defer_w[DEFER_RING] = {};
    int defer_h[DEFER_RING] = {};
    Uint32 defer_cap[DEFER_RING] = {};
    int defer_head = 0;        /* next slot to submit into */
    bool defer_primed = false; /* a prior frame is in flight */

    /* Interleaved [x0,y0,x1,y1,...] affine reference, rebuilt per frame from
     * the frame's separate x/y arrays (zeros when non-affine). */
    std::vector<int32_t> aff_scratch;
    /* Per-line OBJ candidate lists, rebuilt per frame (stride 129). */
    std::vector<uint32_t> cull_scratch;
};

/* std140 mirror of ppu_raster.frag's Params (five 16-byte vectors). */
struct RasterParams {
    int32_t geom[4];   /* width, height, mode, affine */
    uint32_t misc[4];  /* frame_dispcnt, pad, pad, pad */
    int32_t ws[4];     /* bg_clip_x, ws_cols, hud_right_anchor, hud_right_native_x */
    int32_t wsmsg[4];  /* msg_shift, msg_x0, msg_x1, (msg_y0<<16|msg_y1) */
    int32_t wsbase[4]; /* per-BG shadow_base_tile (<0 = none) */
};

static SDL_GPUShader* make_shader(SDL_GPUDevice* dev, SDL_GPUShaderFormat format, const char* entrypoint,
                                  const void* code, size_t len, SDL_GPUShaderStage stage, Uint32 num_storage,
                                  Uint32 num_uniform) {
    SDL_GPUShaderCreateInfo ci;
    SDL_zero(ci);
    ci.code = static_cast<const Uint8*>(code);
    ci.code_size = len;
    ci.entrypoint = entrypoint;
    ci.format = format;
    ci.stage = stage;
    ci.num_storage_buffers = num_storage;
    ci.num_uniform_buffers = num_uniform;
    SDL_GPUShader* s = SDL_CreateGPUShader(dev, &ci);
    if (!s) {
        std::fprintf(stderr, "[gpuraster] CreateGPUShader failed: %s\n", SDL_GetError());
    }
    return s;
}

extern "C" PortGpuRaster* Port_GpuRaster_Create(SDL_GPUDevice* device, SDL_GPUShaderFormat format,
                                                const char* entrypoint, const void* vert, size_t vert_len,
                                                const void* frag, size_t frag_len) {
    if (!device || !vert || !frag) {
        return nullptr;
    }
    if (!entrypoint) {
        entrypoint = "main";
    }
    PortGpuRaster* r = new (std::nothrow) PortGpuRaster();
    if (!r) {
        return nullptr;
    }
    r->device = device;

    r->vert = make_shader(device, format, entrypoint, vert, vert_len, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    r->frag = make_shader(device, format, entrypoint, frag, frag_len, SDL_GPU_SHADERSTAGE_FRAGMENT,
                          PORT_GPU_RASTER_NUM_SSBO, 1);
    if (!r->vert || !r->frag) {
        Port_GpuRaster_Destroy(r);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd;
    SDL_zero(ctd);
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_GPUGraphicsPipelineCreateInfo pci;
    SDL_zero(pci);
    pci.vertex_shader = r->vert;
    pci.fragment_shader = r->frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &ctd;
    r->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    if (!r->pipeline) {
        std::fprintf(stderr, "[gpuraster] CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        Port_GpuRaster_Destroy(r);
        return nullptr;
    }
    return r;
}

extern "C" void Port_GpuRaster_Destroy(PortGpuRaster* r) {
    if (!r) {
        return;
    }
    SDL_GPUDevice* d = r->device;
    if (d) {
        for (int i = 0; i < SSBO_SLOT_COUNT; ++i) {
            if (r->ssbo[i]) {
                SDL_ReleaseGPUBuffer(d, r->ssbo[i]);
            }
        }
        if (r->arena) {
            SDL_ReleaseGPUTransferBuffer(d, r->arena);
        }
        if (r->download) {
            SDL_ReleaseGPUTransferBuffer(d, r->download);
        }
        for (int i = 0; i < PortGpuRaster::DEFER_RING; ++i) {
            if (r->defer_fence[i]) {
                SDL_WaitForGPUFences(d, true, &r->defer_fence[i], 1);
                SDL_ReleaseGPUFence(d, r->defer_fence[i]);
            }
            if (r->defer_xfer[i]) {
                SDL_ReleaseGPUTransferBuffer(d, r->defer_xfer[i]);
            }
        }
        if (r->target) {
            SDL_ReleaseGPUTexture(d, r->target);
        }
        if (r->pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(d, r->pipeline);
        }
        if (r->vert) {
            SDL_ReleaseGPUShader(d, r->vert);
        }
        if (r->frag) {
            SDL_ReleaseGPUShader(d, r->frag);
        }
    }
    delete r;
}

/* Ensure slot `i`'s GPU storage buffer holds >= `size` bytes. */
static bool ensure_ssbo(PortGpuRaster* r, int i, Uint32 size) {
    if (size == 0) {
        size = 4;
    }
    if (r->ssbo[i] && r->ssbo_cap[i] >= size) {
        return true;
    }
    if (r->ssbo[i]) {
        SDL_ReleaseGPUBuffer(r->device, r->ssbo[i]);
        r->ssbo[i] = nullptr;
    }
    SDL_GPUBufferCreateInfo bci;
    SDL_zero(bci);
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = size;
    r->ssbo[i] = SDL_CreateGPUBuffer(r->device, &bci);
    if (!r->ssbo[i]) {
        std::fprintf(stderr, "[gpuraster] ssbo alloc failed (slot %d, %u bytes): %s\n", i, size, SDL_GetError());
        return false;
    }
    r->ssbo_cap[i] = size;
    return true;
}

static bool ensure_arena(PortGpuRaster* r, Uint32 size) {
    if (r->arena && r->arena_cap >= size) {
        return true;
    }
    if (r->arena) {
        SDL_ReleaseGPUTransferBuffer(r->device, r->arena);
        r->arena = nullptr;
    }
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_zero(tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;
    r->arena = SDL_CreateGPUTransferBuffer(r->device, &tci);
    if (!r->arena) {
        std::fprintf(stderr, "[gpuraster] arena alloc failed (%u bytes): %s\n", size, SDL_GetError());
        return false;
    }
    r->arena_cap = size;
    return true;
}

/* One pending SSBO upload (slot + source bytes), packed into the arena. */
struct PendingUpload {
    int slot;
    const void* src;
    Uint32 size;
    Uint32 off;
};

/* Upload all pending SSBOs through ONE arena transfer buffer: map once, memcpy
 * every region, unmap once, then one GPU-timeline copy per slot. Replaces N
 * separate map/unmap pairs (the raster path's dominant cost). */
static bool flush_uploads(PortGpuRaster* r, SDL_GPUCopyPass* cp, PendingUpload* up, int n) {
    Uint32 total = 0;
    for (int i = 0; i < n; ++i) {
        Uint32 sz = up[i].size ? up[i].size : 4u;
        up[i].off = total;
        total += (sz + 15u) & ~15u; /* 16-byte align each region */
        if (!ensure_ssbo(r, up[i].slot, up[i].size)) {
            return false;
        }
    }
    if (!ensure_arena(r, total)) {
        return false;
    }
    uint8_t* base = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(r->device, r->arena, /*cycle=*/true));
    if (!base) {
        std::fprintf(stderr, "[gpuraster] map arena failed: %s\n", SDL_GetError());
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (up[i].size) {
            std::memcpy(base + up[i].off, up[i].src, up[i].size);
        }
    }
    SDL_UnmapGPUTransferBuffer(r->device, r->arena);
    for (int i = 0; i < n; ++i) {
        SDL_GPUTransferBufferLocation loc;
        SDL_zero(loc);
        loc.transfer_buffer = r->arena;
        loc.offset = up[i].off;
        SDL_GPUBufferRegion reg;
        SDL_zero(reg);
        reg.buffer = r->ssbo[up[i].slot];
        reg.offset = 0;
        reg.size = up[i].size ? up[i].size : 4u;
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
    }
    return true;
}

static bool ensure_target(PortGpuRaster* r, int w, int h) {
    if (r->target && r->target_w == w && r->target_h == h) {
        return true;
    }
    if (r->target) {
        SDL_ReleaseGPUTexture(r->device, r->target);
        r->target = nullptr;
    }
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = static_cast<Uint32>(w);
    ti.height = static_cast<Uint32>(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    r->target = SDL_CreateGPUTexture(r->device, &ti);
    if (!r->target) {
        std::fprintf(stderr, "[gpuraster] CreateGPUTexture %dx%d failed: %s\n", w, h, SDL_GetError());
        return false;
    }
    r->target_w = w;
    r->target_h = h;
    return true;
}

/* Record the per-frame upload + params + render pass into `cmd` (up to but not
 * including submit). Shared by Render and RenderReadback. Returns false if a
 * buffer upload failed (caller must still submit `cmd` to release it). */
static bool record_frame(PortGpuRaster* r, const PortGpuRasterFrame* f, SDL_GPUCommandBuffer* cmd) {
    /* Build the interleaved affine reference (zeros when non-affine); slot 5 is
     * always bound so the buffer must always be valid. */
    r->aff_scratch.assign((size_t)f->frame_height * 2u, 0);
    if (f->affine && f->affine_ref_x && f->affine_ref_y) {
        for (int y = 0; y < f->frame_height; ++y) {
            r->aff_scratch[(size_t)y * 2u + 0u] = f->affine_ref_x[y];
            r->aff_scratch[(size_t)y * 2u + 1u] = f->affine_ref_y[y];
        }
    }

    /* Build the per-line OBJ candidate lists (stride 129). */
    r->cull_scratch.assign((size_t)f->frame_height * PORT_GPU_OBJ_CULL_STRIDE, 0);
    Port_GpuObjCull_Build(f->oam, f->frame_width, f->frame_height, r->cull_scratch.data());

    /* Coalesce all SSBO uploads into one arena (one map/unmap), then one copy
     * per slot — replaces 8 separate map/unmap pairs, the raster's hot cost. */
    static const uint32_t ws_dummy = 0u;
    const bool has_ws = (f->ws_shadow && f->ws_shadow_halfwords > 0);
    PendingUpload up[SSBO_SLOT_COUNT] = {
        { SSBO_BG_PALETTE, f->bg_palette, 256u * (Uint32)sizeof(uint16_t), 0 },
        { SSBO_IO_PER_LINE, f->io_per_line, (f->io_uniform ? 1u : (Uint32)f->frame_height) * 0x400u, 0 },
        { SSBO_VRAM, f->vram, 0x18000u, 0 },
        { SSBO_OBJ_PALETTE, f->obj_palette, 256u * (Uint32)sizeof(uint16_t), 0 },
        { SSBO_OAM, f->oam, 512u * (Uint32)sizeof(uint16_t), 0 },
        { SSBO_AFFINE_REF, r->aff_scratch.data(), (Uint32)(r->aff_scratch.size() * sizeof(int32_t)), 0 },
        { SSBO_WS_SHADOW, has_ws ? (const void*)f->ws_shadow : (const void*)&ws_dummy,
          has_ws ? (Uint32)(f->ws_shadow_halfwords * (int)sizeof(uint16_t)) : (Uint32)sizeof(ws_dummy), 0 },
        { SSBO_OBJ_CULL, r->cull_scratch.data(), (Uint32)(r->cull_scratch.size() * sizeof(uint32_t)), 0 },
    };
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    bool ok = flush_uploads(r, cp, up, SSBO_SLOT_COUNT);
    SDL_EndGPUCopyPass(cp);
    if (!ok) {
        return false;
    }

    RasterParams p;
    SDL_zero(p);
    p.geom[0] = f->frame_width;
    p.geom[1] = f->frame_height;
    p.geom[2] = f->mode;
    p.geom[3] = f->affine ? 1 : 0;
    p.misc[0] = f->frame_dispcnt;
    p.misc[1] = f->io_uniform ? 1u : 0u;
    p.misc[2] = (uint32_t)(f->scale > 1 ? f->scale : 1);
    p.ws[0] = f->ws_bg_clip_x ? f->ws_bg_clip_x : 240;
    p.ws[1] = f->ws_cols;
    p.ws[2] = f->ws_hud_right_anchor;
    p.ws[3] = f->ws_hud_right_native_x ? f->ws_hud_right_native_x : 176;
    p.wsmsg[0] = f->ws_msg_shift;
    p.wsmsg[1] = f->ws_msg_x0;
    p.wsmsg[2] = f->ws_msg_x1;
    p.wsmsg[3] = (f->ws_msg_y0 << 16) | (f->ws_msg_y1 & 0xFFFF);
    for (int i = 0; i < 4; ++i) {
        p.wsbase[i] = (f->ws_shadow && f->ws_shadow_base_tile[i] >= 0) ? f->ws_shadow_base_tile[i] : -1;
    }

    SDL_GPUColorTargetInfo ct;
    SDL_zero(ct);
    ct.texture = r->target;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    SDL_BindGPUGraphicsPipeline(rp, r->pipeline);
    SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof(p));
    SDL_GPUBuffer* bind[PORT_GPU_RASTER_NUM_SSBO];
    for (int i = 0; i < PORT_GPU_RASTER_NUM_SSBO; ++i) {
        bind[i] = r->ssbo[i];
    }
    SDL_BindGPUFragmentStorageBuffers(rp, 0, bind, PORT_GPU_RASTER_NUM_SSBO);
    SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
    return true;
}

/* Ensure the persistent download transfer buffer holds >= `need` bytes. */
static bool ensure_download(PortGpuRaster* r, Uint32 need) {
    if (r->download && r->download_cap >= need) {
        return true;
    }
    if (r->download) {
        SDL_ReleaseGPUTransferBuffer(r->device, r->download);
        r->download = nullptr;
    }
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_zero(tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = need;
    r->download = SDL_CreateGPUTransferBuffer(r->device, &tci);
    if (!r->download) {
        std::fprintf(stderr, "[gpuraster] download alloc failed: %s\n", SDL_GetError());
        return false;
    }
    r->download_cap = need;
    return true;
}

extern "C" SDL_GPUTexture* Port_GpuRaster_Render(PortGpuRaster* r, const PortGpuRasterFrame* f) {
    if (!r || !f || f->frame_width <= 0 || f->frame_height <= 0) {
        return nullptr;
    }
    const int S = f->scale > 1 ? f->scale : 1;
    if (!ensure_target(r, f->frame_width * S, f->frame_height * S)) {
        return nullptr;
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return nullptr;
    }
    bool ok = record_frame(r, f, cmd);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }
    return ok ? r->target : nullptr;
}

extern "C" bool Port_GpuRaster_RenderReadback(PortGpuRaster* r, const PortGpuRasterFrame* f, uint32_t* dst, int pitch) {
    if (!r || !f || !dst || f->frame_width <= 0 || f->frame_height <= 0) {
        return false;
    }
    const int S = f->scale > 1 ? f->scale : 1;
    const int W = f->frame_width * S, H = f->frame_height * S;
    if (!ensure_target(r, W, H)) {
        return false;
    }
    if (!ensure_download(r, (Uint32)W * (Uint32)H * 4u)) {
        return false;
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return false;
    }
    if (!record_frame(r, f, cmd)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    /* Download the just-rendered target in the SAME command buffer — the render
     * pass ended above, so this copy pass is legal and executes after it on the
     * GPU timeline. One submit, one fence: no second round-trip. */
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = r->target;
    reg.w = (Uint32)W;
    reg.h = (Uint32)H;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dti;
    SDL_zero(dti);
    dti.transfer_buffer = r->download;
    dti.pixels_per_row = (Uint32)W;
    dti.rows_per_layer = (Uint32)H;
    SDL_DownloadFromGPUTexture(cp, &reg, &dti);
    SDL_EndGPUCopyPass(cp);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }
    const uint32_t* src = (const uint32_t*)SDL_MapGPUTransferBuffer(r->device, r->download, false);
    if (!src) {
        return false;
    }
    if (pitch <= 0) {
        pitch = W;
    }
    for (int y = 0; y < H; ++y) {
        std::memcpy(&dst[(size_t)y * (size_t)pitch], &src[(size_t)y * W], (size_t)W * sizeof(uint32_t));
    }
    SDL_UnmapGPUTransferBuffer(r->device, r->download);
    return true;
}

/* Read one slot's completed download into dst (assumes fence already signaled). */
static bool defer_copy_out(PortGpuRaster* r, int slot, uint32_t* dst, int pitch) {
    const uint32_t* src = (const uint32_t*)SDL_MapGPUTransferBuffer(r->device, r->defer_xfer[slot], false);
    if (!src) {
        return false;
    }
    int w = r->defer_w[slot], h = r->defer_h[slot];
    if (pitch <= 0) {
        pitch = w;
    }
    for (int y = 0; y < h; ++y) {
        std::memcpy(&dst[(size_t)y * (size_t)pitch], &src[(size_t)y * w], (size_t)w * sizeof(uint32_t));
    }
    SDL_UnmapGPUTransferBuffer(r->device, r->defer_xfer[slot]);
    return true;
}

extern "C" bool Port_GpuRaster_RenderReadbackDeferred(PortGpuRaster* r, const PortGpuRasterFrame* f, uint32_t* dst,
                                                      int pitch) {
    if (!r || !f || !dst || f->frame_width <= 0 || f->frame_height <= 0) {
        return false;
    }
    if (!ensure_target(r, f->frame_width, f->frame_height)) {
        return false;
    }
    const int W = f->frame_width, H = f->frame_height;
    const Uint32 need = (Uint32)W * (Uint32)H * 4u;
    const int slot = r->defer_head;

    /* The slot we're about to submit into may still hold an in-flight download
     * from DEFER_RING frames ago; its fence must be retired before we overwrite
     * its transfer buffer. It's almost always already signaled (2 frames old),
     * so this poll rarely blocks. */
    if (r->defer_fence[slot]) {
        SDL_WaitForGPUFences(r->device, true, &r->defer_fence[slot], 1);
        SDL_ReleaseGPUFence(r->device, r->defer_fence[slot]);
        r->defer_fence[slot] = nullptr;
    }
    if (r->defer_cap[slot] < need) {
        if (r->defer_xfer[slot]) {
            SDL_ReleaseGPUTransferBuffer(r->device, r->defer_xfer[slot]);
            r->defer_xfer[slot] = nullptr;
        }
        SDL_GPUTransferBufferCreateInfo tci;
        SDL_zero(tci);
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tci.size = need;
        r->defer_xfer[slot] = SDL_CreateGPUTransferBuffer(r->device, &tci);
        if (!r->defer_xfer[slot]) {
            return false;
        }
        r->defer_cap[slot] = need;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return false;
    }
    if (!record_frame(r, f, cmd)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = r->target;
    reg.w = (Uint32)W;
    reg.h = (Uint32)H;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dti;
    SDL_zero(dti);
    dti.transfer_buffer = r->defer_xfer[slot];
    dti.pixels_per_row = (Uint32)W;
    dti.rows_per_layer = (Uint32)H;
    SDL_DownloadFromGPUTexture(cp, &reg, &dti);
    SDL_EndGPUCopyPass(cp);

    /* Submit WITHOUT waiting — the CPU does not stall on this frame's GPU work. */
    r->defer_fence[slot] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    r->defer_w[slot] = W;
    r->defer_h[slot] = H;

    /* Produce the PREVIOUS frame from the other ring slot if it's ready. */
    bool produced = false;
    if (r->defer_primed) {
        const int prev = (slot + PortGpuRaster::DEFER_RING - 1) % PortGpuRaster::DEFER_RING;
        if (r->defer_fence[prev] && r->defer_w[prev] == W && r->defer_h[prev] == H) {
            /* Non-blocking: only consume if the GPU already finished it. */
            if (SDL_QueryGPUFence(r->device, r->defer_fence[prev])) {
                produced = defer_copy_out(r, prev, dst, pitch);
                SDL_ReleaseGPUFence(r->device, r->defer_fence[prev]);
                r->defer_fence[prev] = nullptr;
            }
        }
    }
    r->defer_primed = true;
    r->defer_head = (slot + 1) % PortGpuRaster::DEFER_RING;
    return produced;
}

extern "C" bool Port_GpuRaster_Readback(PortGpuRaster* r, uint32_t* dst, int width, int height, int pitch) {
    if (!r || !r->target || !dst || width <= 0 || height <= 0) {
        return false;
    }
    if (width > r->target_w || height > r->target_h) {
        return false;
    }
    Uint32 need = static_cast<Uint32>(width) * static_cast<Uint32>(height) * 4u;
    if (!r->download || r->download_cap < need) {
        if (r->download) {
            SDL_ReleaseGPUTransferBuffer(r->device, r->download);
            r->download = nullptr;
        }
        SDL_GPUTransferBufferCreateInfo tci;
        SDL_zero(tci);
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tci.size = need;
        r->download = SDL_CreateGPUTransferBuffer(r->device, &tci);
        if (!r->download) {
            std::fprintf(stderr, "[gpuraster] download alloc failed: %s\n", SDL_GetError());
            return false;
        }
        r->download_cap = need;
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!cmd) {
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg;
    SDL_zero(reg);
    reg.texture = r->target;
    reg.w = static_cast<Uint32>(width);
    reg.h = static_cast<Uint32>(height);
    reg.d = 1;
    SDL_GPUTextureTransferInfo dti;
    SDL_zero(dti);
    dti.transfer_buffer = r->download;
    dti.offset = 0;
    dti.pixels_per_row = static_cast<Uint32>(width);
    dti.rows_per_layer = static_cast<Uint32>(height);
    SDL_DownloadFromGPUTexture(cp, &reg, &dti);
    SDL_EndGPUCopyPass(cp);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(r->device, true, &fence, 1);
        SDL_ReleaseGPUFence(r->device, fence);
    }

    const uint32_t* src = static_cast<const uint32_t*>(SDL_MapGPUTransferBuffer(r->device, r->download, false));
    if (!src) {
        std::fprintf(stderr, "[gpuraster] map download failed: %s\n", SDL_GetError());
        return false;
    }
    if (pitch <= 0) {
        pitch = width;
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(&dst[static_cast<size_t>(y) * static_cast<size_t>(pitch)], &src[static_cast<size_t>(y) * width],
                    static_cast<size_t>(width) * sizeof(uint32_t));
    }
    SDL_UnmapGPUTransferBuffer(r->device, r->download);
    return true;
}
