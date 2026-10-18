#pragma once

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace PortAssetPipeline {

/* Version of the extracted-asset output (schema + content). MUST be bumped
 * whenever the extractor's output format or content changes, so caches
 * stamped by an older binary are treated as stale. Checked by both the
 * JSON-tree rebuild path (RuntimeAssetsNeedRebuild) and the warm-launch
 * stamp check (AssetExtractorApi::RuntimeUpToDate). */
inline constexpr int kBuildStateVersion = 3; /* v3: EU paletteGroups 208 -> 207 */

/* Read the whole file at `path` into `out`, resized to its size.
 * Returns true on success; a 0-byte file succeeds with `out` empty. On
 * any I/O failure `out` is cleared and false is returned. A single bulk
 * ifstream read keeps cold-cache reads fast on slow storage (SD/eMMC/HDD)
 * where istreambuf_iterator's per-byte virtual calls stall the kernel's
 * read-ahead. The std::filesystem::path argument makes Windows open via
 * the native wide path, so non-ASCII path components work with no manual
 * _wfopen/code-page branch. */
inline bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    const auto fsize = std::filesystem::file_size(path, ec);
    if (ec) {
        out.clear();
        return false;
    }
    if (fsize > (1ull << 30)) { /* 1 GiB sanity cap */
        out.clear();
        return false;
    }
    out.resize(static_cast<std::size_t>(fsize));
    if (fsize == 0) {
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.clear();
        return false;
    }
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(fsize));
    if (static_cast<std::uintmax_t>(in.gcount()) != fsize) {
        out.clear();
        return false;
    }
    return true;
}

std::vector<uint8_t> DecodeGbaTiledGfx(std::span<const uint8_t> gfxData, uint16_t width, uint16_t height, uint8_t bpp);
std::vector<uint8_t> EncodeGbaTiledGfx(const std::vector<uint8_t>& pixels, uint16_t width, uint16_t height,
                                       uint8_t bpp);

bool WriteIndexedBmp(const std::filesystem::path& outputPath, std::span<const uint8_t> pixels, uint16_t width,
                     uint16_t height, uint8_t bpp, std::string* error = nullptr);
bool ReadEditableBmp(const std::filesystem::path& inputPath, std::vector<uint8_t>& pixels, uint16_t expectedWidth,
                     uint16_t expectedHeight, uint8_t bpp, std::string* error = nullptr);

bool WritePaletteJson(const std::filesystem::path& outputPath, std::span<const uint8_t> paletteData,
                      std::string* error = nullptr);
bool ReadPaletteJson(const std::filesystem::path& inputPath, std::vector<uint8_t>& paletteData,
                     std::string* error = nullptr);
bool DecodeTmcText(const uint8_t* textData, size_t maxBytes, std::string& text, size_t* consumedBytes = nullptr,
                   std::string* error = nullptr);
bool EncodeTmcText(const std::string& text, std::vector<uint8_t>& textData, std::string* error = nullptr);
bool WriteEditableText(const std::filesystem::path& outputPath, const std::vector<uint8_t>& textData,
                       std::string* error = nullptr);
bool ReadEditableText(const std::filesystem::path& inputPath, std::vector<uint8_t>& textData,
                      std::string* error = nullptr);
bool WriteEditableAnimation(const std::filesystem::path& outputPath, std::span<const uint8_t> animationData,
                            std::string* error = nullptr);
bool ReadEditableAnimation(const std::filesystem::path& inputPath, std::vector<uint8_t>& animationData,
                           std::string* error = nullptr);
bool WriteEditableSpriteFrames(const std::filesystem::path& outputPath, std::span<const uint8_t> frameData,
                               std::string* error = nullptr);
bool ReadEditableSpriteFrames(const std::filesystem::path& inputPath, std::vector<uint8_t>& frameData,
                              std::string* error = nullptr);

bool RuntimeAssetsNeedRebuild(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reason = nullptr);
bool BuildRuntimeAssets(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                        std::string* error = nullptr);
bool EnsureRuntimeAssetsBuilt(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                              std::string* reasonOrError = nullptr);

/// Write the source-state file (`.asset_build_state.json`) into outputRoot
/// based on the current contents of sourceRoot. Used by the extractor once
/// it has dual-written both trees in a single pass, so that subsequent runs
/// can detect the runtime tree as up-to-date and short-circuit.
bool WriteBuildStateFile(const std::filesystem::path& sourceRoot, const std::filesystem::path& outputRoot,
                         std::string* error = nullptr);

} // namespace PortAssetPipeline
