#include "gba/io_reg.h"
#include "main.h"
#include <stdbool.h>
#include "port_config.h"
/* Set by xmake (-DMODE1_GBA_WIDTH=N); falls back to GBA-native 240. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif
/* Height stays at GBA-native 160 always — widescreen only stretches X. */
#ifndef MODE1_GBA_HEIGHT
#define MODE1_GBA_HEIGHT 160
#endif
#include "port_asset_bootstrap.h"
#include "port_audio.h"
#include "port_a11y_cues.h"
#include "port_bugreport.h"
#include "port_debug_verbose.h"
#include "port_discord_rpc.h"
#include "port_gba_mem.h"
#include "port_gpu_renderer.h"
#include "port_icon.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_rom_picker.h"
#include "port_runtime_config.h"
#include "port_second_screen.h"
#include "port_tts.h"
#include "port_types.h"
#include "port_update_check.h"
#include "rando/rando_file_menu.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "port_launcher_bootstrap.h"
#include "port_imgui_menu.h"
#include "port_mods.h"

/*
 * Region-specific asset offset header is included based on detected ROM.
 * Both are always available; the correct mapDataBase is selected at runtime.
 */
#if defined(JP)
/* ROM-gated: generated from a JP build. Absent until then, so a JP build
 * fails here with a clear missing-header error. See docs/JP_PORT_ENABLEMENT.md. */
#include "port_offset_JP.h"
#elif defined(EU)
#include "port_offset_EU.h"
#else
#include "port_offset_USA.h"
#endif

static bool Port_TryInitVideo(const char* videoDriver, const char* renderDriver, bool headless) {
    if (videoDriver) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, videoDriver);
    } else {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    }

    if (renderDriver) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderDriver);
    } else {
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
    }

    if (SDL_Init(SDL_INIT_VIDEO)) {
        const char* currentDriver = SDL_GetCurrentVideoDriver();

        fprintf(stderr, "SDL video driver: %s\n", currentDriver ? currentDriver : "unknown");
        if (headless) {
            fprintf(stderr, "SDL initialized with headless video driver '%s'.\n", videoDriver);
        }
        return true;
    }

    return false;
}

static void Port_LogVideoDiagnostics(void) {
    int driverCount = SDL_GetNumVideoDrivers();

    fprintf(stderr, "Video env: DISPLAY='%s' WAYLAND_DISPLAY='%s' XDG_SESSION_TYPE='%s'\n",
            getenv("DISPLAY") ? getenv("DISPLAY") : "", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "",
            getenv("XDG_SESSION_TYPE") ? getenv("XDG_SESSION_TYPE") : "");

    fprintf(stderr, "SDL compiled video drivers:");
    for (int i = 0; i < driverCount; i++) {
        fprintf(stderr, " %s", SDL_GetVideoDriver(i));
    }
    fprintf(stderr, "\n");
}

static bool Port_InitVideo(void) {
    const char* err = NULL;
    const char* forcedDriver = getenv("SDL_VIDEODRIVER");
    const char* display = getenv("DISPLAY");
    const char* waylandDisplay = getenv("WAYLAND_DISPLAY");

#ifdef __ANDROID__
    /* Adreno-405-era devices (Moto G4, msm8952) ship NO Vulkan ICD, so SDL_GPU
     * cannot initialise and SDL_Renderer is the only path — and it MUST use the
     * GLES backend. Pin it explicitly instead of trusting SDL's default driver
     * priority, which could otherwise select the Vulkan-based "gpu" renderer
     * (fails, no ICD) or silently drop to "software" (CPU blit — slow). The
     * GLES2 renderer runs fine on the device's GLES 3.2 context; "software" is
     * kept as the final in-list safety net. An explicit SDL_VIDEODRIVER env
     * (rare on Android) still overrides via the block below. */
    if (!(forcedDriver && forcedDriver[0] != '\0')) {
        if (Port_TryInitVideo(NULL, "opengles2,software", false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }
#endif

    if (forcedDriver && forcedDriver[0] != '\0') {
        if (Port_TryInitVideo(NULL, NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (waylandDisplay && waylandDisplay[0] != '\0') {
        if (Port_TryInitVideo("wayland", NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (display && display[0] != '\0') {
        if (Port_TryInitVideo("x11", NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (Port_TryInitVideo(NULL, NULL, false)) {
        return true;
    }
    err = SDL_GetError();

    SDL_Quit();
    if (Port_TryInitVideo("dummy", "software", true)) {
        fprintf(stderr, "Initial SDL error: %s\n", err ? err : "unknown error");
        return true;
    }

    Port_LogVideoDiagnostics();
    fprintf(stderr, "SDL video init failed: normal='%s', fallback='%s'\n", err ? err : "unknown error", SDL_GetError());
    return false;
}

static void Port_InitAudio(void) {
    const char* err = NULL;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) && Port_Audio_Init()) {
        return;
    }

    err = SDL_GetError();
    Port_Audio_Shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) && Port_Audio_Init()) {
        fprintf(stderr, "Audio device unavailable, using SDL dummy audio driver.\n");
        gMain.muteAudio = 1;
        return;
    }

    fprintf(stderr, "Audio disabled: normal='%s', fallback='%s'\n", err ? err : "unknown error", SDL_GetError());
    Port_Audio_Shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    gMain.muteAudio = 1;
}

/*
 * On Windows mingw, the heap allocator hands out addresses inside the
 * 0x02000000-0x0A000000 range — the same range port_resolve_addr treats
 * as GBA addresses. Heap pointers passed to DmaCopy* (palette/gfx loads
 * from std::vector buffers) get mistranslated to gEwram[] / gVram[] etc,
 * silently reading zeros and stalling the title-screen palette. Reserve
 * the GBA address window before any heap is opened so the OS allocator
 * can't place anything there. Linux glibc keeps malloc above 0x55... so
 * this is a no-op there; the call is Windows-only.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <windows.h>

static int s_gba_va_reserve_done;

static uintptr_t Port_AlignDownU(uintptr_t x, DWORD gran) {
    return (x / (uintptr_t)gran) * (uintptr_t)gran;
}

static void Port_ReserveGbaAddressSpace(void);

#if defined(__GNUC__)
__attribute__((constructor(101)))
#endif
static void Port_ReserveGbaAddressSpaceEarly(void) {
    Port_ReserveGbaAddressSpace();
}

static void Port_ReserveGbaAddressSpace(void) {
    const uintptr_t range_lo = 0x02000000u;
    const uintptr_t range_hi = 0x0A000000u;
    const size_t want_bytes = (size_t)(range_hi - range_lo);

    if (s_gba_va_reserve_done) {
        return;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const DWORD page = si.dwPageSize ? si.dwPageSize : 4096u;

    LPVOID whole = VirtualAlloc((LPVOID)range_lo, (SIZE_T)want_bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (whole != NULL) {
        if (getenv("TMC_VERBOSE_GBA_VA")) {
            fprintf(stderr, "Reserved GBA address window 0x%zx-0x%zx (heap can't land here).\n", (size_t)range_lo,
                    (size_t)range_hi);
        }
        s_gba_va_reserve_done = 1;
        return;
    }

    fprintf(stderr, "WARN: Single-block GBA VA reserve failed (err=%lu); filling window by sub-regions.\n",
            (unsigned long)GetLastError());

    size_t reserved = 0;
    uintptr_t q = range_lo;
    while (q < range_hi) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)q, &mbi, sizeof(mbi)) == 0) {
            fprintf(stderr, "WARN: VirtualQuery stopped at 0x%zx (err=%lu).\n", (size_t)q,
                    (unsigned long)GetLastError());
            break;
        }

        const uintptr_t reg_base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t reg_end = reg_base + (uintptr_t)mbi.RegionSize;

        if (q < reg_base) {
            q = reg_base;
            continue;
        }

        if (mbi.State != MEM_FREE) {
            q = reg_end;
            continue;
        }

        const uintptr_t lo_in = reg_base > range_lo ? reg_base : range_lo;
        const uintptr_t hi_in = reg_end < range_hi ? reg_end : range_hi;
        if (lo_in < hi_in) {
            uintptr_t u = Port_AlignDownU(lo_in, page);
            if (u < lo_in) {
                u += (uintptr_t)page;
            }
            while (u < hi_in) {
                uintptr_t next = u + (uintptr_t)page;
                if (next > hi_in) {
                    next = hi_in;
                }
                if (u >= lo_in) {
                    const SIZE_T sz = (SIZE_T)(next - u);
                    if (sz > 0 && VirtualAlloc((LPVOID)u, sz, MEM_RESERVE, PAGE_NOACCESS) != NULL) {
                        reserved += (size_t)sz;
                    }
                }
                if (next <= u) {
                    break;
                }
                u = next;
            }
        }

        q = reg_end;
    }

    if (reserved + (size_t)page >= want_bytes) {
        if (getenv("TMC_VERBOSE_GBA_VA")) {
            fprintf(stderr,
                    "Reserved GBA address window 0x%zx-0x%zx (%zu / %zu bytes, sub-regions; heap can't land here).\n",
                    (size_t)range_lo, (size_t)range_hi, reserved, want_bytes);
        }
    } else if (reserved > 0u) {
        fprintf(stderr,
                "WARN: Partial GBA VA reserve %zu / %zu bytes; heap may still use gaps — DmaCopy risk remains.\n",
                reserved, want_bytes);
    } else {
        fprintf(stderr, "WARN: Could not reserve GBA address window 0x%zx-0x%zx; DmaCopy may misbehave.\n",
                (size_t)range_lo, (size_t)range_hi);
    }

    s_gba_va_reserve_done = 1;
}
#else
static void Port_ReserveGbaAddressSpace(void) { /* not needed on Linux/macOS */
}
#endif

/* Boot-splash present, hiding the GPU vs SDL_Renderer split. */
static void PaintSplash(SDL_Window* window, const char* msg) {
#ifdef TMC_GPU_RENDERER
    (void)window;
    (void)msg;
    Port_GPU_PaintBootSplash();
#else
    Port_PaintBootSplash(window, msg);
#endif
}

#ifdef __ANDROID__
/* SDL's Java glue (SDLActivity) loads libmain.so and invokes SDL_main; this
 * include renames main -> SDL_main. Only defined in this TU. */
#include <SDL3/SDL_main.h>
#include <unistd.h>
#endif

int main(int argc, char* argv[]) {

    /* Must run before any std::vector / new / malloc that could land in
     * the GBA window. Static initializers in C++ files are constructed
     * before main, so even this is technically not early enough — but
     * the affected allocations (Port_LoadPaletteGroupFromAssets cache)
     * happen later, after EnsureAssetGroupCache(), so reserving here is
     * sufficient in practice. */
    Port_ReserveGbaAddressSpace();

#ifdef __ANDROID__
    /* First thing: fprintf(stderr) goes to /dev/null on Android — bridge it
     * into logcat (tag "tmc") so every port log line is visible via adb. */
    {
        extern void Port_AndroidLog_Init(void);
        Port_AndroidLog_Init();
    }
#endif

#ifdef __ANDROID__
    /* Every file the port touches (config.json, tmc.sav, assets cache,
     * quicksaves, bugreports) is CWD-relative. On Android the initial CWD
     * is / (unwritable); one chdir makes the whole port land its state in
     * the right place with zero per-file changes.
     *
     * Prefer the app's EXTERNAL files dir (/sdcard/Android/data/<pkg>/files):
     * user-reachable via file manager / adb — which is how the ROM gets in
     * before the in-app SAF picker lands (M3) — and not size-constrained
     * like the private data partition (assets cache is ~60 MB). Falls back
     * to the private pref path when external storage is unavailable.
     * SDL_Init(0) is cheap and safe before the real subsystem init later. */
    if (SDL_Init(0)) {
        const char* ext = SDL_GetAndroidExternalStoragePath();
        if (ext && (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) && chdir(ext) == 0) {
            fprintf(stderr, "[android] data dir (external): %s\n", ext);
        } else {
            char* pref = SDL_GetPrefPath("picori", "tmc");
            if (pref) {
                if (chdir(pref) != 0) {
                    fprintf(stderr, "[android] chdir(%s) failed\n", pref);
                } else {
                    fprintf(stderr, "[android] data dir (private): %s\n", pref);
                }
                SDL_free(pref);
            }
        }
    }
#endif

    fprintf(stderr, "Initializing port layer...\n");
    { Port_DebugVerbose_Init(); }

    /* Auto-capture on SIGSEGV/SIGABRT/SIGBUS — dropped from port_main.c by
     * upstream refactor 5987bf9b1 and not re-added when #108 brought
     * port_bugreport.cpp back. Without this, crashes produce no bundle
     * (F9 manual capture still works via port_bios.c). */
    { Port_BugReport_InstallCrashHandlers(); }

    // Initialize REG_KEYINPUT to all-keys-released (GBA: 1=not pressed)
    *(u16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;

    Port_Config_Load("config.json");

    /* Apply the persisted game master volume to the audio backend (audio is
     * initialised before config load above). */
    Port_Audio_SetMasterVolume(Port_Config_GetMasterVolume());

    /* Re-apply persisted runtime toggles that have no window/GPU dependency
     * (issue #146). Fullscreen, VSync and the shader preset need the window
     * and renderer, so they are applied after Port_PPU_Init below. */
    {
        extern void Port_DiscordRpc_SetEnabled(bool);
        extern void Port_Reborn_ApplyMask(unsigned);
        if (Port_Config_GetDiscordRpc())
            Port_DiscordRpc_SetEnabled(true);
        if (Port_Config_HasRebornMask())
            Port_Reborn_ApplyMask(Port_Config_GetRebornMask());
    }

    /* Honour the persisted save-profile choice. Default ("tmc.sav") is
     * applied if the config doesn't name one. */
    {
        extern const char* Port_Config_ActiveSaveProfile(void);
        extern int Port_Save_SetActivePath(const char* path);
        Port_Save_SetActivePath(Port_Config_ActiveSaveProfile());
    }

    /* Auto-save defaults to on. Apply the persisted interval too. */
    {
        extern bool Port_Config_AutosaveEnabled(void);
        extern u32 Port_Config_AutosaveIntervalMs(void);
        extern void Port_QuickSave_SetAutoEnabled(int enabled);
        extern void Port_QuickSave_SetAutoIntervalMs(u32 ms);
        Port_QuickSave_SetAutoEnabled(Port_Config_AutosaveEnabled() ? 1 : 0);
        Port_QuickSave_SetAutoIntervalMs(Port_Config_AutosaveIntervalMs());
    }

    u8 window_scale = Port_Config_WindowScale();
    int window_base_width = (MODE1_GBA_WIDTH > 240 && Port_Config_WidescreenEnabled()) ? MODE1_GBA_WIDTH : 240;
    bool noAudio = false;
    const char* glslpPath = NULL;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strncmp(argv[i], "--window_scale=", 15) == 0) {
                const char* valueStr = argv[i] + 15;
                int value = atoi(valueStr);
                if (value >= 1 && value <= 10) {
                    window_scale = (uint8_t)value;
                } else {
                    fprintf(stderr, "Invalid window scale '%s'. Must be an integer between 1 and 10.\n", valueStr);
                }
            } else if (strcmp(argv[i], "--loose-assets") == 0) {
                Port_LooseAssetsRequested = 1;
            } else if (strcmp(argv[i], "--no-audio") == 0) {
                noAudio = true;
            } else if (strncmp(argv[i], "--glslp=", 8) == 0) {
                glslpPath = argv[i] + 8;
            } else if (strcmp(argv[i], "--console-parity") == 0) {
                /* Force hardware-equivalent behavior for legit runs. Applied
                 * after Port_Config_Load (line ~316) so it overrides config. */
                Port_Config_SetConsoleParity(true);
                fprintf(stderr, "Console-Parity mode ON: edge-cache off, save-states inert, "
                                "widescreen off, pacing locked to 59.7275 Hz.\n");
            } else if (strcmp(argv[i], "--help") == 0) {
                fprintf(stderr,
                        "Usage: %s [--window_scale=<value>] [--loose-assets] [--no-audio] [--glslp=<path>] "
                        "[--console-parity]\n",
                        argv[0]);
                fprintf(stderr, "  --window_scale=<value>: Set the window scale (1-10, default is 3)\n");
                fprintf(stderr,
                        "  --loose-assets:         Ignore assets/*.pak archives and read loose files instead.\n");
                fprintf(stderr, "  --no-audio:             Skip audio init (workaround for agbplay crash)\n");
                fprintf(stderr, "  --glslp=<path>:         Load a libretro .glslp shader preset (requires "
                                "--gpu_renderer=y build).\n");
                fprintf(stderr, "                          Equivalent to setting TMC_GLSLP_PRESET=<path> env var.\n");
                fprintf(stderr,
                        "  --console-parity:       Force hardware-equivalent behavior for legitimate speedruns\n");
                fprintf(
                    stderr,
                    "                          (no input edge-cache, no save-states, no widescreen, 59.7275 Hz).\n");
                fprintf(stderr, "  config.json: Set window_scale and bindings defaults\n");
                return 0;
            } else {
                fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            }
        }
    }
    /* Expose the path to port_gpu_renderer's startup probe by setting
     * the env var the existing TMC_GLSLP_PRESET check reads. Keeps the
     * load path single-sourced — CLI just becomes another way to fill
     * the same env var. setenv lives behind POSIX feature macros that
     * the build doesn't define on every platform; use a forward extern
     * declaration so we don't need to crank up _POSIX_C_SOURCE. */
    if (glslpPath) {
#if defined(_WIN32)
        /* MinGW lacks POSIX setenv; _putenv_s is the equivalent. */
        extern int _putenv_s(const char*, const char*);
        _putenv_s("TMC_GLSLP_PRESET", glslpPath);
#else
        extern int setenv(const char*, const char*, int);
        setenv("TMC_GLSLP_PRESET", glslpPath, /*overwrite=*/1);
#endif
    }

    // Initialize SDL video first. Audio is optional and handled separately.
    if (!Port_InitVideo()) {
        return 1;
    }

    Port_Config_OpenGamepads();
    Port_SecondScreen_Init();

    /* ROM probe deferred — the prelaunch screen (rendered after PPU
     * init below) handles ROM selection via Port_RomPicker_PromptAndInstall.
     * On first launch with no ROM, the prelaunch shows a "Select ROM..."
     * card. Once the user picks a valid dump, we drop into the regular
     * Port_LoadRom / asset-extract / audio-init chain. */
    const char* romPath = NULL;

    /* Use SDL_CreateWindowAndRenderer so SDL picks the renderer
     * driver (opengl/vulkan/...) and creates the window with the
     * matching visual flags atomically. Calling SDL_CreateRenderer
     * AFTER SDL_CreateWindow on Linux/X11 forces SDL to internally
     * destroy and recreate the X11 window to add SDL_WINDOW_OPENGL,
     * which the user sees as "first window opens, goes black,
     * closes, then second window opens." Confirmed by H9 logs:
     * window flags went from 0x220 → 0x222 across the first
     * SDL_CreateRenderer call, with driver=opengl. */
    SDL_Window* window = NULL;
#ifndef TMC_GPU_RENDERER
    SDL_Renderer* prerenderer = NULL;
#endif
    /* TMC_PC_VERSION is defined by xmake.lua via add_defines("TMC_PC_VERSION=\"...\"") */
    char window_title[64];
    SDL_snprintf(window_title, sizeof(window_title), "The Minish Cap " TMC_PC_VERSION);
#ifdef TMC_GPU_RENDERER
    /* SDL_GPU wants exclusive ownership of the window's swapchain
     * surface. If we go through SDL_CreateWindowAndRenderer, the
     * SDL_Renderer atomically creates its Vulkan surface and SDL_GPU's
     * subsequent ClaimWindow gets back VK_ERROR_SURFACE_LOST_KHR
     * (Wayland: "surface already exists"). Use bare SDL_CreateWindow
     * so SDL_GPU can claim the swapchain cleanly. The bootstrap-splash
     * code paths still call Port_PaintBootSplash unconditionally; on
     * GPU builds that's a no-op (no SDL_Renderer to draw on) — the boot
     * splash trades away on this build for the GPU pipeline.
     *
     * Do not add SDL_WINDOW_VULKAN unconditionally: in a GPU-compiled release
     * build, users can still force Software (and Auto must be able to fall back).
     * A Vulkan window flag makes SDL_CreateWindow fail before that fallback on
     * dummy/no-Vulkan systems. Forced-GPU keeps the Vulkan flag on non-Apple
     * platforms to preserve the explicit swapchain path; Auto tries GPU later
     * from a plain window and falls back cleanly in Port_PPU_Init. */
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#if !defined(__APPLE__)
    if (Port_Config_RenderBackend() == PORT_RENDER_BACKEND_GPU) {
        window_flags |= SDL_WINDOW_VULKAN;
    }
#endif
#ifdef __ANDROID__
    /* No windowed mode on a handheld — request fullscreen at creation time
     * rather than toggling after the fact. SDL's Android backend sizes the
     * native window to the requested w/h and only syncs system-bar/immersive
     * state through SDLActivity on the initial SDL_CreateWindow flags, so a
     * later SDL_SetWindowFullscreen (see Port_PPU_ToggleFullscreen) leaves the
     * nav bar showing and the surface short by its height. */
    window_flags |= SDL_WINDOW_FULLSCREEN;
#endif
    window =
        SDL_CreateWindow(window_title, window_base_width * window_scale, MODE1_GBA_HEIGHT * window_scale, window_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#else
    {
        SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
        window_flags |= SDL_WINDOW_FULLSCREEN;
#endif
        if (!SDL_CreateWindowAndRenderer(window_title, window_base_width * window_scale, MODE1_GBA_HEIGHT * window_scale,
                                         window_flags, &window, &prerenderer)) {
            fprintf(stderr, "SDL_CreateWindowAndRenderer Error: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }
    (void)prerenderer; /* Owned by the window; retrieved via SDL_GetRenderer(window) later. */
#endif

    /* Set window icon BEFORE first present so the title-bar and
     * taskbar entry never flash the default SDL icon. */
    {
        SDL_Surface* icon = Port_CreateAppIcon();
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_SyncWindow(window);

#ifdef launcher
    if (!Port_RunBootstrapLauncher(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
#endif

    /* Paint a "LOADING" splash IMMEDIATELY so the window never
     * shows a blank black rectangle.
     *
     * GPU build: stand up SDL_GPU EARLY (before any boot UI) so its
     * swapchain claim doesn't fight an already-present SDL_Renderer
     * surface. After ClaimWindow succeeds, Port_GPU_PaintBootSplash
     * clears the swapchain to a dark teal — replaces the
     * SDL_Renderer-based splash on GPU builds with something the user
     * can see (it's not a blank window any more). Asset extractor's
     * progress bar still uses SDL_Renderer though, so we still pass
     * NULL window to the extractor on GPU builds.
     *
     * Non-GPU build: the SDL_Renderer that SDL_CreateWindowAndRenderer
     * created is already attached to the window; Port_PaintBootSplash
     * fetches it via SDL_GetRenderer and presents the "LOADING" card. */
#ifdef TMC_GPU_RENDERER
    /* Skip GPU init entirely if the user pinned the renderer to
     * Software in F8 — otherwise the GPU device claims the window
     * (esp. the wayland surface) and the later SDL_Renderer create
     * in Port_PPU_Init conflicts with it ("Surface got destroyed
     * already" / "Wayland display connection closed by server"
     * fatal error). Force-GPU and Auto both want the early init. */
    if (Port_Config_RenderBackend() != PORT_RENDER_BACKEND_SOFTWARE) {
        Port_GPU_Init(window);
        Port_GPU_ClaimWindow(window, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT);
        Port_GPU_PaintBootSplash();
    } else {
        Port_PaintBootSplash(window, "LOADING");
    }
#else
    Port_PaintBootSplash(window, "LOADING");
#endif

    /* PPU init first — creates the SDL_Renderer / SDL_GPU device + the
     * ImGui context. Doesn't touch ROM data, so we can stand up the
     * prelaunch UI before the user has even pointed us at a .gba.
     *
     * On non-GPU builds the GPU stub still needs to fire for the
     * build-flag-off no-op path. */
#ifndef TMC_GPU_RENDERER
    { Port_GPU_Init(window); }
#endif
    Port_PPU_Init(window);

    PaintSplash(window, "LOADING");
    fprintf(stderr, "PPU init complete.\n");

    /* Re-apply persisted window/renderer toggles now that the window, the
     * renderer, and (on GPU builds) the GPU device all exist (issue #146). */
    {
        Port_PPU_SetVSync(Port_Config_GetVSync());
        if (Port_Config_GetFullscreen() && !Port_PPU_IsFullscreen()) {
            Port_PPU_ToggleFullscreen();
        }
        /* Shader preset: skip if TMC_GLSLP_PRESET already loaded one at GPU
         * init. Load is a no-op / graceful failure off the SPIR-V backend. */
        const char* sp = Port_Config_GetShaderPreset();
        if (sp && sp[0] && !getenv("TMC_GLSLP_PRESET")) {
            extern int Port_GlslpRuntime_Load(const char*);
            Port_GlslpRuntime_Load(sp);
        }
    }

    /* TTS init runs after PPU so ImGui is up (the F8 menu's TTS tab
     * uses it immediately) but well before AgbMain — accessibility
     * announcements for the prelaunch screen need a working backend
     * BEFORE the prelaunch frame loop runs. Idempotent + no-op if no
     * backend is available; never blocks. */
    { Port_TTS_Init(); }

    /* Load persisted accessibility cue toggles into the cue module. */
    { Port_A11y_Init(); }

    /* ====================================================================
     * Project Picori prelaunch screen.
     *
     * Renders before any ROM is loaded. If no .gba is present next to
     * the exe, the card shows a "Select your ROM" prompt and a big
     * Select-ROM button. Once the user picks a valid dump (SHA-1
     * matched against the known TMC hashes — see port_rom_picker.c),
     * the card flips to the regular state with version / ROM / Play.
     *
     * Play exits the loop and we fall through to Port_LoadRom + asset
     * extraction + audio init. Quit during the loop cleanly shuts down
     * the partial init we did so far.
     * ==================================================================== */
    {
        romPath = Port_FindBaseRomPath();
        fprintf(stderr, "Prelaunch: %s — waiting for user.\n", romPath ? "ROM detected" : "no ROM yet");

        /* Announce the prelaunch screen for screen-reader users.
         * Port_TTS_Init ran a few lines above so the backend is up;
         * Port_TTS_Speak is a no-op when TTS is disabled in config. */
        {
            PortTtsOptions opts = { 0 };
            opts.priority = PORT_TTS_PRIO_URGENT;
            opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
            opts.dedupe = false;
            if (romPath) {
                Port_TTS_Speak("Project Picori, the Minish Cap PC port. "
                               "ROM detected. Press Enter or Space to play.",
                               &opts);
            } else {
                Port_TTS_Speak("Project Picori, the Minish Cap PC port. "
                               "No ROM found. Press Enter or Space to "
                               "select your Minish Cap GBA ROM file.",
                               &opts);
            }
        }

        bool done = false;
        if (romPath && getenv("TMC_AUTOPLAY")) {
            fprintf(stderr, "Prelaunch: TMC_AUTOPLAY set — skipping menu.\n");
            done = true;
        }
        /* Deadline pacing keeps the prelaunch menu near 60 Hz without
         * busy-waiting. Fixed SDL_Delay(16) drifts and can spin hot on
         * timer jitter; this yields only until the next frame deadline. */
        const Uint64 prelaunchFrameNs = 1000000000ull / 60ull;
        Uint64 nextPrelaunchFrameNs = SDL_GetTicksNS() + prelaunchFrameNs;
        while (!done) {
            /* Region dropdown changes preferred candidate order before Play. */
            romPath = Port_FindBaseRomPath();
            /* Re-derive the displayable filename each iteration in case
             * a Change-ROM swap moved the file. */
            char rom_name_buf[1024];
            const char* rom_name = "(none)";
            if (romPath) {
                const char* slash = strrchr(romPath, '/');
#ifdef _WIN32
                const char* bslash = strrchr(romPath, '\\');
                if (bslash && (!slash || bslash > slash))
                    slash = bslash;
#endif
                if (slash && slash[1]) {
                    snprintf(rom_name_buf, sizeof(rom_name_buf), "%s", slash + 1);
                } else {
                    snprintf(rom_name_buf, sizeof(rom_name_buf), "%s", romPath);
                }
                rom_name = rom_name_buf;
            }

            const bool rom_present = (romPath != NULL);
            bool play_clicked = false;
            bool change_rom_clicked = false;
            const bool imgui_built =
                Port_ImGui_RenderPrelaunch(rom_present, TMC_PC_VERSION, rom_name, &play_clicked, &change_rom_clicked);
#ifdef TMC_GPU_RENDERER
            if (imgui_built) {
                Port_GPU_PresentPrelaunchFrame();
            } else {
                Port_GPU_PaintBootSplash();
            }
#else
            (void)imgui_built; /* SDL_Renderer path presents inline. */
#endif

            if (play_clicked && rom_present) {
                fprintf(stderr, "Prelaunch: play_clicked.\n");
                done = true;
            } else if (change_rom_clicked) {
                fprintf(stderr, "Prelaunch: change_rom_clicked — calling picker.\n");
                int rc = Port_RomPicker_PromptAndInstall();
                fprintf(stderr, "Prelaunch: picker returned %d.\n", rc);
                if (rc == 0) {
                    romPath = Port_FindBaseRomPath();
                    fprintf(stderr, "Prelaunch: post-picker romPath=%s.\n", romPath ? romPath : "(still NULL)");
                    /* Re-announce so the user hears the new state
                     * without having to look at the screen. */
                    {
                        PortTtsOptions opts = { 0 };
                        opts.priority = PORT_TTS_PRIO_URGENT;
                        opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
                        opts.dedupe = false;
                        if (romPath) {
                            Port_TTS_Speak("ROM loaded. Press Enter or "
                                           "Space to play.",
                                           &opts);
                        } else {
                            Port_TTS_Speak("No ROM selected.", &opts);
                        }
                    }
                }
            } else if (play_clicked) {
                fprintf(stderr, "Prelaunch: play_clicked but no ROM (ignored).\n");
            }

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                Port_ImGui_HandleEvent(&ev);
                if (ev.type == SDL_EVENT_QUIT) {
                    Port_GPU_Shutdown();
                    Port_DiscordRpc_Shutdown();
                    Port_PPU_Shutdown();
                    Port_Config_CloseGamepads();
                    SDL_DestroyWindow(window);
                    SDL_Quit();
                    return 0;
                }
            }
            {
                const Uint64 nowNs = SDL_GetTicksNS();
                if (nowNs < nextPrelaunchFrameNs) {
                    const Uint64 remainingNs = nextPrelaunchFrameNs - nowNs;
                    const Uint64 delayMs = remainingNs / 1000000ull;
                    SDL_Delay(delayMs > 0 ? (Uint32)delayMs : 0);
                }
                const Uint64 afterDelayNs = SDL_GetTicksNS();
                if (afterDelayNs > nextPrelaunchFrameNs + prelaunchFrameNs) {
                    nextPrelaunchFrameNs = afterDelayNs + prelaunchFrameNs;
                } else {
                    nextPrelaunchFrameNs += prelaunchFrameNs;
                }
            }
        }
        fprintf(stderr, "Prelaunch: Play — loading ROM and assets.\n");
    }

    /* Play pressed and romPath is set. Select the active mod set before
     * Port_LoadRom touches the asset loader, so TMC_MODS applies to early
     * table/text/area overrides too. */
    Port_Mods_Init();
    Port_LoadRom(romPath);
#ifdef TMC_GPU_RENDERER
    Port_EnsureAssetsReadyWithDisplay(NULL, gRomData, gRomSize);
#else
    Port_EnsureAssetsReadyWithDisplay(window, gRomData, gRomSize);
#endif
    { Port_RandoFileMenu_RestorePersistedSettings(); }
    Port_CheckForUpdates(window);

    /* Now that the ROM and asset tables are loaded, re-set the window
     * icon — Port_CreateAppIcon prefers the ROM-extracted Ezlo sprite
     * over the procedural fallback once gRomData/gSpritePtrs/gFrameObjLists
     * are populated. */
    {
        SDL_Surface* icon = Port_CreateAppIcon();
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

    /* Region cross-check between the compile-time region macro and the
     * runtime-detected ROM region. A mismatch pairs one version's *code*
     * with another version's *data* — a hybrid that matches no real console,
     * so RNG manips and offsets diverge. Casual play only warns (the hybrid
     * is usually playable); Console-Parity treats it as fatal, since a legit
     * run must not execute on a version-mismatched build. */
#ifndef MULTI_REGION
    {
#if defined(JP)
        const RomRegion compiledRegion = ROM_REGION_JP;
        const char* compiledName = "JP";
#elif defined(EU)
        const RomRegion compiledRegion = ROM_REGION_EU;
        const char* compiledName = "EU";
#else
        const RomRegion compiledRegion = ROM_REGION_USA;
        const char* compiledName = "USA";
#endif
        if (gRomRegion != compiledRegion) {
            const char* detectedName = gRomRegion == ROM_REGION_USA  ? "USA"
                                       : gRomRegion == ROM_REGION_EU ? "EU"
                                       : gRomRegion == ROM_REGION_JP ? "JP"
                                                                     : "UNKNOWN";
            if (Port_Config_GetConsoleParity()) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "Console-Parity requires a region-matched ROM: this build is %s but the "
                         "ROM is %s.\n\n"
                         "A version-mismatched hybrid is not console-equivalent. Use a %s ROM, or "
                         "relaunch without Console-Parity.",
                         compiledName, detectedName, compiledName);
                Port_FatalRomError("Minish Cap PC Port - region mismatch", msg);
            }
            {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "This build is %s but the ROM is %s.\n\n"
                         "Graphics, text, and RNG may be wrong - the game runs as a hybrid that "
                         "matches neither console version. Use a %s ROM for a faithful game.",
                         compiledName, detectedName, compiledName);
                fprintf(stderr, "WARNING: %s\n", msg);
                fflush(stderr);
#ifndef TMC_N64
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Minish Cap PC Port - region mismatch", msg, window);
#endif
            }
        }
    }
#endif

    if (noAudio) {
        gMain.muteAudio = 1;
        fprintf(stderr, "Audio disabled by --no-audio flag.\n");
    } else {
        Port_InitAudio();
        fprintf(stderr, "Audio init complete.\n");
    }

    /* Last bridging splash before the game's title fade-in takes
     * over. After this the engine drives the frame loop. */
    PaintSplash(window, "STARTING");
    fprintf(stderr, "Port layer initialized. Entering AgbMain...\n");

    AgbMain();

    { Port_TTS_Shutdown(); }
    { Port_GPU_Shutdown(); }

    { Port_DiscordRpc_Shutdown(); }
    Port_Audio_Shutdown();
    Port_PPU_Shutdown();
    Port_Config_CloseGamepads();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
