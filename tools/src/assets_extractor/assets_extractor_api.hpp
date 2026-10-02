#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

/* Reusable, in-process entry points for the asset extractor. The
 * standalone `asset_extractor` binary is now a thin wrapper around
 * AssetExtractorApi::ExtractAssets, and tmc_pc links the same
 * implementation directly so it can extract assets at startup
 * without shelling out to a separate executable. */
namespace AssetExtractorApi {

struct Options {
    /* Path to baserom.gba on disk. Ignored if rom_buffer is non-empty. */
    std::filesystem::path rom_path;

    /* In-memory ROM bytes. When non-empty, the extractor copies these
     * into its global Rom buffer instead of reading rom_path. tmc_pc
     * uses this so the engine and extractor share a single 16 MB read. */
    std::span<const uint8_t> rom_buffer;

    /* Output trees. editable_root is assets_src/ (human readable);
     * runtime_root is assets/ (engine loadable, optionally packed
     * into per-category .pak archives). */
    std::filesystem::path editable_root;
    std::filesystem::path runtime_root;

    /* Skip writing the assets_src/ tree. Saves ~150 ms but breaks
     * round-tripping through the editable JSONs. */
    bool runtime_only = false;

    /* Pack runtime assets into .pak archives instead of writing
     * thousands of loose files. Defaults to true inside tmc_pc;
     * the standalone binary leaves this false unless --pak is
     * passed for backwards compatibility. */
    bool pack_runtime = true;

    /* Re-extract even if .asset_build_state.json says assets are
     * already current for this ROM fingerprint + pack_format. */
    bool force = false;

    /* Forwarded to PortAssetLog::Reporter::SetVerbose. */
    bool verbose = false;

    /* Phase-complete hook fired from worker threads. tmc_pc uses
     * this to flip per-phase ready flags so the title screen can
     * render before the entire extraction is done. May be null. 
     * The string view is the same name passed to 
     * PortAssetLog::Reporter::BeginPhase. */
    std::function<void(std::string_view phase)> phase_done;
};

/* Returns true on success. On failure, *error (if non-null) receives
 * a human-readable description suitable for an SDL message box. */
bool ExtractAssets(const Options& opt, std::string* error);

/* Fast path the engine can call before showing the progress bar:
 * peek at runtime_root/.asset_build_state.json and compare the
 * recorded ROM fingerprint + pack format against the on-disk ROM.
 * Returns true if the runtime tree can be used as-is. */
bool RuntimeUpToDate(const std::filesystem::path& runtime_root,
                     const std::filesystem::path& rom_path,
                     bool pack_runtime);

/* Buffer-driven warm-launch check. Used by tmc_pc when baserom.gba
 * is not next to the binary but Port_LoadRom resolved it elsewhere
 * (developer-tree fallbacks, cwd-relative paths) — the in-memory
 * buffer is the only fingerprint source we have, so the recorded
 * mtime collapses to 0 on both sides. Returns true iff the state
 * file's rom_size and pack_format both match. */
bool RuntimeUpToDate(const std::filesystem::path& runtime_root,
                     std::uint64_t rom_size,
                     std::int64_t rom_mtime,
                     bool pack_runtime);

/* Resolve the directory containing the running executable, preferring
 * /proc/self/exe (Linux) or GetModuleFileNameW (Windows) over
 * argv[0]. Used by both binaries to locate baserom.gba and the
 * assets/ tree relative to the binary instead of the cwd. */
std::filesystem::path FindExecutableDirectory(const std::filesystem::path& argv0);

/* Read-only views of the bytes the most recent ExtractAssets() call
 * processed. Backed by the extractor's internal Rom span, which
 * either aliases the engine's gRomData (in-process path) or owns a
 * disk-loaded copy (standalone path). Empty before the first
 * ExtractAssets call. Provided so the standalone binary can mirror
 * the bytes into its gRomData / gRomSize globals without including
 * the full assets_extractor.hpp (which defines a forest of non-inline
 * helpers that would multiply-define if pulled into more than one
 * TU). */
std::span<const uint8_t> LoadedRomBytes();

}  // namespace AssetExtractorApi
