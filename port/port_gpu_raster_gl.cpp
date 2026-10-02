/*
 * port_gpu_raster_gl.cpp — GLES 3.1 compute PPU rasterizer. See the header and
 * docs/gpu-rasterizer-design.md.
 *
 * Part of the The Minish Cap PC port — GPL-3.0-or-later.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "port_gpu_raster_gl.h"

#if defined(__ANDROID__) || defined(__linux__)
#define TMC_GLES_RASTER_IMPL 1
#endif

#ifndef TMC_GLES_RASTER_IMPL

extern "C" PortGpuRasterGl* Port_GpuRasterGl_Create(const char* core_glsl, int core_len) {
    (void)core_glsl;
    (void)core_len;
    return nullptr;
}
extern "C" void Port_GpuRasterGl_Destroy(PortGpuRasterGl* r) {
    (void)r;
}
extern "C" bool Port_GpuRasterGl_RenderReadback(PortGpuRasterGl* r, const PortGpuRasterFrame* f, uint32_t* dst,
                                                int pitch) {
    (void)r;
    (void)f;
    (void)dst;
    (void)pitch;
    return false;
}

#else

#include "port_gpu_obj_cull.h"

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

/* SSBO binding points (compute) — must match the prologue below and the shader
 * core's buffer instance names. */
enum {
    GL_SSBO_BG_PALETTE = 0,
    GL_SSBO_IO = 1,
    GL_SSBO_VRAM = 2,
    GL_SSBO_OBJ_PALETTE = 3,
    GL_SSBO_OAM = 4,
    GL_SSBO_AFFINE = 5,
    GL_SSBO_WS_SHADOW = 6,
    GL_SSBO_OBJ_CULL = 7,
    GL_SSBO_OUT = 8,
    GL_SSBO_COUNT = 9
};

struct PortGpuRasterGl {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig cfg = nullptr;
    EGLSurface pbuf = EGL_NO_SURFACE; /* only if surfaceless unsupported */

    GLuint prog = 0;
    GLuint ubo = 0;
    GLuint ssbo[GL_SSBO_COUNT] = {};
    GLsizeiptr ssbo_cap[GL_SSBO_COUNT] = {};

    std::vector<int32_t> aff_scratch;
    std::vector<uint32_t> cull_scratch;
    std::vector<uint32_t> out_scratch;
};

/* std140 mirror of the shared core's Params (five 16-byte vectors). */
struct GlRasterParams {
    int32_t geom[4];
    uint32_t misc[4];
    int32_t ws[4];
    int32_t wsmsg[4];
    int32_t wsbase[4];
};

/* Compute-shader prologue: declares the buffers + params the shared core uses,
 * in GLES syntax, then the core is appended, then main(). Buffer instance names
 * MUST match ppu_core.glsl (bgpal/io/vram/objpal/oam/aff/wss/objcull/params). */
static const char* kPrologue = "#version 310 es\n"
                               "precision highp float;\n"
                               "precision highp int;\n"
                               "layout(local_size_x = 8, local_size_y = 8) in;\n"
                               "layout(std430, binding = 0) readonly buffer BgPalette { uint data[]; } bgpal;\n"
                               "layout(std430, binding = 1) readonly buffer IoPerLine { uint data[]; } io;\n"
                               "layout(std430, binding = 2) readonly buffer Vram { uint data[]; } vram;\n"
                               "layout(std430, binding = 3) readonly buffer ObjPalette { uint data[]; } objpal;\n"
                               "layout(std430, binding = 4) readonly buffer Oam { uint data[]; } oam;\n"
                               "layout(std430, binding = 5) readonly buffer AffineRef { int data[]; } aff;\n"
                               "layout(std430, binding = 6) readonly buffer WsShadow { uint data[]; } wss;\n"
                               "layout(std430, binding = 7) readonly buffer ObjCull { uint data[]; } objcull;\n"
                               "layout(std430, binding = 8) writeonly buffer OutFB { uint data[]; } outfb;\n"
                               "layout(std140, binding = 0) uniform Params {\n"
                               "    ivec4 geom; uvec4 misc; ivec4 ws; ivec4 wsmsg; ivec4 wsbase;\n"
                               "} params;\n";

static const char* kMain = "\nvoid main() {\n"
                           "    int x = int(gl_GlobalInvocationID.x);\n"
                           "    int line = int(gl_GlobalInvocationID.y);\n"
                           "    if (x >= params.geom.x || line >= params.geom.y) return;\n"
                           "    vec4 c = ppu_pixel(x, line);\n"
                           /* packUnorm4x8 rounds c*255; colors are exact multiples of 8/255, so this
                            * matches the CPU LUT (<<3) and Vulkan R8G8B8A8_UNORM byte-for-byte.
                            * Byte order: x=r -> low byte => 0xAABBGGRR == ABGR8888 == frame_buffer. */
                           "    outfb.data[line * params.geom.x + x] = packUnorm4x8(c);\n"
                           "}\n";

static GLuint compile_program(const char* core, int core_len) {
    std::string src;
    src.reserve(std::strlen(kPrologue) + (size_t)core_len + std::strlen(kMain) + 2);
    src.append(kPrologue);
    src.append(core, (size_t)core_len);
    src.append(kMain);

    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    const char* p = src.c_str();
    GLint len = (GLint)src.size();
    glShaderSource(sh, 1, &p, &len);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gpuraster-gl] compute compile failed:\n%s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glDeleteShader(sh);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gpuraster-gl] program link failed:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* RAII-ish guard: make our context current, restore the caller's on scope exit. */
struct EglCurrentGuard {
    EGLDisplay prevDpy;
    EGLContext prevCtx;
    EGLSurface prevDraw;
    EGLSurface prevRead;
    EGLDisplay dpy;
    bool ok;
    EglCurrentGuard(EGLDisplay d, EGLSurface s, EGLContext c) : dpy(d) {
        prevDpy = eglGetCurrentDisplay();
        prevCtx = eglGetCurrentContext();
        prevDraw = eglGetCurrentSurface(EGL_DRAW);
        prevRead = eglGetCurrentSurface(EGL_READ);
        ok = eglMakeCurrent(d, s, s, c);
    }
    ~EglCurrentGuard() {
        if (prevDpy == EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        } else {
            eglMakeCurrent(prevDpy, prevDraw, prevRead, prevCtx);
        }
    }
};

extern "C" PortGpuRasterGl* Port_GpuRasterGl_Create(const char* core_glsl, int core_len) {
    if (!core_glsl || core_len <= 0) {
        return nullptr;
    }
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        return nullptr;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        return nullptr;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    /* Capture whatever context is current NOW (SDL's GLES2 context at init) so
     * Create is side-effect-free — must happen BEFORE our first makeCurrent. */
    EGLContext sdlCtx = eglGetCurrentContext();
    EGLSurface sdlDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface sdlRead = eglGetCurrentSurface(EGL_READ);
    EGLDisplay sdlDpy = eglGetCurrentDisplay();

    const EGLint cfgattr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
    EGLConfig cfg = nullptr;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgattr, &cfg, 1, &ncfg) || ncfg < 1) {
        return nullptr;
    }
    const EGLint ctxattr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT) {
        return nullptr;
    }
    EGLSurface pbuf = EGL_NO_SURFACE;
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        const EGLint pbattr[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        pbuf = eglCreatePbufferSurface(dpy, cfg, pbattr);
        if (pbuf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pbuf, pbuf, ctx)) {
            eglDestroyContext(dpy, ctx);
            return nullptr;
        }
    }
    GLint maxSsbo = 0;
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &maxSsbo);
    if (maxSsbo < GL_SSBO_COUNT) {
        std::fprintf(stderr, "[gpuraster-gl] only %d compute SSBOs (need %d); using CPU rasterizer.\n", maxSsbo,
                     GL_SSBO_COUNT);
        if (pbuf != EGL_NO_SURFACE) {
            eglDestroySurface(dpy, pbuf);
        }
        eglDestroyContext(dpy, ctx);
        return nullptr;
    }

    GLuint prog = compile_program(core_glsl, core_len);
    if (!prog) {
        if (pbuf != EGL_NO_SURFACE) {
            eglDestroySurface(dpy, pbuf);
        }
        eglDestroyContext(dpy, ctx);
        return nullptr;
    }

    PortGpuRasterGl* r = new (std::nothrow) PortGpuRasterGl();
    if (!r) {
        glDeleteProgram(prog);
        if (pbuf != EGL_NO_SURFACE) {
            eglDestroySurface(dpy, pbuf);
        }
        eglDestroyContext(dpy, ctx);
        return nullptr;
    }
    r->dpy = dpy;
    r->ctx = ctx;
    r->cfg = cfg;
    r->pbuf = pbuf;
    r->prog = prog;
    glGenBuffers(1, &r->ubo);
    glGenBuffers(GL_SSBO_COUNT, r->ssbo);

    /* Restore SDL's context (best effort). */
    (void)sdlDpy;
    if (sdlCtx != EGL_NO_CONTEXT) {
        eglMakeCurrent(dpy, sdlDraw, sdlRead, sdlCtx);
    } else {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    std::fprintf(stderr, "[gpuraster-gl] GLES %d.%d compute rasterizer active.\n", major, minor);
    return r;
}

extern "C" void Port_GpuRasterGl_Destroy(PortGpuRasterGl* r) {
    if (!r) {
        return;
    }
    if (r->dpy != EGL_NO_DISPLAY && r->ctx != EGL_NO_CONTEXT) {
        EglCurrentGuard g(r->dpy, r->pbuf, r->ctx);
        if (g.ok) {
            if (r->ubo) {
                glDeleteBuffers(1, &r->ubo);
            }
            glDeleteBuffers(GL_SSBO_COUNT, r->ssbo);
            if (r->prog) {
                glDeleteProgram(r->prog);
            }
        }
    }
    if (r->pbuf != EGL_NO_SURFACE) {
        eglDestroySurface(r->dpy, r->pbuf);
    }
    if (r->ctx != EGL_NO_CONTEXT) {
        eglDestroyContext(r->dpy, r->ctx);
    }
    delete r;
}

static void upload_ssbo(PortGpuRasterGl* r, int slot, const void* data, GLsizeiptr size) {
    if (size <= 0) {
        size = 4;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->ssbo[slot]);
    if (r->ssbo_cap[slot] < size) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
        r->ssbo_cap[slot] = size;
    } else if (data) {
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, r->ssbo[slot]);
}

extern "C" bool Port_GpuRasterGl_RenderReadback(PortGpuRasterGl* r, const PortGpuRasterFrame* f, uint32_t* dst,
                                                int pitch) {
    if (!r || !f || !dst || f->frame_width <= 0 || f->frame_height <= 0) {
        return false;
    }
    EglCurrentGuard g(r->dpy, r->pbuf, r->ctx);
    if (!g.ok) {
        return false;
    }
    const int W = f->frame_width, H = f->frame_height;

    /* Per-frame derived buffers (same as the Vulkan backend). */
    r->aff_scratch.assign((size_t)H * 2u, 0);
    if (f->affine && f->affine_ref_x && f->affine_ref_y) {
        for (int y = 0; y < H; ++y) {
            r->aff_scratch[(size_t)y * 2u + 0u] = f->affine_ref_x[y];
            r->aff_scratch[(size_t)y * 2u + 1u] = f->affine_ref_y[y];
        }
    }
    r->cull_scratch.assign((size_t)H * PORT_GPU_OBJ_CULL_STRIDE, 0);
    Port_GpuObjCull_Build(f->oam, W, H, r->cull_scratch.data());

    static const uint32_t zero = 0;
    upload_ssbo(r, GL_SSBO_BG_PALETTE, f->bg_palette, 256 * (GLsizeiptr)sizeof(uint16_t));
    upload_ssbo(r, GL_SSBO_IO, f->io_per_line, (GLsizeiptr)(f->io_uniform ? 1 : H) * 0x400);
    upload_ssbo(r, GL_SSBO_VRAM, f->vram, 0x18000);
    upload_ssbo(r, GL_SSBO_OBJ_PALETTE, f->obj_palette, 256 * (GLsizeiptr)sizeof(uint16_t));
    upload_ssbo(r, GL_SSBO_OAM, f->oam, 512 * (GLsizeiptr)sizeof(uint16_t));
    upload_ssbo(r, GL_SSBO_AFFINE, r->aff_scratch.data(), (GLsizeiptr)r->aff_scratch.size() * 4);
    {
        const void* ws = (f->ws_shadow && f->ws_shadow_halfwords > 0) ? (const void*)f->ws_shadow : (const void*)&zero;
        GLsizeiptr ws_sz = (f->ws_shadow && f->ws_shadow_halfwords > 0)
                               ? (GLsizeiptr)f->ws_shadow_halfwords * (GLsizeiptr)sizeof(uint16_t)
                               : 4;
        upload_ssbo(r, GL_SSBO_WS_SHADOW, ws, ws_sz);
    }
    upload_ssbo(r, GL_SSBO_OBJ_CULL, r->cull_scratch.data(), (GLsizeiptr)r->cull_scratch.size() * 4);

    /* Output SSBO (W*H u32). */
    GLsizeiptr out_sz = (GLsizeiptr)W * H * 4;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->ssbo[GL_SSBO_OUT]);
    if (r->ssbo_cap[GL_SSBO_OUT] < out_sz) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, out_sz, nullptr, GL_DYNAMIC_READ);
        r->ssbo_cap[GL_SSBO_OUT] = out_sz;
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL_SSBO_OUT, r->ssbo[GL_SSBO_OUT]);

    /* Uniforms. */
    GlRasterParams p;
    std::memset(&p, 0, sizeof(p));
    p.geom[0] = W;
    p.geom[1] = H;
    p.geom[2] = f->mode;
    p.geom[3] = f->affine ? 1 : 0;
    p.misc[0] = f->frame_dispcnt;
    p.misc[1] = f->io_uniform ? 1u : 0u;
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
    glBindBuffer(GL_UNIFORM_BUFFER, r->ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(p), &p, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->ubo);

    glUseProgram(r->prog);
    GLuint gx = (GLuint)((W + 7) / 8), gy = (GLuint)((H + 7) / 8);
    glDispatchCompute(gx, gy, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    if (glGetError() != GL_NO_ERROR) {
        return false;
    }

    /* Read back. glMapBufferRange(READ) then copy row-by-row honoring pitch. */
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, r->ssbo[GL_SSBO_OUT]);
    const uint32_t* src = (const uint32_t*)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, out_sz, GL_MAP_READ_BIT);
    if (!src) {
        return false;
    }
    if (pitch <= 0) {
        pitch = W;
    }
    for (int y = 0; y < H; ++y) {
        std::memcpy(&dst[(size_t)y * (size_t)pitch], &src[(size_t)y * W], (size_t)W * sizeof(uint32_t));
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    return true;
}

#endif /* TMC_GLES_RASTER_IMPL */
