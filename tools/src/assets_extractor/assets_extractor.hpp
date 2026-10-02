#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <optional>
#include <functional>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include "port_asset_pipeline.hpp"
#include "port_asset_log.hpp"
#include "port_asset_pak.hpp"

extern "C" {
#define this this_
#include "port_asset_index.h"
#undef this
}

struct Config
{
    uint32_t gfxGroupsTableOffset;
    uint32_t gfxGroupsTableLength;
    uint32_t paletteGroupsTableOffset;
    uint32_t paletteGroupsTableLength;
    uint32_t globalGfxAndPalettesOffset;
    uint32_t mapDataOffset;
    uint32_t areaRoomHeadersTableOffset;
    uint32_t areaTileSetsTableOffset;
    uint32_t areaRoomMapsTableOffset;
    uint32_t areaTableTableOffset;
    uint32_t areaTilesTableOffset;
    uint32_t spritePtrsTableOffset;
    uint32_t spritePtrsCount;
    uint32_t translationsTableOffset;
    uint8_t language;
    std::string variant;
    std::filesystem::path outputRoot;         // assets_src/ (editable, human-friendly)
    std::filesystem::path runtimeOutputRoot;  // assets/    (engine-loadable, raw bytes)

    /* When packRuntime is true, runtime-side binary writes (pal/bin
     * blobs, gfx blobs, animations, frames, room data, and the final
     * passthrough sweep) are routed into pakBuilders[routeFor(path)]
     * instead of being written as loose files. Aggregate JSONs
     * (gfx_groups.json, palettes.json, sprite_ptrs.json, etc.) are
     * still written loose so the engine can parse them at startup
     * without paying mmap/Lookup cost on tiny files. */
    bool packRuntime{false};
    /* Pointer rather than embedded value so Config stays trivially
     * copyable for the existing call sites and so the lifetime is
     * controlled by the caller (typically main, which constructs a
     * std::array<PakBuilder, kPakCategoryCount> on the stack and
     * writes them out after extract_assets returns). */
    void* pakBuilders{nullptr};  // points to PakBuilderArray; see below
};

/* Per-category route assignment for a runtime-relative path. Order
 * matches kPakNames below; keep them in sync. */
enum class PakCategory : uint32_t {
    Gfx = 0,
    Palettes,
    Animations,
    Sprites,
    Tilemaps,
    Maps,
    RoomProps,
    Data,
    Misc,
    Count,
};

constexpr std::size_t kPakCategoryCount = static_cast<std::size_t>(PakCategory::Count);

inline const std::array<const char*, kPakCategoryCount>& pak_category_names()
{
    static const std::array<const char*, kPakCategoryCount> names = {
        "gfx.pak",        "palettes.pak", "animations.pak", "sprites.pak",  "tilemaps.pak",
        "maps.pak",       "room_props.pak", "data.pak",     "misc.pak",
    };
    return names;
}

inline PakCategory pak_route_for(const std::string& relative_path)
{
    /* Order matters: longer prefixes first so generated/animations/
     * doesn't get caught by a shorter generated/ fallback that we
     * don't have but might add later. */
    auto starts_with = [&](const char* prefix) { return relative_path.rfind(prefix, 0) == 0; };
    if (starts_with("gfx/")) return PakCategory::Gfx;
    if (starts_with("palettes/")) return PakCategory::Palettes;
    if (starts_with("animations/") || starts_with("generated/animations/")) return PakCategory::Animations;
    if (starts_with("sprites/") || starts_with("generated/sprites/")) return PakCategory::Sprites;
    if (starts_with("tilemaps/")) return PakCategory::Tilemaps;
    if (starts_with("maps/")) return PakCategory::Maps;
    if (starts_with("room_properties/") || starts_with("generated/mapdata/")) return PakCategory::RoomProps;
    if (starts_with("data_") || starts_with("data/")) return PakCategory::Data;
    return PakCategory::Misc;
}

using PakBuilderArray = std::array<PortAssetPak::PakBuilder, kPakCategoryCount>;


/* `inline` so the header can be included by both the standalone
 * extractor binary and the in-process API used by tmc_pc without
 * triggering a multiple-definition link error.
 *
 * Two-tier design:
 *   - RomOwned holds bytes when we read them off disk in
 *     load_rom(path). Empty otherwise.
 *   - Rom is a non-owning std::span that views either RomOwned (disk
 *     path) or the caller's buffer (in-process path, where tmc_pc has
 *     already mapped baserom.gba into gRomData and we don't want to
 *     pay for a second 16 MB allocation OR — more importantly —
 *     orphan the engine's existing pointer tables that were resolved
 *     against gRomData).
 *
 * Every read site inside the extractor uses Rom (size/data/[]/begin),
 * so swapping the backing storage is transparent to extract_bytes,
 * read_u32, etc. */
inline std::vector<uint8_t> RomOwned;
inline std::span<const uint8_t> Rom;

inline bool load_rom(const std::filesystem::path& rom_path)
{
    std::ifstream file(rom_path, std::ios::binary);
    if (!file) {
        return false;
    }
    RomOwned.assign(std::istreambuf_iterator<char>(file), {});
    Rom = std::span<const uint8_t>(RomOwned.data(), RomOwned.size());
    return !RomOwned.empty();
}

/* Alias the caller's buffer instead of copying. Used when the engine
 * has already mapped baserom.gba and wants to hand the buffer to the
 * extractor without (a) paying for a second 16 MB read and (b)
 * silently orphaning the original gRomData allocation that
 * Port_LoadRom resolved every ROM-derived pointer table against. The
 * caller must keep `bytes` alive for the duration of the extraction
 * call (tmc_pc holds gRomData for the entire process lifetime). */
inline bool load_rom_from_buffer(std::span<const uint8_t> bytes)
{
    if (bytes.empty()) {
        return false;
    }
    /* Drop any prior owned bytes so we don't keep 16 MB live forever
     * after a switch from disk-load to buffer-load (does not affect
     * tmc_pc, which only ever calls load_rom_from_buffer). */
    RomOwned.clear();
    RomOwned.shrink_to_fit();
    Rom = bytes;
    return true;
}

/* Zero-copy view of the ROM. The ROM buffer is loaded once into the
 * static `Rom` vector at startup and lives for the duration of the
 * extractor process; every span returned here points into that buffer
 * directly. Callers must not retain spans past Rom destruction (which
 * happens at process exit, so in practice never matters). Returns an
 * empty span on out-of-range reads, matching the previous semantics. */
inline std::span<const uint8_t> extract_bytes(uint32_t offset, uint32_t length)
{
    if (length == 0 || offset > Rom.size() || offset + length > Rom.size()) {
        return {};
    }
    return std::span<const uint8_t>(Rom.data() + offset, length);
}

inline uint32_t to_gba_address(uint32_t offset)
{
    return 0x08000000 + offset;
}

inline uint32_t to_rom_address(uint32_t offset)
{
    return offset - 0x08000000;
}

inline uint32_t read_pointer(uint32_t offset)
{
    /* Guard against integer overflow: `offset + 4` wraps for offsets near
     * UINT32_MAX (e.g. a garbage 0xFFFFFFFD computed from a wrong region text
     * root), which previously bypassed this check and read out of bounds. */
    if (offset > Rom.size() || Rom.size() - offset < 4) {
        return 0;
    }
    return Rom[offset] | (Rom[offset + 1] << 8) | (Rom[offset + 2] << 16) | (Rom[offset + 3] << 24);
}

inline bool lz77_uncompress(std::span<const uint8_t> compressed_data, std::vector<uint8_t>& uncompressed_data)
{
    if (compressed_data.size() < 4 || compressed_data[0] != 0x10) {
        return false;
    }

    const uint32_t decompressed_size =
        compressed_data[1] | (compressed_data[2] << 8) | (compressed_data[3] << 16);
    uncompressed_data.clear();
    uncompressed_data.reserve(decompressed_size);

    size_t src_pos = 4;
    while (src_pos < compressed_data.size() && uncompressed_data.size() < decompressed_size) {
        uint8_t flags = compressed_data[src_pos++];
        for (int i = 0; i < 8 && uncompressed_data.size() < decompressed_size; ++i) {
            if ((flags & 0x80) == 0) {
                if (src_pos >= compressed_data.size()) {
                    return false;
                }
                uncompressed_data.push_back(compressed_data[src_pos++]);
            } else {
                if (src_pos + 1 >= compressed_data.size()) {
                    return false;
                }

                const uint8_t first = compressed_data[src_pos++];
                const uint8_t second = compressed_data[src_pos++];
                size_t block_size = (first >> 4) + 3;
                const size_t block_distance = (((first & 0xF) << 8) | second) + 1;

                if (block_distance > uncompressed_data.size()) {
                    return false;
                }

                size_t block_pos = uncompressed_data.size() - block_distance;
                while (block_size-- > 0 && uncompressed_data.size() < decompressed_size) {
                    uncompressed_data.push_back(uncompressed_data[block_pos++]);
                }
            }
            flags <<= 1;
        }
    }

    return uncompressed_data.size() == decompressed_size;
}

/*
 * Exact compressed byte length of a BIOS LZ77 (type 0x10) stream at `rom_offset`
 * (4-byte header + tokens). Mirrors lz77_uncompress's token grammar but only
 * counts consumed input. Compressed map-asset blobs have no explicit length, so
 * extract_map_definition_sequence used to size them by the next-asset boundary
 * gap — which is derived from the USA-baked EmbeddedAssetIndex and so mis-sizes
 * (usually truncates) the blobs on EU/JP ROMs, whose offsets miss that index.
 * A truncated compressed blob makes the runtime LZ77 decoder stop early, leaving
 * the tail tiles of a tileset garbled. Sizing to the real stream end fixes that
 * region-independently. Returns 0 (let the caller fall back) on a malformed or
 * non-0x10 stream, or if it runs off the ROM.
 */
inline uint32_t lz77_compressed_size(uint32_t rom_offset)
{
    if (rom_offset + 4 > Rom.size() || Rom[rom_offset] != 0x10) {
        return 0;
    }
    const uint32_t decompressed_size = Rom[rom_offset + 1] | (Rom[rom_offset + 2] << 8) | (Rom[rom_offset + 3] << 16);
    uint32_t p = rom_offset + 4;
    uint32_t produced = 0;
    while (produced < decompressed_size) {
        if (p >= Rom.size()) {
            return 0;
        }
        uint8_t flags = Rom[p++];
        for (int i = 0; i < 8 && produced < decompressed_size; ++i) {
            if ((flags & 0x80) == 0) {
                if (p >= Rom.size()) {
                    return 0;
                }
                ++p;
                ++produced;
            } else {
                if (p + 1 >= Rom.size()) {
                    return 0;
                }
                const uint8_t first = Rom[p];
                p += 2;
                produced += (first >> 4) + 3;
            }
            flags <<= 1;
        }
    }
    return p - rom_offset;
}

inline bool json_asset_matches_variant(const nlohmann::json& asset, const std::string& variant)
{
    if (!asset.contains("variants")) {
        return true;
    }

    for (const auto& item : asset["variants"]) {
        if (item.get<std::string>() == variant) {
            return true;
        }
    }
    return false;
}

inline bool string_in_list(const std::string& value, const std::vector<std::string>& values)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

struct AssetRecord
{
    std::filesystem::path source_path;
    std::string type;
    uint32_t rom_start;
    uint32_t size;
    bool compressed;
};

inline bool path_has_suffix(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string text_category_name(uint32_t category)
{
    static const char* const kTextCategoryNames[] = {
        "TEXT_SAVE",           "TEXT_CREDITS",        "TEXT_NAMES",          "TEXT_NEWSLETTER",
        "TEXT_ITEMS",          "TEXT_ITEM_GET",       "TEXT_LOCATIONS",      "TEXT_WINDCRESTS",
        "TEXT_FIGURINE_NAMES", "TEXT_FIGURINE_DESCRIPTIONS",
        "TEXT_EMPTY",          "TEXT_EZLO",           "TEXT_EZLO2",          "TEXT_MINISH",
        "TEXT_KINSTONE",       "TEXT_PICORI",         "TEXT_PROLOGUE",       "TEXT_FINDING_EZLO",
        "TEXT_MINISH2",        "TEXT_VAATI",          "TEXT_GUSTAF",         "TEXT_PANEL_TUTORIAL",
        "TEXT_VAATI2",         "TEXT_GUSTAF2",        "TEXT_EMPTY2",         "TEXT_EMPTY3",
        "TEXT_FARMERS",        "TEXT_CARPENTERS",     "TEXT_EZLO_ELEMENTS_DONE",
        "TEXT_GORONS",         "TEXT_EMPTY4",         "TEXT_BELARI",         "TEXT_LON_LON",
        "TEXT_FOREST_MINISH",  "TEXT_EZLO_PORTAL",    "TEXT_PERCY",          "TEXT_BREAK_VAATI_CURSE",
        "TEXT_FESTIVAL",       "TEXT_EMPTY5",         "TEXT_TREASURE_GUARDIAN",
        "TEXT_DAMPE",          "TEXT_BUSINESS_SCRUB", "TEXT_EMPTY6",         "TEXT_PICOLYTE",
        "TEXT_STOCKWELL",      "TEXT_SYRUP",          "TEXT_ITEM_PRICES",    "TEXT_WIND_TRIBE",
        "TEXT_ANJU",           "TEXT_GORMAN_ORACLES", "TEXT_SMITH",          "TEXT_PHONOGRAPH",
        "TEXT_TOWN",           "TEXT_TOWN2",          "TEXT_TOWN3",          "TEXT_TOWN4",
        "TEXT_TOWN5",          "TEXT_TOWN6",          "TEXT_TOWN7",          "TEXT_MILK",
        "TEXT_BAKERY",         "TEXT_SIMON",          "TEXT_SCHOOL",         "TEXT_TINGLE",
        "TEXT_POST",           "TEXT_MUTOH",          "TEXT_BURLOV",         "TEXT_CARLOV",
        "TEXT_REM",            "TEXT_HAPPY_HEARTH",   "TEXT_BLADE_MASTERS",  "TEXT_ANSWER_HOUSE",
        "TEXT_UNK_WISE",       "TEXT_LIBRARY",        "TEXT_TOWN_MINISH1",   "TEXT_TOWN_MINISH2",
        "TEXT_HAGEN",          "TEXT_DR_LEFT",        "TEXT_TOWN8",          "TEXT_CAFE",
    };

    if (category < (sizeof(kTextCategoryNames) / sizeof(kTextCategoryNames[0]))) {
        return kTextCategoryNames[category];
    }

    return "TEXT_" + std::to_string(category);
}

inline std::string text_language_name(uint32_t language)
{
    return "language_" + std::to_string(language);
}

inline std::string text_language_display_name(const Config& config, uint32_t language)
{
    if (config.variant == "USA") {
        return "USA";
    }

    if (config.variant == "EU") {
        switch (language) {
            case 0:
            case 1:
            case 2:
                return "English";
            case 3:
                return "French";
            case 4:
                return "German";
            case 5:
                return "Spanish";
            case 6:
                return "Italian";
            default:
                break;
        }
    }

    return text_language_name(language);
}

inline std::string hex_byte_string(uint32_t value)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (value & 0xFF);
    return stream.str();
}

inline std::string text_message_preview(const std::vector<uint8_t>& bytes)
{
    std::string preview;
    for (uint8_t byte : bytes) {
        if (byte == 0) {
            break;
        }

        if (byte >= 0x20 && byte <= 0x7E) {
            preview.push_back(static_cast<char>(byte));
        } else if (byte == '\n') {
            preview += "\\n";
        } else if (byte == '\r') {
            preview += "\\r";
        } else if (byte == '\t') {
            preview += "\\t";
        } else {
            preview += "<" + hex_byte_string(byte) + ">";
        }
    }
    return preview;
}

inline std::vector<uint8_t> extract_text_message_bytes(uint32_t offset)
{
    std::vector<uint8_t> bytes;
    if (offset >= Rom.size()) {
        return bytes;
    }

    for (uint32_t at = offset; at < Rom.size(); ++at) {
        bytes.push_back(Rom[at]);
        if (Rom[at] == 0) {
            break;
        }
    }

    return bytes;
}

inline std::vector<AssetRecord> collect_embedded_asset_records(const Config& config,
                                                               const std::function<bool(const std::string&)>& predicate)
{
    std::vector<AssetRecord> records;
    const EmbeddedAssetEntry* asset_index = EmbeddedAssetIndex_Get();
    const u32 asset_count = EmbeddedAssetIndex_Count();

    for (u32 i = 0; i < asset_count; ++i) {
        const EmbeddedAssetEntry& entry = asset_index[i];
        const std::string path = entry.path;
        if (!predicate(path)) {
            continue;
        }

        const std::filesystem::path source_path = path;
        records.push_back({source_path, std::string(), entry.offset, entry.size, source_path.extension() == ".lz"});
    }

    return records;
}

inline std::string json_path_string(const std::filesystem::path& path)
{
    return path.generic_string();
}

struct GfxExtractionResult
{
    std::filesystem::path relative_bmp_output;   // assets_src/ path (editable .bmp)
    std::filesystem::path relative_runtime_path; // assets/ path (raw .bin)
    uint32_t output_size;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
};

// Thread-safe set of relative output paths that the specialised passes have
// already written. The final sweep consults these to avoid double-writing
// the same path (which is what the old code's per-entry
// std::filesystem::exists() check did, just much faster).
//
// Per-tree because the editable and runtime trees diverge: a pass can write
// to one tree and not the other (e.g. extract_all_palettes writes
// assets_src/palettes/foo.json + assets/palettes/foo.pal — the asset-index
// entry path "palettes/foo.bin" is in NEITHER set so the sweep still writes
// the raw .bin to assets_src/, matching the old extractor's tree shape).
struct ConsumedPaths {
    std::mutex mu;
    std::unordered_set<std::string> set;

    void Mark(const std::filesystem::path& relative_path) {
        std::lock_guard<std::mutex> lk(mu);
        set.insert(relative_path.generic_string());
    }

    bool Contains(const std::filesystem::path& relative_path) {
        std::lock_guard<std::mutex> lk(mu);
        return set.count(relative_path.generic_string()) != 0;
    }
};

inline ConsumedPaths& g_consumed_editable() {
    static ConsumedPaths instance;
    return instance;
}

inline ConsumedPaths& g_consumed_runtime() {
    static ConsumedPaths instance;
    return instance;
}

// Mirrors the kDirectories list in PortAssetPipeline::CopyRuntimePassthroughAssets.
// Only entries under one of these prefixes get a runtime-tree (assets/) copy
// during the final sweep — everything else is editable-tree only, matching
// the old extractor + pipeline behaviour exactly.
inline bool is_runtime_passthrough_path(const std::string& relative_path)
{
    /* Keep this list in lockstep with kDirectories in
     * PortAssetPipeline::CopyRuntimePassthroughAssets — anything else lives
     * editable-only and must not appear in assets/. */
    static const char* const kPrefixes[] = {
        "tilemaps/", "maps/", "assets/", "animations/", "sprites/",
        "room_properties/", "generated/",
        "data_08000360/", "data_08000F54/", "data_080029B4/", "data_08007DF4/",
        "data_080B2A70/", "data_080B4410/", "data_080B7B74/",
        "data_080D5360/", "data_080FC8A4/", "data_08108E6C/", "data_0811BE38/",
        "data_08127280/",
    };
    for (const char* p : kPrefixes) {
        const std::size_t n = std::strlen(p);
        if (relative_path.compare(0, n, p) == 0) {
            return true;
        }
    }
    return false;
}

/* Bypass std::ofstream's iostream sync layer — fwrite is ~30% faster
 * on the small-file extraction workload (thousands of <16 KiB
 * payloads), and on slow storage that adds up to seconds-of-extraction
 * difference per run.
 *
 * Windows note: path.string() returns the OS *narrow* code page on
 * Windows (CP1252-ish), NOT UTF-8 — so users with non-ASCII chars
 * anywhere in their %USERPROFILE% path (Cyrillic / CJK / accented
 * usernames) silently fail fopen(). Route through _wfopen with the
 * native wide string instead. */
inline bool write_binary_file_fwrite(const std::filesystem::path& output_path,
                                      const uint8_t* data, std::size_t size)
{
    PortAssetLog::EnsureDir(output_path.parent_path());
    FILE* fp = nullptr;
#ifdef _WIN32
    fp = _wfopen(output_path.native().c_str(), L"wb");
#else
    fp = std::fopen(output_path.string().c_str(), "wb");
#endif
    if (!fp) return false;
    bool ok = true;
    if (size > 0 && data != nullptr) {
        if (std::fwrite(data, 1, size, fp) != size) ok = false;
    }
    std::fclose(fp);
    return ok;
}

inline bool write_binary_file(const std::filesystem::path& output_path, const std::vector<uint8_t>& data)
{
    return write_binary_file_fwrite(output_path, data.data(), data.size());
}

inline bool write_binary_file(const std::filesystem::path& output_path, const uint8_t* data, std::size_t size)
{
    return write_binary_file_fwrite(output_path, data, size);
}

/* Single-shot text dump with a fat (256 KiB) buffer behind it. The default
 * std::ofstream buffer is BUFSIZ (≈8 KiB on glibc, 4 KiB on MSVC), which
 * means a multi-megabyte texts.json or sprite_ptrs.json turns into hundreds
 * of write() syscalls. A 256 KiB buffer collapses those into a handful of
 * larger writes, ~2-4× faster on every platform and dramatically faster on
 * Windows where the per-call overhead is highest. */
inline bool write_text_buffered(const std::filesystem::path& output_path, const std::string& contents)
{
    PortAssetLog::EnsureDir(output_path.parent_path());
    static constexpr std::streamsize kBuf = 256 * 1024;
    /* CRITICAL: declare the backing buffer BEFORE the ofstream so it
     * outlives the stream's destructor flush (locals destroy in reverse
     * declaration order). Earlier version flipped this and produced a
     * use-after-free that silently corrupted the trailing ~few KiB of
     * every aggregate JSON. */
    std::vector<char> buf(static_cast<size_t>(kBuf));
    std::ofstream f;
    f.rdbuf()->pubsetbuf(buf.data(), kBuf);
    f.open(output_path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(f);
}

/* Mirror an already-written editable file into the runtime tree. Tries
 * std::filesystem::create_hard_link first (one inode write, no data copy —
 * essentially free on every modern FS we target: ext4/btrfs/xfs/apfs/NTFS).
 * Falls back to a real write if hardlink fails (cross-filesystem, sandbox
 * restrictions, ReFS-without-CreateHardLink, etc.).
 *
 * Both paths must be on the same filesystem for the link to succeed; in
 * our case assets/ and assets_src/ live as siblings under build/pc/, so
 * this holds. */
inline bool mirror_to_runtime(const std::filesystem::path& editable_path,
                              const std::filesystem::path& runtime_path,
                              const uint8_t* data, std::size_t size)
{
    PortAssetLog::EnsureDir(runtime_path.parent_path());
    std::error_code ec;
    /* Hard link can't replace an existing target; remove first.
     * remove() is a no-op + benign error if the file doesn't exist. */
    std::filesystem::remove(runtime_path, ec);
    ec.clear();
    std::filesystem::create_hard_link(editable_path, runtime_path, ec);
    if (!ec) {
        return true;
    }
    return write_binary_file(runtime_path, data, size);
}

/* Single chokepoint for runtime-side binary writes. When packRuntime
 * is enabled, the bytes are forwarded to the appropriate PakBuilder
 * (selected by pak_route_for); otherwise we fall through to the
 * existing mirror_to_runtime hardlink-or-copy path. The relative_path
 * is the path under assets/ (e.g. "gfx/foo.bin"). */
inline bool write_runtime_asset(const Config& config,
                                const std::filesystem::path& editable_path,
                                const std::filesystem::path& relative_path,
                                const uint8_t* data, std::size_t size)
{
    if (config.runtimeOutputRoot.empty()) {
        return false;
    }
    if (config.packRuntime && config.pakBuilders != nullptr) {
        auto* builders = static_cast<PakBuilderArray*>(config.pakBuilders);
        const std::string rel = relative_path.generic_string();
        const auto idx = static_cast<std::size_t>(pak_route_for(rel));
        (*builders)[idx].Add(rel, data, size);
        return true;
    }
    if (!editable_path.empty()) {
        return mirror_to_runtime(editable_path, config.runtimeOutputRoot / relative_path, data, size);
    }
    return write_binary_file(config.runtimeOutputRoot / relative_path, data, size);
}

inline std::vector<uint8_t> gba_palette_to_rgba(const std::vector<uint8_t>& palette_data)
{
    std::vector<uint8_t> rgba;
    rgba.reserve((palette_data.size() / 2) * 4);

    for (size_t i = 0; i + 1 < palette_data.size(); i += 2) {
        const uint16_t color = palette_data[i] | (palette_data[i + 1] << 8);
        const uint8_t r = static_cast<uint8_t>(((color >> 0) & 0x1F) * 255 / 31);
        const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x1F) * 255 / 31);
        const uint8_t b = static_cast<uint8_t>(((color >> 10) & 0x1F) * 255 / 31);
        rgba.push_back(r);
        rgba.push_back(g);
        rgba.push_back(b);
        rgba.push_back(255);
    }

    return rgba;
}

inline std::vector<uint32_t> extract_gfx_group_addresses(uint32_t gfxGroupsTableOffset, uint32_t gfxGroupsTableLength)
{
    std::vector<uint32_t> gfx_group_addresses;
    auto& reporter = PortAssetLog::Reporter::Instance();
    for (uint32_t i = 1; i < gfxGroupsTableLength; i += 1) {
        uint32_t address = to_rom_address(read_pointer(gfxGroupsTableOffset + i*4));
        if (address != 0) {
            gfx_group_addresses.push_back(address);
            if (reporter.Verbose()) {
                reporter.Note(fmt::format("gfx group {}: 0x{:X}", i, address));
            }
        } else if (reporter.Verbose()) {
            reporter.Warn(fmt::format("null pointer at gfx group index {}", i));
        }
    }
    return gfx_group_addresses;
}

inline std::vector<uint32_t> extract_palette_group_addresses(uint32_t paletteGroupsTableOffset, uint32_t paletteGroupsTableLength)
{
    std::vector<uint32_t> palette_group_addresses;
    auto& reporter = PortAssetLog::Reporter::Instance();
    for (uint32_t i = 1; i < paletteGroupsTableLength; i += 1) {
        const uint32_t raw_pointer = read_pointer(paletteGroupsTableOffset + i * 4);
        if (raw_pointer == 0) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("null pointer at palette group index {}", i));
            }
            continue;
        }

        palette_group_addresses.push_back(to_rom_address(raw_pointer));
    }
    return palette_group_addresses;
}

struct PaletteGroupElement
{
    uint16_t paletteId;
    uint8_t destPaletteNum;
    uint8_t numPalettes;
    bool terminator;
};

typedef std::vector<PaletteGroupElement> PaletteGroup;

inline PaletteGroupElement parse_palette_group_element(std::span<const uint8_t> data, uint32_t offset)
{
    PaletteGroupElement element;
    element.paletteId = data[offset] | (data[offset + 1] << 8);
    element.destPaletteNum = data[offset + 2];
    element.numPalettes = data[offset + 3] & 0x0F;
    if (element.numPalettes == 0) {
        element.numPalettes = 16;
    }
    element.terminator = (data[offset + 3] & 0x80) == 0;
    return element;
}

inline PaletteGroup extract_palette_group(uint32_t palette_group_address)
{
    constexpr uint32_t kPaletteGroupEntrySize = 4;
    constexpr uint32_t kMaxEntriesPerGroup = 64;

    std::span<const uint8_t> palette_group_data =
        extract_bytes(palette_group_address, kPaletteGroupEntrySize * kMaxEntriesPerGroup);
    if (palette_group_data.size() < kPaletteGroupEntrySize) {
        auto& reporter = PortAssetLog::Reporter::Instance();
        if (reporter.Verbose()) {
            reporter.Warn(fmt::format("palette group out of ROM range at 0x{:X}", palette_group_address));
        }
        return {};
    }

    PaletteGroup palette_group;
    for (uint32_t i = 0; i + kPaletteGroupEntrySize <= palette_group_data.size(); i += kPaletteGroupEntrySize) {
        PaletteGroupElement element = parse_palette_group_element(palette_group_data, i);
        palette_group.push_back(element);
        if (element.terminator) {
            break;
        }
    }

    return palette_group;
}

struct GfxGroupElement
{
    uint32_t src;
    uint32_t unknown;
    uint32_t dest;
    uint32_t size;
    bool compressed;
    bool terminator;
};

struct GfxGroupElement; 

typedef std::vector<GfxGroupElement> GfxGroup;

inline bool should_extract_gfx_element(const GfxGroupElement& element, uint8_t language)
{
    const uint8_t ctrl = element.unknown & 0x0F;
    switch (ctrl) {
        case 0x7:
            return true;
        case 0xD:
            return false;
        case 0xE:
            return language != 0 && language != 1;
        case 0xF:
            return language != 0;
        default:
            return ctrl == language;
    }
}

GfxGroupElement parse_gfx_group_element(std::span<const uint8_t> data, uint32_t offset)
{
    GfxGroupElement element;
    uint32_t raw0 = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
    uint32_t raw2 = data[offset + 8] | (data[offset + 9] << 8) | (data[offset + 10] << 16) | (data[offset + 11] << 24);
    element.src = raw0 & 0x00FFFFFF;
    element.unknown = (raw0 >> 24) & 0x7F;
    element.dest = data[offset + 4] | (data[offset + 5] << 8) | (data[offset + 6] << 16) | (data[offset + 7] << 24);
    element.size = raw2 & 0x7FFFFFFF;
    element.compressed = ((raw2 >> 31) & 0x1) != 0;
    element.terminator = ((raw0 >> 31) & 0x1) == 0;
    return element;
}
 
inline GfxGroup extract_gfx_group(uint32_t gfx_group_address)
{
    constexpr uint32_t kGfxGroupEntrySize = 12;
    constexpr uint32_t kMaxEntriesPerGroup = 256;

    auto& reporter = PortAssetLog::Reporter::Instance();
    std::span<const uint8_t> gfx_group_data =
        extract_bytes(gfx_group_address, kGfxGroupEntrySize * kMaxEntriesPerGroup);
    if (gfx_group_data.size() < kGfxGroupEntrySize) {
        if (reporter.Verbose()) {
            reporter.Warn(fmt::format("gfx group out of ROM range at 0x{:X}", gfx_group_address));
        }
        return GfxGroup();
    }

    GfxGroup gfx_group;
    for (uint32_t i = 0; i + kGfxGroupEntrySize <= gfx_group_data.size(); i += kGfxGroupEntrySize) {
        GfxGroupElement element = parse_gfx_group_element(gfx_group_data, i);
        gfx_group.push_back(element);

        if (element.terminator) {
            break;
        }

        if (i + kGfxGroupEntrySize >= gfx_group_data.size() && reporter.Verbose()) {
            reporter.Warn(fmt::format("no terminator found for gfx group at 0x{:X}", gfx_group_address));
        }
    }
    return gfx_group;
}

inline bool bin_to_bmp(std::span<const uint8_t> gfx_data, const std::string& output_path, uint16_t width,
                       uint16_t height, uint8_t bpp = 4)
{
    std::string error;
    const std::vector<uint8_t> pixels = PortAssetPipeline::DecodeGbaTiledGfx(gfx_data, width, height, bpp);
    return !pixels.empty() && PortAssetPipeline::WriteIndexedBmp(output_path, pixels, width, height, bpp, &error);
}

struct GfxMetadata
{
    uint16_t width;
    uint16_t height;
    uint8_t bpp;       // 1, 2, 4, or 8 bits per pixel
    bool is_indexed;   // true for palette-based, false for truecolor
};

inline GfxMetadata detect_gfx_metadata(uint32_t src, uint32_t size, uint8_t bpp = 4)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    GfxMetadata meta;
    meta.bpp = bpp;
    meta.is_indexed = (bpp <= 8);

    if (bpp == 0 || bpp > 8) {
        if (reporter.Verbose()) {
            reporter.Warn(fmt::format("invalid bpp {} for gfx at 0x{:X}, using 4", (int)bpp, src));
        }
        meta.bpp = 4;
    }

    const uint32_t bytes_per_tile = (meta.bpp == 8) ? 64 : 32;
    const uint32_t total_tiles = std::max<uint32_t>(1, size / bytes_per_tile);

    if (total_tiles == 0) {
        meta.width = 8;
        meta.height = 8;
        if (reporter.Verbose()) {
            reporter.Warn(fmt::format("empty/tiny gfx data at 0x{:X}, using 8x8", src));
        }
        return meta;
    }

    const uint32_t tiles_wide = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::sqrt(total_tiles))));
    const uint32_t tiles_high = (total_tiles + tiles_wide - 1) / tiles_wide;
    meta.width = static_cast<uint16_t>(tiles_wide * 8);
    meta.height = static_cast<uint16_t>(tiles_high * 8);

    return meta;
}

inline std::optional<GfxExtractionResult> extract_gfx(uint32_t src, uint32_t size, bool compressed,
                                                      uint32_t global_gfx_base_offset, uint8_t bpp,
                                                      uint16_t width, uint16_t height,
                                                      const std::filesystem::path& editable_root,
                                                      const std::filesystem::path& runtime_root,
                                                      const Config* config_for_runtime = nullptr)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    const uint32_t rom_src = global_gfx_base_offset + src;
    std::span<const uint8_t> raw_view = extract_bytes(rom_src, size);
    if (raw_view.empty()) {
        if (reporter.Verbose()) {
            reporter.Warn(fmt::format("failed to read gfx data at ROM 0x{:X}", rom_src));
        }
        return std::nullopt;
    }

    /* When compressed, lz77_uncompress fills a local vector and gfx_data
     * views into it. When not compressed, gfx_data is a zero-copy view
     * into the ROM buffer itself. */
    std::vector<uint8_t> uncompressed_storage;
    std::span<const uint8_t> gfx_data;
    if (compressed) {
        if (!lz77_uncompress(raw_view, uncompressed_storage)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to uncompress gfx at 0x{:X} (ROM 0x{:X})", src, rom_src));
            }
            return std::nullopt;
        }
        gfx_data = uncompressed_storage;
    } else {
        gfx_data = raw_view;
    }

    GfxMetadata meta;
    if (width == 0 || height == 0) {
        meta = detect_gfx_metadata(src, gfx_data.size(), bpp);
    } else {
        meta.width = width;
        meta.height = height;
        meta.bpp = bpp;
        meta.is_indexed = (bpp <= 8);
    }

    const std::string base_name = fmt::format("gfx_{:x}_{}x{}_{}bpp{}", src, meta.width, meta.height, (int)meta.bpp,
                                              compressed ? "_compressed" : "_uncompressed");
    const std::filesystem::path relative_bmp = std::filesystem::path("gfx") / (base_name + ".bmp");
    const std::filesystem::path relative_runtime = std::filesystem::path("gfx") / (base_name + ".bin");

    if (!editable_root.empty()) {
        const std::filesystem::path bmp_output = editable_root / relative_bmp;
        if (!bin_to_bmp(gfx_data, bmp_output.string(), meta.width, meta.height, meta.bpp)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to write editable BMP for gfx 0x{:X}", src));
            }
            return std::nullopt;
        }
    }

    if (!runtime_root.empty()) {
        if (config_for_runtime != nullptr && config_for_runtime->packRuntime &&
            config_for_runtime->pakBuilders != nullptr) {
            auto* builders = static_cast<PakBuilderArray*>(config_for_runtime->pakBuilders);
            const std::string rel = relative_runtime.generic_string();
            const auto idx = static_cast<std::size_t>(pak_route_for(rel));
            (*builders)[idx].Add(rel, gfx_data.data(), gfx_data.size());
        } else {
            const std::filesystem::path runtime_output = runtime_root / relative_runtime;
            if (!write_binary_file(runtime_output, gfx_data.data(), gfx_data.size())) {
                if (reporter.Verbose()) {
                    reporter.Warn(fmt::format("failed to write runtime gfx for 0x{:X}", src));
                }
                return std::nullopt;
            }
        }
    }

    return GfxExtractionResult{relative_bmp, relative_runtime, static_cast<uint32_t>(gfx_data.size()), meta.width,
                               meta.height, meta.bpp};
}



inline bool extract_all_gfx(const Config& config)
{
    auto& reporter = PortAssetLog::Reporter::Instance();

    std::vector<uint32_t> gfx_group_addresses =
        extract_gfx_group_addresses(config.gfxGroupsTableOffset, config.gfxGroupsTableLength);

    std::vector<GfxGroup> gfx_groups;
    gfx_groups.reserve(gfx_group_addresses.size());
    for (uint32_t address : gfx_group_addresses) {
        gfx_groups.push_back(extract_gfx_group(address));
    }

    // Flatten (group_index, element_index) pairs so we can parallelise across all
    // elements at once, not just within a group.
    struct WorkItem {
        size_t group_index;
        size_t element_index;
    };
    std::vector<WorkItem> work;
    size_t total_elements = 0;
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        total_elements += gfx_groups[i].size();
    }
    work.reserve(total_elements);
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        for (size_t j = 0; j < gfx_groups[i].size(); ++j) {
            work.push_back({i, j});
        }
    }

    // Pre-size per-element JSON results so workers can fill their slot without locks.
    std::vector<std::vector<nlohmann::json>> editable_results(gfx_groups.size());
    std::vector<std::vector<nlohmann::json>> runtime_results(gfx_groups.size());
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        editable_results[i].resize(gfx_groups[i].size());
        runtime_results[i].resize(gfx_groups[i].size());
    }

    reporter.BeginPhase("gfx", total_elements);

    PortAssetLog::ParallelFor<size_t>(0, work.size(), [&](size_t k) {
        const WorkItem item = work[k];
        const GfxGroupElement& element = gfx_groups[item.group_index][item.element_index];

        nlohmann::json editable;
        editable["unknown"] = element.unknown;
        editable["dest"] = element.dest;
        editable["compressed"] = element.compressed;
        editable["terminator"] = element.terminator;

        nlohmann::json runtime;
        runtime["unknown"] = element.unknown;
        runtime["dest"] = element.dest;
        runtime["terminator"] = element.terminator;

        if (should_extract_gfx_element(element, config.language)) {
            const std::optional<GfxExtractionResult> extracted = extract_gfx(
                element.src, element.size, element.compressed, config.globalGfxAndPalettesOffset, 4, 0, 0,
                config.outputRoot, config.runtimeOutputRoot, &config);
            if (extracted.has_value()) {
                editable["file"] = json_path_string(extracted->relative_bmp_output);
                editable["size"] = extracted->output_size;
                editable["width"] = extracted->width;
                editable["height"] = extracted->height;
                editable["bpp"] = extracted->bpp;

                runtime["file"] = json_path_string(extracted->relative_runtime_path);
                runtime["size"] = extracted->output_size;
            } else {
                editable["file"] = nullptr;
                editable["size"] = 0;
                runtime["file"] = nullptr;
                runtime["size"] = 0;
            }
        } else {
            editable["file"] = nullptr;
            editable["size"] = 0;
            runtime["file"] = nullptr;
            runtime["size"] = 0;
        }

        editable_results[item.group_index][item.element_index] = std::move(editable);
        runtime_results[item.group_index][item.element_index] = std::move(runtime);
        reporter.Tick();
    });

    nlohmann::json json_editable;
    nlohmann::json json_runtime;
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        nlohmann::json group_editable = nlohmann::json::array();
        nlohmann::json group_runtime = nlohmann::json::array();
        for (size_t j = 0; j < gfx_groups[i].size(); ++j) {
            group_editable.push_back(std::move(editable_results[i][j]));
            group_runtime.push_back(std::move(runtime_results[i][j]));
        }
        json_editable[std::to_string(i + 1)] = std::move(group_editable);
        json_runtime[std::to_string(i + 1)] = std::move(group_runtime);
    }

    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "gfx_groups.json",
                                                      std::move(json_editable), 4);
    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "gfx_groups.json",
                                                          std::move(json_runtime), 0);
    }

    reporter.EndPhase();
    return true;
}

struct PaletteRefRecord {
    std::filesystem::path editable_relative;  // assets_src/palettes/<name>.json
    std::filesystem::path runtime_relative;   // assets/palettes/<name>.pal
    std::filesystem::path source_asset;
    uint32_t first_palette_id;
    uint32_t palette_count;
};

inline nlohmann::json build_palette_file_refs(const std::vector<PaletteRefRecord>& palette_records,
                                              uint32_t palette_id, uint32_t num_palettes, bool runtime)
{
    nlohmann::json refs = nlohmann::json::array();
    uint32_t next_palette_id = palette_id;
    uint32_t remaining_palettes = num_palettes;

    while (remaining_palettes > 0) {
        bool found_segment = false;
        for (const PaletteRefRecord& record : palette_records) {
            const uint32_t record_end = record.first_palette_id + record.palette_count;
            if (next_palette_id < record.first_palette_id || next_palette_id >= record_end) {
                continue;
            }

            const uint32_t palette_offset = next_palette_id - record.first_palette_id;
            const uint32_t palettes_in_segment =
                std::min<uint32_t>(remaining_palettes, record.palette_count - palette_offset);

            nlohmann::json ref;
            ref["asset"] = json_path_string(record.source_asset);
            ref["file"] = json_path_string(runtime ? record.runtime_relative : record.editable_relative);
            ref["palette_offset"] = palette_offset;
            ref["num_palettes"] = palettes_in_segment;
            ref["byte_offset"] = palette_offset * 32;
            ref["size"] = palettes_in_segment * 32;
            refs.push_back(ref);

            next_palette_id += palettes_in_segment;
            remaining_palettes -= palettes_in_segment;
            found_segment = true;
            break;
        }

        if (!found_segment) {
            break;
        }
    }

    return refs;
}

inline std::vector<PaletteRefRecord> extract_all_palettes(const Config& config)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    const std::vector<AssetRecord> palette_records = collect_embedded_asset_records(
        config, [](const std::string& path) { return path.rfind("palettes/", 0) == 0 && !path.empty(); });

    reporter.BeginPhase("palettes", palette_records.size());

    std::vector<std::optional<PaletteRefRecord>> records_out(palette_records.size());
    std::vector<std::optional<nlohmann::json>> editable_json(palette_records.size());
    std::vector<std::optional<nlohmann::json>> runtime_json(palette_records.size());

    PortAssetLog::ParallelFor<size_t>(0, palette_records.size(), [&](size_t i) {
        const AssetRecord& record = palette_records[i];
        const std::span<const uint8_t> palette_data = extract_bytes(record.rom_start, record.size);
        if (palette_data.empty()) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to read palette at 0x{:X}", record.rom_start));
            }
            reporter.Tick();
            return;
        }

        std::filesystem::path filename = record.source_path.filename();
        std::filesystem::path editable_relative = std::filesystem::path("palettes") / filename;
        editable_relative.replace_extension(".json");
        std::filesystem::path runtime_relative = std::filesystem::path("palettes") / filename;
        runtime_relative.replace_extension(".pal");

        std::string err;
        if (!PortAssetPipeline::WritePaletteJson(config.outputRoot / editable_relative, palette_data, &err)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to write palette JSON {}: {}", editable_relative.generic_string(), err));
            }
            reporter.Tick();
            return;
        }
        if (!config.runtimeOutputRoot.empty()) {
            if (!write_runtime_asset(config, /*editable_path=*/{}, runtime_relative,
                                     palette_data.data(), palette_data.size())) {
                if (reporter.Verbose()) {
                    reporter.Warn(fmt::format("failed to write runtime palette {}", runtime_relative.generic_string()));
                }
            }
        }

        const uint32_t first_palette_id =
            (record.rom_start >= config.globalGfxAndPalettesOffset)
                ? ((record.rom_start - config.globalGfxAndPalettesOffset) / 32)
                : 0;
        const uint32_t palette_count = record.size / 32;
        records_out[i] = PaletteRefRecord{editable_relative, runtime_relative, record.source_path, first_palette_id,
                                          palette_count};

        nlohmann::json editable;
        editable["asset"] = json_path_string(record.source_path);
        editable["file"] = json_path_string(editable_relative);
        editable["size"] = record.size;
        if (record.size % 32 == 0 && record.rom_start >= config.globalGfxAndPalettesOffset) {
            editable["palette_id"] = first_palette_id;
            editable["num_palettes"] = palette_count;
        }
        editable_json[i] = std::move(editable);

        nlohmann::json runtime = editable_json[i].value();
        runtime["file"] = json_path_string(runtime_relative);
        runtime_json[i] = std::move(runtime);

        /* Pass writes "palettes/foo.json" + "palettes/foo.pal", neither of which
         * matches the asset-index entry path "palettes/foo.bin"; the sweep needs
         * to write that raw .bin too (matches old extractor's assets_src/ shape).
         * "palettes/" is not a runtime-passthrough prefix, so the sweep skips
         * the runtime side. No consumed marks needed here. */
        reporter.Tick();
    });

    nlohmann::json editable_array = nlohmann::json::array();
    nlohmann::json runtime_array = nlohmann::json::array();
    std::vector<PaletteRefRecord> result;
    result.reserve(palette_records.size());
    for (size_t i = 0; i < palette_records.size(); ++i) {
        if (editable_json[i].has_value()) {
            editable_array.push_back(std::move(*editable_json[i]));
        }
        if (runtime_json[i].has_value()) {
            runtime_array.push_back(std::move(*runtime_json[i]));
        }
        if (records_out[i].has_value()) {
            result.push_back(std::move(*records_out[i]));
        }
    }

    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "palettes.json",
                                                      std::move(editable_array), 4);
    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "palettes.json",
                                                          std::move(runtime_array), 0);
    }

    reporter.EndPhase();
    return result;
}

inline bool extract_all_palette_groups(const Config& config, const std::vector<PaletteRefRecord>& palette_records)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    const std::vector<uint32_t> palette_group_addresses =
        extract_palette_group_addresses(config.paletteGroupsTableOffset, config.paletteGroupsTableLength);

    reporter.BeginPhase("palette_groups", palette_group_addresses.size());

    std::vector<nlohmann::json> editable_groups(palette_group_addresses.size());
    std::vector<nlohmann::json> runtime_groups(palette_group_addresses.size());

    PortAssetLog::ParallelFor<size_t>(0, palette_group_addresses.size(), [&](size_t i) {
        const PaletteGroup group = extract_palette_group(palette_group_addresses[i]);

        nlohmann::json editable_entries = nlohmann::json::array();
        nlohmann::json runtime_entries = nlohmann::json::array();
        for (const PaletteGroupElement& element : group) {
            nlohmann::json shared;
            shared["palette_id"] = element.paletteId;
            shared["dest_palette_num"] = element.destPaletteNum;
            shared["num_palettes"] = element.numPalettes;
            shared["terminator"] = element.terminator;
            shared["size"] = element.numPalettes * 32;

            nlohmann::json editable = shared;
            editable["palette_files"] =
                build_palette_file_refs(palette_records, element.paletteId, element.numPalettes, false);
            editable_entries.push_back(std::move(editable));

            nlohmann::json runtime = shared;
            runtime["palette_files"] =
                build_palette_file_refs(palette_records, element.paletteId, element.numPalettes, true);
            runtime_entries.push_back(std::move(runtime));
        }

        nlohmann::json editable_root;
        editable_root["entries"] = std::move(editable_entries);
        editable_groups[i] = std::move(editable_root);

        nlohmann::json runtime_root;
        runtime_root["entries"] = std::move(runtime_entries);
        runtime_groups[i] = std::move(runtime_root);

        reporter.Tick();
    });

    nlohmann::json editable_obj = nlohmann::json::object();
    nlohmann::json runtime_obj = nlohmann::json::object();
    for (size_t i = 0; i < palette_group_addresses.size(); ++i) {
        const std::string key = std::to_string(i + 1);
        editable_obj[key] = std::move(editable_groups[i]);
        runtime_obj[key] = std::move(runtime_groups[i]);
    }

    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "palette_groups.json",
                                                      std::move(editable_obj), 4);
    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "palette_groups.json",
                                                          std::move(runtime_obj), 0);
    }

    reporter.EndPhase();
    return true;
}

inline bool extract_all_tilemaps(const Config& config)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    const std::vector<AssetRecord> tilemap_records = collect_embedded_asset_records(config, [](const std::string& path) {
        return (path.find("/rooms/") != std::string::npos && path_has_suffix(path, ".bin.lz")) ||
               path.rfind("assets/gAreaRoomMap_", 0) == 0;
    });

    reporter.BeginPhase("tilemaps", tilemap_records.size());

    std::vector<std::optional<nlohmann::json>> results(tilemap_records.size());

    PortAssetLog::ParallelFor<size_t>(0, tilemap_records.size(), [&](size_t i) {
        const AssetRecord& record = tilemap_records[i];
        const std::span<const uint8_t> tilemap_data = extract_bytes(record.rom_start, record.size);
        if (tilemap_data.empty()) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to read tilemap at 0x{:X}", record.rom_start));
            }
            reporter.Tick();
            return;
        }

        const std::filesystem::path raw_relative = std::filesystem::path("tilemaps") / record.source_path;
        if (!write_binary_file(config.outputRoot / raw_relative, tilemap_data.data(), tilemap_data.size())) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to write tilemap {}", raw_relative.generic_string()));
            }
            reporter.Tick();
            return;
        }
        if (!config.runtimeOutputRoot.empty()) {
            write_runtime_asset(config, config.outputRoot / raw_relative, raw_relative,
                                tilemap_data.data(), tilemap_data.size());
        }

        nlohmann::json json_tilemap;
        json_tilemap["asset"] = json_path_string(record.source_path);
        json_tilemap["file"] = json_path_string(raw_relative);
        json_tilemap["size"] = record.size;
        json_tilemap["compressed"] = record.compressed;

        if (record.compressed) {
            std::vector<uint8_t> decompressed_data;
            if (lz77_uncompress(tilemap_data, decompressed_data)) {
                std::filesystem::path decompressed_relative = raw_relative;
                decompressed_relative.replace_extension("");
                if (write_binary_file(config.outputRoot / decompressed_relative, decompressed_data.data(),
                                      decompressed_data.size())) {
                    if (!config.runtimeOutputRoot.empty()) {
                        write_runtime_asset(config, config.outputRoot / decompressed_relative, decompressed_relative,
                                            decompressed_data.data(), decompressed_data.size());
                    }
                    json_tilemap["decompressed_file"] = json_path_string(decompressed_relative);
                    json_tilemap["decompressed_size"] = decompressed_data.size();
                }
            } else if (reporter.Verbose()) {
                reporter.Warn(fmt::format("failed to uncompress tilemap {}", record.source_path.generic_string()));
            }
        }

        /* Pass writes the raw bytes under "tilemaps/<source_path>"; the
         * asset-index entry path is just "<source_path>" (e.g.
         * "assets/gAreaRoomMap_*.bin.lz"), so the sweep still needs to write
         * the entry path itself. No consumed marks. */
        results[i] = std::move(json_tilemap);
        reporter.Tick();
    });

    nlohmann::json editable_array = nlohmann::json::array();
    for (auto& r : results) {
        if (r.has_value()) {
            editable_array.push_back(std::move(*r));
        }
    }

    if (!config.runtimeOutputRoot.empty()) {
        /* Both trees write the same JSON shape; submit a copy for the
         * runtime side first so the original can be moved into the
         * editable submit. */
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "tilemaps.json",
                                                          editable_array, 0);
    }
    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "tilemaps.json",
                                                      std::move(editable_array), 4);

    reporter.EndPhase();
    return true;
}

struct IndexedAssetInfo
{
    std::filesystem::path path;
    uint32_t size;
};

inline std::unordered_map<uint32_t, IndexedAssetInfo> build_embedded_asset_lookup()
{
    std::unordered_map<uint32_t, IndexedAssetInfo> lookup;
    const EmbeddedAssetEntry* asset_index = EmbeddedAssetIndex_Get();
    const u32 asset_count = EmbeddedAssetIndex_Count();
    for (u32 i = 0; i < asset_count; ++i) {
        lookup.emplace(asset_index[i].offset, IndexedAssetInfo{asset_index[i].path, asset_index[i].size});
    }
    return lookup;
}

inline bool is_valid_rom_pointer(uint32_t value)
{
    return value >= 0x08000000u && value < 0x08000000u + Rom.size();
}

inline bool is_valid_table_value(uint32_t value)
{
    return value == 0 || is_valid_rom_pointer(value);
}

inline uint32_t scan_pointer_table_count(uint32_t table_offset, uint32_t max_entries)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < max_entries; ++i) {
        const uint32_t value = read_pointer(table_offset + i * 4);
        if (is_valid_table_value(value)) {
            count = i + 1;
        } else {
            break;
        }
    }
    return count;
}

inline std::string hex_offset_string(uint32_t offset)
{
    std::stringstream ss;
    ss << std::hex << offset;
    return ss.str();
}

inline std::vector<uint32_t> build_inference_boundaries(const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                                        const std::vector<uint32_t>& extra_offsets)
{
    std::set<uint32_t> ordered_offsets;
    for (const auto& item : lookup) {
        ordered_offsets.insert(item.first);
    }
    for (uint32_t offset : extra_offsets) {
        if (offset < Rom.size()) {
            ordered_offsets.insert(offset);
        }
    }
    ordered_offsets.insert(static_cast<uint32_t>(Rom.size()));
    return std::vector<uint32_t>(ordered_offsets.begin(), ordered_offsets.end());
}

inline uint32_t infer_asset_size(uint32_t offset, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                 const std::vector<uint32_t>& boundaries, uint32_t fallback_size = 4)
{
    const auto exact = lookup.find(offset);
    if (exact != lookup.end()) {
        return exact->second.size;
    }

    const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
    if (next != boundaries.end() && *next > offset) {
        return *next - offset;
    }

    if (offset + fallback_size <= Rom.size()) {
        return fallback_size;
    }

    return offset < Rom.size() ? static_cast<uint32_t>(Rom.size()) - offset : 0;
}

// Exact byte-length for a SELF-CONTAINED animation, by mirroring the runtime
// decoder (port/port_animation.c). Each frame is 4 bytes; a frame whose byte[3]
// has bit 7 set (loop flag) is the terminal frame and is followed by ONE
// loop_back byte. The decoder then does `p -= loop_back * 4`.
//
// Option 3 (docs/extractor-animation-remediation-plan.md, validated 2026-05-30):
// only return the trimmed exact extent `N*4+1` when the loop is SELF-CONTAINED,
// i.e. `loop_back ∈ [1, N]` so it jumps back within this animation's own frames.
// Such anims self-terminate (the decoder cycles within [offset, offset+N*4) and
// never reads following bytes), so the trimmed odd size is safe even though it
// bypasses the runtime's contiguous-packing net. This recovers the ~900 anims
// that the boundary-gap heuristic over-counted by 1-3 alignment bytes (→ strict
// parser rejected them → zeroed/blank).
//
// When `loop_back > N` (the loop jumps back BEYOND this anim, into the
// contiguous PRECEDING animation — a shared-frame technique, 38 anims found) or
// `loop_back == 0`, or no loop frame is found, fall back to the boundary-gap
// size (the old infer_asset_size behavior). Those anims stay EVEN-sized so the
// runtime net keeps packing them contiguously and the back-reference resolves —
// trimming them would feed the decoder detached/garbage frames (the failure mode
// of the reverted 28fbfa9ff salvage).
inline uint32_t compute_animation_extent(uint32_t offset,
                                         const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                         const std::vector<uint32_t>& boundaries)
{
    const uint32_t boundary_gap = infer_asset_size(offset, lookup, boundaries, 4);
    uint32_t boundary = offset + boundary_gap;
    if (boundary > Rom.size()) {
        boundary = static_cast<uint32_t>(Rom.size());
    }

    uint32_t p = offset;
    while (p + 4 <= boundary) {
        const uint8_t frame_byte = Rom[p + 3];
        p += 4;
        if (frame_byte & 0x80) { // terminal loop frame
            if (p < boundary) {
                const uint32_t n = (p - offset) / 4; // frames consumed so far
                const uint8_t loop_back = Rom[p];
                if (loop_back >= 1 && loop_back <= n) {
                    // self-contained loop → exact, safe to trim alignment
                    return (p - offset) + 1;
                }
            }
            break; // shared-frame / degenerate → boundary-gap fallback below
        }
    }
    return boundary_gap; // non-self-contained: keep even size for the runtime net
}

struct ChunkInfo
{
    uint32_t offset;
    uint32_t size;
};

inline std::unordered_map<std::string, ChunkInfo> build_path_to_chunk_lookup(
    const std::unordered_map<uint32_t, IndexedAssetInfo>& offset_lookup)
{
    std::unordered_map<std::string, ChunkInfo> by_path;
    by_path.reserve(offset_lookup.size());
    for (const auto& [off, info] : offset_lookup) {
        by_path.emplace(info.path.generic_string(), ChunkInfo{off, info.size});
    }
    return by_path;
}

/* #40, #36: Some asm symbols hold a multi-chunk table — a leading `.incbin`
 * followed by inline `.4byte` pointers and follow-on `.incbin` chunks all under
 * one label (e.g. gUnk_additional_c_HyruleTown_0 spans 192 bytes across
 * `_0.bin` + script_DrLeftDoor + `_0_1.bin` + script_FirstHouseDoor + `_0_2.bin`).
 * The asset index records each chunk as a separate entry, so a naive extract of
 * the leading chunk drops the trailing chunks (and the inline pointers) on the
 * floor. Detect this case from the path-suffix convention (`X.bin` followed by
 * `X_1.bin`, `X_2.bin`, ...) and return the full span size end-relative to the
 * leading offset, so the leading chunk's file holds the whole table when written.
 * Returns 0 if not multi-chunk. */
inline uint32_t multi_chunk_span_size(uint32_t leading_offset,
                                      const std::unordered_map<uint32_t, IndexedAssetInfo>& offset_lookup,
                                      const std::unordered_map<std::string, ChunkInfo>& path_lookup)
{
    auto leading_it = offset_lookup.find(leading_offset);
    if (leading_it == offset_lookup.end()) {
        return 0;
    }
    const std::string leading_path = leading_it->second.path.generic_string();
    constexpr const char kSuffix[] = ".bin";
    constexpr size_t kSuffixLen = sizeof(kSuffix) - 1;
    if (leading_path.size() <= kSuffixLen ||
        leading_path.compare(leading_path.size() - kSuffixLen, kSuffixLen, kSuffix) != 0) {
        return 0;
    }
    const std::string base = leading_path.substr(0, leading_path.size() - kSuffixLen);
    uint32_t span_end = leading_offset + leading_it->second.size;
    uint32_t followup_count = 0;
    for (uint32_t n = 1;; ++n) {
        const std::string next_path = base + "_" + std::to_string(n) + kSuffix;
        auto path_it = path_lookup.find(next_path);
        if (path_it == path_lookup.end()) {
            break;
        }
        /* The chunk must follow the previous one's end with at most a small gap
         * (typically 4 bytes for an inline `.4byte script_X` pointer). Cap at
         * 16 bytes to be defensive against unrelated assets sharing the prefix. */
        const uint32_t next_offset = path_it->second.offset;
        if (next_offset < span_end || next_offset - span_end > 16) {
            break;
        }
        span_end = next_offset + path_it->second.size;
        ++followup_count;
    }
    if (followup_count == 0) {
        return 0;
    }
    return span_end - leading_offset;
}

inline std::filesystem::path extract_asset_or_raw(uint32_t rom_offset, uint32_t size,
                                                  const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                                  const std::filesystem::path& output_root,
                                                  const std::filesystem::path& runtime_output_root,
                                                  const std::filesystem::path& generated_prefix,
                                                  const Config* active_pak_config = nullptr,
                                                  uint32_t size_override = 0)
{
    auto found = lookup.find(rom_offset);
    std::filesystem::path relative_path;
    if (found != lookup.end()) {
        relative_path = found->second.path;
        /* Honour size_override (caller knows the chunk is multi-chunk and wants
         * the full contiguous span written under the leading chunk's filename).
         * Without override, fall back to the index entry's recorded size. */
        size = size_override > found->second.size ? size_override : found->second.size;
    } else {
        relative_path = generated_prefix / ("offset_" + hex_offset_string(rom_offset) + ".bin");
        if (size_override > size) {
            size = size_override;
        }
    }

    const std::span<const uint8_t> data = extract_bytes(rom_offset, size);
    const std::filesystem::path editable_path = output_root / relative_path;
    write_binary_file(editable_path, data.data(), data.size());
    g_consumed_editable().Mark(relative_path);
    if (!runtime_output_root.empty()) {
        if (active_pak_config != nullptr && active_pak_config->packRuntime &&
            active_pak_config->pakBuilders != nullptr) {
            auto* builders = static_cast<PakBuilderArray*>(active_pak_config->pakBuilders);
            const std::string rel = relative_path.generic_string();
            const auto idx = static_cast<std::size_t>(pak_route_for(rel));
            (*builders)[idx].Add(rel, data.data(), data.size());
        } else {
            mirror_to_runtime(editable_path, runtime_output_root / relative_path, data.data(), data.size());
        }
        g_consumed_runtime().Mark(relative_path);
    }
    return relative_path;
}

inline nlohmann::json extract_map_definition_sequence(
    uint32_t sequence_offset, const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
    const std::vector<uint32_t>& boundaries, const std::filesystem::path& generated_prefix)
{
    constexpr uint32_t kMapMultiple = 0x80000000u;
    constexpr uint32_t kMapCompressed = 0x80000000u;
    constexpr uint32_t kEntrySize = 12;

    nlohmann::json json_sequence = nlohmann::json::array();
    uint32_t entry_offset = sequence_offset;
    while (entry_offset + kEntrySize <= Rom.size()) {
        const uint32_t src = read_pointer(entry_offset + 0);
        const uint32_t dest = read_pointer(entry_offset + 4);
        const uint32_t size = read_pointer(entry_offset + 8);
        const bool multiple = (src & kMapMultiple) != 0;
        nlohmann::json json_entry;
        json_entry["multiple"] = multiple;

        if (dest == 0) {
            json_entry["palette_group"] = src & 0xFFFF;
        } else {
            const bool compressed = (size & kMapCompressed) != 0;
            const uint32_t data_offset = config.mapDataOffset + (src & 0x7FFFFFFF);
            const uint32_t data_size = size & 0x7FFFFFFF;
            /* A valid map asset always lies within the ROM. An out-of-ROM source
             * means the sequence's terminator was missed (its real last entry's
             * `multiple` bit looked set) and we are now reading garbage triples;
             * stop before emitting a bogus asset that would load junk into VRAM. */
            if (data_offset >= Rom.size()) {
                break;
            }
            /* For compressed blobs prefer the exact LZ77 stream length. The
             * boundary-gap fallback (infer_asset_size) is derived from the
             * USA-baked EmbeddedAssetIndex and mis-sizes EU/JP compressed map
             * assets (offsets miss the index) — truncating tilesets so the
             * runtime decoder leaves the tail tiles garbled. */
            uint32_t file_size;
            if (compressed) {
                const uint32_t exact = lz77_compressed_size(data_offset);
                file_size = exact != 0 ? exact : infer_asset_size(data_offset, lookup, boundaries, data_size);
            } else {
                file_size = data_size;
            }
            const std::filesystem::path relative_path =
                extract_asset_or_raw(data_offset, file_size, lookup, config.outputRoot, config.runtimeOutputRoot,
                                     generated_prefix, &config);
            json_entry["file"] = json_path_string(relative_path);
            json_entry["dest"] = dest;
            json_entry["size"] = data_size;
            json_entry["compressed"] = compressed;
        }

        json_sequence.push_back(json_entry);
        entry_offset += kEntrySize;
        if (!multiple) {
            break;
        }
    }

    return json_sequence;
}

inline std::vector<uint32_t> collect_area_table_data_offsets(const Config& config)
{
    std::vector<uint32_t> offsets;
    for (uint32_t area = 0; area < 0x90; ++area) {
        const uint32_t area_table_ptr = read_pointer(config.areaTableTableOffset + area * 4);
        if (!is_valid_rom_pointer(area_table_ptr)) {
            continue;
        }

        const uint32_t room_table_offset = to_rom_address(area_table_ptr);
        const uint32_t room_count = scan_pointer_table_count(room_table_offset, 64);
        for (uint32_t room = 0; room < room_count; ++room) {
            const uint32_t room_ptr = read_pointer(room_table_offset + room * 4);
            if (!is_valid_rom_pointer(room_ptr)) {
                continue;
            }

            const uint32_t room_offset = to_rom_address(room_ptr);
            const uint32_t property_count = scan_pointer_table_count(room_offset, 64);
            for (uint32_t idx = 0; idx < property_count; ++idx) {
                const uint32_t value = read_pointer(room_offset + idx * 4);
                if (!is_valid_table_value(value)) {
                    break;
                }
                if ((idx >= 4 && idx <= 7) || !is_valid_rom_pointer(value)) {
                    continue;
                }
                offsets.push_back(to_rom_address(value));
            }
        }
    }
    return offsets;
}

inline std::vector<uint32_t> collect_sprite_animation_table_offsets(const Config& config)
{
    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i < config.spritePtrsCount; ++i) {
        const uint32_t animations_ptr = read_pointer(config.spritePtrsTableOffset + i * 16);
        if (is_valid_rom_pointer(animations_ptr) && (animations_ptr & 1) == 0) {
            offsets.push_back(to_rom_address(animations_ptr));
        }
    }
    return offsets;
}

inline bool extract_area_room_headers(const Config& config)
{
    /* Pure CPU work over 144 independent areas. Each area writes a JSON
     * fragment into a pre-sized slot so the merged document is deterministic
     * regardless of execution order. */
    constexpr uint32_t kAreaCount = 0x90;
    std::vector<nlohmann::json> per_area(kAreaCount);

    PortAssetLog::ParallelFor<uint32_t>(0, kAreaCount, [&](uint32_t area) {
        const uint32_t room_headers_ptr = read_pointer(config.areaRoomHeadersTableOffset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();
        if (is_valid_rom_pointer(room_headers_ptr)) {
            uint32_t room_offset = to_rom_address(room_headers_ptr);
            for (uint32_t room = 0; room < 64 && room_offset + 2 <= Rom.size(); ++room) {
                const uint16_t sentinel = Rom[room_offset + 0] | (Rom[room_offset + 1] << 8);
                if (sentinel == 0xFFFF) {
                    break;
                }
                if (room_offset + 10 > Rom.size()) {
                    break;
                }
                nlohmann::json json_room;
                json_room["map_x"] = Rom[room_offset + 0] | (Rom[room_offset + 1] << 8);
                json_room["map_y"] = Rom[room_offset + 2] | (Rom[room_offset + 3] << 8);
                json_room["pixel_width"] = Rom[room_offset + 4] | (Rom[room_offset + 5] << 8);
                json_room["pixel_height"] = Rom[room_offset + 6] | (Rom[room_offset + 7] << 8);
                json_room["tile_set_id"] = Rom[room_offset + 8] | (Rom[room_offset + 9] << 8);
                json_area.push_back(json_room);
                room_offset += 10;
            }
        }
        per_area[area] = std::move(json_area);
    });

    nlohmann::json json_headers = nlohmann::json::object();
    for (uint32_t area = 0; area < kAreaCount; ++area) {
        json_headers[std::to_string(area)] = std::move(per_area[area]);
    }

    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(
            config.runtimeOutputRoot / "area_room_headers.json", json_headers, 0);
    }
    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "area_room_headers.json",
                                                      std::move(json_headers), 4);
    return true;
}

inline bool extract_area_map_table(const Config& config, uint32_t table_offset,
                                   const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                   const std::vector<uint32_t>& boundaries, const char* json_name,
                                   const std::filesystem::path& generated_prefix, bool direct_sequences)
{
    /* Each area's body calls extract_map_definition_sequence ->
     * extract_asset_or_raw, which writes files to disk. Those writers are
     * thread-safe (write_binary_file uses EnsureDir + per-file open; the
     * consumed-paths sets are mutex-protected). 144 areas in parallel. */
    constexpr uint32_t kAreaCount = 0x90;
    std::vector<nlohmann::json> per_area(kAreaCount);

    PortAssetLog::ParallelFor<uint32_t>(0, kAreaCount, [&](uint32_t area) {
        const uint32_t area_ptr = read_pointer(table_offset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();

        if (is_valid_rom_pointer(area_ptr)) {
            if (direct_sequences) {
                json_area = extract_map_definition_sequence(to_rom_address(area_ptr), config, lookup, boundaries,
                                                            generated_prefix);
            } else {
                const uint32_t subtable_offset = to_rom_address(area_ptr);
                const uint32_t count = scan_pointer_table_count(subtable_offset, 64);
                for (uint32_t i = 0; i < count; ++i) {
                    const uint32_t seq_ptr = read_pointer(subtable_offset + i * 4);
                    if (is_valid_rom_pointer(seq_ptr)) {
                        json_area.push_back(extract_map_definition_sequence(to_rom_address(seq_ptr), config, lookup,
                                                                            boundaries, generated_prefix));
                    } else {
                        json_area.push_back(nlohmann::json::array());
                    }
                }
            }
        }

        per_area[area] = std::move(json_area);
    });

    nlohmann::json json_root = nlohmann::json::object();
    for (uint32_t area = 0; area < kAreaCount; ++area) {
        json_root[std::to_string(area)] = std::move(per_area[area]);
    }

    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / json_name,
                                                          json_root, 0);
    }
    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / json_name,
                                                      std::move(json_root), 4);
    return true;
}

inline bool extract_area_tables(const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                const std::vector<uint32_t>& boundaries)
{
    /* Largest of the area passes — each area can fan out to dozens of rooms,
     * each of which calls extract_asset_or_raw (which writes to both trees).
     * Pre-size and fill in parallel. */
    constexpr uint32_t kAreaCount = 0x90;
    std::vector<nlohmann::json> per_area(kAreaCount);

    /* #40, #36: Multi-chunk room-property tables (e.g. door records spanning
     * multiple `.incbin` chunks interleaved with inline `.4byte` script
     * pointers) need to be extracted as a single contiguous file under the
     * leading chunk's filename so the runtime sees the full table. Build a
     * path→chunk reverse lookup once so we can quickly chain `_N.bin` follow-ons. */
    const auto path_lookup = build_path_to_chunk_lookup(lookup);

    PortAssetLog::ParallelFor<uint32_t>(0, kAreaCount, [&](uint32_t area) {
        const uint32_t area_ptr = read_pointer(config.areaTableTableOffset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();

        if (is_valid_rom_pointer(area_ptr)) {
            const uint32_t subtable_offset = to_rom_address(area_ptr);
            const uint32_t count = scan_pointer_table_count(subtable_offset, 64);

            for (uint32_t room = 0; room < count; ++room) {
                const uint32_t room_ptr = read_pointer(subtable_offset + room * 4);
                nlohmann::json json_room = nlohmann::json::array();
                if (is_valid_rom_pointer(room_ptr)) {
                    const uint32_t room_offset = to_rom_address(room_ptr);
                    const uint32_t property_count = scan_pointer_table_count(room_offset, 64);
                    for (uint32_t idx = 0; idx < property_count; ++idx) {
                        const uint32_t value = read_pointer(room_offset + idx * 4);
                        if (!is_valid_table_value(value)) {
                            break;
                        }

                        if (value == 0 || !is_valid_rom_pointer(value)) {
                            json_room.push_back(nullptr);
                            continue;
                        }

                        const uint32_t data_offset = to_rom_address(value);

                        /* Properties at idx 4..7 are usually room callback functions
                         * that the port resolves via Port_GetRoomFuncProp. But some
                         * rooms (e.g. Minish Forest lily pads, doorway data tables)
                         * put real data pointers there. The asset index distinguishes
                         * code from data: if the ROM offset has an indexed asset entry
                         * (e.g. data_080D5360/...), treat it as data and extract it.
                         * Otherwise it's code — skip. */
                        if (idx >= 4 && idx <= 7) {
                            if (lookup.find(data_offset) == lookup.end()) {
                                json_room.push_back(nullptr);
                                continue;
                            }
                        }

                        const uint32_t data_size = infer_asset_size(data_offset, lookup, boundaries, 4);
                        /* If this property points at the leading chunk of a
                         * multi-chunk table, override the size so the leading
                         * chunk's file holds the entire contiguous span
                         * (including inline `.4byte` pointers between chunks).
                         * Without this the runtime reads a truncated table and
                         * loops off the end into garbage — e.g. Hyrule Town
                         * doors missing decorations (#40) and Cave of Flames
                         * lava platforms not spawning (#36). */
                        const uint32_t span_size = multi_chunk_span_size(data_offset, lookup, path_lookup);
                        const std::filesystem::path relative_path = extract_asset_or_raw(
                            data_offset, data_size, lookup, config.outputRoot, config.runtimeOutputRoot,
                            "room_properties", &config, span_size);
                        json_room.push_back(json_path_string(relative_path));
                    }
                }
                json_area.push_back(json_room);
            }
        }

        per_area[area] = std::move(json_area);
    });

    nlohmann::json json_root = nlohmann::json::object();
    for (uint32_t area = 0; area < kAreaCount; ++area) {
        json_root[std::to_string(area)] = std::move(per_area[area]);
    }

    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "area_tables.json",
                                                          json_root, 0);
    }
    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "area_tables.json",
                                                      std::move(json_root), 4);
    return true;
}

struct SpritePtrPaths {
    std::filesystem::path editable;  // .json (animations/frames) or .bmp (4bpp gfx)
    std::filesystem::path runtime;   // .bin (animations/frames) or .4bpp (gfx)
};

inline bool extract_sprite_ptrs(const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                const std::vector<uint32_t>& boundaries)
{
    auto& reporter = PortAssetLog::Reporter::Instance();

    std::mutex animation_mu, frames_mu, ptr_mu;
    std::unordered_map<std::string, SpritePtrPaths> editable_animation_paths;
    std::unordered_map<std::string, SpritePtrPaths> editable_frame_paths;
    std::unordered_map<std::string, SpritePtrPaths> editable_ptr_paths;

    auto source_path_for = [&](uint32_t rom_offset, const std::filesystem::path& generated_prefix) {
        const auto found = lookup.find(rom_offset);
        if (found != lookup.end()) {
            return found->second.path;
        }
        return generated_prefix / ("offset_" + hex_offset_string(rom_offset) + ".bin");
    };

    auto convert_animation = [&](uint32_t rom_offset, uint32_t size) -> SpritePtrPaths {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/animations");
        const std::string cache_key = raw_relative_path.generic_string();
        {
            std::lock_guard<std::mutex> lk(animation_mu);
            auto found = editable_animation_paths.find(cache_key);
            if (found != editable_animation_paths.end()) {
                return found->second;
            }
        }

        const std::span<const uint8_t> data = extract_bytes(rom_offset, size);
        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".json");
        std::filesystem::path runtime_path = raw_relative_path;
        runtime_path.replace_extension(".bin");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        SpritePtrPaths paths;
        if (!PortAssetPipeline::WriteEditableAnimation(config.outputRoot / editable_path, data, &write_error)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("editable animation {}: {}", editable_path.generic_string(), write_error));
            }
            write_binary_file(config.outputRoot / raw_relative_path, data.data(), data.size());
            paths.editable = raw_relative_path;
        } else {
            paths.editable = editable_path;
        }

        if (!config.runtimeOutputRoot.empty()) {
            write_runtime_asset(config, /*editable_path=*/{}, runtime_path, data.data(), data.size());
            /* runtime_path = raw_relative_path with .bin extension, which IS
             * the asset-index entry path for this offset; mark it so the
             * passthrough sweep doesn't re-write the same bytes. The editable
             * .json lives at a different path so the sweep should still write
             * the raw .bin to assets_src/, matching the old extractor. */
            g_consumed_runtime().Mark(runtime_path);
        }
        paths.runtime = runtime_path;

        {
            std::lock_guard<std::mutex> lk(animation_mu);
            editable_animation_paths.emplace(cache_key, paths);
        }
        return paths;
    };

    auto convert_frames = [&](uint32_t rom_offset, uint32_t size) -> SpritePtrPaths {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/sprites");
        const std::string cache_key = raw_relative_path.generic_string();
        {
            std::lock_guard<std::mutex> lk(frames_mu);
            auto found = editable_frame_paths.find(cache_key);
            if (found != editable_frame_paths.end()) {
                return found->second;
            }
        }

        const std::span<const uint8_t> data = extract_bytes(rom_offset, size);
        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".json");
        std::filesystem::path runtime_path = raw_relative_path;
        runtime_path.replace_extension(".bin");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        SpritePtrPaths paths;
        if (!PortAssetPipeline::WriteEditableSpriteFrames(config.outputRoot / editable_path, data, &write_error)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("editable sprite frames {}: {}", editable_path.generic_string(), write_error));
            }
            write_binary_file(config.outputRoot / raw_relative_path, data.data(), data.size());
            paths.editable = raw_relative_path;
        } else {
            paths.editable = editable_path;
        }

        if (!config.runtimeOutputRoot.empty()) {
            write_runtime_asset(config, /*editable_path=*/{}, runtime_path, data.data(), data.size());
            g_consumed_runtime().Mark(runtime_path);
        }
        paths.runtime = runtime_path;

        {
            std::lock_guard<std::mutex> lk(frames_mu);
            editable_frame_paths.emplace(cache_key, paths);
        }
        return paths;
    };

    auto convert_ptr_gfx = [&](uint32_t rom_offset, uint32_t size, nlohmann::json& editable_entry,
                               nlohmann::json& runtime_entry) -> SpritePtrPaths {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/sprites");
        if (raw_relative_path.extension() != ".4bpp") {
            const std::span<const uint8_t> data = extract_bytes(rom_offset, size);
            const std::filesystem::path editable_path = config.outputRoot / raw_relative_path;
            write_binary_file(editable_path, data.data(), data.size());
            g_consumed_editable().Mark(raw_relative_path);
            if (!config.runtimeOutputRoot.empty()) {
                write_runtime_asset(config, editable_path, raw_relative_path, data.data(), data.size());
                g_consumed_runtime().Mark(raw_relative_path);
            }
            return SpritePtrPaths{raw_relative_path, raw_relative_path};
        }

        const GfxMetadata meta = detect_gfx_metadata(rom_offset, size, 4);
        editable_entry["ptr_runtime_file"] = raw_relative_path.generic_string();
        editable_entry["ptr_width"] = meta.width;
        editable_entry["ptr_height"] = meta.height;
        editable_entry["ptr_bpp"] = meta.bpp;
        editable_entry["ptr_size"] = size;
        runtime_entry["ptr_size"] = size;

        const std::string cache_key = raw_relative_path.generic_string();
        {
            std::lock_guard<std::mutex> lk(ptr_mu);
            auto found = editable_ptr_paths.find(cache_key);
            if (found != editable_ptr_paths.end()) {
                return found->second;
            }
        }

        const std::span<const uint8_t> data = extract_bytes(rom_offset, size);
        const std::vector<uint8_t> pixels =
            PortAssetPipeline::DecodeGbaTiledGfx(data, meta.width, meta.height, meta.bpp);

        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".bmp");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        SpritePtrPaths paths;
        if (pixels.empty() ||
            !PortAssetPipeline::WriteIndexedBmp(config.outputRoot / editable_path, pixels, meta.width, meta.height,
                                                meta.bpp, &write_error)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("editable sprite gfx {}: {}", editable_path.generic_string(), write_error));
            }
            write_binary_file(config.outputRoot / raw_relative_path, data.data(), data.size());
            paths.editable = raw_relative_path;
        } else {
            paths.editable = editable_path;
        }

        if (!config.runtimeOutputRoot.empty()) {
            write_runtime_asset(config, /*editable_path=*/{}, raw_relative_path, data.data(), data.size());
            /* raw_relative_path here is "sprites/foo.4bpp" — i.e. the
             * asset-index path for this 4bpp gfx blob. Mark runtime so the
             * sweep doesn't re-write. Editable lives at .bmp (different
             * path), so the sweep should still write the raw .4bpp to
             * assets_src/, matching old behaviour. */
            g_consumed_runtime().Mark(raw_relative_path);
        }
        paths.runtime = raw_relative_path;

        {
            std::lock_guard<std::mutex> lk(ptr_mu);
            editable_ptr_paths.emplace(cache_key, paths);
        }
        return paths;
    };

    reporter.BeginPhase("sprites", config.spritePtrsCount);

    std::vector<nlohmann::json> editable_entries(config.spritePtrsCount);
    std::vector<nlohmann::json> runtime_entries(config.spritePtrsCount);

    PortAssetLog::ParallelFor<uint32_t>(0, config.spritePtrsCount, [&](uint32_t i) {
        const uint32_t base = config.spritePtrsTableOffset + i * 16;
        const uint32_t animations_ptr = read_pointer(base + 0);
        const uint32_t frames_ptr = read_pointer(base + 4);
        const uint32_t ptr_ptr = read_pointer(base + 8);
        const uint32_t pad = read_pointer(base + 12);

        nlohmann::json editable_entry;
        editable_entry["animations"] = nlohmann::json::array();
        editable_entry["frames_file"] = nullptr;
        editable_entry["ptr_file"] = nullptr;
        editable_entry["pad"] = pad;

        nlohmann::json runtime_entry;
        runtime_entry["animations"] = nlohmann::json::array();
        runtime_entry["frames_file"] = nullptr;
        runtime_entry["ptr_file"] = nullptr;
        runtime_entry["pad"] = pad;

        if (is_valid_rom_pointer(animations_ptr) && (animations_ptr & 1) == 0) {
            const uint32_t table_offset = to_rom_address(animations_ptr);
            const uint32_t table_size = infer_asset_size(table_offset, lookup, boundaries, 4);
            const uint32_t max_count = std::min<uint32_t>(256, table_size / 4);
            for (uint32_t anim = 0; anim < max_count; ++anim) {
                const uint32_t anim_ptr = read_pointer(table_offset + anim * 4);
                if (!is_valid_rom_pointer(anim_ptr)) {
                    break;
                }
                const uint32_t anim_offset = to_rom_address(anim_ptr);
                // Self-contained anims get the exact decoder-derived length
                // (recovers ~900 over-counted/blank ones); shared-frame anims
                // (loop_back beyond their start) keep the even boundary-gap size
                // so the runtime contiguous-packing net still feeds them their
                // preceding frames. (#-port anim-extent fix, Option 3)
                const uint32_t anim_size = compute_animation_extent(anim_offset, lookup, boundaries);
                const SpritePtrPaths paths = convert_animation(anim_offset, anim_size);
                editable_entry["animations"].push_back(json_path_string(paths.editable));
                runtime_entry["animations"].push_back(json_path_string(paths.runtime));
            }
        }

        if (is_valid_rom_pointer(frames_ptr) && (frames_ptr & 1) == 0) {
            const uint32_t frames_offset = to_rom_address(frames_ptr);
            const uint32_t frames_size = infer_asset_size(frames_offset, lookup, boundaries, 4);
            const SpritePtrPaths paths = convert_frames(frames_offset, frames_size);
            editable_entry["frames_file"] = json_path_string(paths.editable);
            runtime_entry["frames_file"] = json_path_string(paths.runtime);
        }

        if (is_valid_rom_pointer(ptr_ptr) && (ptr_ptr & 1) == 0) {
            const uint32_t ptr_offset = to_rom_address(ptr_ptr);
            const uint32_t ptr_size = infer_asset_size(ptr_offset, lookup, boundaries, 4);
            const SpritePtrPaths paths = convert_ptr_gfx(ptr_offset, ptr_size, editable_entry, runtime_entry);
            editable_entry["ptr_file"] = json_path_string(paths.editable);
            runtime_entry["ptr_file"] = json_path_string(paths.runtime);
        }

        editable_entries[i] = std::move(editable_entry);
        runtime_entries[i] = std::move(runtime_entry);
        reporter.Tick();
    });

    nlohmann::json editable_root = nlohmann::json::array();
    nlohmann::json runtime_root = nlohmann::json::array();
    for (auto& e : editable_entries) {
        editable_root.push_back(std::move(e));
    }
    for (auto& e : runtime_entries) {
        runtime_root.push_back(std::move(e));
    }

    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "sprite_ptrs.json",
                                                      std::move(editable_root), 4);
    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "sprite_ptrs.json",
                                                          std::move(runtime_root), 0);
    }

    reporter.EndPhase();
    return true;
}

inline bool extract_texts(const Config& config)
{
    auto& reporter = PortAssetLog::Reporter::Instance();
    std::error_code ec;
    std::filesystem::remove_all(config.outputRoot / "texts", ec);
    if (!config.runtimeOutputRoot.empty()) {
        std::filesystem::remove_all(config.runtimeOutputRoot / "texts", ec);
    }

    struct LanguageGroup {
        uint32_t representative_language;
        uint32_t root_ptr;
        std::vector<uint32_t> engine_slots;
    };

    std::vector<LanguageGroup> groups;
    std::unordered_map<uint32_t, size_t> group_by_root;
    for (uint32_t language = 0; language < 7; ++language) {
        const uint32_t root_ptr = read_pointer(config.translationsTableOffset + language * 4);
        auto found = group_by_root.find(root_ptr);
        if (found == group_by_root.end()) {
            LanguageGroup group;
            group.representative_language = language;
            group.root_ptr = root_ptr;
            group.engine_slots.push_back(language);
            group_by_root.emplace(root_ptr, groups.size());
            groups.push_back(std::move(group));
        } else {
            groups[found->second].engine_slots.push_back(language);
        }
    }

    auto write_le32 = [](std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
        buffer[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    };

    struct PreparedMessage {
        uint32_t index;
        uint32_t text_id;
        uint32_t rom_offset;
        std::filesystem::path editable_relative;
        std::filesystem::path runtime_relative;
        std::vector<uint8_t> bytes;
        std::string preview;
    };

    struct PreparedCategory {
        uint32_t category;
        uint32_t table_rom_offset;
        uint32_t message_count;
        std::string name;
        std::vector<PreparedMessage> messages;
    };

    struct PreparedLanguage {
        uint32_t language;
        uint32_t root_ptr;
        std::string display_name;
        std::vector<uint32_t> engine_slots;
        bool valid;
        uint32_t root_offset;
        uint32_t category_count;
        std::vector<PreparedCategory> categories;
    };

    // First pass (single-threaded, no I/O): parse the ROM tables and collect
    // every (language, category, message) triple into a flat list so we can
    // parallelise the actual decode/write in one ParallelFor.
    std::vector<PreparedLanguage> prepared(groups.size());
    struct WorkItem { size_t lang; size_t cat; size_t msg; };
    std::vector<WorkItem> work;

    for (size_t li = 0; li < groups.size(); ++li) {
        const LanguageGroup& group = groups[li];
        PreparedLanguage& pl = prepared[li];
        pl.language = group.representative_language;
        pl.root_ptr = group.root_ptr;
        pl.display_name = text_language_display_name(config, group.representative_language);
        pl.engine_slots = group.engine_slots;
        pl.valid = false;
        pl.root_offset = 0;
        pl.category_count = 0;

        if (group.root_ptr < 0x08000000u || group.root_ptr >= 0x08000000u + Rom.size()) {
            continue;
        }
        const uint32_t root_offset = to_rom_address(group.root_ptr);
        if (root_offset + 4 > Rom.size()) {
            continue;
        }

        pl.valid = true;
        pl.root_offset = root_offset;
        pl.category_count = read_pointer(root_offset) / 4;
        /* Sanity-cap against a misparsed root (e.g. a region whose text root we
         * resolved wrong): a garbage huge count would otherwise spin the loop
         * for billions of iterations. The real table has a few dozen categories. */
        if (pl.category_count > 0x1000u) {
            pl.category_count = 0;
        }

        for (uint32_t category = 0; category < pl.category_count; ++category) {
            const uint32_t category_table_relative = read_pointer(root_offset + category * 4);
            if (category_table_relative == 0 || category_table_relative >= Rom.size()) {
                continue;
            }
            const uint32_t category_table_offset = root_offset + category_table_relative;
            if (category_table_offset > Rom.size() || Rom.size() - category_table_offset < 4) {
                continue;
            }

            PreparedCategory pc;
            pc.category = category;
            pc.table_rom_offset = category_table_offset;
            pc.message_count = read_pointer(category_table_offset) / 4;
            /* Same guard for messages within a category. */
            if (pc.message_count > 0x10000u) {
                continue;
            }
            pc.name = text_category_name(category);

            for (uint32_t message = 0; message < pc.message_count; ++message) {
                const uint32_t message_relative = read_pointer(category_table_offset + message * 4);
                const uint32_t message_offset = category_table_offset + message_relative;
                if (message_relative == 0 || message_offset >= Rom.size()) {
                    continue;
                }
                PreparedMessage pm;
                pm.index = message;
                pm.text_id = (category << 8) | message;
                pm.rom_offset = message_offset;
                pc.messages.push_back(std::move(pm));
            }
            const size_t cat_index = pl.categories.size();
            pl.categories.push_back(std::move(pc));
            for (size_t mi = 0; mi < pl.categories[cat_index].messages.size(); ++mi) {
                work.push_back({li, cat_index, mi});
            }
        }
    }

    reporter.BeginPhase("texts", work.size());

    PortAssetLog::ParallelFor<size_t>(0, work.size(), [&](size_t k) {
        const WorkItem& item = work[k];
        PreparedLanguage& pl = prepared[item.lang];
        PreparedCategory& pc = pl.categories[item.cat];
        PreparedMessage& pm = pc.messages[item.msg];

        std::string symbolic_text;
        size_t consumed_bytes = 0;
        std::string err;
        if (!PortAssetPipeline::DecodeTmcText(Rom.data() + pm.rom_offset, Rom.size() - pm.rom_offset, symbolic_text,
                                              &consumed_bytes, &err)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("decode text 0x{:X}: {}", pm.rom_offset, err));
            }
            reporter.Tick();
            return;
        }

        pm.bytes.assign(Rom.begin() + pm.rom_offset, Rom.begin() + pm.rom_offset + consumed_bytes);
        pm.preview = std::move(symbolic_text);

        const std::string message_name = fmt::format("{:02X}", pm.index);
        pm.editable_relative = std::filesystem::path("texts") / pl.display_name / pc.name /
                               ("message_" + message_name + ".json");
        pm.runtime_relative = std::filesystem::path("texts") / pl.display_name / pc.name /
                              ("message_" + message_name + ".bin");

        std::string write_err;
        if (!PortAssetPipeline::WriteEditableText(config.outputRoot / pm.editable_relative, pm.bytes, &write_err)) {
            if (reporter.Verbose()) {
                reporter.Warn(fmt::format("editable text {}: {}", pm.editable_relative.generic_string(), write_err));
            }
        }
        if (!config.runtimeOutputRoot.empty()) {
            /* texts/ is one of the passthrough prefixes — route the
             * runtime mirror through the pak builder when --pak is
             * active, otherwise write loose. */
            if (config.packRuntime && config.pakBuilders != nullptr) {
                auto* builders = static_cast<PakBuilderArray*>(config.pakBuilders);
                const std::string rel = pm.runtime_relative.generic_string();
                const auto idx = static_cast<std::size_t>(pak_route_for(rel));
                (*builders)[idx].Add(rel, pm.bytes.data(), pm.bytes.size());
            } else {
                write_binary_file(config.runtimeOutputRoot / pm.runtime_relative, pm.bytes.data(), pm.bytes.size());
            }
        }
        reporter.Tick();
    });

    // Build editable + runtime JSON manifests; build packed translation tables.
    nlohmann::json editable_root = nlohmann::json::object();
    nlohmann::json runtime_root = nlohmann::json::object();

    for (PreparedLanguage& pl : prepared) {
        nlohmann::json language;
        language["name"] = pl.display_name;
        language["engine_slots"] = nlohmann::json::array();
        for (uint32_t slot : pl.engine_slots) {
            language["engine_slots"].push_back(slot);
        }

        if (!pl.valid) {
            language["valid"] = false;
            editable_root[std::to_string(pl.language)] = language;
            for (uint32_t slot : pl.engine_slots) {
                nlohmann::json copy = language;
                copy.erase("engine_slots");
                runtime_root[std::to_string(slot)] = std::move(copy);
            }
            continue;
        }

        language["valid"] = true;
        language["table_rom_offset"] = pl.root_offset;
        language["category_count"] = pl.category_count;

        nlohmann::json editable_categories = nlohmann::json::object();
        nlohmann::json runtime_categories = nlohmann::json::object();
        for (PreparedCategory& pc : pl.categories) {
            nlohmann::json editable_category;
            editable_category["name"] = pc.name;
            editable_category["table_rom_offset"] = pc.table_rom_offset;
            editable_category["message_count"] = pc.message_count;
            nlohmann::json editable_messages = nlohmann::json::array();
            nlohmann::json runtime_messages = nlohmann::json::array();
            for (PreparedMessage& pm : pc.messages) {
                if (pm.bytes.empty()) {
                    continue;
                }
                nlohmann::json em;
                em["index"] = pm.index;
                em["text_id"] = pm.text_id;
                em["rom_offset"] = pm.rom_offset;
                em["file"] = json_path_string(pm.editable_relative);
                em["size"] = pm.bytes.size();
                em["preview"] = pm.preview;
                editable_messages.push_back(em);

                nlohmann::json rm = em;
                rm["file"] = json_path_string(pm.runtime_relative);
                runtime_messages.push_back(std::move(rm));
            }
            editable_category["messages"] = editable_messages;
            editable_categories[std::to_string(pc.category)] = editable_category;

            nlohmann::json runtime_category = editable_category;
            runtime_category["messages"] = std::move(runtime_messages);
            runtime_categories[std::to_string(pc.category)] = std::move(runtime_category);
        }

        nlohmann::json editable_lang = language;
        editable_lang["categories"] = std::move(editable_categories);
        editable_root[std::to_string(pl.language)] = std::move(editable_lang);

        // Build packed runtime translation table for this language and write
        // it to runtime root. Mirrors PortAssetPipeline::buildPackedTranslationTable.
        std::vector<uint8_t> packed(static_cast<size_t>(pl.category_count) * 4, 0);
        for (PreparedCategory& pc : pl.categories) {
            if (pc.category >= pl.category_count) {
                continue;
            }
            std::vector<uint8_t> categoryData(static_cast<size_t>(pc.message_count) * 4, 0);
            size_t writePos = categoryData.size();
            for (PreparedMessage& pm : pc.messages) {
                if (pm.index >= pc.message_count || pm.bytes.empty()) {
                    continue;
                }
                write_le32(categoryData, static_cast<size_t>(pm.index) * 4, static_cast<uint32_t>(writePos));
                categoryData.insert(categoryData.end(), pm.bytes.begin(), pm.bytes.end());
                writePos += pm.bytes.size();
            }
            while ((categoryData.size() & 0xF) != 0) {
                categoryData.push_back(0xFF);
            }
            write_le32(packed, static_cast<size_t>(pc.category) * 4, static_cast<uint32_t>(packed.size()));
            packed.insert(packed.end(), categoryData.begin(), categoryData.end());
        }
        while ((packed.size() & 0xF) != 0) {
            packed.push_back(0xFF);
        }

        // Mirrors PortAssetPipeline::BuildRuntimeTexts: one packed table file
        // per representative language, named after the representative language
        // key, then replicated across every engine slot in the runtime JSON.
        const std::filesystem::path table_relative =
            std::filesystem::path("texts") / "tables" / fmt::format("language_{}.bin", pl.language);
        if (!config.runtimeOutputRoot.empty()) {
            write_runtime_asset(config, /*editable_path=*/{}, table_relative,
                                packed.data(), packed.size());
        }

        nlohmann::json runtime_lang;
        runtime_lang["name"] = pl.display_name;
        runtime_lang["valid"] = true;
        runtime_lang["table_rom_offset"] = pl.root_offset;
        runtime_lang["category_count"] = pl.category_count;
        runtime_lang["categories"] = std::move(runtime_categories);
        runtime_lang["table_size"] = packed.size();
        runtime_lang["table_file"] = table_relative.generic_string();

        for (uint32_t slot : pl.engine_slots) {
            runtime_root[std::to_string(slot)] = runtime_lang;
        }
    }

    PortAssetLog::BackgroundWriter::Instance().Submit(config.outputRoot / "texts.json",
                                                      std::move(editable_root), 4);
    if (!config.runtimeOutputRoot.empty()) {
        PortAssetLog::BackgroundWriter::Instance().Submit(config.runtimeOutputRoot / "texts.json",
                                                          std::move(runtime_root), 0);
    }

    reporter.EndPhase();
    return true;
}


inline bool extract_assets(const Config& config)
{
    auto& reporter = PortAssetLog::Reporter::Instance();

    const auto lookup = build_embedded_asset_lookup();
    std::vector<uint32_t> extra_boundaries = collect_area_table_data_offsets(config);
    const std::vector<uint32_t> sprite_animation_tables = collect_sprite_animation_table_offsets(config);
    extra_boundaries.insert(extra_boundaries.end(), sprite_animation_tables.begin(), sprite_animation_tables.end());
    const std::vector<uint32_t> boundaries = build_inference_boundaries(lookup, extra_boundaries);

    const std::vector<PaletteRefRecord> extracted_palettes = extract_all_palettes(config);
    extract_all_palette_groups(config, extracted_palettes);
    extract_all_gfx(config);
    extract_all_tilemaps(config);

    /* Five passes share the same shape (loop over 144 areas, write JSON +
     * referenced raw assets). Group them under one progress phase so their
     * cost is visible. The passes are themselves parallel-over-areas. */
    {
        reporter.BeginPhase("areas", 5);
        extract_area_room_headers(config);
        reporter.Tick();
        extract_area_map_table(config, config.areaTileSetsTableOffset, lookup, boundaries, "area_tile_sets.json",
                               "generated/mapdata", false);
        reporter.Tick();
        extract_area_map_table(config, config.areaRoomMapsTableOffset, lookup, boundaries, "area_room_maps.json",
                               "generated/mapdata", false);
        reporter.Tick();
        extract_area_map_table(config, config.areaTilesTableOffset, lookup, boundaries, "area_tiles.json",
                               "generated/mapdata", true);
        reporter.Tick();
        extract_area_tables(config, lookup, boundaries);
        reporter.Tick();
        reporter.EndPhase();
    }

    extract_sprite_ptrs(config, lookup, boundaries);
    extract_texts(config);

    /* Final sweep: extract everything in the embedded asset index that the
     * specialised passes above didn't already produce. This covers data that's
     * referenced indirectly (e.g. data_080D5360/ * room-property data pointed
     * at by gUnk_additional_* offsets reached from compiled code rather than
     * from area tables). Without this, release tarballs miss those subtrees
     * and rooms that depend on them silently misbehave (lily pads stationary,
     * doors not appearing, etc.).
     *
     * We use per-tree consumed-paths sets populated by the specialised passes
     * instead of std::filesystem::exists, which on Windows is one
     * GetFileAttributesW per entry and dominates wall time. The runtime
     * branch additionally honours the passthrough-prefix list so assets/
     * tracks the old CopyRuntimePassthroughAssets shape exactly (palettes,
     * code_*, etc. live editable-only). */
    const EmbeddedAssetEntry* idx = EmbeddedAssetIndex_Get();
    const u32 idx_count = EmbeddedAssetIndex_Count();

    reporter.BeginPhase("sweep", idx_count);
    std::atomic<uint32_t> extracted_count{0};
    PortAssetLog::ParallelFor<size_t>(0, idx_count, [&](size_t k) {
        const EmbeddedAssetEntry& entry = idx[k];
        const std::filesystem::path entry_path = entry.path;
        const std::string entry_path_str = entry_path.generic_string();

        const bool need_editable = !g_consumed_editable().Contains(entry_path);
        const bool need_runtime = !config.runtimeOutputRoot.empty() &&
                                  is_runtime_passthrough_path(entry_path_str) &&
                                  !g_consumed_runtime().Contains(entry_path);

        if (!need_editable && !need_runtime) {
            reporter.Tick();
            return;
        }

        const std::span<const uint8_t> data = extract_bytes(entry.offset, entry.size);
        const std::filesystem::path editable_full = config.outputRoot / entry_path;
        bool wrote_any = false;
        if (need_editable && write_binary_file(editable_full, data.data(), data.size())) {
            wrote_any = true;
        }
        if (need_runtime) {
            /* If we just wrote the editable copy this tick, hardlink the
             * runtime side to it (one syscall, zero data bytes) instead of
             * paying a second open+write+close. Falls back to a real write
             * if hardlinking isn't available. If we DIDN'T write editable
             * this tick (consumed by a special pass), do a normal write.
             * In pak mode write_runtime_asset routes to the appropriate
             * PakBuilder regardless of whether editable was written. */
            const bool ok =
                write_runtime_asset(config, need_editable ? editable_full : std::filesystem::path{},
                                    entry_path, data.data(), data.size());
            if (ok) {
                wrote_any = true;
            }
        }
        if (wrote_any) {
            extracted_count.fetch_add(1, std::memory_order_relaxed);
        }
        reporter.Tick();
    });
    reporter.EndPhase();

    if (reporter.Verbose()) {
        reporter.Note(fmt::format("final sweep: {} additional indexed assets", extracted_count.load()));
    }
    return true;
}
