#include "port_ppu.h"
#include "port_debug_menu.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_upscale.h"
#include "port_runtime_config.h"
#include "port_filter.h"
#include "port_gpu_renderer.h" /* PortGpuFilter for the GPU-backend filter cycle */
#include "port_gpu_raster.h"   /* GPU PPU rasterizer (docs/gpu-rasterizer-design.md) */
#include "port_imgui_menu.h"
#include "port_softslots.h"
#include "port_touch_controls.h"
#include "port_widescreen.h"

#ifdef launcher
#include "tmc_launcher.h"
#endif

#include <cpu/mode1.h>
#include <virtuappu.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#if defined(__clang__) || defined(__INTEL_COMPILER)
/* libomp export missing from the NDK's (and stock clang's) omp.h.
 * Runtime equivalent of KMP_BLOCKTIME; see the Port_PPU_Init block. */
extern "C" void kmp_set_blocktime(int msec);
#endif
#endif

/* Manual access to gMain (the engine's Main struct): including main.h
 * would pull in player.h, which uses `this` as a C parameter name and
 * doesn't compile as C++. Treat the symbol as opaque bytes and read the
 * task field at known offset 2 (interruptFlag, sleepStatus, task —
 * include/main.h). C linkage matches the engine's Main gMain. */
extern "C" uint8_t gMain[]; // fix for MSVC (UWP PORT)
/* Phase-timer hooks (defined in port_bios.c, C linkage). */
extern "C" int Port_Profile_Enabled(void);
extern "C" uint64_t gPortProfileRenderNs;
/* Port_Widescreen_* lives in port_linked_stubs.c; include
 * port_widescreen.h for the runtime WIP toggle gate. */

enum class RenderBackend {
    None,
    Renderer,
    Surface,
    Gpu, /* Stage 2: SDL_GPU pipeline owns the swapchain */
};

/* User-cycled presentation modes. F12 advances through these. */
enum class PresentMode {
    NearestRaw = 0, /* upload 240x160 directly, nearest-neighbor stretch  */
    XbrzLinear,     /* xBRZ 4x → 960x640, linear stretch (smooth, default) */
    XbrzNearest,    /* xBRZ 4x → 960x640, nearest stretch (sharp)          */
    LinearRaw,      /* upload 240x160 directly, linear stretch (blurry)    */
    Count
};

static const int kNativeHiResW = 240 * 4;
static const int kNativeHiResH = 160 * 4;

static RenderBackend sBackend = RenderBackend::None;
static SDL_Renderer* sRenderer = nullptr;
static SDL_Texture* sLowResTexture = nullptr; /* 240x160 raw upload */
static int sLowResTextureW = 0;
static int sLowResTextureH = 0;
static SDL_Texture* sHiResTexture = nullptr; /* xBRZ 4x upload */
static int sHiResTextureW = 0;
static int sHiResTextureH = 0;
/* Internal-render-scale streaming texture: re-sized lazily when scale
 * changes (240*S x 160*S). Used when Port_Config_InternalScale() > 1
 * and the user has chosen a non-xBRZ presentation mode.
 *
 * The CPU scratch framebuffer is a fixed BSS pool, not malloc/free'd
 * from the frame path. Linux only backs touched pages, so a 10x worst
 * case is cheap until the user actually selects that scale, and it
 * avoids allocator locks / fragmentation on handheld-class CPUs. */
static SDL_Texture* sScaledTexture = nullptr;
static int sScaledTextureW = 0;
static int sScaledTextureH = 0;
static int sScaledTextureScale = 0;
static constexpr int kMaxInternalScale = 10;
static constexpr size_t kMaxScaledPixels =
    (size_t)MODE1_GBA_WIDTH * (size_t)MODE1_GBA_HEIGHT * (size_t)kMaxInternalScale * (size_t)kMaxInternalScale;
alignas(64) static uint32_t sScaledBufStorage[kMaxScaledPixels];
static int sScaledBufW = 0;
static int sScaledBufH = 0;
static int sScaledBufScale = 0;
/* GPU supersample present: TryGpuRaster renders the whole scene at Sx into
 * sScaledBufStorage (sub-pixel affine, unlike BuildScaledFrame's nearest
 * replicate). Mutually exclusive with the BuildScaledFrame path per frame, so
 * the buffer is shared. Valid only for the frame TryGpuRaster set it. */
static bool sSuperValid = false;
static int sSuperW = 0;
static int sSuperH = 0;
static SDL_Window* sWindow = nullptr;

static int Port_PPU_WindowBaseWidth(void) {
    return (MODE1_GBA_WIDTH > 240 && Port_Config_WidescreenEnabled()) ? MODE1_GBA_WIDTH : 240;
}

#ifdef launcher
static SDL_Window* sBootstrapWindow = nullptr;
#endif

extern "C" SDL_Window* Port_PPU_ActiveWindow(void) {
#ifdef launcher
    return sWindow ? sWindow : sBootstrapWindow;
#else
    return sWindow;
#endif
}

#ifdef launcher
extern "C" void Port_SetBootstrapWindow(SDL_Window* window) {
    sBootstrapWindow = window;
}
#endif
static SDL_Surface* sFrameSurface = nullptr;
static PresentMode sPresentMode = PresentMode::NearestRaw;
static PortFilterType sFilter = PORT_FILTER_NONE;
/* xBRZ scratch is also a fixed pool. These buffers are hot during
 * filter/upscale frames, so 64-byte alignment keeps row starts friendly
 * to cache-line prefetch and SIMD memcpy implementations. */
static constexpr size_t kMaxXbrz2xPixels = (size_t)MODE1_GBA_WIDTH * 2u * (size_t)MODE1_GBA_HEIGHT * 2u;
static constexpr size_t kMaxXbrz4xPixels = (size_t)MODE1_GBA_WIDTH * 4u * (size_t)MODE1_GBA_HEIGHT * 4u;
alignas(64) static uint32_t sUpscale2xBuf[kMaxXbrz2xPixels]; /* 2x intermediate */
alignas(64) static uint32_t sUpscale4xBuf[kMaxXbrz4xPixels]; /* 4x final        */
static size_t sUpscale2xPixels = 0;
static size_t sUpscale4xPixels = 0;

/* Width VirtuaPPU can render safely from scene data this frame. Map-backed
 * gameplay can use the live widescreen width; fixed 240px canvases (title,
 * menus, narrow rooms, overlays) intentionally stay native here and are
 * presented aspect-correct (pillarboxed), never stretched. */
static int Port_PPU_VisibleFrameWidth(void) {
    if (MODE1_GBA_WIDTH == 240) {
        return MODE1_GBA_WIDTH;
    }
    /* True widescreen: publish the live window size so the view width can
     * track the window's aspect (16:9 -> 284, ultrawide -> cap at
     * MODE1_GBA_WIDTH, 3:2/4:3 -> 240). Then a wide present additionally
     * requires gameplay widescreen AND the map BGs actually feeding the
     * frame (ShadowsLive) — overlay screens inside TASK_GAME (prologue
     * storybook, pause menu) swap the BG control regs away from the maps,
     * the shadow match fails, and we fall back to a native 240 frame. */
    if (sWindow != nullptr) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(sWindow, &w, &h);
        Port_Widescreen_SetWindowPixels(w, h);
    }
    if (gMain[2] == 2 /* TASK_GAME */ && Port_Widescreen_IsActive() && Port_Widescreen_ShadowsLive()) {
        return Port_Widescreen_EffectiveViewWidth();
    }
    return 240;
}

/* xBRZ scratch buffers are sized to the frame we are actually presenting:
 * 240x160 when the WIP widescreen option is off/falling back, or
 * MODE1_GBA_WIDTHx160 while true widescreen is active. */
static bool Port_PPU_EnsureXbrzBuffers(int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) {
        sUpscale2xPixels = 0;
        sUpscale4xPixels = 0;
        return false;
    }
    const size_t need2x = (size_t)srcW * 2u * (size_t)srcH * 2u;
    const size_t need4x = (size_t)srcW * 4u * (size_t)srcH * 4u;
    if (need2x > kMaxXbrz2xPixels || need4x > kMaxXbrz4xPixels) {
        sUpscale2xPixels = 0;
        sUpscale4xPixels = 0;
        return false;
    }
    sUpscale2xPixels = need2x;
    sUpscale4xPixels = need4x;
    return true;
}

static SDL_Texture* sTextureScaleModeTexture = nullptr;
static SDL_ScaleMode sTextureScaleMode = SDL_SCALEMODE_NEAREST;
static bool sTextureScaleModeValid = false;

static void Port_PPU_InvalidateTextureScaleMode(SDL_Texture* tex) {
    if (sTextureScaleModeTexture == tex) {
        sTextureScaleModeTexture = nullptr;
        sTextureScaleModeValid = false;
    }
}

static void Port_PPU_SetTextureScaleModeCached(SDL_Texture* tex, SDL_ScaleMode mode) {
    if (tex == nullptr) {
        return;
    }
    if (sTextureScaleModeValid && sTextureScaleModeTexture == tex && sTextureScaleMode == mode) {
        return;
    }
    SDL_SetTextureScaleMode(tex, mode);
    sTextureScaleModeTexture = tex;
    sTextureScaleMode = mode;
    sTextureScaleModeValid = true;
}

static SDL_Texture* Port_PPU_EnsureTexture(SDL_Texture** slot, int* curW, int* curH, int w, int h) {
    if (*slot != nullptr && *curW == w && *curH == h) {
        return *slot;
    }
    if (*slot != nullptr) {
        SDL_DestroyTexture(*slot);
        Port_PPU_InvalidateTextureScaleMode(*slot);
        *slot = nullptr;
    }
    *curW = 0;
    *curH = 0;
    *slot = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (*slot != nullptr) {
        *curW = w;
        *curH = h;
    }
    return *slot;
}

static const uint32_t* Port_PPU_SelectPresentFrame(int* outW, int* outH, int* outPitchBytes) {
    const int visibleW = Port_PPU_VisibleFrameWidth();
    if (outW)
        *outW = visibleW;
    if (outH)
        *outH = MODE1_GBA_HEIGHT;
    if (outPitchBytes) {
        *outPitchBytes = MODE1_GBA_WIDTH * static_cast<int>(sizeof(uint32_t));
    }
    return virtuappu_frame_buffer;
}

static void Port_PPU_LoadConfig(void) {
    const char* method = Port_Config_UpscaleMethod();
    if (std::strcmp(method, "nearest") == 0) {
        sPresentMode = PresentMode::NearestRaw;
    } else if (std::strcmp(method, "linear") == 0) {
        sPresentMode = PresentMode::LinearRaw;
    } else if (std::strcmp(method, "xbrz_nearest") == 0) {
        sPresentMode = PresentMode::XbrzNearest;
    } else {
        sPresentMode = PresentMode::XbrzLinear;
    }
}

static const char* Port_PPU_MethodForMode(PresentMode mode) {
    switch (mode) {
        case PresentMode::NearestRaw:
            return "nearest";
        case PresentMode::LinearRaw:
            return "linear";
        case PresentMode::XbrzNearest:
            return "xbrz_nearest";
        case PresentMode::XbrzLinear:
        default:
            return "xbrz_linear";
    }
}

extern "C" const char* Port_PPU_PresentationModeName(void) {
    static const char* const kNames[] = {
        "nearest",
        "xBRZ smooth",
        "xBRZ sharp",
        "linear",
    };
    return kNames[(int)sPresentMode];
}

// Largest rect with aspect aspW:aspH fitting inside (w, h), centered.
static void Port_PPU_FitAspectRect(int w, int h, int aspW, int aspH, int* outX, int* outY, int* outW, int* outH) {
    int rw;
    int rh;
    if (w * aspH >= h * aspW) {
        rh = h;
        rw = (h * aspW) / aspH;
    } else {
        rw = w;
        rh = (w * aspH) / aspW;
    }
    *outX = (w - rw) / 2;
    *outY = (h - rh) / 2;
    *outW = rw;
    *outH = rh;
}

// Compute both the "stage" rect (the visible area honoring the user's
// configured aspect mode — gets the chosen background fill) and the
// GBA "frame" rect that sits centered inside the stage. Outside the
// stage is always black. For PORT_ASPECT_NATIVE_3_2 the stage spans the
// whole window, so the background fill covers everything around the frame.
static void Port_PPU_ComputeViewportRects(int outW, int outH, int fbW, int fbH, int* stageX, int* stageY, int* stageW,
                                          int* stageH, int* frameX, int* frameY, int* frameW, int* frameH) {
    const int FW = fbW;
    const int FH = fbH;

    int aspW = FW;
    int aspH = FH;
    const PortAspectMode mode = Port_Config_AspectMode();
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:
            aspW = 16;
            aspH = 9;
            break;
        case PORT_ASPECT_ULTRAWIDE_21_9:
            aspW = 21;
            aspH = 9;
            break;
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9:
            aspW = 32;
            aspH = 9;
            break;
        case PORT_ASPECT_NATIVE_3_2:
        default:
            /* "No constraint": the stage spans the whole window. With the
             * historical black fill this is pixel-identical to the old
             * stage==frame behavior; with solid/blurred fills it lets the
             * ambient backdrop cover the entire monitor, so fixed-canvas
             * scenes (title, one-screen rooms) have no dead black bars. */
            aspW = outW;
            aspH = outH;
            break;
    }
    Port_PPU_FitAspectRect(outW, outH, aspW, aspH, stageX, stageY, stageW, stageH);
    // Inside the stage, fit the GBA frame at its native 3:2.
    int fx, fy, fw, fh;
    Port_PPU_FitAspectRect(*stageW, *stageH, FW, FH, &fx, &fy, &fw, &fh);
    *frameX = *stageX + fx;
    *frameY = *stageY + fy;
    *frameW = fw;
    *frameH = fh;
}

static void Port_PPU_QueryOutputSize(int* outW, int* outH) {
    int w = 0;
    int h = 0;
    if (sRenderer) {
        SDL_GetCurrentRenderOutputSize(sRenderer, &w, &h);
    }
    if (w > 0 && h > 0) {
        *outW = w;
        *outH = h;
        return;
    }
    if (sWindow) {
        SDL_GetWindowSize(sWindow, &w, &h);
    }
    if (w > 0 && h > 0) {
        *outW = w;
        *outH = h;
        return;
    }
    *outW = 960;
    *outH = 540;
}

/* Nearest-replicate into a preallocated scratch framebuffer. Horizontal
 * expansion is performed once per source row, then duplicated vertically
 * with memcpy. This keeps the source row hot in L1 and lets libc copy whole
 * cache-line-aligned rows instead of re-running the inner pixel loop S times. */
static void Port_PPU_ReplicateNearest(uint32_t* dstFrame, const uint32_t* srcFrame, int FW, int FH, int srcPitchPixels,
                                      int S) {
    const int dstW = FW * S;
    const size_t rowBytes = (size_t)dstW * sizeof(uint32_t);
    for (int sy = 0; sy < FH; ++sy) {
        const uint32_t* src = &srcFrame[(size_t)sy * (size_t)srcPitchPixels];
        uint32_t* row0 = &dstFrame[(size_t)sy * (size_t)S * (size_t)dstW];
        switch (S) {
            case 2:
                for (int sx = 0; sx < FW; ++sx) {
                    const uint32_t c = src[sx];
                    uint32_t* d = &row0[sx * 2];
                    d[0] = c;
                    d[1] = c;
                }
                break;
            case 3:
                for (int sx = 0; sx < FW; ++sx) {
                    const uint32_t c = src[sx];
                    uint32_t* d = &row0[sx * 3];
                    d[0] = c;
                    d[1] = c;
                    d[2] = c;
                }
                break;
            case 4:
                for (int sx = 0; sx < FW; ++sx) {
                    const uint32_t c = src[sx];
                    uint32_t* d = &row0[sx * 4];
                    d[0] = c;
                    d[1] = c;
                    d[2] = c;
                    d[3] = c;
                }
                break;
            default:
                for (int sx = 0; sx < FW; ++sx) {
                    const uint32_t c = src[sx];
                    uint32_t* d = &row0[(size_t)sx * (size_t)S];
                    for (int dx = 0; dx < S; ++dx) {
                        d[dx] = c;
                    }
                }
                break;
        }
        for (int dy = 1; dy < S; ++dy) {
            std::memcpy(row0 + (size_t)dy * (size_t)dstW, row0, rowBytes);
        }
    }
}

/* Build (or reuse) the fixed scratch buffer at scale S and S*S-replicate
 * the selected presentation frame into it. Source pitch may be wider than
 * visible width when VirtuaPPU is culling a 240-wide viewport inside a
 * fixed-pitch widescreen framebuffer. */
static uint32_t* Port_PPU_BuildScaledFrame(const uint32_t* srcFrame, int FW, int FH, int srcPitchBytes, int S,
                                           int* outW, int* outH) {
    if (S <= 1 || S > kMaxInternalScale || srcFrame == nullptr || FW <= 0 || FH <= 0 ||
        srcPitchBytes < FW * (int)sizeof(uint32_t)) {
        if (outW)
            *outW = 0;
        if (outH)
            *outH = 0;
        return nullptr;
    }
    const int w = FW * S;
    const int h = FH * S;
    const size_t needPixels = (size_t)w * (size_t)h;
    if (needPixels > kMaxScaledPixels) {
        if (outW)
            *outW = 0;
        if (outH)
            *outH = 0;
        return nullptr;
    }

    sScaledBufScale = S;
    sScaledBufW = FW;
    sScaledBufH = FH;
    Port_PPU_ReplicateNearest(sScaledBufStorage, srcFrame, FW, FH, srcPitchBytes / (int)sizeof(uint32_t), S);

    if (outW)
        *outW = w;
    if (outH)
        *outH = h;
    return sScaledBufStorage;
}

static SDL_Texture* Port_PPU_EnsureScaledTexture(int w, int h, int S) {
    if (S <= 1)
        return nullptr;
    SDL_Texture* tex = Port_PPU_EnsureTexture(&sScaledTexture, &sScaledTextureW, &sScaledTextureH, w, h);
    if (tex != nullptr) {
        sScaledTextureScale = S;
    }
    return tex;
}

static void Port_PPU_PresentSurfaceFrame(void) {
    SDL_Surface* windowSurface = SDL_GetWindowSurface(sWindow);
    int x;
    int y;
    int w;
    int h;
    SDL_Rect srcRect;
    SDL_Rect dstRect;

    if (!windowSurface || !sFrameSurface) {
        return;
    }

    const int frameW = Port_PPU_VisibleFrameWidth();
    Port_PPU_FitAspectRect(windowSurface->w, windowSurface->h, frameW, MODE1_GBA_HEIGHT, &x, &y, &w, &h);
    srcRect = { 0, 0, frameW, MODE1_GBA_HEIGHT };
    dstRect = { x, y, w, h };
    SDL_FillSurfaceRect(windowSurface, nullptr, 0);
    SDL_BlitSurfaceScaled(sFrameSurface, &srcRect, windowSurface, &dstRect, SDL_SCALEMODE_NEAREST);
    SDL_UpdateWindowSurface(sWindow);
}

static bool sVSyncEnabled = true;

/* Current display refresh rate (Hz, rounded), 0 when unknown (headless /
 * dummy driver / no window yet). Re-queried every ~2 s so dragging the
 * window to another monitor is picked up; SDL's mode lookup is cheap but
 * this is called once per engine tick. */
extern "C" unsigned Port_PPU_DisplayRefreshRate(void) {
    static unsigned cached = 0;
    static int countdown = 0;
    if (--countdown <= 0) {
        countdown = 120;
        cached = 0;
        SDL_Window* win = sWindow;
#ifdef launcher
        if (win == nullptr)
            win = sBootstrapWindow;
#endif
        if (win != nullptr) {
            SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
            if (disp != 0) {
                const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(disp);
                if (m != nullptr && m->refresh_rate > 0.0f) {
                    cached = (unsigned)std::lround(m->refresh_rate);
                }
            }
        }
    }
    return cached;
}

extern "C" void Port_PPU_SetVSync(bool enabled) {
    if (sVSyncEnabled == enabled) {
        return;
    }
    sVSyncEnabled = enabled;
    switch (sBackend) {
        case RenderBackend::Renderer:
            if (sRenderer != nullptr) {
                SDL_SetRenderVSync(sRenderer, enabled ? 1 : 0);
            }
            break;
        case RenderBackend::Gpu:
            /* SDL_GPU swapchain: present mode is per-window, not per-renderer.
             * Previously this case silently did nothing, so the F8 VSync
             * checkbox (and fast-forward's forced vsync-off) never took
             * effect on GPU builds. */
            Port_GPU_SetVSync(enabled);
            break;
        case RenderBackend::Surface:
            if (sWindow != nullptr) {
                SDL_SetWindowSurfaceVSync(sWindow, enabled ? 1 : 0);
            }
            break;
        case RenderBackend::None:
            break; /* flag recorded; backend init applies it */
    }
}

extern "C" bool Port_PPU_VSyncEnabled(void) {
    return sVSyncEnabled;
}

/* ---- GBA reflective-LCD display transforms (PC port) -----------------------
 *
 * Both run as present-time post-processes on the final composited ABGR8888
 * frame (virtuappu_frame_buffer), after virtuappu_render_frame() and after any
 * shm publish, so they touch only what reaches the screen and cover every BG
 * mode plus the engine's blend/brightness output uniformly. Doing this here
 * rather than inside ViruaPPU keeps the pinned submodule untouched.
 *
 * Colour correction: GBA games were authored for a dim, high-gamma reflective
 * panel and look over-bright/over-saturated when their palettes are shown raw
 * (the engine expands each 5-bit channel with a plain <<3) on an sRGB monitor.
 * We decode each channel through the GBA panel display gamma to linear light
 * and re-encode with the standard sRGB transfer function. Clean-room: the only
 * constants are the GBA display gamma and the published sRGB OETF -- no
 * third-party colour tables. Since the source is 5-bit, a 256-entry 8-bit LUT
 * reproduces a 15-bit table exactly while also correcting blended pixels.
 *
 * Persistence: the GBA LCD had slow pixel response, and some TMC-era effects
 * (dithered / flicker transparency) leaned on it. We keep a per-pixel
 * accumulator and move it toward each new frame by (1 - rho). Opt-in. */
#define PORT_GBA_LCD_GAMMA 4.0 /* "classic" GBA reflective-panel display gamma */

static bool sColorCorrectEnabled = true;
static uint8_t sColorLut[256];
static bool sColorLutReady = false;

static bool sPersistEnabled = false;
static float sPersistRho = 0.35f; /* fraction of the previous frame retained */
static uint32_t sPersistAccum[VIRTUAPPU_FRAME_BUFFER_SIZE];
static bool sPersistAccumValid = false;
/* Decoupled pacing (port_bios.c) can present the same game tick several
 * times. Persistence rho is defined per GBA frame, so the accumulator only
 * folds on the first present of each tick; repeats read it without writing
 * (otherwise 144 Hz presents would over-decay the ghost ~2.4x). */
static bool sPresentFirstOfTick = true;
extern "C" void Port_PPU_SetPresentIsFirstOfTick(bool first) {
    sPresentFirstOfTick = first;
}

static double Port_PPU_SrgbOetf(double linear) {
    if (linear <= 0.0031308) {
        return 12.92 * linear;
    }
    return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

static void Port_PPU_BuildColorLut(void) {
    for (int v = 0; v < 256; ++v) {
        double x = static_cast<double>(v) / 255.0;
        double enc = Port_PPU_SrgbOetf(std::pow(x, PORT_GBA_LCD_GAMMA));
        if (enc < 0.0)
            enc = 0.0;
        if (enc > 1.0)
            enc = 1.0;
        sColorLut[v] = static_cast<uint8_t>(enc * 255.0 + 0.5);
    }
    sColorLutReady = true;
}

extern "C" void Port_PPU_SetColorCorrection(bool enabled) {
    sColorCorrectEnabled = enabled;
}

extern "C" bool Port_PPU_ColorCorrectionEnabled(void) {
    return sColorCorrectEnabled;
}

extern "C" void Port_PPU_SetPersistence(bool enabled, float rho) {
    sPersistEnabled = enabled;
    if (rho >= 0.0f && rho < 1.0f) {
        sPersistRho = rho;
    }
    if (!enabled) {
        sPersistAccumValid = false; /* drop history so re-enabling starts clean */
    }
}

/* Colour-correct `count` ABGR8888 pixels in place via the per-channel LUT.
 * No-op when disabled. Shared by the native post-process and the GPU
 * supersample buffer (per-pixel LUT commutes with S*S replication, so applying
 * it to the S*S buffer matches applying it to the native frame then scaling). */
static void Port_PPU_ColorCorrectBuffer(uint32_t* buf, int count) {
    if (!sColorCorrectEnabled || !sColorLutReady) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        uint32_t p = buf[i];
        uint32_t r = sColorLut[p & 0xFFu];
        uint32_t g = sColorLut[(p >> 8) & 0xFFu];
        uint32_t b = sColorLut[(p >> 16) & 0xFFu];
        buf[i] = (p & 0xFF000000u) | (b << 16) | (g << 8) | r;
    }
}

/* Apply colour correction then persistence in place over the first
 * `pixelCount` ABGR8888 pixels of virtuappu_frame_buffer. Called on the main
 * thread after the OpenMP scanline render has joined, so no locking needed. */
static void Port_PPU_ApplyDisplayPostProcess(int pixelCount) {
    if (pixelCount <= 0) {
        return;
    }
    if (pixelCount > VIRTUAPPU_FRAME_BUFFER_SIZE) {
        pixelCount = VIRTUAPPU_FRAME_BUFFER_SIZE;
    }

    Port_PPU_ColorCorrectBuffer(virtuappu_frame_buffer, pixelCount);

    if (sPersistEnabled) {
        const float add = 1.0f - sPersistRho;
        if (!sPersistAccumValid) {
            std::memcpy(sPersistAccum, virtuappu_frame_buffer, sizeof(uint32_t) * static_cast<size_t>(pixelCount));
            sPersistAccumValid = true;
        } else {
            for (int i = 0; i < pixelCount; ++i) {
                uint32_t cur = virtuappu_frame_buffer[i];
                uint32_t acc = sPersistAccum[i];
                uint32_t out = cur & 0xFF000000u; /* keep current alpha */
                for (int s = 0; s <= 16; s += 8) {
                    float c = static_cast<float>((cur >> s) & 0xFFu);
                    float a = static_cast<float>((acc >> s) & 0xFFu);
                    float val = a + (c - a) * add;
                    if (val < 0.0f)
                        val = 0.0f;
                    if (val > 255.0f)
                        val = 255.0f;
                    out |= static_cast<uint32_t>(val + 0.5f) << s;
                }
                if (sPresentFirstOfTick)
                    sPersistAccum[i] = out;
                virtuappu_frame_buffer[i] = out;
            }
        }
    }
}
#ifdef __ANDROID__
/* Count the "performance" CPU cores at runtime so the scanline render pool can
 * fill the fast cluster and stop — the measured Android policy (see the cap
 * below). On heterogeneous ARM (big.LITTLE / tri-cluster) the slow cores are
 * in-order (A53/A55); spilling scanlines onto them makes the OpenMP barrier
 * wait on the slowest worker, a net regression. So: read each core's
 * cpuinfo_max_freq, find the slowest cluster's freq, and count every core
 * faster than it (the big — and, on tri-cluster, prime+gold — cores). A
 * uniform SoC (all cores same freq) returns 0, signalling "no distinct fast
 * cluster" so the caller uses its uniform-core policy. Best-effort: any /sys
 * read failure returns 0 (caller falls back). */
static int Port_CountPerfCores(void) {
    long freq[64];
    int ncpu = 0;
    for (int c = 0; c < 64; ++c) {
        char path[96];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            break; /* no more cores enumerated */
        }
        long v = 0;
        if (std::fscanf(f, "%ld", &v) != 1) {
            v = 0;
        }
        std::fclose(f);
        freq[ncpu++] = v;
    }
    if (ncpu < 2) {
        return 0; /* single core or unreadable -> let caller decide */
    }
    long lo = freq[0];
    long hi = freq[0];
    for (int i = 1; i < ncpu; ++i) {
        if (freq[i] < lo)
            lo = freq[i];
        if (freq[i] > hi)
            hi = freq[i];
    }
    if (lo <= 0 || hi == lo) {
        return 0; /* uniform (or unreadable) -> not heterogeneous */
    }
    /* Count cores above the slowest cluster's frequency: the fast core(s). */
    int perf = 0;
    for (int i = 0; i < ncpu; ++i) {
        if (freq[i] > lo) {
            ++perf;
        }
    }
    return perf;
}
#endif

extern "C" void Port_PPU_Init(SDL_Window* window) {
    sWindow = window;
#ifdef launcher
    sBootstrapWindow = nullptr;
#endif
    Port_PPU_LoadConfig();

    /* Cap the OpenMP scanline-render pool. virtuappu_mode1_render_frame
     * parallelizes 160 scanlines with a barrier; using ALL physical cores
     * oversubscribes against the main + audio threads and the barrier thrashes.
     * Reserve a core (ncores-1) and cap at the workload's scaling knee. The
     * parallelism dimension is a FIXED 160 scanlines, so the knee is set by
     * lines-per-thread, not core count. Measured on a 22-core Ultra 7 155H
     * (ppu_bench): a heavy widescreen (384px) frame scales 0.85 ms (1t) ->
     * 0.33 ms (6t) -> 0.23 ms (8t) -> 0.17 ms (12t) -> 0.16 ms (16t) — the
     * per-thread marginal gain collapses from 51 us/thread (6->8) to 3 us/thread
     * (12->16), so 12 captures ~all of the win. Past ~16 it REGRESSES (per-thread
     * work < 10 lines; fork/join + near-full subscription dominate). Was
     * hard-capped at 6 (a stale 8-core-era value that left heavy scenes ~2x
     * slower than they scale to on many-core hosts). Desktop is normally
     * VSync-bound (render ~0.4 ms of a 16.7 ms frame), so this only matters for
     * the CPU-bound case: weak many-core HW, uncapped fps, or heavy geometry.
     * The render pragma has no num_threads clause, so this nthreads default
     * governs it. TMC_RENDER_THREADS forces a value; an explicit OMP_NUM_THREADS
     * is left untouched (power-user override). */
#ifdef _OPENMP
    {
        /* Make the scanline workers SLEEP between frames instead of
         * spinning. libomp's default KMP_BLOCKTIME is 200 ms and the
         * render's parallel region recurs every ~16.7 ms, so with the
         * default policy the 5 worker threads never block — measured
         * on a Galaxy Tab A7 (simpleperf): >52% of ALL app cycles in
         * kmp_flag_64::wait spin. On passively-cooled devices that is
         * heat -> thermal throttle -> jank; on everything it is wasted
         * power. The wake-up cost on the next frame is microseconds
         * against a 16 ms budget.
         *
         * Env vars (OMP_WAIT_POLICY/KMP_BLOCKTIME) are unreliable here:
         * libomp snapshots them at runtime init, which static-lib
         * constructors can trigger before this code runs. The kmp API
         * takes effect regardless of timing, so prefer it and keep the
         * env var only for libgomp (GCC builds), where it IS read
         * lazily at first parallel region. User overrides win. */
        if (!getenv("OMP_WAIT_POLICY")) {
            SDL_setenv_unsafe("OMP_WAIT_POLICY", "passive", 1); /* libgomp */
        }
#if defined(__clang__) || defined(__INTEL_COMPILER)
        if (!getenv("KMP_BLOCKTIME")) {
            kmp_set_blocktime(0); /* declared at file scope above */
        }
#endif
    }
    {
        int n = -1;
        const char* force = getenv("TMC_RENDER_THREADS");
        char marker_buf[16];
        if (!force || !*force) {
            /* Android has no shell env; a 'render_threads' marker file in the
             * app data dir (CWD) holding a decimal count forces the value, same
             * convention as the 'profile'/'verbose' markers. Lets thread count
             * be A/B-tuned in-game on a device:
             *   adb shell "echo 4 > .../files/render_threads"  (rm to restore) */
            FILE* mf = std::fopen("render_threads", "rb");
            if (mf) {
                size_t got = std::fread(marker_buf, 1, sizeof(marker_buf) - 1, mf);
                marker_buf[got] = '\0';
                std::fclose(mf);
                if (got > 0 && (marker_buf[0] >= '0' && marker_buf[0] <= '9')) {
                    force = marker_buf;
                }
            }
        }
        if (force && *force) {
            n = atoi(force);
        } else if (getenv("OMP_NUM_THREADS") == nullptr) {
            n = omp_get_num_procs() - 1;
#ifdef __ANDROID__
            /* Fill the fast CPU cluster, then stop. On heterogeneous ARM the
             * slow cores are in-order (A53/A55); spilling scanline work onto
             * them makes the OpenMP barrier wait on the slowest worker (a net
             * regression), and near-full subscription starves main/audio. So
             * scale the pool to the detected performance-core count instead of a
             * fixed number, so it tracks the actual SoC (dual/quad/hexa big
             * cluster, or prime+gold on tri-cluster) rather than assuming 4+4.
             * Measured in-game (forge, CPU raster), one thread per fast core:
             *   Galaxy Tab A7 (4x A73 @2.0 + 4x A53): 3t=4.50 ms, 4t=3.13 ms
             *     (-30%), 5t=4.18, 6t=4.14, 8t=4.71 -> 4 (=perf cores) optimal;
             *     5+ spill onto the A53s and regress.
             *   Moto G4 (8x A53, weak higher-clocked big cluster): 4 perf cores,
             *     3t=3.89 ms vs 4t=3.94 ms -> 4 neutral, not a regression.
             * A uniform SoC (no distinct fast cluster) returns 0 -> fall back to
             * ncores-1. Everything is then capped at the 160-scanline knee (12)
             * like desktop. render_threads marker / TMC_RENDER_THREADS override. */
            {
                int perf = Port_CountPerfCores();
                if (perf >= 1) {
                    n = perf; /* fill exactly the fast cluster */
                }
                std::fprintf(stderr, "[render] perf-core detection: %d fast core(s) of %d\n", perf,
                             omp_get_num_procs());
                /* perf==0: heterogeneity undetectable -> keep ncores-1 from above */
            }
            if (n > 12)
                n = 12;
#else
            if (n > 12)
                n = 12;
#endif
        }
        if (n >= 1) {
            omp_set_num_threads(n);
            std::fprintf(stderr, "[render] OpenMP scanline threads = %d (of %d cores)\n", n, omp_get_num_procs());
        }
    }
#endif

    /* Stage 2: if the GPU renderer is compiled in and the device
     * initialised successfully (Port_GPU_Init was called before us in
     * port_main.c), tear down any pre-existing SDL_Renderer on this
     * window and hand it to the SDL_GPU pipeline. Only one device can
     * own a window's swapchain, so the SDL_Renderer fallback must give
     * up the window first. On any GPU-side failure we keep the
     * SDL_Renderer path — no behavioural change vs. pre-Stage-2 builds.
     *
     * Bootstrap-renderer reuse note: bootstrap may have created an
     * SDL_Renderer for the LOADING splash. Destroying it before the
     * GPU claim is the safe order — without that, SDL_ClaimWindow
     * returns VK_ERROR_SURFACE_LOST_KHR. If GPU init fails the
     * fallback path re-creates the renderer below. */
    (void)Port_GPU_Init; /* called earlier in port_main.c */

    if (SDL_Renderer* existing = SDL_GetRenderer(window)) {
        /* Try GPU first; if it claims, we drop the renderer. Otherwise
         * fall through to the normal SDL_Renderer reuse path. */
        SDL_DestroyRenderer(existing);
    }
    /* Honour the user's renderer preference from the F8 menu. AUTO
     * (default) tries GPU and falls back to SDL_Renderer on failure;
     * SOFTWARE skips the GPU claim entirely; GPU forces the claim
     * and refuses to fall back (so a missing CRT shader pipeline
     * surfaces loudly instead of silently degrading to the SW path). */
    {
        const PortRenderBackend pref = Port_Config_RenderBackend();
        const bool tryGpu = (pref != PORT_RENDER_BACKEND_SOFTWARE);
        const bool forceGpu = (pref == PORT_RENDER_BACKEND_GPU);
        if (tryGpu && Port_GPU_ClaimWindow(window, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT)) {
            sBackend = RenderBackend::Gpu;
            std::fprintf(stderr, "PPU initialized with SDL_GPU backend (pref=%s).\n",
                         Port_Config_RenderBackendName(pref));
            /* GPU path doesn't need any of the SDL_Renderer / SDL_Texture
             * state below — skip straight to the ViruaPPU memory bind. */
            goto bind_virtuappu_memory;
        }
        if (forceGpu) {
            std::fprintf(stderr, "PPU: render_backend=gpu requested but Port_GPU_ClaimWindow "
                                 "failed; falling back to SDL_Renderer anyway.\n");
        }
    }

    /* GPU not available (build flag off, init failed, or claim failed):
     * fall back to SDL_Renderer. SDL_GetRenderer is NULL here because
     * we destroyed any existing one above; create one fresh. */
    sRenderer = SDL_CreateRenderer(window, nullptr);
    if (sRenderer) {
        SDL_SetRenderTarget(sRenderer, nullptr);
        SDL_SetRenderClipRect(sRenderer, nullptr);
    }
    if (!sRenderer) {
        printf("Port_PPU_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
    } else {
        const char* rname = SDL_GetRendererName(sRenderer);
        std::fprintf(stderr, "PPU: SDL_Renderer driver = %s\n", rname ? rname : "?");
        if (!SDL_SetRenderVSync(sRenderer, 1)) {
            printf("Port_PPU_Init: SDL_SetRenderVSync failed: %s\n", SDL_GetError());
        }
        sLowResTexture = Port_PPU_EnsureTexture(&sLowResTexture, &sLowResTextureW, &sLowResTextureH, MODE1_GBA_WIDTH,
                                                MODE1_GBA_HEIGHT);
        sHiResTexture =
            Port_PPU_EnsureTexture(&sHiResTexture, &sHiResTextureW, &sHiResTextureH, kNativeHiResW, kNativeHiResH);
        if (!sLowResTexture || !sHiResTexture) {
            printf("Port_PPU_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
            SDL_DestroyRenderer(sRenderer);
            sRenderer = nullptr;
        } else {
            Port_PPU_EnsureXbrzBuffers(240, 160);
            sBackend = RenderBackend::Renderer;
        }
    }

bind_virtuappu_memory: {
    VirtuaPPUMode1GbaMemory memory = {
        gIoMem, gVram, gBgPltt, gObjPltt, gOamMem,
    };
    virtuappu_mode1_bind_gba_memory(&memory);
}

    virtuappu_registers.frame_width = Port_PPU_VisibleFrameWidth();
    virtuappu_registers.frame_pitch = MODE1_GBA_WIDTH;
    virtuappu_registers.mode = 1;

    /* GBA-LCD display transforms. Source of truth is the persisted config
     * (F8 Display menu); the TMC_* env vars override it for headless A/B.
     * Colour correction defaults on, LCD persistence off. The LUT is built
     * here on the main thread before the first frame, so the render's OpenMP
     * scanline workers never see a half-filled table. */
    {
        bool cc = Port_Config_GetColorCorrection();
        const char* ccEnv = getenv("TMC_COLOR_CORRECTION");
        if (ccEnv && ccEnv[0]) {
            cc = (ccEnv[0] != '0');
        }
        sColorCorrectEnabled = cc;
        Port_PPU_BuildColorLut();

        bool lcd = Port_Config_GetLcdPersistence();
        float rho = Port_Config_GetLcdPersistenceRho();
        const char* lpEnv = getenv("TMC_LCD_PERSISTENCE");
        if (lpEnv && lpEnv[0]) {
            lcd = (lpEnv[0] != '0');
        }
        const char* rvEnv = getenv("TMC_LCD_PERSISTENCE_RHO");
        if (rvEnv && *rvEnv) {
            float v = static_cast<float>(atof(rvEnv));
            if (v >= 0.0f && v < 1.0f) {
                rho = v;
            }
        }
        Port_PPU_SetPersistence(lcd, rho);
    }

    if (sBackend == RenderBackend::None) {
        sFrameSurface =
            SDL_CreateSurfaceFrom(MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT, SDL_PIXELFORMAT_ABGR8888, virtuappu_frame_buffer,
                                  MODE1_GBA_WIDTH * static_cast<int>(sizeof(uint32_t)));
        if (!sFrameSurface) {
            printf("Port_PPU_Init: SDL_CreateSurfaceFrom failed: %s\n", SDL_GetError());
            return;
        }

        if (!SDL_SetWindowSurfaceVSync(window, 1)) {
            printf("Port_PPU_Init: SDL_SetWindowSurfaceVSync failed: %s\n", SDL_GetError());
        }

        sBackend = RenderBackend::Surface;
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
        SDL_SyncWindow(window);
        Port_PPU_PresentSurfaceFrame();
        printf("PPU initialized with SDL window surface fallback.\n");
    } else if (sBackend == RenderBackend::Renderer) {
        printf("PPU initialized with SDL renderer backend.\n");
    }
    /* GPU backend already logged "PPU initialized with SDL_GPU backend"
     * inside the early Port_GPU_ClaimWindow branch above. */

    /* Hand the renderer to the ImGui menu layer so it can draw on top
     * of the rasterized GBA frame. Failure to init ImGui is non-fatal —
     * the legacy SDL-text menu still works.
     *
     * Stage 2 GPU build: Port_ImGui_Init detects sBackend internally
     * and uses the SDL_GPU ImGui backend (ImGui_ImplSDLGPU3_*) when the
     * GPU pipeline owns the window. Renderer can be NULL in that case. */
    if (sBackend == RenderBackend::Renderer) {
        Port_ImGui_Init(window, sRenderer);
    } else if (sBackend == RenderBackend::Gpu) {
        Port_ImGui_Init(window, nullptr);
    }
}

/* ---------------------------------------------------------------------------
 * GPU PPU rasterizer bridge (docs/gpu-rasterizer-design.md).
 *
 * When the SDL_GPU presentation path owns the window, the frame can be
 * rasterized on the GPU instead of the CPU. The CPU rasterizer stays the
 * golden reference and the automatic fallback: any failure here (device
 * unavailable, pipeline build, unsupported feature, or the obj-clip swamp-sink
 * which the shader doesn't implement) returns false and the caller runs
 * virtuappu_render_frame as usual. Compiled out entirely without the GPU path.
 * ------------------------------------------------------------------------- */
#ifdef TMC_GPU_RENDERER
/* swamp-sink obj-clip flag (defined in mode1.c; also externed in port_draw.c). */
extern "C" int virtuappu_mode1_obj_clip_enable;
/* Embedded SPIR-V for the PPU rasterizer. The utils.bin2c rule emits a raw
 * byte-list header (<file>.spv.h) with the include path added automatically;
 * wrap each in a static array so sizeof gives the length (same idiom as
 * port_gpu_renderer.cpp). */
static const unsigned char kPpuRasterVertSpv[] = {
#include "ppu_raster.vert.spv.h"
};
static const unsigned char kPpuRasterFragSpv[] = {
#include "ppu_raster.frag.spv.h"
};
/* Optional committed MSL (generated by port/shaders/build.sh via spirv-cross)
 * for the SDL_GPU Metal backend. When absent, Apple builds fall back to the CPU
 * rasterizer (the SPIR-V blob won't load on an MSL device). __has_include keeps
 * the build clean whether or not the .inl has been regenerated + committed. */
#if defined(__has_include)
#if __has_include("ppu_raster_msl.inl")
#include "ppu_raster_msl.inl"
#define TMC_HAVE_PPU_RASTER_MSL 1
#endif
#endif

/* GLES compute rasterizer (port_gpu_raster_gl.*): the fallback GPU backend for
 * devices without Vulkan (Adreno 4xx etc.). The shared core GLSL is embedded as
 * a NUL-terminated blob (utils.bin2c). No-op stub on Windows/macOS. */
#include "port_gpu_raster_gl.h"
static const char kPpuCoreGlsl[] = {
#include "ppu_core.glsl.h"
    , 0
};

static PortGpuRaster* sGpuRaster = nullptr;
static bool sGpuRasterTried = false;
static bool sGpuRasterUnavailable = false;
static PortGpuRasterGl* sGlesRaster = nullptr;
static bool sGlesRasterTried = false;
static bool sGlesRasterUnavailable = false;

/* Per-frame prepare buffers (the CPU sequential pass output the GPU consumes). */
static uint8_t sRasterIoPerLine[MODE1_GBA_HEIGHT * MODE1_IO_MEM_SIZE];
static uint16_t sRasterDispcntPerLine[MODE1_GBA_HEIGHT];
static int32_t sRasterAffRefX[MODE1_GBA_HEIGHT];
static int32_t sRasterAffRefY[MODE1_GBA_HEIGHT];
/* Concatenated 4-BG widescreen shadow tilemap (built per frame when active). */
static uint16_t sRasterWsShadow[MODE1_GBA_BG_COUNT * MODE1_WS_SHADOW_ROWS * MODE1_WS_SHADOW_COLS];

static bool Port_PPU_RasterEnsure(void) {
    if (sGpuRasterUnavailable) {
        return false;
    }
    if (sGpuRaster) {
        return true;
    }
    if (sGpuRasterTried) {
        return false;
    }
    sGpuRasterTried = true;
    if (!Port_GPU_IsActive()) {
        sGpuRasterUnavailable = true;
        return false;
    }
    SDL_GPUDevice* dev = Port_GPU_GetDevice();
    if (!dev) {
        sGpuRasterUnavailable = true;
        return false;
    }

    /* Pick the shader blob + entrypoint matching the device's shader format:
     * SPIR-V on Vulkan (Linux/Windows/Android); MSL on Metal (Apple). */
    SDL_GPUShaderFormat fmt = Port_GPU_GetShaderFormat();
    const void* vert = kPpuRasterVertSpv;
    size_t vert_len = sizeof(kPpuRasterVertSpv);
    const void* frag = kPpuRasterFragSpv;
    size_t frag_len = sizeof(kPpuRasterFragSpv);
    const char* entry = "main";
    if (fmt == SDL_GPU_SHADERFORMAT_MSL) {
#ifdef TMC_HAVE_PPU_RASTER_MSL
        vert = kPpuRasterVertMsl;
        vert_len = sizeof(kPpuRasterVertMsl);
        frag = kPpuRasterFragMsl;
        frag_len = sizeof(kPpuRasterFragMsl);
        entry = "main0"; /* SDL_GPU MSL entry-point convention */
#else
        /* Metal device but no committed MSL: use the CPU rasterizer. Regenerate
         * port/shaders/build/ppu_raster_msl.inl via port/shaders/build.sh. */
        std::fprintf(stderr, "[gpuraster] Metal backend without committed MSL; using CPU rasterizer.\n");
        sGpuRasterUnavailable = true;
        return false;
#endif
    } else if (fmt != SDL_GPU_SHADERFORMAT_SPIRV) {
        sGpuRasterUnavailable = true;
        return false;
    }

    sGpuRaster = Port_GpuRaster_Create(dev, fmt, entry, vert, vert_len, frag, frag_len);
    if (!sGpuRaster) {
        std::fprintf(stderr, "[gpuraster] create failed; using CPU rasterizer.\n");
        sGpuRasterUnavailable = true;
        return false;
    }
    std::fprintf(stderr, "[gpuraster] GPU PPU rasterizer active.\n");
    return true;
}

/* Ensure the GLES compute rasterizer (fallback backend for no-Vulkan devices).
 * Tried only when the Vulkan/SDL_GPU raster is unavailable. */
static bool Port_PPU_GlesEnsure(void) {
    if (sGlesRasterUnavailable) {
        return false;
    }
    if (sGlesRaster) {
        return true;
    }
    if (sGlesRasterTried) {
        return false;
    }
    sGlesRasterTried = true;
    /* sizeof-1: the embedded core blob is NUL-terminated (see kPpuCoreGlsl). */
    sGlesRaster = Port_GpuRasterGl_Create(kPpuCoreGlsl, (int)(sizeof(kPpuCoreGlsl) - 1));
    if (!sGlesRaster) {
        sGlesRasterUnavailable = true;
        return false;
    }
    return true;
}

/* Rasterize the current frame on the GPU into virtuappu_frame_buffer. Returns
 * false so the caller runs the CPU rasterizer instead. */
static bool Port_PPU_TryGpuRaster(void) {
    if (!Port_Config_GetGpuRaster()) {
        return false;
    }
    if (virtuappu_mode1_obj_clip_enable) {
        return false; /* swamp-sink obj-clip not in the shader */
    }
    static int perfcap = -1;
    if (perfcap < 0) {
        const char* e = getenv("TMC_PERFCAP");
        perfcap = (e && *e && e[0] != '0') ? 1 : 0;
    }
    /* Scale-aware default. The GPU rasterizer only pays off when it produces
     * the supersampled present buffer below (internal scale > 1, a raw present
     * mode, no LCD-persistence). At scale 1 it rasterizes the native frame and
     * reads it back — measured ~2x SLOWER than the multi-threaded CPU
     * rasterizer on an Adreno 610 (Galaxy Tab A7: render ~8 ms vs ~4 ms, 58 vs
     * 60 fps) with byte-identical output, the same small-frame readback-overhead
     * reason mGBA defaults to software. So outside a perfcap capture (which must
     * keep the GPU path live for its golden-hash parity), fall back to the CPU
     * rasterizer whenever the supersample path below wouldn't engage — CPU wins
     * there on every device measured (G4 no-Vulkan, Tab A7 Vulkan @ scale 1). */
    if (!perfcap) {
        const int scaleAware = (int)Port_Config_InternalScale();
        const bool supersample = (scaleAware > 1 && scaleAware <= kMaxInternalScale && !sPersistEnabled &&
                                  (sPresentMode == PresentMode::NearestRaw || sPresentMode == PresentMode::LinearRaw));
        if (!supersample) {
            return false;
        }
    }
    /* Prefer the Vulkan/SDL_GPU raster (a real win where present). The GLES
     * compute backend is OPT-IN only: on the archetypal no-Vulkan device
     * (Adreno 405 / Moto G4) it measured ~5x slower than the 3-thread CPU
     * rasterizer, so it must never auto-enable — CPU is the default fallback. */
    bool useVk = Port_PPU_RasterEnsure();
    bool useGles = false;
    if (!useVk) {
        if (!Port_Config_GetGpuRasterGles()) {
            return false; /* GLES not opted in -> CPU rasterizer */
        }
        useGles = Port_PPU_GlesEnsure();
        if (!useGles) {
            return false;
        }
    }

    uint16_t frame_dispcnt = 0;
    virtuappu_mode1_prepare_frame(&virtuappu_registers, sRasterIoPerLine, sRasterDispcntPerLine, sRasterAffRefX,
                                  sRasterAffRefY, &frame_dispcnt);

    PortGpuRasterFrame f;
    std::memset(&f, 0, sizeof(f));
    f.frame_width = virtuappu_mode1_frame_width();
    f.frame_height = MODE1_GBA_HEIGHT;
    f.frame_pitch = virtuappu_mode1_frame_pitch();
    f.mode = virtuappu_registers.mode;
    f.affine = (virtuappu_registers.mode == 2);
    f.frame_dispcnt = frame_dispcnt;

    VirtuaPPUMode1GbaMemory mem;
    virtuappu_mode1_get_bound_gba_memory(&mem);
    f.vram = mem.vram;
    f.bg_palette = mem.bg_palette;
    f.obj_palette = mem.obj_palette;
    f.oam = mem.oam_mem;
    f.io_per_line = sRasterIoPerLine;
    /* Uniform IO (only row 0 valid) when there's no per-line HDMA callback —
     * prepare_frame snapshots just row 0 then, and the shader reads row 0. */
    f.io_uniform = (virtuappu_mode1_pre_line_callback == nullptr);
    f.dispcnt_per_line = sRasterDispcntPerLine;
    f.affine_ref_x = f.affine ? sRasterAffRefX : nullptr;
    f.affine_ref_y = f.affine ? sRasterAffRefY : nullptr;

    /* Widescreen Option A globals -> frame (inert at native 240). */
    f.ws_bg_clip_x = MODE1_GBA_BG_CLIP_X;
    f.ws_cols = MODE1_WS_SHADOW_COLS;
    f.ws_hud_right_native_x = MODE1_WS_HUD_RIGHT_NATIVE_X;
    f.ws_hud_right_anchor = virtuappu_mode1_ws_hud_right_anchor;
    f.ws_msg_shift = virtuappu_mode1_ws_msg_shift;
    f.ws_msg_x0 = virtuappu_mode1_ws_msg_x0;
    f.ws_msg_x1 = virtuappu_mode1_ws_msg_x1;
    f.ws_msg_y0 = virtuappu_mode1_ws_msg_y0;
    f.ws_msg_y1 = virtuappu_mode1_ws_msg_y1;
    bool any_shadow = false;
    for (int b = 0; b < MODE1_GBA_BG_COUNT; ++b) {
        if (virtuappu_mode1_ws_shadow[b] != nullptr) {
            any_shadow = true;
        }
    }
    if (any_shadow) {
        const int cols = MODE1_WS_SHADOW_COLS;
        std::memset(sRasterWsShadow, 0, sizeof(sRasterWsShadow));
        for (int b = 0; b < MODE1_GBA_BG_COUNT; ++b) {
            f.ws_shadow_base_tile[b] =
                (virtuappu_mode1_ws_shadow[b] != nullptr) ? virtuappu_mode1_ws_shadow_base_tile[b] : -1;
            if (virtuappu_mode1_ws_shadow[b] != nullptr) {
                std::memcpy(&sRasterWsShadow[(size_t)b * MODE1_WS_SHADOW_ROWS * cols], virtuappu_mode1_ws_shadow[b],
                            (size_t)MODE1_WS_SHADOW_ROWS * cols * sizeof(uint16_t));
            }
        }
        f.ws_shadow = sRasterWsShadow;
        f.ws_shadow_halfwords = MODE1_GBA_BG_COUNT * MODE1_WS_SHADOW_ROWS * cols;
    } else {
        for (int b = 0; b < MODE1_GBA_BG_COUNT; ++b) {
            f.ws_shadow_base_tile[b] = -1;
        }
        f.ws_shadow = nullptr;
        f.ws_shadow_halfwords = 0;
    }

    /* Render on the chosen backend into virtuappu_frame_buffer so the present
     * path (filters/shm/upscale/screenshot) is unchanged.
     *
     * DEFERRED READBACK: in normal play the Vulkan path submits this frame and
     * returns the PREVIOUS frame's pixels via a non-blocking fence poll, so the
     * CPU never stalls waiting on the GPU (the per-frame sync point that
     * otherwise dominates — see docs/gpu-rasterizer-parity-notes.md). Costs 1
     * frame of latency. Under perfcap we MUST hand back the current frame (the
     * byte-exact golden hash), so use the synchronous merged path there. */
    const int pitch = virtuappu_mode1_frame_pitch();
    if (useVk) {
        if (perfcap) {
            if (!Port_GpuRaster_RenderReadback(sGpuRaster, &f, virtuappu_frame_buffer, pitch)) {
                return false;
            }
        } else {
            /* Deferred: false only during warm-up (no prior frame ready yet) —
             * then let the CPU render this one frame. */
            if (!Port_GpuRaster_RenderReadbackDeferred(sGpuRaster, &f, virtuappu_frame_buffer, pitch)) {
                return false;
            }
        }
        /* GPU supersample present (desktop quality): render the whole scene at
         * Sx with sub-pixel affine into the shared scaled buffer, colour-correct
         * it, and flag it for the present path — sharper rotated/scaled (affine)
         * BGs than BuildScaledFrame's nearest replicate. Gated: no persistence
         * (native-sized accumulator), no perfcap (needs the native golden hash),
         * and only the raw present modes (xBRZ owns its own 4x upscaler). */
        int S = (int)Port_Config_InternalScale();
        if (S > 1 && S <= kMaxInternalScale && !perfcap && !sPersistEnabled &&
            (sPresentMode == PresentMode::NearestRaw || sPresentMode == PresentMode::LinearRaw)) {
            const int sw = f.frame_width * S, sh = f.frame_height * S;
            if ((size_t)sw * (size_t)sh <= kMaxScaledPixels) {
                PortGpuRasterFrame sf = f;
                sf.scale = S;
                if (Port_GpuRaster_RenderReadback(sGpuRaster, &sf, sScaledBufStorage, sw)) {
                    Port_PPU_ColorCorrectBuffer(sScaledBufStorage, sw * sh);
                    sSuperW = sw;
                    sSuperH = sh;
                    sSuperValid = true;
                }
            }
        }
    } else { /* useGles */
        if (!Port_GpuRasterGl_RenderReadback(sGlesRaster, &f, virtuappu_frame_buffer, pitch)) {
            return false;
        }
    }
    return true;
}
#endif /* TMC_GPU_RENDERER */

extern "C" void Port_PPU_PresentFrame(void) {
    uint16_t dispcnt;
    uint8_t gbaMode;

    if (sBackend == RenderBackend::None) {
        return;
    }
#ifdef TMC_GPU_RENDERER
    sSuperValid = false; /* set true only if TryGpuRaster renders the Sx buffer this frame */
#endif

    virtuappu_registers.frame_width = Port_PPU_VisibleFrameWidth();
    virtuappu_registers.frame_pitch = MODE1_GBA_WIDTH;

    dispcnt = (uint16_t)(gIoMem[0x00] | (gIoMem[0x01] << 8));
    gbaMode = (uint8_t)(dispcnt & 0x07);

    /* GBA->VPPU render-mode routing. The vendored PPU has exactly two render
     * paths: mode 1 = tiled/text (mode1.c), mode 2 = affine (mode2.c).
     *   GBA mode 0          -> VPPU 1  (4 text BGs + OBJ)
     *   GBA mode 1 (and 2)  -> VPPU 2  (BG0/BG1 text + BG2 affine + OBJ)
     * Routing GBA mode 1 to VPPU 1 instead would read BG2 with text-BG
     * indexing and the title-screen affine sword renders as garbage tiles.
     * (Originally fixed in ad9b4d94, regressed in matheo merge dec390c2.)
     * Guarded by the `title` scene in the PPU parity corpus, which is GBA
     * mode 1 -> VPPU 2 (tools/ppu_corpus.txt). */
    switch (gbaMode) {
        case 0:
            virtuappu_registers.mode = 1;
            break;
        case 1:
        case 2:
            virtuappu_registers.mode = 2;
            break;
        default:
            virtuappu_registers.mode = 1;
            break;
    }

    /* HBlank-DMA simulation: only enable the scanline callback while a
     * channel is active. Affine BG rendering treats BG2X/BG2Y differently
     * when HDMA has already supplied per-line reference points. */
    virtuappu_mode1_pre_line_callback = port_hdma_has_active_channels() ? port_hdma_step_line : nullptr;

    /* Affine reference write strobes: a per-line HDMA write to BG2X/BG2Y
     * must reload the internal latch even when it writes the SAME value
     * (constant-value HDMA pins the layer on hardware; value-diff detection
     * inside the PPU can't see idempotent writes). */
    virtuappu_mode1_bg2x_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x28, gIoMem + 0x2C) != 0;
    virtuappu_mode1_bg2y_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x2C, gIoMem + 0x30) != 0;

    {
        bool gpu_ok = false;
#ifdef TMC_GPU_RENDERER
        if (Port_Profile_Enabled()) {
            uint64_t t0 = SDL_GetTicksNS();
            gpu_ok = Port_PPU_TryGpuRaster();
            gPortProfileRenderNs += SDL_GetTicksNS() - t0;
        } else {
            gpu_ok = Port_PPU_TryGpuRaster();
        }
        if (!gpu_ok) {
            /* TryGpuRaster may have run the HDMA per-line callback via
             * prepare_frame before failing; rewind so the CPU render re-runs it
             * from frame start (port_hdma_vblank_reset is idempotent). */
            port_hdma_vblank_reset();
        }
#endif
        if (!gpu_ok) {
            if (Port_Profile_Enabled()) {
                uint64_t t0 = SDL_GetTicksNS();
                virtuappu_render_frame();
                gPortProfileRenderNs += SDL_GetTicksNS() - t0;
            } else {
                virtuappu_render_frame();
            }
        }
    }

    /* If TMC_PUBLISH_FRAMEBUFFER is on, re-render each main world BG
     * (BG1 + BG2) separately into static buffers and ship them over
     * shm so the RT scaffold can render them as distinct world-Z
     * quads. Cheap: the same per-line render the composite already
     * does, just called again with a dedicated destination. Skipped
     * when the env var isn't set. */
    {
        extern void Port_Shm_PublishFramebuffer(const uint32_t*, int, int);
        extern void Port_Shm_PublishBgPlanes(const uint32_t*, const uint32_t*, int, int);
        extern void Port_Shm_PublishSpritePlane(const uint32_t*, int, int);
        extern int Port_Shm_IsActive(void);
        if (Port_Shm_IsActive()) {
            /* The plane buffers are GBA-native sized — render at 240x160
             * regardless of widescreen or InternalScale.  MODE1_GBA_WIDTH
             * is the compile-time widescreen override (>240 stretches the
             * composite) which we explicitly bypass here because the engine
             * BG/OBJ renderers stop at 240 (32-tile BG buffer extent). */
            const int W = 240;
            const int H = 160;
            static uint32_t bg1Plane[240 * 160];
            static uint32_t bg2Plane[240 * 160];
            static uint32_t spritePlane[240 * 160];
            static uint8_t pri[240];
            std::memset(bg1Plane, 0, sizeof(bg1Plane));
            std::memset(bg2Plane, 0, sizeof(bg2Plane));
            /* Sprite plane pre-cleared to 0x00000000 — render_obj_line
             * skips transparent texels (color_index == 0), so untouched
             * pixels stay at alpha=0. The RT consumer (vk_rt_experiment)
             * uses this to mask emissive boosts to actual silhouettes
             * instead of bleeding across the full 16×16 sprite quad. */
            std::memset(spritePlane, 0, sizeof(spritePlane));
            /* DISPCNT bit 6 = OBJ_1D (1D char mapping); read via the
             * engine's IO accessor so the per-line HDMA snapshot view
             * stays consistent. */
            extern uint16_t virtuappu_mode1_io_read16(uint16_t offset);
            const bool obj_1d = (virtuappu_mode1_io_read16(0) & 0x40) != 0;
            PPUMemory nativePlaneGeometry = virtuappu_registers;
            nativePlaneGeometry.frame_width = W;
            nativePlaneGeometry.frame_pitch = W;
            virtuappu_mode1_set_frame_geometry(&nativePlaneGeometry);
            for (int line = 0; line < H; ++line) {
                virtuappu_mode1_render_text_bg_line(1, line, &bg1Plane[line * W], pri);
                virtuappu_mode1_render_text_bg_line(2, line, &bg2Plane[line * W], pri);
                /* Reset priority buffer between scanlines (render_obj_line
                 * reads it to depth-test OAM entries against each other).
                 * 0xFF = "none drawn yet", same convention as render_frame. */
                std::memset(pri, 0xFF, W);
                virtuappu_mode1_render_obj_line(line, obj_1d, &spritePlane[line * W], pri);
            }
            virtuappu_mode1_set_frame_geometry(&virtuappu_registers);
            /* Build a native-size 240×160 composite for the shm publish so
             * the consumer's atlas dims match the BG/sprite planes.
             *
             * virtuappu_frame_buffer is laid out at stride=MODE1_GBA_WIDTH
             * (>240 in widescreen builds, but the engine only renders to
             * cols 0..239 — the rest is the stale right-edge that the
             * widescreen post-pass stretches away).  We're called BEFORE
             * that post-pass so cols 0..239 are valid GBA-native pixels.
             *
             * When MODE1_GBA_WIDTH == 240 the buffer is contiguous and we
             * can publish directly; otherwise copy the first 240 cols of
             * each scanline into a packed scratch buffer. */
            const uint32_t* compositeSrc;
            static uint32_t compositeNative[240 * 160];
            if (MODE1_GBA_WIDTH == 240) {
                compositeSrc = virtuappu_frame_buffer;
            } else {
                for (int y = 0; y < H; ++y) {
                    std::memcpy(&compositeNative[y * 240], &virtuappu_frame_buffer[y * MODE1_GBA_WIDTH],
                                240 * sizeof(uint32_t));
                }
                compositeSrc = compositeNative;
            }
            Port_Shm_PublishBgPlanes(bg1Plane, bg2Plane, W, H);
            Port_Shm_PublishSpritePlane(spritePlane, W, H);
            Port_Shm_PublishFramebuffer(compositeSrc, W, H);
        }
    }

    /* GBA-LCD display transforms (colour correction + persistence) over the
     * final composited frame, after any shm publish so published planes stay
     * raw game data. No-ops when disabled. Operates on the native render extent
     * (stride MODE1_GBA_WIDTH x MODE1_GBA_HEIGHT); present-side upscale/xBRZ run
     * afterwards on the transformed pixels. */
    Port_PPU_ApplyDisplayPostProcess(MODE1_GBA_WIDTH * MODE1_GBA_HEIGHT);

    int presentW = 0;
    int presentH = 0;
    int presentPitchBytes = 0;
    const uint32_t* presentFrame = Port_PPU_SelectPresentFrame(&presentW, &presentH, &presentPitchBytes);

    /* Stage 2: SDL_GPU present path. Stretched 240x160 → swapchain via
     * the passthrough shader. Internal-scale and xBRZ now route through
     * here too (the upscaled buffer is just fed to Port_GPU_PresentFrame
     * which recreates its source texture on size change). Aspect-mode
     * and filter (CRT/LCD) remain on the SDL_Renderer path; Stage 3
     * will rebuild them on the GPU side as shader passes.
     *
     * ImGui must run BEFORE Port_GPU_PresentFrame so its NewFrame + UI
     * build + ImGui::Render() collect draw data that PresentFrame can
     * then upload (PrepareDrawData) and overlay inside the same render
     * pass. The SDL_Renderer branch below calls Port_ImGui_Render at
     * the end (after the game's RenderPresent), which works because
     * the renderer presents-then-overlays in one call; SDL_GPU needs
     * the order swapped because the menu draws into the game's pass. */
    if (sBackend == RenderBackend::Gpu) {
        Port_ImGui_Render();

        SDL_ScaleMode scale = SDL_SCALEMODE_NEAREST;
        if (sPresentMode == PresentMode::XbrzLinear || sPresentMode == PresentMode::LinearRaw) {
            scale = SDL_SCALEMODE_LINEAR;
        }
        Port_GPU_SetTextureScaleMode(scale);
        /* xBRZ on the GPU path. Same mutual exclusion with internal
         * scale that the SDL_Renderer branch uses (xBRZ is itself a
         * 4× upscaler; combining would compound smoothing). Filter chain
         * (CRT/LCD) is still SW-only. */
        if (sPresentMode == PresentMode::XbrzLinear || sPresentMode == PresentMode::XbrzNearest) {
            if (Port_PPU_EnsureXbrzBuffers(presentW, presentH)) {
                const int hiW = presentW * 4;
                const int hiH = presentH * 4;
                Port_Upscale_xBRZ_4x_Pitch(presentFrame, presentW, presentH, presentPitchBytes / (int)sizeof(uint32_t),
                                           sUpscale2xBuf, sUpscale4xBuf);
                Port_GPU_PresentFrame(sUpscale4xBuf, hiW, hiH, hiW * (int)sizeof(uint32_t));
                return;
            }
            /* Buffer alloc failed: fall through to internal-scale path
             * rather than dropping the frame. */
        }

        /* Honour internal render scale on the GPU path too — same
         * Port_PPU_BuildScaledFrame the SDL_Renderer branch uses,
         * which nearest-replicates the source then overlays the
         * affine OAM at sub-pixel precision. Feeding the bigger
         * buffer to PresentFrame is enough; PresentFrame recreates
         * its source texture when fb_w/fb_h change. Falls back to
         * the native frame at scale==1 (no allocation, fast path). */
        /* GPU supersampled buffer (sub-pixel affine) takes precedence over the
         * nearest-replicate scaler when TryGpuRaster produced it this frame. */
        if (sSuperValid) {
            Port_GPU_PresentFrame(sScaledBufStorage, sSuperW, sSuperH, sSuperW * (int)sizeof(uint32_t));
            return;
        }
        const int gpuScale = (int)Port_Config_InternalScale();
        if (gpuScale > 1) {
            int sw = 0, sh = 0;
            uint32_t* scaled =
                Port_PPU_BuildScaledFrame(presentFrame, presentW, presentH, presentPitchBytes, gpuScale, &sw, &sh);
            if (scaled) {
                Port_GPU_PresentFrame(scaled, sw, sh, sw * (int)sizeof(uint32_t));
                return;
            }
        }
        Port_GPU_PresentFrame(presentFrame, presentW, presentH, presentPitchBytes);
        return;
    }

    if (sBackend == RenderBackend::Renderer) {
        int outW = 0;
        int outH = 0;
        Port_PPU_QueryOutputSize(&outW, &outH);
        Port_TouchControls_NotifyRenderSize(outW, outH);
        int sx, sy, sw_stage, sh_stage;
        int x, y, w, h;
        Port_PPU_ComputeViewportRects(outW, outH, presentW, presentH, &sx, &sy, &sw_stage, &sh_stage, &x, &y, &w, &h);
        SDL_FRect stage = { (float)sx, (float)sy, (float)sw_stage, (float)sh_stage };
        SDL_FRect dst = { (float)x, (float)y, (float)w, (float)h };

        SDL_Texture* tex = nullptr;
        SDL_ScaleMode scale;
        const int internalS = (int)Port_Config_InternalScale();
        switch (sPresentMode) {
            case PresentMode::XbrzLinear:
            case PresentMode::XbrzNearest:
                /* xBRZ owns its own 4x upscaler — internal-render-scale
                 * is mutually exclusive with it. It consumes whichever
                 * frame is active this tick: native 240 or true wide. */
                if (Port_PPU_EnsureXbrzBuffers(presentW, presentH)) {
                    const int hiW = presentW * 4;
                    const int hiH = presentH * 4;
                    SDL_Texture* hiTex =
                        Port_PPU_EnsureTexture(&sHiResTexture, &sHiResTextureW, &sHiResTextureH, hiW, hiH);
                    if (hiTex != nullptr) {
                        Port_Upscale_xBRZ_4x_Pitch(presentFrame, presentW, presentH,
                                                   presentPitchBytes / (int)sizeof(uint32_t), sUpscale2xBuf,
                                                   sUpscale4xBuf);
                        Port_Filter_Apply(sUpscale4xBuf, hiW, hiH, 4, sFilter);
                        SDL_UpdateTexture(hiTex, nullptr, sUpscale4xBuf, hiW * (int)sizeof(uint32_t));
                        tex = hiTex;
                    }
                }
                if (tex == nullptr) {
                    SDL_Texture* rawTex =
                        Port_PPU_EnsureTexture(&sLowResTexture, &sLowResTextureW, &sLowResTextureH, presentW, presentH);
                    if (rawTex == nullptr)
                        return;
                    SDL_UpdateTexture(rawTex, nullptr, presentFrame, presentPitchBytes);
                    tex = rawTex;
                }
                scale = (sPresentMode == PresentMode::XbrzLinear) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            case PresentMode::LinearRaw:
            case PresentMode::NearestRaw:
            default: {
                int sw = 0, sh = 0;
                /* Filter needs a scaled buffer to operate on (1x has too
                 * few pixels per phosphor cell). Force at least 4x when
                 * a filter is active, otherwise honour the user's
                 * internal-scale setting. */
                int effScale = internalS;
                if (sFilter != PORT_FILTER_NONE && effScale < 4) {
                    effScale = 4;
                }
                uint32_t* scaled =
                    Port_PPU_BuildScaledFrame(presentFrame, presentW, presentH, presentPitchBytes, effScale, &sw, &sh);
                SDL_Texture* scaledTex = scaled ? Port_PPU_EnsureScaledTexture(sw, sh, effScale) : nullptr;
                if (scaled && scaledTex) {
                    Port_Filter_Apply(scaled, sw, sh, effScale, sFilter);
                    SDL_UpdateTexture(scaledTex, nullptr, scaled, sw * (int)sizeof(uint32_t));
                    tex = scaledTex;
                } else {
                    SDL_Texture* rawTex =
                        Port_PPU_EnsureTexture(&sLowResTexture, &sLowResTextureW, &sLowResTextureH, presentW, presentH);
                    if (rawTex == nullptr)
                        return;
                    SDL_UpdateTexture(rawTex, nullptr, presentFrame, presentPitchBytes);
                    tex = rawTex;
                }
                scale = (sPresentMode == PresentMode::LinearRaw) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
                break;
            }
        }
        /* Clear-to-black covers anything outside the stage rect (the
         * area honoring the user's aspect-ratio choice). Inside the
         * stage, the chosen background-fill style applies; on top of
         * that, the sharp GBA frame is composited in `dst`. */
        SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sRenderer);

        const PortBgFill bgFill = Port_Config_BgFill();
        if (bgFill == PORT_BG_FILL_SOLID_COLOR) {
            u8 bgR = 0, bgG = 0, bgB = 0;
            Port_Config_BgFillColor(&bgR, &bgG, &bgB);
            SDL_SetRenderDrawColor(sRenderer, bgR, bgG, bgB, 255);
            SDL_RenderFillRect(sRenderer, &stage);
        } else if (bgFill == PORT_BG_FILL_BLURRED_FRAME) {
            /* Stretch the same texture across the whole stage with
             * linear filtering for a soft "ambient mode" halo, then
             * the sharp letterboxed copy paints over the center. */
            Port_PPU_SetTextureScaleModeCached(tex, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(sRenderer, tex, nullptr, &stage);
        }

        Port_PPU_SetTextureScaleModeCached(tex, scale);
        SDL_RenderTexture(sRenderer, tex, nullptr, &dst);
        {
            /* Try the ImGui-based menu first; if disabled (or init
             * failed), fall back to the legacy SDL_RenderDebugText
             * overlay so the menu still works. The soft-slot overlay
             * is a separate HUD layer and always uses SDL primitives. */
            if (!Port_ImGui_Render()) {
                Port_DebugMenu_Render(sRenderer, outW, outH);
            }
            Port_SoftSlots_RenderOverlay(sRenderer, outW, outH);
            Port_TouchControls_Render(sRenderer, outW, outH);
        }
        if (!SDL_RenderPresent(sRenderer)) {
            /* Log throttled: a failing present on Android (EGL surface loss
             * after lifecycle churn) otherwise dies silently and the screen
             * freezes on the last good frame. */
            static uint32_t sLastPresentErrLog = 0;
            uint32_t now = SDL_GetTicks();
            if (now - sLastPresentErrLog > 2000) {
                sLastPresentErrLog = now;
                fprintf(stderr, "[ppu] SDL_RenderPresent FAILED: %s\n", SDL_GetError());
            }
        }
        return;
    }

    Port_PPU_PresentSurfaceFrame();
}

extern "C" void Port_PPU_SetWindowTitle(const char* title) {
    if (!sWindow || !title) {
        return;
    }
    SDL_SetWindowTitle(sWindow, title);
}

extern "C" void Port_PPU_ApplyCursorVisibility(void); /* defined just below */

extern "C" void Port_PPU_ToggleFullscreen(void) {
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (!w) {
        return;
    }
    SDL_WindowFlags flags = SDL_GetWindowFlags(w);
    bool wantFullscreen = (flags & SDL_WINDOW_FULLSCREEN) == 0;
    SDL_SetWindowFullscreen(w, wantFullscreen);
    SDL_SyncWindow(w);
    /* Hide the cursor while fullscreen, show it again in windowed mode — the
     * bare-mouse cursor floating over Hyrule Town gets distracting fast
     * (flagged by nayyar in the suggestions thread). The hide is now opt-out
     * via the "Hide cursor in fullscreen" display setting; the policy lives in
     * Port_PPU_ApplyCursorVisibility so a mid-fullscreen toggle can re-apply. */
    Port_PPU_ApplyCursorVisibility();
}

extern "C" bool Port_PPU_IsFullscreen(void) {
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (!w) {
        return false;
    }
    return (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
}

/* Cursor-visibility policy for the current window state: in fullscreen the OS
 * cursor is hidden when the "hide cursor in fullscreen" setting is on (the
 * default), and always shown in windowed mode. Called on every fullscreen
 * transition and whenever the setting is toggled in the display menu. */
extern "C" void Port_PPU_ApplyCursorVisibility(void) {
    if (Port_PPU_IsFullscreen() && Port_Config_GetFullscreenHideCursor()) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

extern "C" unsigned char Port_PPU_WindowScale(void) {
    return Port_Config_WindowScale();
}

extern "C" void Port_PPU_ApplyWindowScale(void) {
    SDL_Window* w = Port_PPU_ActiveWindow();
    if (w && !Port_PPU_IsFullscreen()) {
        const u8 scale = Port_Config_WindowScale();
        SDL_SetWindowSize(w, Port_PPU_WindowBaseWidth() * scale, MODE1_GBA_HEIGHT * scale);
        SDL_SyncWindow(w);
    }
}

extern "C" void Port_PPU_CycleWindowScale(int direction) {
    u8 scale = Port_Config_WindowScale();
    if (direction < 0) {
        scale = scale <= 1 ? 10 : (u8)(scale - 1);
    } else {
        scale = scale >= 10 ? 1 : (u8)(scale + 1);
    }
    Port_Config_SetWindowScale(scale);
    Port_PPU_ApplyWindowScale();
}

extern "C" void Port_PPU_CyclePresentationMode(int direction) {
    int next = (int)sPresentMode + (direction < 0 ? -1 : 1);
    if (next < 0) {
        next = (int)PresentMode::Count - 1;
    } else if (next >= (int)PresentMode::Count) {
        next = 0;
    }
    sPresentMode = (PresentMode)next;
    Port_Config_SetUpscaleMethod(Port_PPU_MethodForMode(sPresentMode));
    fprintf(stderr, "PPU upscale: %s\n", Port_PPU_PresentationModeName());
}

extern "C" void Port_PPU_ToggleSmoothing(void) {
    Port_PPU_CyclePresentationMode(1);
}

extern "C" void Port_PPU_CycleFilter(int direction) {
    /* On the SDL_GPU backend, the F8 → Filter button drives the GLSL
     * pipeline switcher in port_gpu_renderer.cpp instead of the
     * CPU-side filter (which is bypassed on the GPU path entirely).
     * Stage 3 currently exposes 2 GPU filters (None + LCD Grid);
     * Stages 4+ port the rest of port_filter.c's CRT / scanlines /
     * vignette presets as GLSL shaders. */
    if (sBackend == RenderBackend::Gpu) {
        extern PortGpuFilter Port_GPU_GetFilter(void);
        extern void Port_GPU_SetFilter(PortGpuFilter);
        extern const char* Port_GPU_FilterName(PortGpuFilter);
        int next = (int)Port_GPU_GetFilter() + (direction < 0 ? -1 : 1);
        if (next < 0)
            next = (int)PORT_GPU_FILTER_COUNT - 1;
        else if (next >= (int)PORT_GPU_FILTER_COUNT)
            next = 0;
        Port_GPU_SetFilter((PortGpuFilter)next);
        fprintf(stderr, "GPU filter: %s\n", Port_GPU_FilterName((PortGpuFilter)next));
        return;
    }

    int next = (int)sFilter + (direction < 0 ? -1 : 1);
    if (next < 0) {
        next = (int)PORT_FILTER_COUNT - 1;
    } else if (next >= (int)PORT_FILTER_COUNT) {
        next = 0;
    }
    sFilter = (PortFilterType)next;
    fprintf(stderr, "PPU filter: %s\n", Port_Filter_Name(sFilter));
}

extern "C" const char* Port_PPU_FilterName(void) {
    if (sBackend == RenderBackend::Gpu) {
        extern PortGpuFilter Port_GPU_GetFilter(void);
        extern const char* Port_GPU_FilterName(PortGpuFilter);
        return Port_GPU_FilterName(Port_GPU_GetFilter());
    }
    return Port_Filter_Name(sFilter);
}

extern "C" bool Port_InGameSettingsModalIsOpen(void) {
#ifdef launcher
    return TmcSettings_IsModalOpen();
#else
    return false;
#endif
}

extern "C" void Port_OpenInGameSettingsModal(void) {
#ifdef launcher
    if (TmcSettings_IsModalOpen()) {
        return;
    }
    SDL_Window* w = sWindow ? sWindow : sBootstrapWindow;
    if (!w) {
        return;
    }
    SDL_Renderer* r = sRenderer;
    if (!r) {
        r = SDL_GetRenderer(w);
    }
    if (!r) {
        return;
    }
    if (!TmcSettings_RunModalInGame(w, r)) {
        SDL_Event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
#else
    /* No launcher module linked (public builds): route the touch gear
     * button to the F8 ribbon menu instead of silently doing nothing —
     * it carries every setting the modal would have (Display, Controls,
     * Audio, Saves...). Previously this was a no-op and the gear button
     * looked broken on Android. */
    extern void Port_DebugMenu_Toggle(void);
    Port_DebugMenu_Toggle();
#endif
}

extern "C" bool Port_PPU_OverlaysUseRenderer(void) {
    return sBackend == RenderBackend::Renderer;
}

extern "C" void Port_PPU_Shutdown(void) {
    if (sWindow && sBackend == RenderBackend::Surface) {
        SDL_DestroyWindowSurface(sWindow);
    }
    if (sFrameSurface) {
        SDL_DestroySurface(sFrameSurface);
        sFrameSurface = nullptr;
    }
    if (sLowResTexture) {
        SDL_DestroyTexture(sLowResTexture);
        sLowResTexture = nullptr;
    }
    sLowResTextureW = 0;
    sLowResTextureH = 0;
    if (sHiResTexture) {
        SDL_DestroyTexture(sHiResTexture);
        sHiResTexture = nullptr;
    }
    sHiResTextureW = 0;
    sHiResTextureH = 0;
    if (sScaledTexture) {
        SDL_DestroyTexture(sScaledTexture);
        sScaledTexture = nullptr;
    }
    sScaledTextureW = 0;
    sScaledTextureH = 0;
    sScaledTextureScale = 0;
    sUpscale2xPixels = 0;
    sUpscale4xPixels = 0;
    sScaledBufW = 0;
    sScaledBufH = 0;
    sScaledBufScale = 0;
    sTextureScaleModeTexture = nullptr;
    sTextureScaleModeValid = false;
    if (sRenderer) {
        SDL_DestroyRenderer(sRenderer);
        sRenderer = nullptr;
    }
    sBackend = RenderBackend::None;
    sWindow = nullptr;
}
