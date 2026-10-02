/* Order matters: std headers first so the GBA-style `min`/`max`
 * macros from include/global.h (pulled in transitively via
 * port_asset_bootstrap.h) don't collide with std::min/std::max
 * inside <algorithm>. */
#include <algorithm>
#include "port_asset_bootstrap.h"
#include "port_asset_pipeline.hpp"
#include "port_exe_path.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "port_asset_bootstrap.h"
/* global.h (transitively included by port_asset_bootstrap.h) defines
 * GBA-style min/max as object-style macros, which break C++ headers
 * that overload std::min/std::max. Strip them here so the rest of
 * this TU can use the standard library cleanly. */
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "port_asset_loader.h"
#include "port_asset_log.hpp"
#include "port_asset_pipeline.hpp"
#include "region.h" /* RegionAssetSubdir() — per-region cache folder */

#include "assets_extractor_api.hpp"
extern "C" const char* Port_GetLoadedRomPath(void);

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdlib>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

int Port_LooseAssetsRequested = 0;

namespace {

/* Approximate number of distinct phases the extractor walks through.
 * Used to show "(N / EXPECTED)" beside the bar so the user has a
 * sense of progress between phase boundaries. The actual phase
 * count varies slightly by ROM contents but is stable enough for
 * UI purposes; we clamp the displayed numerator at the denominator
 * if it overshoots. */
constexpr int kExpectedPhaseCount = 9;

/* Lock-free snapshot of extractor progress. Producer is the
 * Reporter callback (called from worker threads); consumer is the
 * GUI thread that polls every ~50 ms while the bar is on screen. */
struct ProgressSnapshot {
    std::atomic<std::size_t> done{ 0 };
    std::atomic<std::size_t> total{ 0 };
    std::atomic<int> phase_index{ 0 };
    std::atomic<bool> running{ true };

    /* Phase name lives behind a mutex because std::string isn't
     * trivially atomic. The GUI takes a copy under lock once per
     * frame which is fine at 60 Hz. */
    std::mutex name_mu;
    std::string phase_name;
    std::string last_phase_name; // used to detect phase transitions
};

void MountPaksForRoot(const std::filesystem::path& root) {
    if (Port_LooseAssetsRequested) {
        std::fprintf(stderr, "[ASSET] paks present but disabled by --loose-assets\n");
        return;
    }
    /* Per-region cache: assets/<region>/. Keeps USA/EU/JP paks from clobbering
     * each other when the same install runs more than one ROM. */
    const std::filesystem::path assetsDir = root / "assets" / RegionAssetSubdir();
    const int mounted = Port_MountAssetPaks(assetsDir.string().c_str());
    if (mounted > 0) {
        std::fprintf(stderr, "[ASSET] paks mounted: %d (%d entries)\n", mounted, Port_PakEntryCount());
    } else {
        std::fprintf(stderr, "[ASSET] no paks mounted; using loose files\n");
    }
}

std::optional<std::filesystem::path> GetExecutableDirectory() {
    return port::ExecutableDir();
}

std::filesystem::path PreferredAssetRoot() {
#ifdef __ANDROID__
    const char* androidRuntimeDir = std::getenv("TMC_ANDROID_RUNTIME_DIR");
    if (androidRuntimeDir != nullptr && androidRuntimeDir[0] != '\0') {
        return std::filesystem::path(androidRuntimeDir);
    }

    std::error_code androidEc;
    const auto androidCwd = std::filesystem::current_path(androidEc);
    if (!androidEc) {
        return androidCwd;
    }
#endif

    const auto exeDir = GetExecutableDirectory();
    if (exeDir.has_value()) {
        return *exeDir;
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

/* ----------------------------------------------------------------
 *  5x7 bitmap font for the progress bar UI.
 *
 *  Extended from the original A/C/E/G/I/N/R/S/T/X/. set so we can
 *  spell out arbitrary phase names ("loading palettes", "writing
 *  paks", etc.) plus digits for the "3 / 9" counter.
 * ---------------------------------------------------------------- */
using GlyphRows = std::array<unsigned char, 7>;

GlyphRows GlyphFor(char c) {
    switch (c) {
        case 'A':
            return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B':
            return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C':
            return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D':
            return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E':
            return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F':
            return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G':
            return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
        case 'H':
            return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I':
            return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'J':
            return { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
        case 'K':
            return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L':
            return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M':
            return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N':
            return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
        case 'O':
            return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P':
            return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q':
            return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R':
            return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S':
            return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T':
            return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U':
            return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V':
            return { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };
        case 'W':
            return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
        case 'X':
            return { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11 };
        case 'Y':
            return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z':
            return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0':
            return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1':
            return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2':
            return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
        case '3':
            return { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
        case '4':
            return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5':
            return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
        case '6':
            return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7':
            return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8':
            return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9':
            return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
        case '/':
            return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
        case '-':
            return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '_':
            return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F };
        case '%':
            return { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
        case '.':
            return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        case ':':
            return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
        default:
            return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }
}

void DrawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float scale,
              SDL_Color color = { 235, 240, 245, 255 }) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    /* Uppercase the text up front so phase names submitted as "palettes"
     * still render via the uppercase-only glyph table. Cheaper and
     * smaller than maintaining a parallel lowercase set. */
    for (char raw : text) {
        char c = raw;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - ('a' - 'A'));
        const GlyphRows glyph = GlyphFor(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0)
                    continue;
                SDL_FRect pixel = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        x += (c == ' ') ? scale * 4.0f : scale * 6.0f;
    }
}

float MeasureText(std::string_view text, float scale) {
    /* Mirrors the per-character advance in DrawText so callers can
     * compute centred positions without rendering twice. */
    float w = 0.0f;
    for (char c : text) {
        w += (c == ' ') ? scale * 4.0f : scale * 6.0f;
    }
    return w;
}

/* Shared splash/progress frame scaffolding: resolve the window's
 * renderer (creating one only as a fallback), compute fw/fh and the
 * UI scale every painter used, clear to bgColor, run the caller's draw
 * body, then present. No-op if there's no renderer. */
template <typename Body> void PaintFrame(SDL_Window* window, SDL_Color bgColor, Body&& body) {
    if (!window)
        return;
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer)
        return;

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    const float scale = std::max(2.0f, std::round(fw / 240.0f));

    SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
    SDL_RenderClear(renderer);
    body(renderer, fw, fh, scale);
    SDL_RenderPresent(renderer);
}

/* One horizontally-centred text line plus the vertical gap that follows
 * it, for DrawCenteredTextBlock. */
struct TextBlockLine {
    std::string_view text;
    float scale;
    SDL_Color color;
    float gapAfter;
};

/* Vertically-centre a stack of horizontally-centred lines. Block height
 * is the sum of each line's glyph height (7*scale) plus its gapAfter, so
 * the last line's gapAfter is included — callers reproduce their exact
 * original spacing by setting it. */
void DrawCenteredTextBlock(SDL_Renderer* renderer, float fw, float fh, const std::vector<TextBlockLine>& lines) {
    float blockH = 0.0f;
    for (const auto& ln : lines) {
        blockH += 7.0f * ln.scale + ln.gapAfter;
    }
    float y = (fh - blockH) * 0.5f;
    for (const auto& ln : lines) {
        const float w = MeasureText(ln.text, ln.scale);
        DrawText(renderer, ln.text, (fw - w) * 0.5f, y, ln.scale, ln.color);
        y += 7.0f * ln.scale + ln.gapAfter;
    }
}

void DrawProgressScreen(SDL_Window* window, const ProgressSnapshot& snap) {
    PaintFrame(window, SDL_Color{ 12, 16, 22, 255 }, [&snap](SDL_Renderer* renderer, float fw, float fh, float scale) {
        /* Header line, drawn once and unaffected by the ticking phase. */
        constexpr std::string_view kHeader = "EXTRACTING ASSETS";
        const float headerW = MeasureText(kHeader, scale);
        DrawText(renderer, kHeader, (fw - headerW) * 0.5f, fh * 0.30f, scale);

        /* Phase label (snapshot under lock since std::string isn't atomic). */
        std::string phaseName;
        {
            std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(snap.name_mu));
            phaseName = snap.phase_name.empty() ? std::string("preparing") : snap.phase_name;
        }
        const std::string phaseLine = std::string("loading ") + phaseName;
        const float phaseScale = std::max(1.5f, scale * 0.7f);
        const float phaseW = MeasureText(phaseLine, phaseScale);
        DrawText(renderer, phaseLine, (fw - phaseW) * 0.5f, fh * 0.46f, phaseScale, { 180, 200, 220, 255 });

        /* Progress bar geometry. */
        const float barWidth = fw * 0.65f;
        const float barHeight = std::max(8.0f * scale, fh * 0.04f);
        const float barX = (fw - barWidth) * 0.5f;
        const float barY = fh * 0.55f;

        const std::size_t done = snap.done.load(std::memory_order_acquire);
        const std::size_t total = snap.total.load(std::memory_order_acquire);
        const int phaseIdx = snap.phase_index.load(std::memory_order_acquire);
        const int phaseDen = std::max(kExpectedPhaseCount, phaseIdx + 1);

        const double inPhase = (total == 0) ? 1.0 : static_cast<double>(done) / static_cast<double>(total);
        /* Stage-aware fraction: each completed phase is worth 1/phaseDen
         * of the bar, the active phase contributes its own internal
         * percentage. Clamped because the last phase often completes
         * with done > total when EndPhase fires before the GUI re-snaps. */
        const double overall = std::clamp(
            (static_cast<double>(phaseIdx) + std::clamp(inPhase, 0.0, 1.0)) / static_cast<double>(phaseDen), 0.0, 1.0);

        /* Bar border (light) + fill (warm accent). */
        SDL_SetRenderDrawColor(renderer, 60, 70, 80, 255);
        SDL_FRect border = { barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f };
        SDL_RenderFillRect(renderer, &border);
        SDL_SetRenderDrawColor(renderer, 18, 24, 32, 255);
        SDL_FRect inner = { barX, barY, barWidth, barHeight };
        SDL_RenderFillRect(renderer, &inner);
        SDL_SetRenderDrawColor(renderer, 86, 168, 124, 255);
        SDL_FRect fill = { barX, barY, static_cast<float>(barWidth * overall), barHeight };
        SDL_RenderFillRect(renderer, &fill);

        /* Footer: "PHASE n/m   pp%" */
        char footer[64];
        std::snprintf(footer, sizeof(footer), "%d/%d  %d%%", std::min(phaseIdx + 1, phaseDen), phaseDen,
                      static_cast<int>(overall * 100.0 + 0.5));
        const float footerScale = std::max(1.5f, scale * 0.6f);
        const float footerW = MeasureText(footer, footerScale);
        DrawText(renderer, footer, (fw - footerW) * 0.5f, barY + barHeight + 1.5f * scale, footerScale,
                 { 160, 175, 195, 255 });
    });
}

#ifdef TMC_GPU_RENDERER
/* Implemented in port_gpu_renderer.cpp / port_imgui_menu.cpp. Declared here so
 * the GPU branch of RunWithProgressScreen can present an ImGui progress frame
 * on the SDL_GPU swapchain (GPU builds have no SDL_Renderer to draw into). */
extern "C" bool Port_GPU_IsActive(void);
extern "C" bool Port_GPU_PresentPrelaunchFrame(void);
extern "C" bool Port_GPU_PaintBootSplash(void);
extern "C" bool Port_ImGui_RenderExtractProgress(const char* phase, float fraction, int phase_index, int phase_total);
#endif

template <typename Task> bool RunWithProgressScreen(SDL_Window* window, ProgressSnapshot& snap, Task task) {
    /* Adopt the renderer that SDL_CreateWindowAndRenderer made at
     * launch time. Calling SDL_CreateRenderer here would fail
     * because SDL3 enforces one renderer per window, and we'd
     * silently fall through to task() with no progress UI — which
     * is exactly how the EXTRACTING ASSETS bar disappeared after
     * the atomic-create switch. Fall back to creating one only if
     * for some reason the window doesn't have a renderer yet. */
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    if (!renderer && window) {
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer) {
#ifdef TMC_GPU_RENDERER
        /* GPU build: the window is owned by the SDL_GPU swapchain and has no
         * SDL_Renderer, so the SDL_Renderer bar below can't draw. Drive an
         * ImGui progress screen on the GPU swapchain instead of extracting
         * blind — the window otherwise just sat there and read as a hang. */
        if (Port_GPU_IsActive()) {
            auto future = std::async(std::launch::async, std::forward<Task>(task));
            auto paint = [&snap]() {
                std::string phaseName;
                {
                    std::lock_guard<std::mutex> lk(snap.name_mu);
                    phaseName = snap.phase_name;
                }
                const std::size_t done = snap.done.load(std::memory_order_acquire);
                const std::size_t total = snap.total.load(std::memory_order_acquire);
                const int phaseIdx = snap.phase_index.load(std::memory_order_acquire);
                const int phaseDen = std::max(kExpectedPhaseCount, phaseIdx + 1);
                const double inPhase = (total == 0) ? 1.0 : static_cast<double>(done) / static_cast<double>(total);
                const double overall = std::clamp((static_cast<double>(phaseIdx) + std::clamp(inPhase, 0.0, 1.0)) /
                                                      static_cast<double>(phaseDen),
                                                  0.0, 1.0);
                const bool built = Port_ImGui_RenderExtractProgress(phaseName.empty() ? "preparing" : phaseName.c_str(),
                                                                    static_cast<float>(overall),
                                                                    std::min(phaseIdx + 1, phaseDen), phaseDen);
                if (built)
                    Port_GPU_PresentPrelaunchFrame();
                else
                    Port_GPU_PaintBootSplash();
            };
            while (future.wait_for(std::chrono::milliseconds(33)) != std::future_status::ready) {
                /* Drain events so the OS doesn't flag the window unresponsive;
                 * ignore QUIT so a half-written install can't happen here. */
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {}
                paint();
            }
            paint(); /* one last frame so the bar lands near 100% */
            return future.get();
        }
#endif
        return task();
    }

    auto future = std::async(std::launch::async, std::forward<Task>(task));
    while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                /* Keep extracting so the install isn't left half-written. */
            }
        }
        DrawProgressScreen(window, snap);
    }

    /* One final paint so the bar visibly reaches 100% before we
     * hand the renderer off to Port_PPU_Init. We deliberately do
     * NOT destroy the renderer here — destroying it and letting
     * Port_PPU_Init create a new one for the same window causes a
     * visible compositor flash that reads as "extractor window
     * closed, game window opened". Port_PPU_Init now adopts this
     * renderer via SDL_GetRenderer(window), and PPU shutdown owns
     * its lifetime from then on. */
    DrawProgressScreen(window, snap);
    const bool ok = future.get();
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderClipRect(renderer, nullptr);
    return ok;
}

void InstallReporterCallback(ProgressSnapshot& snap) {
    /* Snapshot writer. Runs on the extractor's worker threads under
     * the Reporter mutex, so it must be O(few stores) and never
     * block. The only allocation is the one-shot string copy when
     * the phase name changes, which happens ~9 times per run. */
    PortAssetLog::Reporter::Instance().SetProgressCallback(
        [&snap](std::string_view phase, std::size_t done, std::size_t total) {
            snap.done.store(done, std::memory_order_release);
            snap.total.store(total, std::memory_order_release);

            std::lock_guard<std::mutex> lk(snap.name_mu);
            const std::string incoming(phase);
            if (incoming != snap.last_phase_name) {
                if (!snap.last_phase_name.empty()) {
                    snap.phase_index.fetch_add(1, std::memory_order_acq_rel);
                }
                snap.last_phase_name = incoming;
                snap.phase_name = incoming;
            }
        });
}

void ClearReporterCallback() {
    PortAssetLog::Reporter::Instance().SetProgressCallback({});
}

} // namespace

extern "C" void Port_EnsureAssetsReadyWithDisplay(SDL_Window* window, const u8* rom_data, u32 rom_size) {
    const std::filesystem::path root = PreferredAssetRoot();
    const char* loadedRomPath = Port_GetLoadedRomPath();
    const std::filesystem::path rom =
        (loadedRomPath && loadedRomPath[0]) ? std::filesystem::path(loadedRomPath) : (root / "baserom.gba");
    const bool packMode = !Port_LooseAssetsRequested;

    /* Per-region cache roots: assets/<region>/ and assets_src/<region>/. gRomRegion
     * was set by Port_LoadRom() before we got here, so RegionAssetSubdir() is valid.
     * Isolating per region stops a USA/EU/JP swap from reusing or overwriting another
     * region's extracted tree — the 16 MB size fingerprint alone can't tell them apart. */
    const char* regionSub = RegionAssetSubdir();
    const std::filesystem::path runtimeRoot = root / "assets" / regionSub;
    const std::filesystem::path editableRoot = root / "assets_src" / regionSub;

    /* Step 1: warm-launch fast path. Same ROM fingerprint + pack
     * mode recorded in assets/ means current runtime tree matches the
     * actually loaded ROM. Multi-region builds must not compare JP/EU
     * runs against root/baserom.gba (usually USA), or stale USA assets
     * override JP/EU title graphics and text tables. */
    std::error_code rom_ec;
    const bool rom_on_disk = std::filesystem::exists(rom, rom_ec);
    bool up_to_date = false;
    if (rom_on_disk) {
        up_to_date = AssetExtractorApi::RuntimeUpToDate(runtimeRoot, rom, packMode);
    } else if (rom_data != nullptr && rom_size > 0) {
        up_to_date = AssetExtractorApi::RuntimeUpToDate(runtimeRoot, static_cast<std::uint64_t>(rom_size), 0, packMode);
    }
    if (up_to_date) {
        MountPaksForRoot(root);
        /* Port_LoadRom already ran Port_Load{Texts,SpritePtrs,
         * AreaTables}FromAssets before paks were mounted. Those
         * helpers do binary lookups via LoadBinaryFileCached — for
         * sprite animations in particular — and fail (returning
         * FALSE) when the relevant .bin files only live inside
         * paks. The first launch happened to find the legacy loose
         * tree from build/<ver>/assets/ so the calls succeeded;
         * once the first extraction wiped those subdirs every
         * subsequent warm launch fell back to ROM-derived sprite
         * pointers, producing scrambled NPC tiles + animation
         * regressions. Re-run the same fixup the extract path
         * does so warm and cold launches end in the same state. */
        Port_AssetLoader_Reload();
        Port_LoadTextsFromAssets();
        Port_LoadSpritePtrsFromAssets();
        Port_LoadAreaTablesFromAssets();
        return;
    }

    /* Step 2: spawn the extractor with a real progress bar. */
    ProgressSnapshot snap;
    InstallReporterCallback(snap);

    AssetExtractorApi::Options opt;
    opt.rom_path = rom;
    if (rom_data != nullptr && rom_size > 0) {
        /* Phase 6: reuse the engine's already-loaded ROM buffer so
         * we don't pay for a second 16 MB read off disk. The
         * extractor copies bytes into its own vector internally. */
        opt.rom_buffer = std::span<const uint8_t>(rom_data, rom_size);
    }
    opt.editable_root = editableRoot;
    opt.runtime_root = runtimeRoot;
    opt.runtime_only = true; // engine doesn't need the editable JSON tree
    opt.pack_runtime = packMode;
    opt.force = false;
    opt.verbose = false;

    std::string err;
    const bool ok = RunWithProgressScreen(window, snap, [&] { return AssetExtractorApi::ExtractAssets(opt, &err); });

    ClearReporterCallback();

    if (ok) {
        MountPaksForRoot(root);
        /* Port_LoadRom (which already ran at this point) probed the
         * asset loader before assets/ existed. Re-trigger the scan
         * now that gfx_groups.json / palette_groups.json / area JSONs
         * are on disk so the engine sees them on the next lookup. */
        Port_AssetLoader_Reload();
        Port_LoadTextsFromAssets();
        Port_LoadSpritePtrsFromAssets();
        Port_LoadAreaTablesFromAssets();
        return;
    }

    const std::string message = err.empty() ? std::string("Asset extraction failed.") : err;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Asset extraction failed", message.c_str(), window);
}

extern "C" void Port_PaintBootSplash(SDL_Window* window, const char* message) {
    /* Reuse whichever renderer is already attached to the window (the
     * bootstrap progress UI's, or PPU's) via PaintFrame; on warm launch
     * none exists yet so PaintFrame creates one here, so the window stops
     * being a blank black rectangle for the ~1.4 s it would otherwise
     * take to reach Port_PPU_Init. PPU later adopts the same renderer via
     * SDL_GetRenderer(window). */
    const std::string title = "PROJECT PICORI";
    const std::string text = message ? std::string(message) : std::string("STARTING");

    /* Project Picori boot splash — matches the green-themed ImGui
     * chrome so the boot frame flows visually into the F8 menu without
     * a colour-tone flip. RGB (15, 18, 18) is the bgBase shade from
     * port_imgui_menu.cpp scaled into 8-bit space. */
    PaintFrame(window, SDL_Color{ 15, 18, 18, 255 }, [&](SDL_Renderer* renderer, float fw, float fh, float scale) {
        /* Title above the status line, status line below. The title is the
         * project name (stable across launches); the status line is
         * "LOADING" / "STARTING" / etc. passed by main.c. */
        const float titleScale = scale * 1.6f;
        DrawCenteredTextBlock(renderer, fw, fh,
                              {
                                  { title, titleScale, SDL_Color{ 235, 240, 245, 255 }, 16.0f },
                                  { text, scale, SDL_Color{ 235, 240, 245, 255 }, 0.0f },
                              });
    });
}
